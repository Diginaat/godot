/**************************************************************************/
/*  weather_sound_effect.cpp                                              */
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

#include "weather_sound_effect.h"

#include "weather_sound_synth.h"

#include "../weather.h"

#include "core/config/engine.h"
#include "core/math/math_funcs.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/worker_thread_pool.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/resources/audio/audio_stream_wav.h"
#include "servers/audio/audio_server.h"
#include "servers/audio/effects/audio_effect_filter.h"
#include "servers/audio/effects/audio_effect_reverb.h"

static_assert((int)WeatherSoundEffect::SOUND_MAX == (int)WeatherSoundSynth::SOUND_MAX);

Ref<AudioStreamWAV> WeatherSoundEffect::synthesized[SOUND_MAX];
Vector<uint8_t> WeatherSoundEffect::synthesized_data[SOUND_MAX];
int64_t WeatherSoundEffect::synthesis_group = -1;
int WeatherSoundEffect::thunder_bus_users = 0;

// Synthesis.

void WeatherSoundEffect::_synthesize_one(void *p_userdata, uint32_t p_index) {
	// Each task writes only its own slot.
	synthesized_data[p_index] = WeatherSoundSynth::generate((WeatherSoundSynth::Sound)p_index);
}

void WeatherSoundEffect::_start_synthesis() {
	if (synthesized[0].is_valid() || synthesis_group != -1) {
		return;
	}
	synthesis_group = WorkerThreadPool::get_singleton()->add_native_group_task(&WeatherSoundEffect::_synthesize_one, nullptr, SOUND_MAX, -1, false, "Weather sounds");
}

bool WeatherSoundEffect::_poll_synthesis() {
	if (synthesized[0].is_valid()) {
		return true;
	}
	if (synthesis_group == -1 || !WorkerThreadPool::get_singleton()->is_group_task_completed(synthesis_group)) {
		return false;
	}
	WorkerThreadPool::get_singleton()->wait_for_group_task_completion(synthesis_group);
	synthesis_group = -1;
	for (int i = 0; i < SOUND_MAX; i++) {
		synthesized[i] = _make_stream((Sound)i, synthesized_data[i]);
		synthesized_data[i] = Vector<uint8_t>();
	}
	return true;
}

Ref<AudioStreamWAV> WeatherSoundEffect::_make_stream(Sound p_sound, const Vector<uint8_t> &p_data) {
	Ref<AudioStreamWAV> stream;
	stream.instantiate();
	stream->set_format(AudioStreamWAV::FORMAT_16_BITS);
	stream->set_mix_rate(WeatherSoundSynth::MIX_RATE);
	stream->set_stereo(false);
	stream->set_data(p_data);
	if (WeatherSoundSynth::is_loop((WeatherSoundSynth::Sound)p_sound)) {
		stream->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
		stream->set_loop_begin(0);
		stream->set_loop_end(p_data.size() / 2);
	}
	return stream;
}

void WeatherSoundEffect::free_synthesized_sounds() {
	if (synthesis_group != -1) {
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(synthesis_group);
		synthesis_group = -1;
	}
	for (int i = 0; i < SOUND_MAX; i++) {
		synthesized[i].unref();
		synthesized_data[i] = Vector<uint8_t>();
	}
}

Ref<AudioStreamWAV> WeatherSoundEffect::synthesize(Sound p_sound) {
	ERR_FAIL_INDEX_V(p_sound, SOUND_MAX, Ref<AudioStreamWAV>());
	return _make_stream(p_sound, WeatherSoundSynth::generate((WeatherSoundSynth::Sound)p_sound));
}

// Running.

AudioStreamPlayer *WeatherSoundEffect::_make_player(const String &p_name) {
	AudioStreamPlayer *player = memnew(AudioStreamPlayer);
	player->set_name(p_name);
	player->set_bus(audio_bus);
	player->set_volume_db(-60.0f);
	weather->add_child(player, false, Node::INTERNAL_MODE_FRONT);
	return player;
}

