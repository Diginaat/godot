/**************************************************************************/
/*  weather_sound_synth.cpp                                               */
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

#include "weather_sound_synth.h"

#include "core/math/math_funcs.h"
#include "core/math/math_funcs_binary.h"
#include "core/math/random_pcg.h"
#include "core/templates/local_vector.h"

namespace WeatherSoundSynth {

typedef LocalVector<double> Signal;

// Spectral shaping.

// In-place radix-2 FFT; the length must be a power of two. The inverse is
// scaled by 1 / n.
static void fft(Signal &r_re, Signal &r_im, bool p_inverse) {
	const uint32_t n = r_re.size();
	for (uint32_t i = 1, j = 0; i < n; i++) {
		uint32_t bit = n >> 1;
		for (; j & bit; bit >>= 1) {
			j ^= bit;
		}
		j ^= bit;
		if (i < j) {
			SWAP(r_re[i], r_re[j]);
			SWAP(r_im[i], r_im[j]);
		}
	}
	// Twiddles from a table, not a recurrence: a million steps would drift.
	Signal cos_table;
	Signal sin_table;
	cos_table.resize(n / 2);
	sin_table.resize(n / 2);
	for (uint32_t k = 0; k < n / 2; k++) {
		const double angle = Math::TAU * k / n;
		cos_table[k] = Math::cos(angle);
		sin_table[k] = p_inverse ? Math::sin(angle) : -Math::sin(angle);
	}
	for (uint32_t length = 2; length <= n; length <<= 1) {
		const uint32_t half = length / 2;
		const uint32_t step = n / length;
		for (uint32_t start = 0; start < n; start += length) {
			for (uint32_t k = 0; k < half; k++) {
				const double wr = cos_table[k * step];
				const double wi = sin_table[k * step];
				const uint32_t a = start + k;
				const uint32_t b = a + half;
				const double tr = r_re[b] * wr - r_im[b] * wi;
				const double ti = r_re[b] * wi + r_im[b] * wr;
				r_re[b] = r_re[a] - tr;
				r_im[b] = r_im[a] - ti;
				r_re[a] += tr;
				r_im[a] += ti;
			}
		}
	}
	if (p_inverse) {
		for (uint32_t i = 0; i < n; i++) {
			r_re[i] /= n;
			r_im[i] /= n;
		}
	}
}

// A smooth band-pass gain, with an optional 1/f (pink) tilt.
struct Band {
	double low;
	double high;
	double slope_low = 2.0;
	double slope_high = 2.0;
	bool pink = true;

	double operator()(double p_frequency) const {
		const double rise = Math::pow(p_frequency / low, slope_low);
		double gain = rise / (1.0 + rise);
		gain /= 1.0 + Math::pow(p_frequency / high, slope_high);
		if (pink) {
			gain /= Math::sqrt(p_frequency / low);
		}
		return gain;
	}
};

static double _lowpass(double p_frequency, double p_cutoff) {
	return 1.0 / (1.0 + Math::pow(p_frequency / p_cutoff, 4.0));
}

static double _highpass(double p_frequency, double p_cutoff) {
	const double ratio = Math::pow(p_frequency / p_cutoff, 4.0);
	return ratio / (1.0 + ratio);
}

static double _max_abs(const Signal &p_signal) {
	double peak = 0.0;
	for (const double sample : p_signal) {
		peak = MAX(peak, Math::abs(sample));
	}
	return peak + 1e-9;
}

static double _rms(const Signal &p_signal) {
	double sum = 0.0;
	for (const double sample : p_signal) {
		sum += sample * sample;
	}
	return Math::sqrt(sum / MAX((int)p_signal.size(), 1)) + 1e-9;
}

// Noise with the spectrum gain(frequency), periodic over its power-of-two
// working length, peak 1, cut to p_length samples. White noise has Gaussian
// Fourier coefficients, so the spectrum is drawn directly.
template <typename G>
static Signal _shaped_noise(RandomPCG &r_rng, uint32_t p_length, const G &p_gain) {
	const uint32_t n = Math::next_power_of_2(p_length);
	Signal re;
	Signal im;
	re.resize(n);
	im.resize(n);
	for (uint32_t k = 0; k <= n / 2; k++) {
		const double gain = p_gain(MAX((double)k * MIX_RATE / n, 1.0));
		const double a = r_rng.randfn(0.0, 1.0) * gain;
		const double b = (k == 0 || k == n / 2) ? 0.0 : r_rng.randfn(0.0, 1.0) * gain;
		re[k] = a;
		im[k] = b;
		if (k > 0 && k < n / 2) {
			re[n - k] = a;
			im[n - k] = -b;
		}
	}
	fft(re, im, true);
	re.resize(p_length);
	const double peak = _max_abs(re);
	for (double &sample : re) {
		sample /= peak;
	}
	return re;
}

// Filters by a zero-phase gain(frequency). Padded to a power of two with
// silence, so the end doesn't bleed into the start; pass a power-of-two
// length for a loop, which wraps instead.
template <typename G>
static void _filter(Signal &r_signal, const G &p_gain) {
	const uint32_t length = r_signal.size();
	const uint32_t n = Math::next_power_of_2(length);
	Signal re(r_signal);
	Signal im;
	re.resize(n);
	for (uint32_t i = length; i < n; i++) {
		re[i] = 0.0;
	}
	im.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		im[i] = 0.0;
	}
	fft(re, im, false);
	for (uint32_t k = 0; k <= n / 2; k++) {
		const double gain = p_gain(MAX((double)k * MIX_RATE / n, 1.0));
		re[k] *= gain;
		im[k] *= gain;
		if (k > 0 && k < n / 2) {
			re[n - k] *= gain;
			im[n - k] *= gain;
		}
	}
	fft(re, im, true);
	for (uint32_t i = 0; i < length; i++) {
		r_signal[i] = re[i];
	}
}

