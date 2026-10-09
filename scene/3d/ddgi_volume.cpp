/**************************************************************************/
/*  ddgi_volume.cpp                                                       */
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

#include "ddgi_volume.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server.h"

// DDGIProbeData

void DDGIProbeData::set_data(const Dictionary &p_data) {
	data = p_data;
	emit_changed();
}

Dictionary DDGIProbeData::get_data() const {
	return data;
}

int DDGIProbeData::get_cascades() const {
	return data.get("cascades", 0);
}

Vector3i DDGIProbeData::get_probe_grid() const {
	return data.get("probe_grid", Vector3i());
}

float DDGIProbeData::get_probe_spacing() const {
	return data.get("probe_spacing", 0.0);
}

Vector3 DDGIProbeData::get_center() const {
	return data.get("center", Vector3());
}

bool DDGIProbeData::matches(int p_cascades, const Vector3i &p_grid, float p_spacing, const Vector3 &p_center) const {
	return !data.is_empty() && get_cascades() == p_cascades && get_probe_grid() == p_grid &&
			Math::is_equal_approx(get_probe_spacing(), p_spacing) && get_center().is_equal_approx(p_center);
}

void DDGIProbeData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_data", "data"), &DDGIProbeData::set_data);
	ClassDB::bind_method(D_METHOD("get_data"), &DDGIProbeData::get_data);
	ClassDB::bind_method(D_METHOD("get_cascades"), &DDGIProbeData::get_cascades);
	ClassDB::bind_method(D_METHOD("get_probe_grid"), &DDGIProbeData::get_probe_grid);
	ClassDB::bind_method(D_METHOD("get_probe_spacing"), &DDGIProbeData::get_probe_spacing);
	ClassDB::bind_method(D_METHOD("get_center"), &DDGIProbeData::get_center);

	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "data", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NO_EDITOR | PROPERTY_USAGE_INTERNAL), "set_data", "get_data");
}

// DDGIVolume

DDGIVolume::BakeBeginFunc DDGIVolume::bake_begin_function = nullptr;
DDGIVolume::BakeStepFunc DDGIVolume::bake_step_function = nullptr;
DDGIVolume::BakeEndFunc DDGIVolume::bake_end_function = nullptr;

Vector3i DDGIVolume::_grid_from_size() const {
	return Vector3i(
			CLAMP(int(Math::ceil(size.x / probe_spacing)) + 1, 2, 64),
			CLAMP(int(Math::ceil(size.y / probe_spacing)) + 1, 2, 64),
			CLAMP(int(Math::ceil(size.z / probe_spacing)) + 1, 2, 64));
}

Ref<Environment> DDGIVolume::_get_environment() const {
	if (!is_inside_world()) {
		return Ref<Environment>();
	}
	Ref<World3D> world = get_world_3d();
	return world.is_valid() ? world->get_environment() : Ref<Environment>();
}

bool DDGIVolume::_probe_data_matches() const {
	return probe_data.is_valid() && is_inside_tree() && probe_data->matches(cascades, _grid_from_size(), probe_spacing, get_global_position());
}

void DDGIVolume::_apply_to_environment() {
	Ref<Environment> env = _get_environment();
	if (env.is_null()) {
		if (is_inside_tree()) {
			update_configuration_warnings();
		}
		return;
	}
	bool new_environment = applied_environment != env;
	applied_environment = env;

	env->set_ddgi_enabled(enabled);
	env->set_ddgi_cascades(cascades);
	env->set_ddgi_probe_spacing(probe_spacing);
	env->set_ddgi_probe_grid(_grid_from_size());
	env->set_ddgi_energy(energy);
	env->set_ddgi_bounce_energy(bounce_energy);
	env->set_ddgi_normal_bias(normal_bias);
	env->set_ddgi_view_bias(view_bias);
	env->set_ddgi_hysteresis(hysteresis);
	env->set_ddgi_probe_relocation(probe_relocation);
	env->set_ddgi_probe_classification(probe_classification);
	// Following the camera, the size only sets the probe grid; the cascades
	// scroll with the camera instead of staying in this box.
	env->set_ddgi_follow_camera(follow_camera);
	env->set_ddgi_debug_mode(debug_mode);
	env->set_ddgi_ao_enabled(ao_enabled);
	env->set_ddgi_ao_strength(ao_strength);
	env->set_ddgi_ao_radius(ao_radius);
	env->set_ddgi_volume(enabled && !follow_camera, get_global_position(), size);
	if (new_environment) {
		_apply_baked_data();
	}
	update_configuration_warnings();
}

