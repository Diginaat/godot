/**************************************************************************/
/*  ddgi_auto_reflections.h                                               */
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

#pragma once

#include "core/math/aabb.h"
#include "core/math/transform_3d.h"
#include "core/object/object_id.h"
#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/array.h"

class Mesh;
class Node;
class Node3D;

// Automatic reflections for a DDGIVolume (its "auto_reflections" options):
//  - one box-projected reflection probe for the volume, captured once (in
//    budgeted, time-sliced steps) and normalized by DDGI; with an infinite
//    world it follows the camera in steps of a quarter of the volume;
//  - planar reflectors on large, flat, smooth surfaces found in the scene's
//    meshes: triangles grouped by plane, coplanar neighbors merged, only
//    surfaces whose material is smooth enough;
//  - a small box-projected probe in front of each large upright reflector
//    (mirror), captured from the mirror: what a mirror seen from close by
//    reflects is behind the camera, off screen, so the planar reflection
//    falls back to the probes there.
// Everything lives in the RenderingServer: nothing is added to the scene or
// saved with it. Volumes or surfaces that already have a ReflectionProbe or
// PlanarReflector3D of their own are left alone.
class DDGIAutoReflections {
public:
	struct Settings {
		bool probe = true;
		bool probe_realtime = false; // Recaptured continuously, within the face budget.
		bool planar = true;
		float planar_max_roughness = 0.3; // Floors, water, ramps (normal within 45 degrees of up).
		float planar_mirror_max_roughness = 0.02; // Upright surfaces: mirrors, walls.
		float planar_min_area = 2.0;
		bool mirror_probes = true;
	};

	static constexpr int MAX_PLANAR_REFLECTORS = 16;
	static constexpr int MAX_MIRROR_PROBES = 8;

private:
	// A group of coplanar triangles of one mesh surface, in mesh space.
	struct Cluster {
		Vector3 normal;
		Vector3 axis_u;
		Vector3 axis_v;
		float distance = 0.0;
		float area = 0.0;
		Vector2 min;
		Vector2 max;
	};
	struct MeshEntry {
		AABB aabb; // Detects changed meshes (a resized BoxMesh keeps its RID).
		LocalVector<LocalVector<Cluster>> surfaces;
	};
	HashMap<RID, MeshEntry> mesh_cache;

	struct Reflector {
		Transform3D transform; // Basis X and Z unit axes, Y the normal; origin the center.
		Vector2 size;
		float distortion = 0.0;
		float area = 0.0;
		bool is_equal_approx(const Reflector &p_other) const;
	};
	LocalVector<Reflector> reflectors;
	LocalVector<RID> reflector_rids;

	struct MirrorProbe {
		RID probe;
		RID instance;
		Transform3D transform;
		Vector3 size;
		Vector3 origin_offset;
		float blend_distance = 0.0;
		bool realtime = false;
	};
	LocalVector<MirrorProbe> mirror_probes;

	// Mesh instances that produced reflectors, and where they were: moving one rescans.
	HashMap<ObjectID, Transform3D> sources;

	RID scenario;
	RID probe;
	RID probe_instance;
	Transform3D probe_transform;
	Vector3 probe_size;
	bool probe_box_projection = true;
	bool probe_realtime = false;
	bool has_manual_probe = false;

	bool dirty = true;
	double since_scan = 1e9; // Scan right away.
	double since_watch = 0.0;

	const LocalVector<Cluster> &_get_clusters(const Ref<Mesh> &p_mesh, int p_surface);
	void _scan(Node3D *p_volume, Node *p_root, const Settings &p_settings, const AABB &p_bounds, bool p_bounded, bool &r_manual_probe);
	void _apply_reflectors(const LocalVector<Reflector> &p_reflectors);
	void _apply_mirror_probes(const LocalVector<Reflector> &p_reflectors, const LocalVector<AABB> &p_manual_probes, const Settings &p_settings, const AABB &p_bounds, bool p_bounded);
	void _set_probe(bool p_enabled, const Transform3D &p_transform, const Vector3 &p_size, bool p_box_projection, bool p_realtime = false);

public:
	// Something in the scene changed (meshes, reflectors, probes, settings).
	void mark_dirty() { dirty = true; }
	// Called every frame by the volume.
	void update(Node3D *p_volume, const Settings &p_settings, const Vector3 &p_volume_size, bool p_infinite_world, double p_delta);
	// Frees all server objects.
	void clear();
	int get_planar_reflector_count() const { return reflectors.size(); }
	// Each reflector as {transform, size, distortion} (debugging, tests).
	Array get_planar_reflectors() const;
	bool has_probe() const { return probe_instance.is_valid(); }
	int get_mirror_probe_count() const { return mirror_probes.size(); }

	~DDGIAutoReflections();
};
