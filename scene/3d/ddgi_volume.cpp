/**************************************************************************/
/*  ddgi_volume.cpp                                                        */
/**************************************************************************/

#include "ddgi_volume.h"

#include "core/object/class_db.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/rendering/rendering_server.h"

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

void DDGIVolume::_apply_to_environment() {
	Ref<Environment> env = _get_environment();
	if (env.is_null()) {
		if (is_inside_tree()) {
			update_configuration_warnings();
		}
		return;
	}
	applied_environment = env;

	env->set_ddgi_enabled(enabled);
	env->set_ddgi_cascades(cascades);
	env->set_ddgi_probe_spacing(probe_spacing);
	env->set_ddgi_probe_grid(_grid_from_size());
	env->set_ddgi_energy(energy);
	env->set_ddgi_normal_bias(normal_bias);
	env->set_ddgi_view_bias(view_bias);
	env->set_ddgi_hysteresis(hysteresis);
	env->set_ddgi_probe_relocation(probe_relocation);
	env->set_ddgi_probe_classification(probe_classification);
	env->set_ddgi_follow_camera(false);
	env->set_ddgi_debug_mode(debug_mode);
	env->set_ddgi_volume(enabled, get_global_position(), size);
	update_configuration_warnings();
}

void DDGIVolume::_notification(int p_what) {
	VisualInstance3D::_notification(p_what);
	switch (p_what) {
		case NOTIFICATION_ENTER_WORLD:
		case NOTIFICATION_TRANSFORM_CHANGED: {
			_apply_to_environment();
		} break;
		case NOTIFICATION_EXIT_WORLD: {
			if (applied_environment.is_valid()) {
				applied_environment->set_ddgi_volume(false, Vector3(), Vector3(1, 1, 1));
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

void DDGIVolume::set_debug_mode(Environment::DDGIDebugMode p_mode) {
	debug_mode = p_mode;
	_apply_to_environment();
}

Environment::DDGIDebugMode DDGIVolume::get_debug_mode() const {
	return debug_mode;
}

AABB DDGIVolume::get_aabb() const {
	return AABB(-size / 2.0f, size);
}

PackedStringArray DDGIVolume::get_configuration_warnings() const {
	PackedStringArray warnings = VisualInstance3D::get_configuration_warnings();
	if (_get_environment().is_null()) {
		warnings.push_back(RTR("DDGIVolume needs a WorldEnvironment with an Environment resource in the same World3D."));
	}
	if (RenderingServer::get_singleton()->get_current_rendering_method() != "forward_plus") {
		warnings.push_back(RTR("DDGI needs the Forward+ renderer."));
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
	ClassDB::bind_method(D_METHOD("set_debug_mode", "mode"), &DDGIVolume::set_debug_mode);
	ClassDB::bind_method(D_METHOD("get_debug_mode"), &DDGIVolume::get_debug_mode);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "probe_spacing", PROPERTY_HINT_RANGE, "0.05,16,0.01,or_greater,suffix:m"), "set_probe_spacing", "get_probe_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cascades", PROPERTY_HINT_RANGE, "1,4,1"), "set_cascades", "get_cascades");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,8,0.01,or_greater"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "normal_bias", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_normal_bias", "get_normal_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "view_bias", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_view_bias", "get_view_bias");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "hysteresis", PROPERTY_HINT_RANGE, "0,0.999,0.001"), "set_hysteresis", "get_hysteresis");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_relocation"), "set_probe_relocation", "is_probe_relocation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "probe_classification"), "set_probe_classification", "is_probe_classification_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_mode", PROPERTY_HINT_ENUM, "Disabled,Indirect Light,Probe Irradiance,Probe Distance,Probe States,Probe Update Priority,Cascades"), "set_debug_mode", "get_debug_mode");
}

DDGIVolume::DDGIVolume() {
	set_disable_scale(true);
}
