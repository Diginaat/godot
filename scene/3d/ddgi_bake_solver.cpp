/**************************************************************************/
/*  ddgi_bake_solver.cpp                                                  */
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

#include "ddgi_bake_solver.h"

#include "core/io/image.h"
#include "core/math/vector2i.h"
#include "core/math/vector3i.h"
#include "core/object/worker_thread_pool.h"

namespace {

// DDGI_PROBE_* in ddgi_inc.glsl.
enum ProbeState {
	PROBE_NEW = 0,
	PROBE_ACTIVE = 1,
	PROBE_INACTIVE = 2,
	PROBE_INSIDE = 3,
	PROBE_SCROLLED = 4,
};

float sign_of(float p_v) {
	return p_v > 0.0f ? 1.0f : (p_v < 0.0f ? -1.0f : 0.0f);
}

// As oct_inc.glsl: vec3_to_oct() returns 0..1, oct_to_vec3() takes -1..1.
Vector2 vec3_to_oct(Vector3 p_n) {
	p_n /= Math::abs(p_n.x) + Math::abs(p_n.y) + Math::abs(p_n.z);
	Vector2 v(p_n.x, p_n.y);
	if (p_n.z < 0.0f) {
		v = Vector2((1.0f - Math::abs(p_n.y)) * (p_n.x >= 0.0f ? 1.0f : -1.0f), (1.0f - Math::abs(p_n.x)) * (p_n.y >= 0.0f ? 1.0f : -1.0f));
	}
	return v * 0.5f + Vector2(0.5f, 0.5f);
}

Vector3 oct_to_vec3(const Vector2 &p_e) {
	Vector3 v(p_e.x, p_e.y, 1.0f - Math::abs(p_e.x) - Math::abs(p_e.y));
	float t = MAX(-v.z, 0.0f);
	v.x += t * -sign_of(v.x);
	v.y += t * -sign_of(v.y);
	return v.normalized();
}

// ddgi_border_source() in ddgi_inc.glsl.
Vector2i border_source(const Vector2i &p_texel, int p_n) {
	bool left = p_texel.x < 0, right = p_texel.x >= p_n, top = p_texel.y < 0, bottom = p_texel.y >= p_n;
	if ((left || right) && (top || bottom)) {
		return Vector2i(left ? p_n - 1 : 0, top ? p_n - 1 : 0);
	}
	if (top || bottom) {
		return Vector2i(p_n - 1 - p_texel.x, top ? 0 : p_n - 1);
	}
	return Vector2i(left ? 0 : p_n - 1, p_n - 1 - p_texel.y);
}

// A probe atlas as floats: one tile of texels x texels (plus a border) per
// probe, per_row tiles per row (a power of two).
struct Atlas {
	int width = 0;
	int height = 0;
	int channels = 0;
	int texels = 0;
	int per_row = 0;
	int shift = 0;
	Vector<float> data;

	bool load(const Ref<Image> &p_image, Image::Format p_format, int p_channels, int p_texels) {
		if (p_image.is_null() || p_texels <= 0) {
			return false;
		}
		Ref<Image> image;
		image.instantiate();
		image->copy_internals_from(p_image);
		image->convert(p_format);
		width = image->get_width();
		height = image->get_height();
		channels = p_channels;
		texels = p_texels;
		per_row = width / (texels + 2);
		if (per_row <= 0 || (per_row & (per_row - 1)) != 0) {
			return false;
		}
		shift = 0;
		while ((1 << shift) < per_row) {
			shift++;
		}
		Vector<uint8_t> bytes = image->get_data();
		data.resize(width * height * channels);
		if (bytes.size() != int64_t(data.size()) * int64_t(sizeof(float))) {
			return false;
		}
		memcpy(data.ptrw(), bytes.ptr(), bytes.size());
		return true;
	}

	Ref<Image> to_image(Image::Format p_float_format, Image::Format p_format) const {
		Vector<uint8_t> bytes;
		bytes.resize(data.size() * sizeof(float));
		memcpy(bytes.ptrw(), data.ptr(), bytes.size());
		Ref<Image> image = Image::create_from_data(width, height, false, p_float_format, bytes);
		image->convert(p_format);
		return image;
	}

	Vector2i origin(uint32_t p_probe) const {
		return Vector2i(int(p_probe & uint32_t(per_row - 1)), int(p_probe >> shift)) * (texels + 2) + Vector2i(1, 1);
	}

