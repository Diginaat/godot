/**************************************************************************/
/*  ddgi_auto_reflections.cpp                                             */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "ddgi_auto_reflections.h"

#include "core/object/object_id.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/planar_reflector_3d.h"
#include "scene/3d/reflection_probe.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/rendering_server.h"

// Rescans at most this often after a change, and this often anyway (material
// roughness, mesh or visibility changes don't send signals).
static constexpr double RESCAN_MIN_INTERVAL = 0.5;
static constexpr double RESCAN_PERIOD = 3.0;
// How often the mesh instances behind the reflectors are checked for movement.
static constexpr double WATCH_PERIOD = 0.2;
// Meshes with more vertices than this per surface aren't scanned (terrain,
// scanned models: rarely one big plane, and slow to read back).
static constexpr int MAX_SCAN_VERTICES = 262144;

static float _material_roughness(const Ref<Material> &p_material) {
	Ref<BaseMaterial3D> base = p_material;
	if (base.is_valid()) {
		return base->get_roughness();
	}
	Ref<ShaderMaterial> shader = p_material;
	if (shader.is_valid()) {
		Variant v = shader->get_shader_parameter(SNAME("roughness"));
		if (v.get_type() == Variant::FLOAT || v.get_type() == Variant::INT) {
			return float(v);
		}
	}
	return -1.0; // Unknown: not used.
}

static bool _material_is_transparent(const Ref<Material> &p_material) {
	Ref<BaseMaterial3D> base = p_material;
	return base.is_valid() && base->get_transparency() != BaseMaterial3D::TRANSPARENCY_DISABLED;
}

static bool _material_has_normal_map(const Ref<Material> &p_material) {
	Ref<BaseMaterial3D> base = p_material;
	return base.is_valid() && base->get_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING);
}

static void _plane_axes(const Vector3 &p_normal, Vector3 &r_u, Vector3 &r_v) {
	// Deterministic in-plane axes, aligned with the mesh's axes where possible.
	Vector3 ref = Math::abs(p_normal.y) > 0.9 ? Vector3(1, 0, 0) : Vector3(0, 1, 0);
	r_u = (ref - p_normal * p_normal.dot(ref)).normalized();
	r_v = r_u.cross(p_normal);
}

bool DDGIAutoReflections::Reflector::is_equal_approx(const Reflector &p_other) const {
	return transform.is_equal_approx(p_other.transform) && size.is_equal_approx(p_other.size) && Math::is_equal_approx(distortion, p_other.distortion);
}

