// User-mode sampling profiler for a running process (no elevation needed; see
// devtools/profile/README.md). Every interval it suspends each thread of the
// target that used CPU since the last tick, walks its stack and resumes it.
// Afterwards it resolves the addresses with dbghelp, including inlined frames
// (innermost first), while the target still runs.
//
// sampler.exe <pid> <seconds> <interval_us> <out.tsv> [symbol path]
// Output lines: count, thread id, module, the leaf's frames
// ("function@file:line", inline chain innermost first, then the physical
// function), then for each caller "|" and its frames. "#thread" lines name
// the threads.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <timeapi.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dbghelp.lib")

struct ThreadState {
	HANDLE handle = nullptr;
	ULONG64 cycles = 0;
	std::string name;
};

static void refresh_threads(DWORD p_pid, std::map<DWORD, ThreadState> &r_threads) {
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return;
	}
	THREADENTRY32 entry = {};
	entry.dwSize = sizeof(entry);
	for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
		if (entry.th32OwnerProcessID != p_pid || r_threads.count(entry.th32ThreadID)) {
			continue;
		}
		HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
		if (thread) {
			ThreadState state;
			state.handle = thread;
			QueryThreadCycleTime(thread, &state.cycles);
			PWSTR description = nullptr;
			if (SUCCEEDED(GetThreadDescription(thread, &description)) && description) {
				char name[256] = {};
				WideCharToMultiByte(CP_UTF8, 0, description, -1, name, sizeof(name) - 1, nullptr, nullptr);
				state.name = name;
				LocalFree(description);
			}
			r_threads[entry.th32ThreadID] = state;
		}
	}
	CloseHandle(snapshot);
}

static std::string frame_text(HANDLE p_process, DWORD64 p_address, DWORD p_inline_context, bool p_inline) {
	alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 512] = {};
	SYMBOL_INFO *symbol = reinterpret_cast<SYMBOL_INFO *>(buffer);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = 511;
	DWORD64 displacement = 0;
	BOOL found = p_inline ? SymFromInlineContext(p_process, p_address, p_inline_context, &displacement, symbol) : SymFromAddr(p_process, p_address, &displacement, symbol);
	std::string text = found ? std::string(symbol->Name) : std::string("?");
	IMAGEHLP_LINE64 line = {};
	line.SizeOfStruct = sizeof(line);
	DWORD line_displacement = 0;
	BOOL has_line = p_inline ? SymGetLineFromInlineContext(p_process, p_address, p_inline_context, 0, &line_displacement, &line) : SymGetLineFromAddr64(p_process, p_address, &line_displacement, &line);
	if (has_line && line.FileName) {
		text += "@";
		text += line.FileName;
		text += ":" + std::to_string(line.LineNumber);
	}
	for (char &c : text) {
		if (c == '\t' || c == '\n') {
			c = ' ';
		}
	}
	return text;
}

