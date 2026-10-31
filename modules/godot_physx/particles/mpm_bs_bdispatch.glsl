#[compute]
#version 450

#include "mpm_block_inc.glsl"

// The per-block passes' indirect dispatch args. Only this pass binds them (see
// buf_bargs in mpm_fluid_solver.h).
layout(set = 0, binding = 12, std430) restrict writeonly buffer BArgs {
	uint bargs[];
};

layout(local_size_x = 1) in;

// One thread. Clamp the active-block count and write the indirect group count
// for the per-block passes (grid / couple / clearnodes / march): one workgroup
// per block, local_size_x = 64 = one thread per cell.
void main() {
	uint n = min(bcounts[0], MAXB);
	bcounts[0] = n;
	bargs[0] = n;
	bargs[1] = 1u;
	bargs[2] = 1u;
}
