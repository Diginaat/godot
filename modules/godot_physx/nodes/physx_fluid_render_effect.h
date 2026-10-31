/**************************************************************************/
/*  physx_fluid_render_effect.h                                           */
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

#include "core/math/color.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/resources/compositor.h"
#include "servers/rendering/rendering_device.h"

class Camera3D;
class QuadMesh;
class Shader;
class ShaderMaterial;
class Texture2DRD;
class World3D;
class RenderData;
class RenderSceneBuffersRD;

// Screen-space surface for PhysXParticleFluid3D (screen_space on): instead of
// marching an isosurface mesh every step, the particles are splatted as
// spheres into a depth + thickness buffer each frame, the depth is smoothed,
// and the surface is shaded straight into the frame (refraction, absorption,
// fresnel, a sun highlight). Cost follows the pixels the fluid covers, not its
// volume, and nothing is read back.
//
// PBD foam is drawn here too, as soft white splats over the surface (fading
// with how far below it they are). It runs before the transparent pass and
// writes the liquid's depth into the frame's depth buffer, so transparent
// geometry sorts against it, and so does PhysXFlowRenderEffect (after
// transparent). Installed by PhysXCompositorEffects.
class PhysXFluidRenderEffect : public CompositorEffect {
	GDCLASS(PhysXFluidRenderEffect, CompositorEffect);

public:
	// What one fluid hands the effect each step (main thread -> render thread).
	struct Snapshot {
		PackedFloat32Array points; // per particle: world x, y, z, splat radius
		PackedFloat32Array foam; // per foam particle: world x, y, z, splat radius
		Color foam_color = Color(1, 1, 1, 0.85); // a: the most a foam patch covers
		Color absorption = Color(6.3, 1.6, 0.8); // per meter of fluid, per channel
		float refraction = 0.04f; // screen-space offset at full thickness (see PROXY_SHADER)
		float smoothing = 0.04f; // world radius of the depth smoothing
		float depth_threshold = 0.04f; // a depth jump larger than this is an edge the smoothing keeps
		float fade_thickness = 0.06f; // fluid thinner than this fades into the scene
		int resolution_divisor = 2; // frame pixels per surface texel, each way (1, 2 or 4)
		Color surface = Color(0, 0, 0, 0); // body colour (linear); a: opacity per meter of fluid, 0 = clear
		float roughness = 0.05f; // of the surface, for Godot's lighting
		// The splatted spheres overlap: their summed chords over this is the
		// fluid's own thickness (a particle's volume over its sphere's).
		float thickness_scale = 1.0f / 14.137f;
		float metallic = 0.0f;
		Color emission = Color(0, 0, 0);
		Vector3 center;
	};

	static void set_fluid(const ObjectID &p_fluid, const Snapshot &p_snapshot);
	static void set_foam(const ObjectID &p_fluid, const PackedFloat32Array &p_foam);
	static void remove_fluid(const ObjectID &p_fluid);

	static void attach_world(World3D *p_world);
	static void detach_world(const ObjectID &p_world);
	static void install(World3D *p_world, Camera3D *p_camera);
	static Ref<PhysXFluidRenderEffect> get_shared();
	static void free_shared();

	PhysXFluidRenderEffect();
	~PhysXFluidRenderEffect();

private:
	static Mutex mutex;
	static HashMap<ObjectID, Snapshot> fluids;

	static HashMap<ObjectID, int> worlds; // fluids in screen-space mode, per world
	// The quad that shades the surface in each world (see PROXY_SHADER), its
	// material, and the surface textures it reads (render thread -> main thread).
	static HashMap<ObjectID, RID> proxies;
	static Ref<QuadMesh> proxy_mesh;
	static Ref<ShaderMaterial> proxy_material;
	static Ref<Shader> proxy_shader;
	static Ref<Texture2DRD> surf_a_texture, surf_b_texture;
	static RID published_a, published_b;
	static bool textures_hooked, proxies_shown;
	static uint64_t ready_since; // Engine::get_frames_drawn() when it could first show
	static void _update_proxy(Camera3D *p_camera);

	// GPU state (render thread only).
	static constexpr uint32_t PARAMS_FLOATS = 96; // the shaders' Params block, std140
	bool failed = false;
	RID shaders[6];
	RID pipelines[6];
	RID linear_sampler, point_sampler;
	RID params_buffer;
	RID points_buffer;
	uint32_t points_capacity = 0;
	RID foam_buffer;
	uint32_t foam_capacity = 0;
	Size2i size; // the surface buffers
	Size2i frame_size; // the full-resolution targets
	RID depth_u, thick_u, foam_depth_u, foam_u, smooth_a, smooth_b;
	RID surf_a, surf_b, liquid_depth; // the composite: the surface for the proxy, and the depth buffer's new value
	struct Retired {
		RID rid;
		uint64_t frame = 0;
	};
	LocalVector<Retired> retired;
	RID write_shader;
	HashMap<int64_t, RID> write_pipelines; // per framebuffer format

	bool _ensure(RenderingDevice *p_rd);
	void _ensure_targets(RenderingDevice *p_rd, const Size2i &p_size, const Size2i &p_frame);
	void _free_targets(RenderingDevice *p_rd, bool p_now);
	void _dispatch(RenderingDevice *p_rd, int p_pass, const Vector<RD::Uniform> &p_uniforms, uint32_t p_x, uint32_t p_y, const void *p_push, uint32_t p_push_size);
	void _write(RenderingDevice *p_rd, RenderSceneBuffersRD *p_buffers, const RID &p_depth);
	void _render(int p_callback_type, const RenderData *p_render_data);

protected:
	static void _bind_methods() {}
};