// Slow loudness wobble that repeats exactly over the loop.
static double _lfo(uint32_t p_index, uint32_t p_length, double p_cycles, double p_phase, double p_depth) {
	return 1.0 + p_depth * Math::sin(Math::TAU * p_cycles * p_index / p_length + p_phase);
}

static double _envelope(uint32_t p_index, double p_attack, double p_decay, double p_start) {
	const double t = (double)p_index / MIX_RATE - p_start;
	if (t < 0.0) {
		return 0.0;
	}
	return MIN(t / MAX(p_attack, 1e-4), 1.0) * Math::exp(-t / p_decay);
}

// Rain.

// One drop landing on soil, grass or leaves: a short, dull noise tap. No
// ringing tone: the tonal "plink" of a raindrop comes from an air bubble
// trapped under a water surface (Minnaert resonance), so drops on the ground
// sound like soft untuned taps, not like rain on a pool.
static Signal _tap(RandomPCG &r_rng, double p_seconds, double p_tau, int p_smooth) {
	const uint32_t length = MAX((uint32_t)(MIX_RATE * p_seconds), 8u);
	Signal tap;
	tap.resize(length);
	for (uint32_t i = 0; i < length; i++) {
		tap[i] = r_rng.randfn(0.0, 1.0) * Math::exp(-(double)i / MIX_RATE / p_tau);
	}
	// Soft surfaces absorb the high frequencies: smooth the tap a little
	// (a centered moving average).
	Signal smooth;
	smooth.resize(length);
	const int offset = (p_smooth - 1) / 2;
	for (uint32_t i = 0; i < length; i++) {
		double sum = 0.0;
		for (int j = 0; j < p_smooth; j++) {
			const int64_t source = (int64_t)i + offset - j;
			if (source >= 0 && source < length) {
				sum += tap[source];
			}
		}
		smooth[i] = sum / p_smooth;
	}
	const double peak = _max_abs(smooth);
	for (double &sample : smooth) {
		sample /= peak;
	}
	return smooth;
}

// Drops at random times (p_rate per second), each one of the taps. Drops near
// the end wrap around to the start, so the loop stays seamless.
static Signal _scatter(RandomPCG &r_rng, uint32_t p_length, double p_rate, const LocalVector<Signal> &p_taps, double p_amplitude_low, double p_amplitude_high) {
	Signal out;
	out.resize(p_length);
	for (double &sample : out) {
		sample = 0.0;
	}
	const int count = int(p_rate * p_length / MIX_RATE / p_taps.size());
	for (const Signal &tap : p_taps) {
		for (int i = 0; i < count; i++) {
			const uint32_t start = r_rng.rand(p_length);
			const double amplitude = r_rng.random(p_amplitude_low, p_amplitude_high) * (r_rng.rand(2) ? 1.0 : -1.0);
			for (uint32_t j = 0; j < tap.size(); j++) {
				out[(start + j) % p_length] += tap[j] * amplitude;
			}
		}
	}
	return out;
}