	float *texel(uint32_t p_probe, const Vector2i &p_texel) {
		Vector2i p = origin(p_probe) + p_texel;
		return data.ptrw() + (int64_t(p.y) * width + p.x) * channels;
	}

	const float *texel(uint32_t p_probe, const Vector2i &p_texel) const {
		Vector2i p = origin(p_probe) + p_texel;
		return data.ptr() + (int64_t(p.y) * width + p.x) * channels;
	}

	// What a linear sampler returns at ddgi_atlas_uv(p_probe, p_dir).
	void sample(uint32_t p_probe, const Vector3 &p_dir, float *r_out) const {
		Vector2 pos = Vector2(origin(p_probe)) + vec3_to_oct(p_dir) * float(texels) - Vector2(0.5f, 0.5f);
		int x0 = int(Math::floor(pos.x));
		int y0 = int(Math::floor(pos.y));
		float fx = pos.x - float(x0);
		float fy = pos.y - float(y0);
		for (int c = 0; c < channels; c++) {
			r_out[c] = 0.0f;
		}
		for (int k = 0; k < 4; k++) {
			int x = CLAMP(x0 + (k & 1), 0, width - 1);
			int y = CLAMP(y0 + (k >> 1), 0, height - 1);
			float w = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
			const float *p = data.ptr() + (int64_t(y) * width + x) * channels;
			for (int c = 0; c < channels; c++) {
				r_out[c] += p[c] * w;
			}
		}
	}

	void copy_borders(uint32_t p_probe) {
		for (int y = -1; y <= texels; y++) {
			for (int x = -1; x <= texels; x++) {
				if (x >= 0 && x < texels && y >= 0 && y < texels) {
					continue;
				}
				const float *src = texel(p_probe, border_source(Vector2i(x, y), texels));
				float *dst = texel(p_probe, Vector2i(x, y));
				for (int c = 0; c < channels; c++) {
					dst[c] = src[c];
				}
			}
		}
	}
};

// One bake (RenderDDGI::get_probe_data()).
struct Bake {
	int cascades = 0;
	Vector3i grid;
	float spacing = 0.0f;
	Vector3 center;
	Atlas irradiance;
	Atlas distance;
	PackedFloat32Array probes; // Offset (xyz, world units) and state per probe.

	bool load(const Dictionary &p_data) {
		cascades = p_data.get("cascades", 0);
		grid = p_data.get("probe_grid", Vector3i());
		spacing = p_data.get("probe_spacing", 0.0f);
		center = p_data.get("center", Vector3());
		probes = p_data.get("probes", PackedFloat32Array());
		if (cascades <= 0 || grid.x < 2 || grid.y < 2 || grid.z < 2 || spacing <= 0.0f) {
			return false;
		}
		if (probes.size() != int64_t(per_cascade()) * cascades * 4) {
			return false;
		}
		return irradiance.load(p_data.get("irradiance", Ref<Image>()), Image::FORMAT_RGBAF, 4, p_data.get("irradiance_texels", 0)) &&
				distance.load(p_data.get("distance", Ref<Image>()), Image::FORMAT_RGF, 2, p_data.get("distance_texels", 0));
	}

	int per_cascade() const {
		return grid.x * grid.y * grid.z;
	}

	uint32_t index(int p_cascade, const Vector3i &p_logical) const {
		return uint32_t(p_cascade * per_cascade() + p_logical.x + p_logical.y * grid.x + p_logical.z * grid.x * grid.y);
	}

	float cascade_spacing(int p_cascade) const {
		return spacing * float(1 << p_cascade);
	}

	Vector3 position(int p_cascade, const Vector3i &p_logical) const {
		uint32_t i = index(p_cascade, p_logical);
		const float *o = probes.ptr() + i * 4;
		return center + (Vector3(p_logical) - (Vector3(grid) - Vector3(1, 1, 1)) * 0.5f) * cascade_spacing(p_cascade) + Vector3(o[0], o[1], o[2]);
	}

	int state(uint32_t p_probe) const {
		return int(probes[p_probe * 4 + 3]);
	}