void DDGIVolume::_apply_baked_data() {
	if (applied_environment.is_null()) {
		return;
	}
	// The renderer checks that the data fits the volume every frame, so a
	// moved or resized volume falls back to dynamic updates until rebaked.
	bool use = bake_mode != BAKE_MODE_DYNAMIC && probe_data.is_valid() && !follow_camera;
	applied_environment->set_ddgi_baked_data(use ? int(bake_mode) : 0, use ? probe_data->get_data() : Dictionary());
}

void DDGIVolume::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD:
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_apply_to_environment();
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			if (applied_environment.is_valid()) {
				// The node is the editor's DDGI switch: without it, DDGI is off.
				applied_environment->set_ddgi_enabled(false);
				applied_environment->set_ddgi_volume(false, Vector3(), Vector3(1, 1, 1));
				applied_environment->set_ddgi_baked_data(0, Dictionary());
				applied_environment.unref();
			}
		} break;
	}
}

void DDGIVolume::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	_apply_to_environment();
}

bool DDGIVolume::is_enabled() const {
	return enabled;
}

void DDGIVolume::set_size(const Vector3 &p_size) {
	size = p_size.maxf(0.1f);
	update_gizmos();
	_apply_to_environment();
}

Vector3 DDGIVolume::get_size() const {
	return size;
}

void DDGIVolume::set_probe_spacing(float p_spacing) {
	probe_spacing = MAX(0.05f, p_spacing);
	_apply_to_environment();
}

float DDGIVolume::get_probe_spacing() const {
	return probe_spacing;
}

void DDGIVolume::set_cascades(int p_cascades) {
	cascades = CLAMP(p_cascades, 1, 4);
	_apply_to_environment();
}

int DDGIVolume::get_cascades() const {
	return cascades;
}

void DDGIVolume::set_energy(float p_energy) {
	energy = MAX(0.0f, p_energy);
	_apply_to_environment();
}

float DDGIVolume::get_energy() const {
	return energy;
}

void DDGIVolume::set_bounce_energy(float p_energy) {
	bounce_energy = CLAMP(p_energy, 0.0f, 2.0f);
	_apply_to_environment();
}

float DDGIVolume::get_bounce_energy() const {
	return bounce_energy;
}

void DDGIVolume::set_normal_bias(float p_bias) {
	normal_bias = p_bias;
	_apply_to_environment();
}

float DDGIVolume::get_normal_bias() const {
	return normal_bias;
}

void DDGIVolume::set_view_bias(float p_bias) {
	view_bias = p_bias;
	_apply_to_environment();
}

float DDGIVolume::get_view_bias() const {
	return view_bias;
}

void DDGIVolume::set_hysteresis(float p_hysteresis) {
	hysteresis = CLAMP(p_hysteresis, 0.0f, 0.999f);
	_apply_to_environment();
}

float DDGIVolume::get_hysteresis() const {
	return hysteresis;
}

void DDGIVolume::set_probe_relocation(bool p_enabled) {
	probe_relocation = p_enabled;
	_apply_to_environment();
}

bool DDGIVolume::is_probe_relocation_enabled() const {
	return probe_relocation;
}

void DDGIVolume::set_probe_classification(bool p_enabled) {
	probe_classification = p_enabled;
	_apply_to_environment();
}

bool DDGIVolume::is_probe_classification_enabled() const {
	return probe_classification;
}

void DDGIVolume::set_follow_camera(bool p_enabled) {
	follow_camera = p_enabled;
	_apply_to_environment();
	_apply_baked_data();
}

bool DDGIVolume::is_following_camera() const {
	return follow_camera;
}

void DDGIVolume::set_ao_enabled(bool p_enabled) {
	ao_enabled = p_enabled;
	_apply_to_environment();
}

bool DDGIVolume::is_ao_enabled() const {
	return ao_enabled;
}

void DDGIVolume::set_ao_strength(float p_strength) {
	ao_strength = CLAMP(p_strength, 0.0f, 1.0f);
	_apply_to_environment();
}

float DDGIVolume::get_ao_strength() const {
	return ao_strength;
}

void DDGIVolume::set_ao_radius(float p_radius) {
	ao_radius = MAX(0.01f, p_radius);
	_apply_to_environment();
}

float DDGIVolume::get_ao_radius() const {
	return ao_radius;
}

void DDGIVolume::set_debug_mode(Environment::DDGIDebugMode p_mode) {
	debug_mode = p_mode;
	_apply_to_environment();
}