const LocalVector<DDGIAutoReflections::Cluster> &DDGIAutoReflections::_get_clusters(const Ref<Mesh> &p_mesh, int p_surface) {
	static const LocalVector<Cluster> empty;
	RID mesh_rid = p_mesh->get_rid();
	MeshEntry *entry = mesh_cache.getptr(mesh_rid);
	if (entry && (!entry->aabb.is_equal_approx(p_mesh->get_aabb()) || (int)entry->surfaces.size() != p_mesh->get_surface_count())) {
		mesh_cache.erase(mesh_rid);
		entry = nullptr;
	}
	if (!entry) {
		MeshEntry new_entry;
		new_entry.aabb = p_mesh->get_aabb();
		new_entry.surfaces.resize(p_mesh->get_surface_count());
		entry = &mesh_cache.insert(mesh_rid, new_entry)->value;
	}
	if (p_surface >= (int)entry->surfaces.size()) {
		return empty;
	}
	LocalVector<Cluster> &clusters = entry->surfaces[p_surface];
	if (!clusters.is_empty() || p_mesh->surface_get_primitive_type(p_surface) != Mesh::PRIMITIVE_TRIANGLES) {
		return clusters;
	}

	Array arrays = p_mesh->surface_get_arrays(p_surface);
	if (arrays.size() != Mesh::ARRAY_MAX) {
		return clusters;
	}
	PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
	PackedVector3Array normals = arrays[Mesh::ARRAY_NORMAL];
	PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
	if (vertices.size() < 3 || vertices.size() > MAX_SCAN_VERTICES) {
		clusters.push_back(Cluster()); // Mark as scanned: an empty cluster is skipped below.
		return clusters;
	}
	const Vector3 *v = vertices.ptr();
	const bool has_normals = normals.size() == vertices.size();
	const int tri_count = indices.size() > 0 ? indices.size() / 3 : vertices.size() / 3;

	// Group triangles by plane: normal quantized to about 1 degree, distance to 1 cm.
	struct Group {
		Vector3 normal_sum;
		float distance_sum = 0.0;
		float weight = 0.0;
		LocalVector<int> points;
	};
	HashMap<Vector4i, Group> groups;
	for (int t = 0; t < tri_count; t++) {
		int ia = indices.size() > 0 ? indices[t * 3 + 0] : t * 3 + 0;
		int ib = indices.size() > 0 ? indices[t * 3 + 1] : t * 3 + 1;
		int ic = indices.size() > 0 ? indices[t * 3 + 2] : t * 3 + 2;
		if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size()) {
			continue;
		}
		Vector3 n = (v[ib] - v[ia]).cross(v[ic] - v[ia]);
		float len = n.length();
		if (len < 1e-8) {
			continue;
		}
		n /= len;
		if (has_normals && n.dot(normals[ia] + normals[ib] + normals[ic]) < 0.0) {
			n = -n; // Face the way the surface faces.
		}
		float d = n.dot(v[ia]);
		Vector4i key(Math::round(n.x * 64.0), Math::round(n.y * 64.0), Math::round(n.z * 64.0), Math::round(d * 100.0));
		Group &g = groups[key];
		g.normal_sum += n * len;
		g.distance_sum += d * len;
		g.weight += len;
		g.points.push_back(ia);
		g.points.push_back(ib);
		g.points.push_back(ic);
	}

	for (const KeyValue<Vector4i, Group> &kv : groups) {
		const Group &g = kv.value;
		Cluster c;
		c.normal = g.normal_sum.normalized();
		c.distance = g.distance_sum / g.weight;
		_plane_axes(c.normal, c.axis_u, c.axis_v);
		c.min = Vector2(1e20, 1e20);
		c.max = Vector2(-1e20, -1e20);
		for (int p : g.points) {
			Vector2 q(c.axis_u.dot(v[p]), c.axis_v.dot(v[p]));
			c.min = c.min.min(q);
			c.max = c.max.max(q);
		}
		// Area of the triangles (weight is twice it), against a rectangle much
		// larger than the surface (an L shape): keep groups that fill their box.
		Vector2 extent = c.max - c.min;
		if (g.weight * 0.5 < 0.5 * extent.x * extent.y) {
			continue;
		}
		c.area = g.weight * 0.5;
		clusters.push_back(c);
	}
	// Curved meshes give a group per triangle: keep the largest few.
	struct ByArea {
		bool operator()(const Cluster &a, const Cluster &b) const { return a.area > b.area; }
	};
	clusters.sort_custom<ByArea>();
	if (clusters.size() > 32) {
		clusters.resize(32);
	}
	if (clusters.is_empty()) {
		clusters.push_back(Cluster()); // Scanned, nothing flat.
	}
	return clusters;
}