// Rain on open ground, built like real rain recordings sound:
//  - wash: countless distant drops blur into a smooth, slightly bright hiss;
//  - grain: thousands of tiny ticks per second, the fine crackle of real rain;
//  - taps: a few soft, dull taps of drops landing close by;
//  - roar (heavy rain): a low roar of water hitting everything at once.
static Signal _rain(RandomPCG &r_rng, double p_grain_rate, double p_tap_rate, double p_roar) {
	const uint32_t n = LOOP_SAMPLES;
	Signal out = _shaped_noise(r_rng, n, Band{ 400.0, 7000.0, 2.0, 3.0 });
	const double phase_a = r_rng.random(0.0, Math::TAU);
	const double phase_b = r_rng.random(0.0, Math::TAU);
	for (uint32_t i = 0; i < n; i++) {
		out[i] *= _lfo(i, n, 3.0, phase_a, 0.08) * _lfo(i, n, 7.0, phase_b, 0.05);
	}
	double scale = 0.6 / _rms(out);
	for (double &sample : out) {
		sample *= scale;
	}

	LocalVector<Signal> fine;
	for (int i = 0; i < 6; i++) {
		const double tau = r_rng.random(0.0004, 0.0012);
		fine.push_back(_tap(r_rng, 0.004, tau, r_rng.random(1, 3)));
	}
	Signal grain = _scatter(r_rng, n, p_grain_rate, fine, 0.2, 1.0);
	// Keep the grain in the rain's range: no rumble, no harsh top.
	_filter(grain, [](double f) { return _highpass(f, 500.0) * _lowpass(f, 8000.0); });
	scale = 0.7 / _rms(grain);
	for (uint32_t i = 0; i < n; i++) {
		out[i] += grain[i] * scale;
	}

	LocalVector<Signal> soft;
	for (int i = 0; i < 5; i++) {
		const double tau = r_rng.random(0.0015, 0.004);
		soft.push_back(_tap(r_rng, 0.012, tau, r_rng.random(4, 9)));
	}
	Signal taps = _scatter(r_rng, n, p_tap_rate, soft, 0.3, 1.0);
	_filter(taps, [](double f) { return _highpass(f, 250.0); });
	scale = 0.12 / _rms(taps);
	for (uint32_t i = 0; i < n; i++) {
		out[i] += taps[i] * scale;
	}

	if (p_roar > 0.0) {
		Signal roar = _shaped_noise(r_rng, n, Band{ 150.0, 2500.0, 2.0, 2.5 });
		const double phase = r_rng.random(0.0, Math::TAU);
		for (uint32_t i = 0; i < n; i++) {
			roar[i] *= _lfo(i, n, 2.0, phase, 0.08);
		}
		scale = p_roar / _rms(roar);
		for (uint32_t i = 0; i < n; i++) {
			out[i] += roar[i] * scale;
		}
	}
	return out;
}

// Wind: a low rush, a mid band swelling in gusts, and a faint whistle at the
// strongest gusts. Every rhythm is whole cycles over the loop.
static Signal _wind(RandomPCG &r_rng) {
	const uint32_t n = LOOP_SAMPLES;
	const Signal low = _shaped_noise(r_rng, n, Band{ 60.0, 260.0, 2.0, 2.0, false });
	const Signal mid = _shaped_noise(r_rng, n, Band{ 250.0, 800.0, 2.5, 2.5 });
	const Signal high = _shaped_noise(r_rng, n, Band{ 900.0, 2200.0, 4.0, 4.0 });
	const double gust_a = r_rng.random(0.0, Math::TAU);
	const double gust_b = r_rng.random(0.0, Math::TAU);
	const double swell = r_rng.random(0.0, Math::TAU);
	Signal out;
	out.resize(n);
	for (uint32_t i = 0; i < n; i++) {
		const double gust = _lfo(i, n, 2.0, gust_a, 0.45) * _lfo(i, n, 5.0, gust_b, 0.2);
		out[i] = low[i] * 0.8 * _lfo(i, n, 1.0, swell, 0.25);
		out[i] += mid[i] * 0.55 * gust;
		out[i] += high[i] * 0.12 * Math::pow(MAX(gust - 0.9, 0.0), 1.5);
	}
	return out;
}

// Thunder.

