/**************************************************************************/
/*  weather_sound_effect.h                                                */
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

#include "../weather_effect.h"

#include "core/math/random_pcg.h"
#include "core/math/vector3.h"
#include "core/templates/local_vector.h"
#include "core/variant/typed_array.h"
#include "scene/resources/audio/audio_stream.h"

class AudioEffectLowPassFilter;
class AudioStreamPlayer;
class AudioStreamWAV;

// Rain, wind and thunder sound for a Weather node, in game only. The sounds
// are synthesized once per run on worker threads (WeatherSoundSynth), so the
// engine ships no audio files; a project can drop in its own recordings per
// slot. Light and heavy rain crossfade with the rain, the wind follows the
// wind speed with its gusts, and thunder answers the lightning
// (Weather.lightning_struck) after the time sound takes to travel: near
// strikes crack, far ones only rumble, through a low-pass on a thunder bus.
class WeatherSoundEffect : public WeatherEffect {
	GDCLASS(WeatherSoundEffect, WeatherEffect);

public:
	enum Sound {
		SOUND_RAIN_LIGHT,
		SOUND_RAIN_HEAVY,
		SOUND_WIND,
		SOUND_THUNDER_FAR_1,
		SOUND_THUNDER_FAR_2,
		SOUND_THUNDER_MEDIUM,
		SOUND_THUNDER_NEAR_1,
		SOUND_THUNDER_NEAR_2,
		SOUND_MAX,
	};

	static constexpr float SPEED_OF_SOUND = 343.0f;
	static constexpr const char *THUNDER_BUS = "WeatherThunder";

private:
	float volume_db = -4.0f;
	float rain_volume_db = 0.0f;
	float wind_volume_db = 0.0f;
	float thunder_volume_db = 0.0f;
	float wind_full_speed = 20.0f;
	StringName audio_bus = "Master";
	Ref<AudioStream> rain_sound;
	Ref<AudioStream> heavy_rain_sound;
	Ref<AudioStream> wind_sound;
	TypedArray<AudioStream> thunder_sounds;
	TypedArray<AudioStream> near_thunder_sounds;

	// Loop copies of the project's own sounds (the originals stay as they are).
	Ref<AudioStream> rain_loop;
	Ref<AudioStream> heavy_rain_loop;
	Ref<AudioStream> wind_loop;

	struct Thunder {
		double delay = 0.0;
		float strength = 0.0f;
	};

	Weather *weather = nullptr;
	AudioStreamPlayer *rain_player = nullptr;
	AudioStreamPlayer *heavy_rain_player = nullptr;
	AudioStreamPlayer *wind_player = nullptr;
	AudioStreamPlayer *thunder_players[2] = {};
	Ref<AudioEffectLowPassFilter> thunder_filter;
	LocalVector<Thunder> pending_thunder;
	bool streams_applied = false;
	float rain_amount = 0.0f;
	float wind_amount = 0.0f;
	RandomPCG rng;

	// Synthesized once per run and shared by every sound effect.
	static Ref<AudioStreamWAV> synthesized[SOUND_MAX];
	static Vector<uint8_t> synthesized_data[SOUND_MAX];
	static int64_t synthesis_group;
	static int thunder_bus_users;
	static void _synthesize_one(void *p_userdata, uint32_t p_index);
	static void _start_synthesis();
	static bool _poll_synthesis();
	static Ref<AudioStreamWAV> _make_stream(Sound p_sound, const Vector<uint8_t> &p_data);

	static Ref<AudioStream> _looping(const Ref<AudioStream> &p_stream);
	AudioStreamPlayer *_make_player(const String &p_name);
	void _make_thunder_bus();
	void _apply_streams();
	void _set_stream(AudioStreamPlayer *p_player, const Ref<AudioStream> &p_stream);
	Ref<AudioStream> _pick_thunder(float p_strength);
	void _update_loop(AudioStreamPlayer *p_player, float p_amount, float p_trim_db, double p_delta);
	void _play_thunder(float p_strength);
	void _on_lightning_struck(const Vector3 &p_position, float p_distance);
	void _clear();

protected:
	void _validate_property(PropertyInfo &p_property) const;
	static void _bind_methods();

public:
	static void free_synthesized_sounds();
	static Ref<AudioStreamWAV> synthesize(Sound p_sound);

	void set_volume_db(float p_db);
	float get_volume_db() const;
	void set_rain_volume_db(float p_db);
	float get_rain_volume_db() const;
	void set_wind_volume_db(float p_db);
	float get_wind_volume_db() const;
	void set_thunder_volume_db(float p_db);
	float get_thunder_volume_db() const;
	void set_wind_full_speed(float p_speed);
	float get_wind_full_speed() const;
	void set_audio_bus(const StringName &p_bus);
	StringName get_audio_bus() const;
	void set_rain_sound(const Ref<AudioStream> &p_sound);
	Ref<AudioStream> get_rain_sound() const;
	void set_heavy_rain_sound(const Ref<AudioStream> &p_sound);
	Ref<AudioStream> get_heavy_rain_sound() const;
	void set_wind_sound(const Ref<AudioStream> &p_sound);
	Ref<AudioStream> get_wind_sound() const;
	void set_thunder_sounds(const TypedArray<AudioStream> &p_sounds);
	TypedArray<AudioStream> get_thunder_sounds() const;
	void set_near_thunder_sounds(const TypedArray<AudioStream> &p_sounds);
	TypedArray<AudioStream> get_near_thunder_sounds() const;

	bool is_ready() const;
	float get_rain_amount() const;
	float get_wind_amount() const;
	int get_pending_thunder_count() const;

	virtual void start(Weather *p_weather) override;
	virtual void stop(Weather *p_weather) override;
	virtual void update(Weather *p_weather, double p_delta) override;
};

VARIANT_ENUM_CAST(WeatherSoundEffect::Sound);