void DDGIAutoReflections::_scan(Node3D *p_volume, Node *p_root, const Settings &p_settings, const AABB &p_bounds, bool p_bounded, bool &r_manual_probe) {
	LocalVector<Reflector> candidates;
	LocalVector<ObjectID> candidate_sources;
	LocalVector<Reflector> manual;
	HashMap<ObjectID, Transform3D> new_sources;
	r_manual_probe = false;

	Ref<World3D> world = p_volume->get_world_3d();
	LocalVector<Node *> stack;
	stack.push_back(p_root);
	while (!stack.is_empty()) {
		Node *node = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		// Internal children too: level kits keep their meshes there.
		for (int i = node->get_child_count(true) - 1; i >= 0; i--) {
			stack.push_back(node->get_child(i, true));
		}

		Node3D *n3d = Object::cast_to<Node3D>(node);
		if (!n3d || !n3d->is_inside_world() || n3d->get_world_3d() != world || !n3d->is_visible_in_tree()) {
			continue;
		}

		if (ReflectionProbe *rp = Object::cast_to<ReflectionProbe>(node)) {
			AABB box = rp->get_global_transform().xform(rp->get_aabb());
			if (!p_bounded || box.intersects(p_bounds.grow(0.5))) {
				r_manual_probe = true;
			}
			continue;
		}
		if (PlanarReflector3D *pr = Object::cast_to<PlanarReflector3D>(node)) {
			Reflector r;
			Transform3D t = pr->get_global_transform();
			r.transform = Transform3D(Basis(t.basis.get_column(0).normalized(), t.basis.get_column(1).normalized(), t.basis.get_column(2).normalized()), t.origin);
			r.size = Vector2(pr->get_size().x * t.basis.get_column(0).length(), pr->get_size().y * t.basis.get_column(2).length());
			manual.push_back(r);
			continue;
		}

		MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(node);
		if (!mi || !p_settings.planar) {
			continue;
		}
		Ref<Mesh> mesh = mi->get_mesh();
		if (mesh.is_null()) {
			continue;
		}
		Transform3D xform = mi->get_global_transform();
		// Grown: floors usually lie exactly on the volume's bottom face, and touching
		// doesn't count as intersecting.
		if (p_bounded && !xform.xform(mesh->get_aabb()).intersects(p_bounds.grow(0.5))) {
			continue;
		}
		Basis normal_basis = xform.basis.inverse().transposed();
		for (int s = 0; s < mesh->get_surface_count(); s++) {
			Ref<Material> material = mi->get_active_material(s);
			float roughness = _material_roughness(material);
			if (roughness < 0.0 || roughness > p_settings.planar_max_roughness) {
				continue;
			}
			const LocalVector<Cluster> &clusters = _get_clusters(mesh, s);
			for (const Cluster &c : clusters) {
				if (c.normal == Vector3()) {
					continue;
				}
				Vector3 corners[3] = {
					xform.xform(c.axis_u * c.min.x + c.axis_v * c.min.y + c.normal * c.distance),
					xform.xform(c.axis_u * c.max.x + c.axis_v * c.min.y + c.normal * c.distance),
					xform.xform(c.axis_u * c.min.x + c.axis_v * c.max.y + c.normal * c.distance),
				};
				Vector3 e_u = corners[1] - corners[0];
				Vector3 e_v = corners[2] - corners[0];
				float area = e_u.length() * e_v.length();
				if (area < p_settings.planar_min_area || MIN(e_u.length(), e_v.length()) < 0.5) {
					continue; // Small, or a thin strip (wall tops, slab edges).
				}
				Vector3 face_center = corners[0] + (e_u + e_v) * 0.5;
				if (p_bounded && !p_bounds.grow(0.25).has_point(face_center)) {
					continue; // Outside the volume, or a slab's buried side below its floor.
				}
				Vector3 normal = normal_basis.xform(c.normal).normalized();
				if (normal.y < -0.7) {
					continue; // Undersides (of floors and slabs): almost never seen, never worth a reflector.
				}
				if (normal.y < 0.7 && _material_is_transparent(material)) {
					continue; // Upright glass: its faint reflection comes from the probes; a planar one reads as an overlay.
				}
				Vector3 x = e_u.normalized();
				Vector3 z = x.cross(normal).normalized();
				Reflector r;
				r.transform = Transform3D(Basis(x, normal, z), corners[0] + (e_u + e_v) * 0.5);
				r.size = Vector2(e_u.length(), e_v.length());
				r.area = area;
				r.distortion = _material_has_normal_map(material) ? 0.3 : 0.0;
				candidates.push_back(r);
				candidate_sources.push_back(mi->get_instance_id());
			}
		}
	}

	// Rectangle of b in a's frame (x along a's X, y along a's Z).
	auto rect_in = [](const Reflector &a, const Reflector &b) -> Rect2 {
		Vector3 hx = b.transform.basis.get_column(0) * (b.size.x * 0.5);
		Vector3 hz = b.transform.basis.get_column(2) * (b.size.y * 0.5);
		Rect2 r;
		bool first = true;
		for (int i = 0; i < 4; i++) {
			Vector3 p = b.transform.origin + ((i & 1) ? hx : -hx) + ((i & 2) ? hz : -hz) - a.transform.origin;
			Vector2 q(p.dot(a.transform.basis.get_column(0)), p.dot(a.transform.basis.get_column(2)));
			if (first) {
				r = Rect2(q, Vector2());
				first = false;
			} else {
				r.expand_to(q);
			}
		}
		return r;
	};
	auto coplanar = [](const Reflector &a, const Reflector &b) -> bool {
		Vector3 na = a.transform.basis.get_column(1);
		Vector3 nb = b.transform.basis.get_column(1);
		return na.dot(nb) > 0.999 && Math::abs(na.dot(b.transform.origin - a.transform.origin)) < 0.02;
	};

	// Merge coplanar, touching, axis-aligned rectangles (floor tiles, split meshes).
	bool merged = true;
	while (merged) {
		merged = false;
		for (uint32_t i = 0; i < candidates.size() && !merged; i++) {
			for (uint32_t j = i + 1; j < candidates.size() && !merged; j++) {
				Reflector &a = candidates[i];
				const Reflector &b = candidates[j];
				float axis_dot = Math::abs(a.transform.basis.get_column(0).dot(b.transform.basis.get_column(0)));
				if (!coplanar(a, b) || (axis_dot < 0.999 && axis_dot > 0.001)) {
					continue;
				}
				Rect2 ra(-a.size * 0.5, a.size);
				Rect2 rb = rect_in(a, b);
				if (!ra.grow(0.05).intersects(rb)) {
					continue;
				}
				Rect2 u = ra.merge(rb);
				a.transform.origin += a.transform.basis.get_column(0) * u.get_center().x + a.transform.basis.get_column(2) * u.get_center().y;
				a.size = u.size;
				a.area += b.area;
				a.distortion = MAX(a.distortion, b.distortion);
				new_sources[candidate_sources[j]] = Transform3D();
				candidates.remove_at(j);
				candidate_sources.remove_at(j);
				merged = true;
			}
		}
	}

	// Leave surfaces that have a reflector of their own.
	LocalVector<Reflector> result;
	for (uint32_t i = 0; i < candidates.size(); i++) {
		bool covered = false;
		for (const Reflector &m : manual) {
			if (coplanar(m, candidates[i]) && Rect2(-m.size * 0.5, m.size).intersects(rect_in(m, candidates[i]))) {
				covered = true;
				break;
			}
		}
		if (!covered) {
			result.push_back(candidates[i]);
			new_sources[candidate_sources[i]] = Transform3D();
		}
	}

	// The largest ones; the renderer uses the nearest few of these per view.
	struct ByArea {
		bool operator()(const Reflector &a, const Reflector &b) const { return a.area > b.area; }
	};
	result.sort_custom<ByArea>();
	if (result.size() > MAX_PLANAR_REFLECTORS) {
		result.resize(MAX_PLANAR_REFLECTORS);
	}
	_apply_reflectors(result);

	sources.clear();
	for (const KeyValue<ObjectID, Transform3D> &kv : new_sources) {
		Node3D *src = ObjectDB::get_instance<Node3D>(kv.key);
		if (src) {
			sources[kv.key] = src->get_global_transform();
		}
	}
}