void WeatherSoundEffect::_make_thunder_bus() {
	AudioServer *audio = AudioServer::get_singleton();
	int index = audio->get_bus_index(THUNDER_BUS);
	if (index != -1 && thunder_bus_users > 0 && audio->get_bus_effect_count(index) > 0) {
		// Another weather runs: share its bus.
		thunder_filter = audio->get_bus_effect(index, 0);
	}
	if (thunder_filter.is_null()) {
		if (index == -1) {
			index = audio->get_bus_count();
			audio->add_bus(index);
			audio->set_bus_name(index, THUNDER_BUS);
		}
		while (audio->get_bus_effect_count(index) > 0) {
			audio->remove_bus_effect(index, 0);
		}
		// Far thunder loses its high frequencies (each clap moves the cutoff).
		thunder_filter.instantiate();
		thunder_filter->set_cutoff(8000.0f);
		audio->add_bus_effect(index, thunder_filter);
		Ref<AudioEffectReverb> reverb;
		reverb.instantiate();
		reverb->set_room_size(0.95f);
		reverb->set_wet(0.35f);
		audio->add_bus_effect(index, reverb);
	}
	audio->set_bus_send(index, audio_bus);
	thunder_bus_users++;
}

void WeatherSoundEffect::start(Weather *p_weather) {
	weather = p_weather;
	// No sound in the editor's weather preview.
	if (!Engine::get_singleton()->is_editor_hint() && AudioServer::get_singleton()) {
		rng.randomize();
		_start_synthesis();
		rain_player = _make_player("RainSound");
		heavy_rain_player = _make_player("HeavyRainSound");
		wind_player = _make_player("WindSound");
		_make_thunder_bus();
		for (int i = 0; i < 2; i++) {
			thunder_players[i] = _make_player(vformat("Thunder%d", i + 1));
			thunder_players[i]->set_bus(THUNDER_BUS);
		}
		streams_applied = false;
		_apply_streams();
		p_weather->connect(SNAME("lightning_struck"), callable_mp(this, &WeatherSoundEffect::_on_lightning_struck));
	}
	WeatherEffect::start(p_weather);
}

void WeatherSoundEffect::stop(Weather *p_weather) {
	WeatherEffect::stop(p_weather);
	if (rain_player) {
		p_weather->disconnect(SNAME("lightning_struck"), callable_mp(this, &WeatherSoundEffect::_on_lightning_struck));
		thunder_bus_users--;
		AudioServer *audio = AudioServer::get_singleton();
		const int index = audio->get_bus_index(THUNDER_BUS);
		if (thunder_bus_users <= 0 && index != -1) {
			thunder_bus_users = 0;
			audio->remove_bus(index);
		}
	}
	_clear();
	weather = nullptr;
}

void WeatherSoundEffect::_clear() {
	// Out of the tree at once, so a restart can take the same names.
	for (AudioStreamPlayer *player : { rain_player, heavy_rain_player, wind_player, thunder_players[0], thunder_players[1] }) {
		if (player) {
			player->get_parent()->remove_child(player);
			player->queue_free();
		}
	}
	rain_player = nullptr;
	heavy_rain_player = nullptr;
	wind_player = nullptr;
	thunder_players[0] = nullptr;
	thunder_players[1] = nullptr;
	thunder_filter.unref();
	pending_thunder.clear();
	rain_amount = 0.0f;
	wind_amount = 0.0f;
}

Ref<AudioStream> WeatherSoundEffect::_looping(const Ref<AudioStream> &p_stream) {
	if (p_stream.is_null()) {
		return Ref<AudioStream>();
	}
	Ref<AudioStream> copy = p_stream->duplicate();
	const Ref<AudioStreamWAV> wav = copy;
	if (wav.is_valid()) {
		if (wav->get_loop_mode() == AudioStreamWAV::LOOP_DISABLED) {
			wav->set_loop_mode(AudioStreamWAV::LOOP_FORWARD);
			wav->set_loop_begin(0);
			wav->set_loop_end(int(wav->get_length() * wav->get_mix_rate()));
		}
	} else {
		// Ogg Vorbis, MP3.
		bool valid = false;
		copy->get(SNAME("loop"), &valid);
		if (valid) {
			copy->set(SNAME("loop"), true);
		}
	}
	return copy;
}