	static bool usable(int p_state) {
		return p_state == PROBE_ACTIVE || p_state == PROBE_INACTIVE;
	}
};

struct Row {
	uint32_t probe[8]; // Coarse probe, index within the cascade.
	float weight[8]; // Normalized.
	int count = 0;
	float target[3]; // Square root of the fine probe's irradiance.
};

struct CascadeJob {
	const Bake *coarse = nullptr;
	const Bake *fine = nullptr;
	DDGIBakeSolver::Settings settings;
	int cascade = 0;
	Vector<Vector3i> fine_probes; // Active fine probes (logical).
	Vector<uint32_t> edge_a; // Smoothness edges between coarse probes that see each other.
	Vector<uint32_t> edge_b;
	// Written by the texel tasks, each its own entries.
	LocalVector<float> solved; // Per texel, per coarse probe: rgb.
	// Per texel, for the report.
	LocalVector<double> residual_before;
	LocalVector<double> residual_after;
	LocalVector<int> row_count;

	// Weight of a coarse probe for a lookup point: ddgi_probe_weight() in
	// ddgi_sample_inc.glsl.
	float probe_weight(uint32_t p_probe, const Vector3 &p_probe_pos, const Vector3i &p_offs, const Vector3 &p_alpha, const Vector3 &p_point, const Vector3 &p_normal) const {
		int state = coarse->state(p_probe);
		if (!Bake::usable(state)) {
			return 0.0f;
		}
		Vector3 to_probe = p_probe_pos - p_point;
		float dist = to_probe.length();
		Vector3 dir = dist > 0.0f ? to_probe / dist : p_normal;
		Vector3 tri(p_offs.x ? p_alpha.x : 1.0f - p_alpha.x, p_offs.y ? p_alpha.y : 1.0f - p_alpha.y, p_offs.z ? p_alpha.z : 1.0f - p_alpha.z);
		float trilinear = tri.x * tri.y * tri.z;
		float wrap = (dir.dot(p_normal) + 1.0f) * 0.5f;
		float weight = wrap * wrap + 0.2f;
		float moments[2];
		coarse->distance.sample(p_probe, -dir, moments);
		if (dist > moments[0]) {
			float variance = Math::abs(moments[0] * moments[0] - moments[1]);
			float d = dist - moments[0];
			float chebyshev = variance / (variance + d * d);
			weight *= MAX(chebyshev * chebyshev * chebyshev, 0.0f);
		}
		weight = MAX(weight, 0.000001f);
		const float crush_threshold = 0.2f;
		if (weight < crush_threshold) {
			weight *= weight * weight / (crush_threshold * crush_threshold);
		}
		weight *= trilinear;
		if (state == PROBE_INACTIVE) {
			weight *= 0.05f;
		}
		return weight;
	}