void DDGIAutoReflections::_apply_reflectors(const LocalVector<Reflector> &p_reflectors) {
	RenderingServer *rs = RenderingServer::get_singleton();
	bool same = p_reflectors.size() == reflectors.size();
	for (uint32_t i = 0; same && i < p_reflectors.size(); i++) {
		same = p_reflectors[i].is_equal_approx(reflectors[i]);
	}
	if (same) {
		return; // Stable: no server calls when nothing changed.
	}

	while (reflector_rids.size() > p_reflectors.size()) {
		rs->free_rid(reflector_rids[reflector_rids.size() - 1]);
		reflector_rids.resize(reflector_rids.size() - 1);
	}
	while (reflector_rids.size() < p_reflectors.size()) {
		RID rid = rs->planar_reflection_create();
		rs->planar_reflection_set_layer_mask(rid, (1 << 20) - 1);
		rs->planar_reflection_set_scenario(rid, scenario);
		reflector_rids.push_back(rid);
	}
	for (uint32_t i = 0; i < p_reflectors.size(); i++) {
		rs->planar_reflection_set_transform(reflector_rids[i], p_reflectors[i].transform);
		rs->planar_reflection_set_size(reflector_rids[i], p_reflectors[i].size);
		rs->planar_reflection_set_normal_distortion(reflector_rids[i], p_reflectors[i].distortion);
	}
	reflectors = p_reflectors;
}

void DDGIAutoReflections::_set_probe(bool p_enabled, const Transform3D &p_transform, const Vector3 &p_size, bool p_box_projection, bool p_realtime) {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!p_enabled) {
		if (probe_instance.is_valid()) {
			rs->free_rid(probe_instance);
			rs->free_rid(probe);
			probe_instance = RID();
			probe = RID();
		}
		return;
	}
	if (probe.is_null()) {
		probe = rs->reflection_probe_create();
		probe_realtime = p_realtime;
		rs->reflection_probe_set_update_mode(probe, p_realtime ? RSE::REFLECTION_PROBE_UPDATE_ALWAYS : RSE::REFLECTION_PROBE_UPDATE_ONCE);
		rs->reflection_probe_set_ambient_mode(probe, RSE::REFLECTION_PROBE_AMBIENT_DISABLED); // DDGI gives the diffuse light.
		rs->reflection_probe_set_enable_shadows(probe, true);
		rs->reflection_probe_set_blend_distance(probe, 0.5);
		probe_instance = rs->instance_create();
		rs->instance_set_base(probe_instance, probe);
		rs->instance_set_scenario(probe_instance, scenario);
		probe_transform = Transform3D(Basis(), Vector3(1e20, 1e20, 1e20));
		probe_size = Vector3();
	}
	if (p_realtime != probe_realtime) {
		probe_realtime = p_realtime;
		rs->reflection_probe_set_update_mode(probe, p_realtime ? RSE::REFLECTION_PROBE_UPDATE_ALWAYS : RSE::REFLECTION_PROBE_UPDATE_ONCE);
	}
	if (p_size != probe_size || p_box_projection != probe_box_projection) {
		probe_size = p_size;
		probe_box_projection = p_box_projection;
		rs->reflection_probe_set_size(probe, p_size);
		rs->reflection_probe_set_max_distance(probe, p_size.length()); // Walls on the box faces are captured too.
		rs->reflection_probe_set_enable_box_projection(probe, p_box_projection);
	}
	if (!p_transform.is_equal_approx(probe_transform)) {
		probe_transform = p_transform;
		rs->instance_set_transform(probe_instance, p_transform); // Recaptured, in budgeted steps.
	}
}