void WeatherSoundEffect::_set_stream(AudioStreamPlayer *p_player, const Ref<AudioStream> &p_stream) {
	if (p_player->get_stream() == p_stream) {
		return;
	}
	p_player->stop();
	p_player->set_stream(p_stream);
	p_player->set_volume_db(-60.0f);
}

void WeatherSoundEffect::_apply_streams() {
	if (!rain_player) {
		return;
	}
	const bool ready = _poll_synthesis();
	streams_applied = ready;
	_set_stream(rain_player, rain_loop.is_valid() ? rain_loop : (ready ? Ref<AudioStream>(synthesized[SOUND_RAIN_LIGHT]) : Ref<AudioStream>()));
	_set_stream(heavy_rain_player, heavy_rain_loop.is_valid() ? heavy_rain_loop : (ready ? Ref<AudioStream>(synthesized[SOUND_RAIN_HEAVY]) : Ref<AudioStream>()));
	_set_stream(wind_player, wind_loop.is_valid() ? wind_loop : (ready ? Ref<AudioStream>(synthesized[SOUND_WIND]) : Ref<AudioStream>()));
}

void WeatherSoundEffect::_update_loop(AudioStreamPlayer *p_player, float p_amount, float p_trim_db, double p_delta) {
	if (p_player->get_stream().is_null()) {
		return;
	}
	const float target_db = volume_db + p_trim_db + Math::linear_to_db(MAX(p_amount, 0.0001f));
	p_player->set_volume_db(Math::lerp(p_player->get_volume_db(), target_db, 1.0f - (float)Math::exp(-3.0 * p_delta)));
	if (p_amount > 0.001f && !p_player->is_playing()) {
		p_player->set_volume_db(-60.0f);
		p_player->play();
	} else if (p_amount <= 0.001f && p_player->is_playing() && p_player->get_volume_db() < -50.0f) {
		p_player->stop();
	}
}

void WeatherSoundEffect::update(Weather *p_weather, double p_delta) {
	WeatherEffect::update(p_weather, p_delta);
	if (!rain_player) {
		return;
	}
	if (!streams_applied) {
		_apply_streams();
	}
	rain_amount = p_weather->get_rain_amount();
	wind_amount = CLAMP(p_weather->get_wind().length() / MAX(wind_full_speed, 0.1f), 0.0f, 1.0f);
	// The heavy rain takes over as the rain gets heavier (equal power).
	const float heavy = heavy_rain_player->get_stream().is_valid() ? Math::smoothstep(0.35f, 0.9f, rain_amount) : 0.0f;
	_update_loop(rain_player, rain_amount * Math::sqrt(1.0f - heavy), rain_volume_db, p_delta);
	_update_loop(heavy_rain_player, rain_amount * Math::sqrt(heavy), rain_volume_db, p_delta);
	_update_loop(wind_player, wind_amount, wind_volume_db, p_delta);

	for (uint32_t i = 0; i < pending_thunder.size();) {
		pending_thunder[i].delay -= p_delta;
		if (pending_thunder[i].delay <= 0.0) {
			_play_thunder(pending_thunder[i].strength);
			pending_thunder.remove_at(i);
		} else {
			i++;
		}
	}
}

// Thunder.

void WeatherSoundEffect::_on_lightning_struck(const Vector3 &p_position, float p_distance) {
	float near = 300.0f;
	float far = 3000.0f;
	const Ref<WeatherEffect> lightning = weather ? weather->get_effect(SNAME("WeatherLightningEffect")) : Ref<WeatherEffect>();
	if (lightning.is_valid()) {
		near = lightning->get(SNAME("distance_min"));
		far = lightning->get(SNAME("distance_max"));
	}
	Thunder thunder;
	// Light arrives at once; the sound takes its time.
	thunder.delay = p_distance / SPEED_OF_SOUND;
	thunder.strength = CLAMP(1.0f - Math::inverse_lerp(near, MAX(far, near + 1.0f), p_distance), 0.0f, 1.0f);
	pending_thunder.push_back(thunder);
}

