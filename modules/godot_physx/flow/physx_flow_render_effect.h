/**************************************************************************/
/*  physx_flow_render_effect.h                                            */
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

#include "flow_scatter_rd.h"

#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "scene/resources/compositor.h"

class Camera3D;
class PhysXFlow3D;
class World3D;
class RenderingDevice;
class RenderData;

// Draws every PhysXFlow3D into the frame with Flow's own ray marcher
// (self-shadowed, straight from Flow's sparse grid on the GPU), after
// transparent geometry.
//
// While a world has flows, the effect is made to run without touching
// anything the user owns or saves (see PhysXCompositorEffects). A
// screen-space fluid writes its surface into the depth before transparent
// geometry, so the gas is marched up to the liquid.
// When the world's last flow leaves, all of that is undone.
class PhysXFlowRenderEffect : public CompositorEffect {
	GDCLASS(PhysXFlowRenderEffect, CompositorEffect);

	static LocalVector<PhysXFlow3D *> flows;
	// Registered on the main thread, drawn on the render thread: held while a frame draws them.
	static Mutex flows_mutex;

	static HashMap<ObjectID, int> worlds; // flows per world

	// Copies Flow's composited result into Godot's color buffer, which
	// RenderingDevice won't copy into directly (no copy-to usage) but a
	// compute pass can write.
	RID copy_shader;
	RID copy_pipeline;
	RID copy_sampler;
	bool copy_failed = false;
	bool _ensure_copy_pipeline(RenderingDevice *p_rd);
	// A frame that can't be written from a compute pass (the Mobile renderer's,
	// also stored halved): the gas is drawn on this full-brightness copy, then
	// written back by a fullscreen draw.
	RID work;
	Size2i work_size;
	RID write_back_shader;
	HashMap<int64_t, RID> write_back_pipelines; // per framebuffer format
	void _write_back(RenderingDevice *p_rd, const RID &p_src, const RID &p_color, float p_scale);

	// PhysXFlow3D.light_scattering's pass (shared by every flow).
	FlowScatterRD scatter;

	void _render(int p_callback_type, const RenderData *p_render_data);

protected:
	static void _bind_methods();

public:
	// Writes p_src (sampled) over p_dst (an RGBA16F storage image), rgb scaled
	// by p_scale, on the render thread.
	void copy_texture(RenderingDevice *p_rd, const RID &p_src, const RID &p_dst, const Size2i &p_size, float p_scale = 1.0f);
	// Adds point / spot light scattering in a flow's gas onto p_color.
	FlowScatterRD &get_scatter() { return scatter; }

	static void register_flow(PhysXFlow3D *p_flow);
	static void unregister_flow(PhysXFlow3D *p_flow);

	// A world gained / lost a flow (counted); install keeps the effect
	// running there (call every step: the user may swap compositors).
	static void attach_world(World3D *p_world);
	static void detach_world(const ObjectID &p_world);
	static void install(World3D *p_world, Camera3D *p_camera);
	// Diagnostics: the Compositor a World3D currently renders with (World3D
	// doesn't expose it to scripts).
	static Ref<Compositor> get_world_compositor(const Ref<World3D> &p_world);
	// The effect every PhysXFlow3D installs (one is enough: it draws them
	// all).
	static Ref<PhysXFlowRenderEffect> get_shared();
	static void free_shared();

	PhysXFlowRenderEffect();
	~PhysXFlowRenderEffect();
};