int main(int argc, char **argv) {
	if (argc < 5) {
		std::fprintf(stderr, "usage: sampler <pid> <seconds> <interval_us> <out.tsv> [symbol path]\n");
		return 2;
	}
	const DWORD pid = DWORD(std::strtoul(argv[1], nullptr, 10));
	const double seconds = std::atof(argv[2]);
	const int interval_us = std::atoi(argv[3]);
	HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
	if (!process) {
		std::fprintf(stderr, "OpenProcess failed: %lu\n", GetLastError());
		return 1;
	}
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
	if (!SymInitialize(process, argc > 5 ? argv[5] : nullptr, TRUE)) {
		std::fprintf(stderr, "SymInitialize failed: %lu\n", GetLastError());
	}
	timeBeginPeriod(1);
	const size_t max_depth = 12;
	std::map<DWORD, ThreadState> threads;
	std::map<std::vector<DWORD64>, std::unordered_map<DWORD, uint64_t>> samples;
	LARGE_INTEGER frequency, start, now, last_refresh;
	QueryPerformanceFrequency(&frequency);
	QueryPerformanceCounter(&start);
	last_refresh.QuadPart = 0;
	uint64_t ticks = 0, total = 0;
	for (;;) {
		QueryPerformanceCounter(&now);
		const double elapsed = double(now.QuadPart - start.QuadPart) / double(frequency.QuadPart);
		if (elapsed >= seconds) {
			break;
		}
		DWORD exit_code = 0;
		if (GetExitCodeProcess(process, &exit_code) && exit_code != STILL_ACTIVE) {
			break;
		}
		if (double(now.QuadPart - last_refresh.QuadPart) / double(frequency.QuadPart) > 0.1) {
			refresh_threads(pid, threads);
			last_refresh = now;
		}
		for (auto &entry : threads) {
			ThreadState &state = entry.second;
			ULONG64 cycles = 0;
			if (!QueryThreadCycleTime(state.handle, &cycles) || cycles == state.cycles) {
				continue;
			}
			state.cycles = cycles;
			if (SuspendThread(state.handle) == DWORD(-1)) {
				continue;
			}
			CONTEXT context = {};
			context.ContextFlags = CONTEXT_FULL;
			if (GetThreadContext(state.handle, &context)) {
				std::vector<DWORD64> stack;
				STACKFRAME64 frame = {};
				frame.AddrPC.Offset = context.Rip;
				frame.AddrPC.Mode = AddrModeFlat;
				frame.AddrFrame.Offset = context.Rbp;
				frame.AddrFrame.Mode = AddrModeFlat;
				frame.AddrStack.Offset = context.Rsp;
				frame.AddrStack.Mode = AddrModeFlat;
				while (stack.size() < max_depth && StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, state.handle, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
					if (frame.AddrPC.Offset == 0) {
						break;
					}
					// A caller's return address points after its call instruction.
					stack.push_back(stack.empty() ? frame.AddrPC.Offset : frame.AddrPC.Offset - 1);
				}
				if (stack.empty()) {
					stack.push_back(context.Rip);
				}
				samples[stack][entry.first]++;
				total++;
			}
			ResumeThread(state.handle);
		}
		ticks++;
		LARGE_INTEGER tick_end;
		do {
			QueryPerformanceCounter(&tick_end);
			if (interval_us >= 1000) {
				Sleep(0);
			}
		} while (double(tick_end.QuadPart - now.QuadPart) * 1e6 / double(frequency.QuadPart) < interval_us);
	}
	timeEndPeriod(1);
	std::printf("ticks=%llu samples=%llu unique_stacks=%zu threads=%zu\n", (unsigned long long)ticks, (unsigned long long)total, samples.size(), threads.size());

	FILE *out = std::fopen(argv[4], "w");
	if (!out) {
		return 1;
	}
	for (const auto &thread : threads) {
		std::fprintf(out, "#thread\t%lu\t%s\n", thread.first, thread.second.name.c_str());
	}
	std::unordered_map<DWORD64, std::string> resolved;
	auto resolve = [&](DWORD64 p_address) -> const std::string & {
		auto found = resolved.find(p_address);
		if (found != resolved.end()) {
			return found->second;
		}
		std::string text;
		const DWORD inline_count = SymAddrIncludeInlineTrace(process, p_address);
		DWORD context = 0, frame_index = 0;
		if (inline_count > 0 && SymQueryInlineTrace(process, p_address, 0, p_address, p_address, &context, &frame_index)) {
			for (DWORD i = 0; i < inline_count && i < 6; i++) {
				text += "\t" + frame_text(process, p_address, context + i, true);
			}
		}
		text += "\t" + frame_text(process, p_address, 0, false);
		return resolved.emplace(p_address, text).first->second;
	};
	for (const auto &sample : samples) {
		IMAGEHLP_MODULE64 module = {};
		module.SizeOfStruct = sizeof(module);
		std::string line = SymGetModuleInfo64(process, sample.first[0], &module) ? module.ModuleName : "?";
		line += resolve(sample.first[0]);
		for (size_t i = 1; i < sample.first.size(); i++) {
			line += "\t|" + resolve(sample.first[i]);
		}
		for (const auto &thread : sample.second) {
			std::fprintf(out, "%llu\t%lu\t%s\n", (unsigned long long)thread.second, thread.first, line.c_str());
		}
	}
	std::fclose(out);
	SymCleanup(process);
	return 0;
}