Ref<AudioStream> WeatherSoundEffect::_pick_thunder(float p_strength) {
	LocalVector<Ref<AudioStream>> near_list;
	LocalVector<Ref<AudioStream>> far_list;
	for (const Variant &sound : near_thunder_sounds) {
		if (Ref<AudioStream>(sound).is_valid()) {
			near_list.push_back(sound);
		}
	}
	for (const Variant &sound : thunder_sounds) {
		if (Ref<AudioStream>(sound).is_valid()) {
			far_list.push_back(sound);
		}
	}
	if (_poll_synthesis()) {
		if (near_list.is_empty()) {
			near_list.push_back(synthesized[SOUND_THUNDER_NEAR_1]);
			near_list.push_back(synthesized[SOUND_THUNDER_NEAR_2]);
		}
		if (far_list.is_empty()) {
			far_list.push_back(synthesized[SOUND_THUNDER_FAR_1]);
			far_list.push_back(synthesized[SOUND_THUNDER_FAR_2]);
			far_list.push_back(synthesized[SOUND_THUNDER_MEDIUM]);
		}
	}
	// Near strikes crack; far ones only rumble.
	const LocalVector<Ref<AudioStream>> &list = (p_strength > 0.65f && !near_list.is_empty()) || far_list.is_empty() ? near_list : far_list;
	if (list.is_empty()) {
		return Ref<AudioStream>();
	}
	return list[rng.rand(list.size())];
}

void WeatherSoundEffect::_play_thunder(float p_strength) {
	const Ref<AudioStream> stream = _pick_thunder(p_strength);
	if (stream.is_null()) {
		return;
	}
	AudioStreamPlayer *player = thunder_players[0]->is_playing() ? thunder_players[1] : thunder_players[0];
	player->set_stream(stream);
	// Pitch varies a little each clap, so it never sounds like the same file.
	player->set_pitch_scale(Math::lerp(0.9f, 1.05f, p_strength) * rng.random(0.97f, 1.03f));
	player->set_volume_db(volume_db + thunder_volume_db + Math::linear_to_db(Math::lerp(0.25f, 1.0f, p_strength)));
	thunder_filter->set_cutoff(Math::lerp(600.0f, 12000.0f, p_strength * p_strength));
	player->play();
}

bool WeatherSoundEffect::is_ready() const {
	return streams_applied;
}

float WeatherSoundEffect::get_rain_amount() const {
	return rain_amount;
}

float WeatherSoundEffect::get_wind_amount() const {
	return wind_amount;
}

int WeatherSoundEffect::get_pending_thunder_count() const {
	return pending_thunder.size();
}

// Settings.

void WeatherSoundEffect::set_volume_db(float p_db) {
	volume_db = p_db;
	emit_changed();
}

float WeatherSoundEffect::get_volume_db() const {
	return volume_db;
}

void WeatherSoundEffect::set_rain_volume_db(float p_db) {
	rain_volume_db = p_db;
	emit_changed();
}

float WeatherSoundEffect::get_rain_volume_db() const {
	return rain_volume_db;
}

void WeatherSoundEffect::set_wind_volume_db(float p_db) {
	wind_volume_db = p_db;
	emit_changed();
}

float WeatherSoundEffect::get_wind_volume_db() const {
	return wind_volume_db;
}

void WeatherSoundEffect::set_thunder_volume_db(float p_db) {
	thunder_volume_db = p_db;
	emit_changed();
}

float WeatherSoundEffect::get_thunder_volume_db() const {
	return thunder_volume_db;
}

void WeatherSoundEffect::set_wind_full_speed(float p_speed) {
	wind_full_speed = MAX(0.1f, p_speed);
	emit_changed();
}

float WeatherSoundEffect::get_wind_full_speed() const {
	return wind_full_speed;
}

void WeatherSoundEffect::set_audio_bus(const StringName &p_bus) {
	audio_bus = p_bus;
	if (rain_player) {
		for (AudioStreamPlayer *player : { rain_player, heavy_rain_player, wind_player }) {
			player->set_bus(audio_bus);
		}
		AudioServer *audio = AudioServer::get_singleton();
		const int index = audio->get_bus_index(THUNDER_BUS);
		if (index != -1) {
			audio->set_bus_send(index, audio_bus);
		}
	}
	emit_changed();
}