Environment::DDGIDebugMode DDGIVolume::get_debug_mode() const {
	return debug_mode;
}

void DDGIVolume::set_bake_mode(BakeMode p_mode) {
	bake_mode = p_mode;
	_apply_baked_data();
	update_configuration_warnings();
}

DDGIVolume::BakeMode DDGIVolume::get_bake_mode() const {
	return bake_mode;
}

void DDGIVolume::set_probe_data(const Ref<DDGIProbeData> &p_data) {
	probe_data = p_data;
	_apply_baked_data();
	update_configuration_warnings();
}

Ref<DDGIProbeData> DDGIVolume::get_probe_data() const {
	return probe_data;
}

Ref<DDGIProbeData> DDGIVolume::bake(int p_updates_per_probe) {
	ERR_FAIL_COND_V_MSG(!is_inside_tree(), Ref<DDGIProbeData>(), "DDGIVolume must be inside the scene tree to bake.");
	ERR_FAIL_COND_V_MSG(!enabled, Ref<DDGIProbeData>(), "DDGIVolume is disabled; enable it to bake.");
	ERR_FAIL_COND_V_MSG(follow_camera, Ref<DDGIProbeData>(), "A DDGIVolume that follows the camera can't be baked. Turn off follow_camera.");
	Ref<Environment> env = _get_environment();
	ERR_FAIL_COND_V_MSG(env.is_null(), Ref<DDGIProbeData>(), "DDGIVolume needs a WorldEnvironment with an Environment to bake.");
	RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
	ERR_FAIL_COND_V_MSG(!rd || !rd->has_feature(RD::SUPPORTS_RAYTRACING_PIPELINE), Ref<DDGIProbeData>(), "Baking DDGI needs hardware ray tracing: the Forward+ renderer, the Vulkan driver and a GPU with ray tracing pipelines.");

	// Every probe is updated about this many times. The probes traced per
	// frame come from the quality setting (see RenderDDGI::get_quality()).
	const int updates = p_updates_per_probe > 0 ? p_updates_per_probe : 128;
	static const int preset_probes_per_frame[4] = { 1024, 2048, 4096, 8192 };
	int quality = GLOBAL_GET("rendering/global_illumination/ddgi/quality");
	int probes_per_frame = (quality >= 0 && quality < 4) ? preset_probes_per_frame[quality] : int(GLOBAL_GET("rendering/global_illumination/ddgi/custom_probes_per_frame"));
	const Vector3i grid = _grid_from_size();
	const int64_t total_probes = int64_t(grid.x) * grid.y * grid.z * cascades;
	const int frames = (int)CLAMP(int64_t(updates) * total_probes / MAX(probes_per_frame, 64), int64_t(120), int64_t(20000));

	// Trace from scratch, not from an older bake.
	env->set_ddgi_baked_data(0, Dictionary());
	const float saved_hysteresis = env->get_ddgi_hysteresis();

	// A small offscreen view of the same world: the probes of a fixed volume
	// don't depend on the camera, and the view keeps them updating.
	SubViewport *vp = memnew(SubViewport);
	vp->set_size(Size2i(64, 64));
	vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
	vp->set_world_3d(get_world_3d());
	Camera3D *camera = memnew(Camera3D);
	vp->add_child(camera);
	add_child(vp, false, INTERNAL_MODE_BACK);
	camera->set_transform(Transform3D(Basis(), get_global_position()));
	camera->make_current();

	if (bake_begin_function) {
		bake_begin_function();
	}
	bool cancelled = false;
	for (int i = 0; i < frames; i++) {
		// The second half averages over more updates (a higher hysteresis):
		// less noise in the result.
		if (i == frames / 2) {
			env->set_ddgi_hysteresis(MAX(saved_hysteresis, 0.98f));
		}
		RS::get_singleton()->draw(false);
		if (bake_step_function && (i % 8) == 0 && bake_step_function(int(int64_t(i) * 1000 / frames), vformat(RTR("Tracing probes (%d / %d frames)"), i, frames))) {
			cancelled = true;
			break;
		}
	}

	Dictionary data;
	if (!cancelled) {
		data = RS::get_singleton()->viewport_get_ddgi_probe_data(vp->get_viewport_rid());
	}
	env->set_ddgi_hysteresis(saved_hysteresis);
	remove_child(vp);
	memdelete(vp);
	if (bake_end_function) {
		bake_end_function();
	}
	_apply_baked_data();

	if (cancelled) {
		return Ref<DDGIProbeData>();
	}
	ERR_FAIL_COND_V_MSG(data.is_empty(), Ref<DDGIProbeData>(), "DDGI bake failed: the renderer returned no probes. DDGI must be running for this volume (Forward+, Vulkan, ray tracing).");

	Ref<DDGIProbeData> result;
	result.instantiate();
	result->set_data(data);
	set_probe_data(result);
	return result;
}