	void solve_texel(uint32_t p_texel, int p_unused) {
		const int n = coarse->irradiance.texels;
		const Vector2i t(int(p_texel) % n, int(p_texel) / n);
		const Vector3 normal = oct_to_vec3((Vector2(t) + Vector2(0.5f, 0.5f)) / float(n) * 2.0f - Vector2(1.0f, 1.0f));
		const float sc = coarse->cascade_spacing(cascade);
		const Vector3i grid = coarse->grid;
		const int count = coarse->per_cascade();
		const uint32_t base_index = uint32_t(cascade * count);

		// Rows: the renderer's blend of the coarse probes at each fine probe,
		// looked up with this texel's direction as the surface normal (biased
		// along it, as the renderer does; the view bias depends on the camera
		// and is left out).
		LocalVector<Row> rows;
		rows.reserve(fine_probes.size());
		for (const Vector3i &lf : fine_probes) {
			Vector3 point = fine->position(cascade, lf) + normal * (settings.normal_bias * sc);
			Vector3 g = (point - coarse->center) / sc + (Vector3(grid) - Vector3(1, 1, 1)) * 0.5f;
			Vector3i base(CLAMP(int(Math::floor(g.x)), 0, grid.x - 2), CLAMP(int(Math::floor(g.y)), 0, grid.y - 2), CLAMP(int(Math::floor(g.z)), 0, grid.z - 2));
			Vector3 alpha = (g - Vector3(base)).clamp(Vector3(), Vector3(1, 1, 1));
			Row row;
			float sum = 0.0f;
			for (int i = 0; i < 8; i++) {
				Vector3i offs(i & 1, (i >> 1) & 1, (i >> 2) & 1);
				Vector3i logical = base + offs;
				uint32_t probe = coarse->index(cascade, logical);
				float w = probe_weight(probe, coarse->position(cascade, logical), offs, alpha, point, normal);
				if (w <= 0.0f) {
					continue;
				}
				row.probe[row.count] = probe - base_index;
				row.weight[row.count] = w;
				row.count++;
				sum += w;
			}
			if (row.count == 0 || sum <= 0.0f) {
				continue;
			}
			for (int i = 0; i < row.count; i++) {
				row.weight[i] /= sum;
			}
			const float *e = fine->irradiance.texel(fine->index(cascade, lf), t);
			for (int c = 0; c < 3; c++) {
				row.target[c] = Math::sqrt(MAX(e[c], 0.0f));
			}
			rows.push_back(row);
		}
		row_count[p_texel] = int(rows.size());

		LocalVector<float> s0;
		s0.resize(count * 3);
		for (int i = 0; i < count; i++) {
			const float *e = coarse->irradiance.texel(base_index + uint32_t(i), t);
			for (int c = 0; c < 3; c++) {
				s0[i * 3 + c] = Math::sqrt(MAX(e[c], 0.0f));
			}
		}

		const float smoothness = settings.smoothness;
		const float prior = settings.prior;
		// M x = A^T A x + smoothness L x + prior x, for one channel.
		auto apply = [&](const LocalVector<float> &p_x, LocalVector<float> &r_y) {
			for (int i = 0; i < count; i++) {
				r_y[i] = prior * p_x[i];
			}
			for (const Row &row : rows) {
				float v = 0.0f;
				for (int k = 0; k < row.count; k++) {
					v += row.weight[k] * p_x[row.probe[k]];
				}
				for (int k = 0; k < row.count; k++) {
					r_y[row.probe[k]] += row.weight[k] * v;
				}
			}
			for (int e = 0; e < edge_a.size(); e++) {
				float d = smoothness * (p_x[edge_a[e]] - p_x[edge_b[e]]);
				r_y[edge_a[e]] += d;
				r_y[edge_b[e]] -= d;
			}
		};
		auto residual = [&](const LocalVector<float> &p_x, int p_channel) {
			double sum = 0.0;
			for (const Row &row : rows) {
				float v = 0.0f;
				for (int k = 0; k < row.count; k++) {
					v += row.weight[k] * p_x[row.probe[k]];
				}
				sum += double(v - row.target[p_channel]) * double(v - row.target[p_channel]);
			}
			return sum;
		};

		LocalVector<float> x, b, r, p, ap;
		x.resize(count);
		b.resize(count);
		r.resize(count);
		p.resize(count);
		ap.resize(count);
		double before = 0.0;
		double after = 0.0;
		for (int c = 0; c < 3; c++) {
			for (int i = 0; i < count; i++) {
				x[i] = s0[i * 3 + c];
				b[i] = prior * x[i];
			}
			for (const Row &row : rows) {
				for (int k = 0; k < row.count; k++) {
					b[row.probe[k]] += row.weight[k] * row.target[c];
				}
			}
			before += residual(x, c);
			if (!rows.is_empty()) {
				apply(x, ap);
				double rs = 0.0;
				double bb = 0.0;
				for (int i = 0; i < count; i++) {
					r[i] = b[i] - ap[i];
					p[i] = r[i];
					rs += double(r[i]) * r[i];
					bb += double(b[i]) * b[i];
				}
				for (int it = 0; it < settings.iterations && rs > 1e-12 * bb; it++) {
					apply(p, ap);
					double pap = 0.0;
					for (int i = 0; i < count; i++) {
						pap += double(p[i]) * ap[i];
					}
					if (pap <= 0.0) {
						break;
					}
					float a = float(rs / pap);
					double rs_new = 0.0;
					for (int i = 0; i < count; i++) {
						x[i] += a * p[i];
						r[i] -= a * ap[i];
						rs_new += double(r[i]) * r[i];
					}
					float beta = float(rs_new / rs);
					rs = rs_new;
					for (int i = 0; i < count; i++) {
						p[i] = r[i] + beta * p[i];
					}
				}
			}
			after += residual(x, c);
			for (int i = 0; i < count; i++) {
				solved[(int64_t(p_texel) * count + i) * 3 + c] = MAX(x[i], 0.0f);
			}
		}
		residual_before[p_texel] = before;
		residual_after[p_texel] = after;
	}
};

} // namespace

