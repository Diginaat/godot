#[compute]

#version 450

#VERSION_DEFINES

// Writes packed GPU instance table records (docs/renderer, Phase 1.3) to
// their slots: record i of the upload goes to table[slots[i]]. Copies the
// raw bits, so the table equals the engine's CPU copy exactly.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct Record {
	uvec4 data[8]; // 128 bytes, GPUInstanceTable::Record.
};

layout(set = 0, binding = 0, std430) restrict writeonly buffer Table {
	Record records[];
}
table;

layout(set = 0, binding = 1, std430) restrict readonly buffer Upload {
	Record records[];
}
upload;

layout(set = 0, binding = 2, std430) restrict readonly buffer Slots {
	uint slots[];
}
slots;

layout(push_constant, std430) uniform Params {
	uint count;
	uint pad[3];
}
params;

void main() {
	uint index = gl_GlobalInvocationID.x;
	if (index >= params.count) {
		return;
	}
	table.records[slots.slots[index]] = upload.records[index];
}