AABB DDGIVolume::get_aabb() const {
	return AABB(-size / 2.0f, size);
}

PackedStringArray DDGIVolume::get_configuration_warnings() const {
	PackedStringArray warnings = VisualInstance3D::get_configuration_warnings();
	if (_get_environment().is_null()) {
		warnings.push_back(RTR("DDGIVolume needs a WorldEnvironment with an Environment resource in the same World3D."));
	}
	// Sampling baked probes needs no ray tracing; updating them does.
	const bool baked_only = bake_mode == BAKE_MODE_BAKED && _probe_data_matches() && !follow_camera;
	if (RenderingServer::get_singleton()->get_current_rendering_method() != "forward_plus") {
		warnings.push_back(RTR("DDGI needs the Forward+ renderer."));
	} else if (!baked_only && OS::get_singleton()->get_current_rendering_driver_name() == "d3d12") {
		warnings.push_back(RTR("DDGI needs hardware ray tracing, which the D3D12 driver doesn't implement. Use the Vulkan driver, or bake the probes and set Bake Mode to Baked."));
	} else if (!baked_only) {
		RenderingDevice *rd = RenderingServer::get_singleton()->get_rendering_device();
		if (rd && !rd->has_feature(RD::SUPPORTS_RAYTRACING_PIPELINE)) {
			warnings.push_back(RTR("DDGI needs a GPU and driver with Vulkan ray tracing pipelines; this one has none. Bake the probes on a machine with ray tracing and set Bake Mode to Baked."));
		}
	}
	if (!follow_camera && is_inside_tree() && !get_global_basis().orthonormalized().is_equal_approx(Basis())) {
		warnings.push_back(RTR("DDGIVolume ignores rotation: the probe grid is always aligned with the world axes."));
	}
	if (bake_mode != BAKE_MODE_DYNAMIC) {
		if (follow_camera) {
			warnings.push_back(RTR("Baked probes need a fixed volume: turn off Follow Camera."));
		} else if (probe_data.is_null()) {
			warnings.push_back(RTR("No baked probe data: select the DDGIVolume and use Bake DDGI in the 3D editor toolbar. Until then the probes are updated dynamically."));
		} else if (!_probe_data_matches()) {
			warnings.push_back(RTR("The baked probe data doesn't fit this volume anymore (position, size, probe spacing or cascades changed). Bake again; until then the probes are updated dynamically."));
		}
	}
	return warnings;
}