StringName WeatherSoundEffect::get_audio_bus() const {
	return audio_bus;
}

void WeatherSoundEffect::set_rain_sound(const Ref<AudioStream> &p_sound) {
	rain_sound = p_sound;
	rain_loop = _looping(p_sound);
	_apply_streams();
	emit_changed();
}

Ref<AudioStream> WeatherSoundEffect::get_rain_sound() const {
	return rain_sound;
}

void WeatherSoundEffect::set_heavy_rain_sound(const Ref<AudioStream> &p_sound) {
	heavy_rain_sound = p_sound;
	heavy_rain_loop = _looping(p_sound);
	_apply_streams();
	emit_changed();
}

Ref<AudioStream> WeatherSoundEffect::get_heavy_rain_sound() const {
	return heavy_rain_sound;
}

void WeatherSoundEffect::set_wind_sound(const Ref<AudioStream> &p_sound) {
	wind_sound = p_sound;
	wind_loop = _looping(p_sound);
	_apply_streams();
	emit_changed();
}

Ref<AudioStream> WeatherSoundEffect::get_wind_sound() const {
	return wind_sound;
}

void WeatherSoundEffect::set_thunder_sounds(const TypedArray<AudioStream> &p_sounds) {
	thunder_sounds = p_sounds;
	emit_changed();
}

TypedArray<AudioStream> WeatherSoundEffect::get_thunder_sounds() const {
	return thunder_sounds;
}

void WeatherSoundEffect::set_near_thunder_sounds(const TypedArray<AudioStream> &p_sounds) {
	near_thunder_sounds = p_sounds;
	emit_changed();
}

TypedArray<AudioStream> WeatherSoundEffect::get_near_thunder_sounds() const {
	return near_thunder_sounds;
}

void WeatherSoundEffect::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "audio_bus" && AudioServer::get_singleton()) {
		String options;
		for (int i = 0; i < AudioServer::get_singleton()->get_bus_count(); i++) {
			options += (i > 0 ? "," : "") + AudioServer::get_singleton()->get_bus_name(i);
		}
		p_property.hint_string = options;
	}
}