Dictionary DDGIBakeSolver::solve(const Dictionary &p_coarse, const Dictionary &p_fine, const Settings &p_settings, Report *r_report, String *r_error) {
	Bake coarse;
	Bake fine;
	if (!coarse.load(p_coarse) || !fine.load(p_fine)) {
		*r_error = "the probe data couldn't be read";
		return Dictionary();
	}
	if (fine.cascades != coarse.cascades || fine.grid != coarse.grid * 2 - Vector3i(1, 1, 1) || !Math::is_equal_approx(fine.spacing * 2.0f, coarse.spacing) || !fine.center.is_equal_approx(coarse.center) || fine.irradiance.texels != coarse.irradiance.texels) {
		*r_error = "the fine bake doesn't fit the coarse one (grid, spacing, center or texels)";
		return Dictionary();
	}

	Atlas result = coarse.irradiance;
	const int n = coarse.irradiance.texels;
	const int count = coarse.per_cascade();
	double before = 0.0;
	double after = 0.0;
	int64_t rows = 0;
	double change = 0.0;
	double total = 0.0;
	int samples = 0;
	for (int c = 0; c < coarse.cascades; c++) {
		CascadeJob job;
		job.coarse = &coarse;
		job.fine = &fine;
		job.settings = p_settings;
		job.cascade = c;
		for (int z = 0; z < fine.grid.z; z++) {
			for (int y = 0; y < fine.grid.y; y++) {
				for (int x = 0; x < fine.grid.x; x++) {
					if (fine.state(fine.index(c, Vector3i(x, y, z))) == PROBE_ACTIVE) {
						job.fine_probes.push_back(Vector3i(x, y, z));
					}
				}
			}
		}
		samples += job.fine_probes.size();
		// Smoothness only between neighbors that see each other (each one's
		// mean distance toward the other reaches it), so light doesn't smooth
		// through walls.
		for (int z = 0; z < coarse.grid.z; z++) {
			for (int y = 0; y < coarse.grid.y; y++) {
				for (int x = 0; x < coarse.grid.x; x++) {
					Vector3i la(x, y, z);
					uint32_t ia = coarse.index(c, la);
					if (!Bake::usable(coarse.state(ia))) {
						continue;
					}
					for (int axis = 0; axis < 3; axis++) {
						Vector3i lb = la;
						lb[axis]++;
						if (lb[axis] >= coarse.grid[axis]) {
							continue;
						}
						uint32_t ib = coarse.index(c, lb);
						if (!Bake::usable(coarse.state(ib))) {
							continue;
						}
						Vector3 pa = coarse.position(c, la);
						Vector3 pb = coarse.position(c, lb);
						float dist = pa.distance_to(pb);
						if (dist <= 0.0f) {
							continue;
						}
						Vector3 dir = (pb - pa) / dist;
						float ma[2];
						float mb[2];
						coarse.distance.sample(ia, dir, ma);
						coarse.distance.sample(ib, -dir, mb);
						if (ma[0] >= 0.9f * dist && mb[0] >= 0.9f * dist) {
							job.edge_a.push_back(ia - uint32_t(c * count));
							job.edge_b.push_back(ib - uint32_t(c * count));
						}
					}
				}
			}
		}
		job.solved.resize(int64_t(n) * n * count * 3);
		job.residual_before.resize(n * n);
		job.residual_after.resize(n * n);
		job.row_count.resize(n * n);
		WorkerThreadPool::GroupID group = WorkerThreadPool::get_singleton()->add_template_group_task(&job, &CascadeJob::solve_texel, 0, n * n, -1, true, "DDGI bake solve");
		WorkerThreadPool::get_singleton()->wait_for_group_task_completion(group);

		for (int t = 0; t < n * n; t++) {
			before += job.residual_before[t];
			after += job.residual_after[t];
			rows += job.row_count[t];
			Vector2i texel(t % n, t / n);
			for (int i = 0; i < count; i++) {
				uint32_t probe = uint32_t(c * count + i);
				if (!Bake::usable(coarse.state(probe))) {
					continue;
				}
				float *dst = result.texel(probe, texel);
				for (int ch = 0; ch < 3; ch++) {
					float s = job.solved[(int64_t(t) * count + i) * 3 + ch];
					float e = s * s;
					change += Math::abs(e - dst[ch]);
					total += Math::abs(dst[ch]);
					dst[ch] = e;
				}
			}
		}
		for (int i = 0; i < count; i++) {
			result.copy_borders(uint32_t(c * count + i));
		}
	}

	if (r_report) {
		r_report->samples = samples;
		double denom = MAX(double(rows) * 3.0, 1.0);
		r_report->residual_before = float(Math::sqrt(before / denom));
		r_report->residual_after = float(Math::sqrt(after / denom));
		r_report->mean_change = total > 0.0 ? float(change / total) : 0.0f;
	}

	Dictionary out = p_coarse.duplicate();
	out["irradiance"] = result.to_image(Image::FORMAT_RGBAF, Image::FORMAT_RGBAH);
	return out;
}