static Signal _thunder(RandomPCG &r_rng, double p_seconds, bool p_near, double p_far_cutoff) {
	const uint32_t n = (uint32_t)(MIX_RATE * p_seconds);
	Signal out;
	out.resize(n);
	for (double &sample : out) {
		sample = 0.0;
	}
	// Rolling rumble: sound from different parts of the channel arriving over seconds.
	const int rolls = r_rng.random(4, 7);
	for (int roll = 0; roll < rolls; roll++) {
		const double start = roll == 0 ? r_rng.random(0.0, 0.5) : r_rng.random(0.3, p_seconds * 0.45);
		const Signal rumble = _shaped_noise(r_rng, n, Band{ 30.0, p_near ? 260.0 : 200.0, 2.0, 3.0 });
		const double attack = r_rng.random(0.15, 0.6);
		const double decay = r_rng.random(0.8, 2.2);
		const double loudness = r_rng.random(0.4, 1.0) * Math::pow(0.85, (double)roll);
		for (uint32_t i = 0; i < n; i++) {
			out[i] += rumble[i] * _envelope(i, attack, decay, start) * loudness;
		}
	}
	if (p_near) {
		// Crack: a sharp, tearing burst of high frequencies, then a heavy boom.
		const Signal crack = _shaped_noise(r_rng, n, Band{ 900.0, 9000.0, 2.0, 2.0, false });
		struct Tear {
			double decay;
			double start;
			double loudness;
		};
		LocalVector<Tear> tears;
		const int tear_count = r_rng.random(5, 9);
		for (int i = 0; i < tear_count; i++) {
			const double decay = r_rng.random(0.02, 0.06);
			const double start = r_rng.random(0.0, 0.35);
			tears.push_back({ decay, start, r_rng.random(0.4, 1.0) });
		}
		const Signal boom = _shaped_noise(r_rng, n, Band{ 28.0, 130.0, 2.0, 3.0, false });
		const Signal crackle = _shaped_noise(r_rng, n, Band{ 200.0, 1600.0 });
		for (uint32_t i = 0; i < n; i++) {
			double tear = 0.0;
			for (const Tear &t : tears) {
				tear += _envelope(i, 0.002, t.decay, t.start) * t.loudness;
			}
			out[i] += crack[i] * MIN(tear, 1.2) * 0.8;
			out[i] += boom[i] * _envelope(i, 0.02, 1.3, 0.03) * 2.4;
			// Mid-range crackle that rolls off into the rumble.
			out[i] += crackle[i] * _envelope(i, 0.05, 1.0, 0.1) * 0.6;
		}
	} else {
		// Distance: the air has absorbed the high frequencies.
		_filter(out, [p_far_cutoff](double f) { return _lowpass(f, p_far_cutoff); });
	}
	// Fade the very end so it never clicks.
	const uint32_t fade = MIX_RATE / 2;
	for (uint32_t i = 0; i < fade; i++) {
		out[n - fade + i] *= 1.0 - (double)i / (fade - 1);
	}
	return out;
}

// Output.

// Scales to a loudness (p_rms_db) or a peak, and softly limits above 0.9
// so loud drops and cracks never clip.
static Vector<uint8_t> _to_pcm16(Signal &r_signal, double p_rms_db, double p_peak) {
	const double scale = p_peak > 0.0 ? p_peak / _max_abs(r_signal) : Math::pow(10.0, p_rms_db / 20.0) / _rms(r_signal);
	Vector<uint8_t> data;
	data.resize(r_signal.size() * 2);
	uint8_t *write = data.ptrw();
	for (uint32_t i = 0; i < r_signal.size(); i++) {
		double x = r_signal[i] * scale;
		if (Math::abs(x) > 0.9) {
			x = SIGN(x) * (0.9 + 0.09 * Math::tanh((Math::abs(x) - 0.9) / 0.09));
		}
		const int16_t sample = (int16_t)CLAMP(Math::round(x * 32767.0), -32767.0, 32767.0);
		write[i * 2] = (uint8_t)(sample & 0xff);
		write[i * 2 + 1] = (uint8_t)((sample >> 8) & 0xff);
	}
	return data;
}

bool is_loop(Sound p_sound) {
	return p_sound == RAIN_LIGHT || p_sound == RAIN_HEAVY || p_sound == WIND;
}

Vector<uint8_t> generate(Sound p_sound) {
	ERR_FAIL_INDEX_V(p_sound, SOUND_MAX, Vector<uint8_t>());
	RandomPCG rng(20261103 + p_sound);
	Signal signal;
	switch (p_sound) {
		case RAIN_LIGHT:
			signal = _rain(rng, 1500.0, 4.0, 0.0);
			return _to_pcm16(signal, -24.0, 0.0);
		case RAIN_HEAVY:
			signal = _rain(rng, 6000.0, 12.0, 0.6);
			return _to_pcm16(signal, -17.0, 0.0);
		case WIND:
			signal = _wind(rng);
			return _to_pcm16(signal, -22.0, 0.0);
		case THUNDER_FAR_1:
			signal = _thunder(rng, 11.0, false, 300.0);
			return _to_pcm16(signal, 0.0, 0.9);
		case THUNDER_FAR_2:
			signal = _thunder(rng, 12.0, false, 300.0);
			return _to_pcm16(signal, 0.0, 0.9);
		case THUNDER_MEDIUM:
			signal = _thunder(rng, 10.0, false, 700.0);
			return _to_pcm16(signal, 0.0, 0.9);
		case THUNDER_NEAR_1:
			signal = _thunder(rng, 9.0, true, 0.0);
			return _to_pcm16(signal, 0.0, 0.95);
		case THUNDER_NEAR_2:
			signal = _thunder(rng, 10.0, true, 0.0);
			return _to_pcm16(signal, 0.0, 0.95);
		default:
			return Vector<uint8_t>();
	}
}

} // namespace WeatherSoundSynth
