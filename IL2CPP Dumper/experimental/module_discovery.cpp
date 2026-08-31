#include "module_discovery.h"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace module_discovery {
namespace {

bool module_has_export(HMODULE module, const char* name) {
	return module && GetProcAddress(module, name) != nullptr;
}

int count_il2cpp_exports(HMODULE module) {
	if (!module) {
		return 0;
	}

	auto* base = reinterpret_cast<uint8_t*>(module);
	auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}

	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) {
		return 0;
	}

	auto* exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	auto* names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);

	int count = 0;
	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const char* name = reinterpret_cast<const char*>(base + names[i]);
		if (std::strncmp(name, "il2cpp_", 7) == 0) {
			++count;
		}
	}
	return count;
}

bool module_looks_renamed(HMODULE module) {
	if (!module) {
		return false;
	}

	auto* base = reinterpret_cast<uint8_t*>(module);
	auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) {
		return false;
	}

	auto* exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	if (exp->NumberOfNames < 20) {
		return false;
	}

	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const char* name = reinterpret_cast<const char*>(
			base + reinterpret_cast<DWORD*>(base + exp->AddressOfNames)[i]);
		if (std::strncmp(name, "il2cpp_", 7) == 0) {
			return false;
		}
	}
	return true;
}

int score_module_name(const char* name) {
	if (!name) {
		return 0;
	}

	if (_stricmp(name, "GameAssembly.dll") == 0) {
		return 40;
	}
	if (_stricmp(name, "UserAssembly.dll") == 0) {
		return 35;
	}
	if (std::strstr(name, "Assembly") != nullptr) {
		return 20;
	}
	if (std::strstr(name, "Game") != nullptr) {
		return 10;
	}
	return 0;
}

} // namespace

std::vector<Candidate> scan_loaded_modules() {
	std::vector<Candidate> out;

	const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
		GetCurrentProcessId());
	if (snap == INVALID_HANDLE_VALUE) {
		return out;
	}

	MODULEENTRY32W entry{};
	entry.dwSize = sizeof(entry);

	if (Module32FirstW(snap, &entry)) {
		do {
			char name[MAX_PATH]{};
			WideCharToMultiByte(CP_UTF8, 0, entry.szModule, -1, name, MAX_PATH, nullptr, nullptr);

			HMODULE module = entry.hModule;
			Candidate candidate{};
			candidate.name = name;
			candidate.has_domain_get = module_has_export(module, "il2cpp_domain_get");
			candidate.has_il2cpp_exports = count_il2cpp_exports(module) > 0;
			candidate.looks_renamed = module_looks_renamed(module);

			candidate.score = score_module_name(name);
			if (candidate.has_domain_get) {
				candidate.score += 100;
			}
			if (candidate.has_il2cpp_exports) {
				candidate.score += 50;
			}
			if (candidate.looks_renamed) {
				candidate.score += 25;
			}
			if (module_has_export(module, "il2cpp_thread_attach")) {
				candidate.score += 15;
			}

			if (candidate.score >= 20) {
				out.push_back(std::move(candidate));
			}
		} while (Module32NextW(snap, &entry));
	}

	CloseHandle(snap);

	std::sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
		return a.score > b.score;
	});

	return out;
}

bool auto_detect(std::string* out_module_name) {
	const auto candidates = scan_loaded_modules();
	if (candidates.empty()) {
		return false;
	}

	if (out_module_name) {
		*out_module_name = candidates.front().name;
	}
	return true;
}

} // namespace module_discovery