void WeatherSoundEffect::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_volume_db", "db"), &WeatherSoundEffect::set_volume_db);
	ClassDB::bind_method(D_METHOD("get_volume_db"), &WeatherSoundEffect::get_volume_db);
	ClassDB::bind_method(D_METHOD("set_rain_volume_db", "db"), &WeatherSoundEffect::set_rain_volume_db);
	ClassDB::bind_method(D_METHOD("get_rain_volume_db"), &WeatherSoundEffect::get_rain_volume_db);
	ClassDB::bind_method(D_METHOD("set_wind_volume_db", "db"), &WeatherSoundEffect::set_wind_volume_db);
	ClassDB::bind_method(D_METHOD("get_wind_volume_db"), &WeatherSoundEffect::get_wind_volume_db);
	ClassDB::bind_method(D_METHOD("set_thunder_volume_db", "db"), &WeatherSoundEffect::set_thunder_volume_db);
	ClassDB::bind_method(D_METHOD("get_thunder_volume_db"), &WeatherSoundEffect::get_thunder_volume_db);
	ClassDB::bind_method(D_METHOD("set_wind_full_speed", "speed"), &WeatherSoundEffect::set_wind_full_speed);
	ClassDB::bind_method(D_METHOD("get_wind_full_speed"), &WeatherSoundEffect::get_wind_full_speed);
	ClassDB::bind_method(D_METHOD("set_audio_bus", "bus"), &WeatherSoundEffect::set_audio_bus);
	ClassDB::bind_method(D_METHOD("get_audio_bus"), &WeatherSoundEffect::get_audio_bus);
	ClassDB::bind_method(D_METHOD("set_rain_sound", "sound"), &WeatherSoundEffect::set_rain_sound);
	ClassDB::bind_method(D_METHOD("get_rain_sound"), &WeatherSoundEffect::get_rain_sound);
	ClassDB::bind_method(D_METHOD("set_heavy_rain_sound", "sound"), &WeatherSoundEffect::set_heavy_rain_sound);
	ClassDB::bind_method(D_METHOD("get_heavy_rain_sound"), &WeatherSoundEffect::get_heavy_rain_sound);
	ClassDB::bind_method(D_METHOD("set_wind_sound", "sound"), &WeatherSoundEffect::set_wind_sound);
	ClassDB::bind_method(D_METHOD("get_wind_sound"), &WeatherSoundEffect::get_wind_sound);
	ClassDB::bind_method(D_METHOD("set_thunder_sounds", "sounds"), &WeatherSoundEffect::set_thunder_sounds);
	ClassDB::bind_method(D_METHOD("get_thunder_sounds"), &WeatherSoundEffect::get_thunder_sounds);
	ClassDB::bind_method(D_METHOD("set_near_thunder_sounds", "sounds"), &WeatherSoundEffect::set_near_thunder_sounds);
	ClassDB::bind_method(D_METHOD("get_near_thunder_sounds"), &WeatherSoundEffect::get_near_thunder_sounds);
	ClassDB::bind_method(D_METHOD("is_ready"), &WeatherSoundEffect::is_ready);
	ClassDB::bind_method(D_METHOD("get_rain_amount"), &WeatherSoundEffect::get_rain_amount);
	ClassDB::bind_method(D_METHOD("get_wind_amount"), &WeatherSoundEffect::get_wind_amount);
	ClassDB::bind_method(D_METHOD("get_pending_thunder_count"), &WeatherSoundEffect::get_pending_thunder_count);
	ClassDB::bind_static_method("WeatherSoundEffect", D_METHOD("synthesize", "sound"), &WeatherSoundEffect::synthesize);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "volume_db", PROPERTY_HINT_RANGE, "-80,24,0.1,suffix:dB"), "set_volume_db", "get_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain_volume_db", PROPERTY_HINT_RANGE, "-40,20,0.5,suffix:dB"), "set_rain_volume_db", "get_rain_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_volume_db", PROPERTY_HINT_RANGE, "-40,20,0.5,suffix:dB"), "set_wind_volume_db", "get_wind_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "thunder_volume_db", PROPERTY_HINT_RANGE, "-40,20,0.5,suffix:dB"), "set_thunder_volume_db", "get_thunder_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_full_speed", PROPERTY_HINT_RANGE, "1,60,0.1,or_greater,suffix:m/s"), "set_wind_full_speed", "get_wind_full_speed");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "audio_bus", PROPERTY_HINT_ENUM, "Master"), "set_audio_bus", "get_audio_bus");
	ADD_GROUP("Own Sounds", "");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "rain_sound", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_rain_sound", "get_rain_sound");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "heavy_rain_sound", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_heavy_rain_sound", "get_heavy_rain_sound");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "wind_sound", PROPERTY_HINT_RESOURCE_TYPE, "AudioStream"), "set_wind_sound", "get_wind_sound");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "thunder_sounds", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("AudioStream")), "set_thunder_sounds", "get_thunder_sounds");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "near_thunder_sounds", PROPERTY_HINT_ARRAY_TYPE, MAKE_RESOURCE_TYPE_HINT("AudioStream")), "set_near_thunder_sounds", "get_near_thunder_sounds");

	BIND_ENUM_CONSTANT(SOUND_RAIN_LIGHT);
	BIND_ENUM_CONSTANT(SOUND_RAIN_HEAVY);
	BIND_ENUM_CONSTANT(SOUND_WIND);
	BIND_ENUM_CONSTANT(SOUND_THUNDER_FAR_1);
	BIND_ENUM_CONSTANT(SOUND_THUNDER_FAR_2);
	BIND_ENUM_CONSTANT(SOUND_THUNDER_MEDIUM);
	BIND_ENUM_CONSTANT(SOUND_THUNDER_NEAR_1);
	BIND_ENUM_CONSTANT(SOUND_THUNDER_NEAR_2);
	BIND_ENUM_CONSTANT(SOUND_MAX);
}