void DDGIAutoReflections::update(Node3D *p_volume, const Settings &p_settings, const Vector3 &p_volume_size, bool p_infinite_world, double p_delta) {
	ERR_FAIL_NULL(p_volume);
	if (!p_volume->is_inside_world()) {
		clear();
		return;
	}
	RID new_scenario = p_volume->get_world_3d()->get_scenario();
	if (new_scenario != scenario) {
		clear();
		scenario = new_scenario;
		dirty = true;
	}

	// Watch the mesh instances behind the reflectors: moving one rescans.
	since_watch += p_delta;
	if (since_watch >= WATCH_PERIOD) {
		since_watch = 0.0;
		for (const KeyValue<ObjectID, Transform3D> &kv : sources) {
			Node3D *src = ObjectDB::get_instance<Node3D>(kv.key);
			if (!src || !src->is_inside_tree() || !src->get_global_transform().is_equal_approx(kv.value)) {
				dirty = true;
				break;
			}
		}
	}

	since_scan += p_delta;
	bool scan = (dirty && since_scan >= RESCAN_MIN_INTERVAL) || since_scan >= RESCAN_PERIOD;
	bool manual_probe = false;
	if (scan) {
		dirty = false;
		since_scan = 0.0;
		Node *root = p_volume->get_owner() ? p_volume->get_owner() : p_volume->get_parent();
		if (p_volume->is_inside_tree() && p_volume->get_tree()->get_current_scene() && p_volume->get_tree()->get_current_scene()->is_ancestor_of(p_volume)) {
			root = p_volume->get_tree()->get_current_scene();
		}
		if (!root) {
			root = p_volume;
		}
		AABB bounds(p_volume->get_global_position() - p_volume_size * 0.5, p_volume_size);
		_scan(p_volume, root, p_settings, bounds, !p_infinite_world, manual_probe);
		has_manual_probe = manual_probe;
	}

	// The probe: the volume's box, or a box around the camera with an infinite world.
	if (p_infinite_world) {
		Vector3 center = p_volume->get_global_position();
		Viewport *vp = p_volume->get_viewport();
		Camera3D *camera = vp ? vp->get_camera_3d() : nullptr;
		if (camera) {
			center = camera->get_global_position();
		}
		float step = MAX(MAX(p_volume_size.x, p_volume_size.z) * 0.25f, 1.0f);
		center = (center / step).round() * step;
		_set_probe(p_settings.probe && !has_manual_probe, Transform3D(Basis(), center), p_volume_size, false, p_settings.probe_realtime);
	} else {
		_set_probe(p_settings.probe && !has_manual_probe, Transform3D(Basis(), p_volume->get_global_position()), p_volume_size + Vector3(1, 1, 1), true, p_settings.probe_realtime);
	}
}

void DDGIAutoReflections::clear() {
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	for (const RID &rid : reflector_rids) {
		rs->free_rid(rid);
	}
	reflector_rids.clear();
	reflectors.clear();
	sources.clear();
	_set_probe(false, Transform3D(), Vector3(), true);
	scenario = RID();
	has_manual_probe = false;
	dirty = true;
	since_scan = 1e9;
}

Array DDGIAutoReflections::get_planar_reflectors() const {
	Array result;
	for (const Reflector &r : reflectors) {
		Dictionary d;
		d["transform"] = r.transform;
		d["size"] = r.size;
		d["distortion"] = r.distortion;
		result.push_back(d);
	}
	return result;
}

DDGIAutoReflections::~DDGIAutoReflections() {
	clear();
}