void DDGIVolume::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &DDGIVolume::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &DDGIVolume::is_enabled);
	ClassDB::bind_method(D_METHOD("set_size", "size"), &DDGIVolume::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &DDGIVolume::get_size);
	ClassDB::bind_method(D_METHOD("set_probe_spacing", "spacing"), &DDGIVolume::set_probe_spacing);
	ClassDB::bind_method(D_METHOD("get_probe_spacing"), &DDGIVolume::get_probe_spacing);
	ClassDB::bind_method(D_METHOD("set_cascades", "cascades"), &DDGIVolume::set_cascades);
	ClassDB::bind_method(D_METHOD("get_cascades"), &DDGIVolume::get_cascades);
	ClassDB::bind_method(D_METHOD("set_energy", "energy"), &DDGIVolume::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &DDGIVolume::get_energy);
	ClassDB::bind_method(D_METHOD("set_bounce_energy", "energy"), &DDGIVolume::set_bounce_energy);
	ClassDB::bind_method(D_METHOD("get_bounce_energy"), &DDGIVolume::get_bounce_energy);
	ClassDB::bind_method(D_METHOD("set_normal_bias", "bias"), &DDGIVolume::set_normal_bias);
	ClassDB::bind_method(D_METHOD("get_normal_bias"), &DDGIVolume::get_normal_bias);
	ClassDB::bind_method(D_METHOD("set_view_bias", "bias"), &DDGIVolume::set_view_bias);
	ClassDB::bind_method(D_METHOD("get_view_bias"), &DDGIVolume::get_view_bias);
	ClassDB::bind_method(D_METHOD("set_hysteresis", "hysteresis"), &DDGIVolume::set_hysteresis);
	ClassDB::bind_method(D_METHOD("get_hysteresis"), &DDGIVolume::get_hysteresis);
	ClassDB::bind_method(D_METHOD("set_probe_relocation", "enabled"), &DDGIVolume::set_probe_relocation);
	ClassDB::bind_method(D_METHOD("is_probe_relocation_enabled"), &DDGIVolume::is_probe_relocation_enabled);
	ClassDB::bind_method(D_METHOD("set_probe_classification", "enabled"), &DDGIVolume::set_probe_classification);
	ClassDB::bind_method(D_METHOD("is_probe_classification_enabled"), &DDGIVolume::is_probe_classification_enabled);
	ClassDB::bind_method(D_METHOD("set_follow_camera", "enabled"), &DDGIVolume::set_follow_camera);
	ClassDB::bind_method(D_METHOD("is_following_camera"), &DDGIVolume::is_following_camera);
	ClassDB::bind_method(D_METHOD("set_ao_enabled", "enabled"), &DDGIVolume::set_ao_enabled);
	ClassDB::bind_method(D_METHOD("is_ao_enabled"), &DDGIVolume::is_ao_enabled);
	ClassDB::bind_method(D_METHOD("set_ao_strength", "strength"), &DDGIVolume::set_ao_strength);
	ClassDB::bind_method(D_METHOD("get_ao_strength"), &DDGIVolume::get_ao_strength);
	ClassDB::bind_method(D_METHOD("set_ao_radius", "radius"), &DDGIVolume::set_ao_radius);
	ClassDB::bind_method(D_METHOD("get_ao_radius"), &DDGIVolume::get_ao_radius);
	ClassDB::bind_method(D_METHOD("set_debug_mode", "mode"), &DDGIVolume::set_debug_mode);
	ClassDB::bind_method(D_METHOD("get_debug_mode"), &DDGIVolume::get_debug_mode);
	ClassDB::bind_method(D_METHOD("set_bake_mode", "mode"), &DDGIVolume::set_bake_mode);
	ClassDB::bind_method(D_METHOD("get_bake_mode"), &DDGIVolume::get_bake_mode);
	ClassDB::bind_method(D_METHOD("set_probe_data", "data"), &DDGIVolume::set_probe_data);
	ClassDB::bind_method(D_METHOD("get_probe_data"), &DDGIVolume::get_probe_data);
	ClassDB::bind_method(D_METHOD("bake", "updates_per_probe"), &DDGIVolume::bake, DEFVAL(0));

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_spacing", PROPERTY_HINT_RANGE, "0.05,16,0.01,or_greater,suffix:m"), "set_probe_spacing", "get_probe_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cascades", PROPERTY_HINT_RANGE, "1,4,1"), "set_cascades", "get_cascades");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bounce_energy", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_bounce_energy", "get_bounce_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_bias", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_normal_bias", "get_normal_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_bias", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_view_bias", "get_view_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hysteresis", PROPERTY_HINT_RANGE, "0,0.999,0.001"), "set_hysteresis", "get_hysteresis");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_relocation"), "set_probe_relocation", "is_probe_relocation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_classification"), "set_probe_classification", "is_probe_classification_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "follow_camera"), "set_follow_camera", "is_following_camera");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_mode", PROPERTY_HINT_ENUM, "Disabled,Indirect Light,Probe Irradiance,Probe Distance,Probe States,Probe Update Priority,Cascades,Ambient Occlusion"), "set_debug_mode", "get_debug_mode");
	ADD_GROUP("Ambient Occlusion", "ao_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "ao_enabled", PROPERTY_HINT_GROUP_ENABLE), "set_ao_enabled", "is_ao_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ao_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_ao_strength", "get_ao_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ao_radius", PROPERTY_HINT_RANGE, "0.05,4,0.01,or_greater,suffix:m"), "set_ao_radius", "get_ao_radius");
	ADD_GROUP("Baking", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "bake_mode", PROPERTY_HINT_ENUM, "Dynamic,Baked,Baked + Dynamic"), "set_bake_mode", "get_bake_mode");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "probe_data", PROPERTY_HINT_RESOURCE_TYPE, DDGIProbeData::get_class_static()), "set_probe_data", "get_probe_data");

	BIND_ENUM_CONSTANT(BAKE_MODE_DYNAMIC);
	BIND_ENUM_CONSTANT(BAKE_MODE_BAKED);
	BIND_ENUM_CONSTANT(BAKE_MODE_BAKED_DYNAMIC);
}

DDGIVolume::DDGIVolume() {
	set_disable_scale(true);
}
