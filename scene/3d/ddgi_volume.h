/**************************************************************************/
/*  ddgi_volume.h                                                          */
/**************************************************************************/

#pragma once

#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/environment.h"

class DDGIVolume : public VisualInstance3D {
	GDCLASS(DDGIVolume, VisualInstance3D);

	bool enabled = true;
	Vector3 size = Vector3(60.264f, 5.774f, 62.86f);
	float probe_spacing = 2.0f;
	int cascades = 3;
	float energy = 1.0f;
	float normal_bias = 0.1f;
	float view_bias = 0.3f;
	float hysteresis = 0.95f;
	bool probe_relocation = false;
	bool probe_classification = false;
	Environment::DDGIDebugMode debug_mode = Environment::DDGI_DEBUG_DISABLED;
	Ref<Environment> applied_environment;

	Vector3i _grid_from_size() const;
	Ref<Environment> _get_environment() const;
	void _apply_to_environment();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_enabled(bool p_enabled);
	bool is_enabled() const;

	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_probe_spacing(float p_spacing);
	float get_probe_spacing() const;

	void set_cascades(int p_cascades);
	int get_cascades() const;

	void set_energy(float p_energy);
	float get_energy() const;

	void set_normal_bias(float p_bias);
	float get_normal_bias() const;

	void set_view_bias(float p_bias);
	float get_view_bias() const;

	void set_hysteresis(float p_hysteresis);
	float get_hysteresis() const;

	void set_probe_relocation(bool p_enabled);
	bool is_probe_relocation_enabled() const;

	void set_probe_classification(bool p_enabled);
	bool is_probe_classification_enabled() const;

	void set_debug_mode(Environment::DDGIDebugMode p_mode);
	Environment::DDGIDebugMode get_debug_mode() const;

	virtual AABB get_aabb() const override;
	PackedStringArray get_configuration_warnings() const override;

	DDGIVolume();
};
