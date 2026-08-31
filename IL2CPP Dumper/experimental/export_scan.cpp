#include "export_scan.h"

#include <Windows.h>

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace export_scan {
namespace {

std::unordered_map<std::string, void*>& cache() {
	static std::unordered_map<std::string, void*> value;
	return value;
}

bool& ready() {
	static bool value = false;
	return value;
}

std::string& path_label() {
	static std::string value = "none";
	return value;
}

bool safe_readable(const void* address, std::size_t size) {
	if (!address || size == 0) {
		return false;
	}
	__try {
		volatile std::uint8_t sum = 0;
		const auto* p = static_cast<const std::uint8_t*>(address);
		sum = static_cast<std::uint8_t>(p[0] + p[size - 1]);
		(void)sum;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool is_il2cpp_export_name(const char* text) {
	if (!text || !safe_readable(text, 8)) {
		return false;
	}
	if (std::memcmp(text, "il2cpp_", 7) != 0) {
		return false;
	}
	std::size_t n = 7;
	while (n < 96) {
		if (!safe_readable(text + n, 1)) {
			return false;
		}
		const char ch = text[n];
		if (ch == '\0') {
			return n > 7;
		}
		const bool ok =
			(ch >= 'a' && ch <= 'z') ||
			(ch >= '0' && ch <= '9') ||
			ch == '_';
		if (!ok) {
			return false;
		}
		++n;
	}
	return false;
}

struct ModuleView {
	std::uintptr_t base = 0;
	std::uintptr_t image_end = 0;
	std::uintptr_t exec_start = 0;
	std::uintptr_t exec_end = 0;
	std::vector<std::pair<std::uintptr_t, std::uintptr_t>> data;
};

bool fill_module_view(HMODULE module, ModuleView& view) {
	if (!module) {
		return false;
	}
	view.base = reinterpret_cast<std::uintptr_t>(module);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(view.base);
	if (!safe_readable(dos, sizeof(IMAGE_DOS_HEADER)) || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return false;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(view.base + dos->e_lfanew);
	if (!safe_readable(nt, sizeof(IMAGE_NT_HEADERS)) || nt->Signature != IMAGE_NT_SIGNATURE) {
		return false;
	}

	view.image_end = view.base + nt->OptionalHeader.SizeOfImage;
	const auto* section = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		const auto start = view.base + section[i].VirtualAddress;
		const auto size = section[i].Misc.VirtualSize
			? section[i].Misc.VirtualSize
			: section[i].SizeOfRawData;
		if (!size) {
			continue;
		}
		const auto end = start + size;
		const bool readable = (section[i].Characteristics & IMAGE_SCN_MEM_READ) != 0;
		const bool executable = (section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
		if (readable && !executable) {
			view.data.emplace_back(start, end);
		}
		if (executable) {
			if (!view.exec_start) {
				view.exec_start = start;
				view.exec_end = end;
			} else {
				if (start < view.exec_start) {
					view.exec_start = start;
				}
				if (end > view.exec_end) {
					view.exec_end = end;
				}
			}
		}
	}
	return view.image_end > view.base;
}

void remember(const char* name, void* address) {
	if (!name || !address || !is_il2cpp_export_name(name)) {
		return;
	}
	cache().emplace(name, address);
}

void collect_pe_exports(HMODULE module) {
	const auto base = reinterpret_cast<std::uintptr_t>(module);
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress || !dir.Size) {
		return;
	}

	const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
	const auto* names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
	const auto* ords = reinterpret_cast<const WORD*>(base + exports->AddressOfNameOrdinals);
	const auto* funcs = reinterpret_cast<const DWORD*>(base + exports->AddressOfFunctions);

	for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
		const char* name = reinterpret_cast<const char*>(base + names[i]);
		if (!is_il2cpp_export_name(name)) {
			continue;
		}
		const DWORD rva = funcs[ords[i]];
		if (!rva || (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size)) {
			continue;
		}
		remember(name, reinterpret_cast<void*>(base + rva));
	}
}

void collect_il2cpp_strings(const ModuleView& view,
	std::unordered_map<std::uintptr_t, const char*>& strings) {
	for (const auto& [start, end] : view.data) {
		if (end <= start + 8) {
			continue;
		}
		__try {
			auto* p = reinterpret_cast<const char*>(start);
			auto* last = reinterpret_cast<const char*>(end - 8);
			for (; p < last; ++p) {
				if (p[0] != 'i' || std::memcmp(p, "il2cpp_", 7) != 0) {
					continue;
				}
				if (is_il2cpp_export_name(p)) {
					strings.emplace(reinterpret_cast<std::uintptr_t>(p), p);
					p += 6;
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

bool in_exec(const ModuleView& view, std::uintptr_t address) {
	return view.exec_start && address >= view.exec_start && address < view.exec_end;
}

void collect_pointer_tables(const ModuleView& view,
	const std::unordered_map<std::uintptr_t, const char*>& strings) {
	if (strings.empty()) {
		return;
	}

	for (const auto& [start, end] : view.data) {
		if (end < start + 16) {
			continue;
		}
		__try {
			const auto aligned = (start + 7) & ~static_cast<std::uintptr_t>(7);
			for (auto addr = aligned; addr + 16 <= end; addr += 8) {
				const auto* pair = reinterpret_cast<const std::uintptr_t*>(addr);
				const auto a = pair[0];
				const auto b = pair[1];

				if (const auto it = strings.find(a); it != strings.end() && in_exec(view, b)) {
					remember(it->second, reinterpret_cast<void*>(b));
				} else if (const auto it2 = strings.find(b); it2 != strings.end() && in_exec(view, a)) {
					remember(it2->second, reinterpret_cast<void*>(a));
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
		}
	}
}

} // namespace

void reset() {
	cache().clear();
	ready() = false;
	path_label() = "none";
}

void build_cache(const char* module_name) {
	if (ready() || !module_name) {
		return;
	}
	ready() = true;

	HMODULE module = GetModuleHandleA(module_name);
	if (!module) {
		return;
	}

	collect_pe_exports(module);
	const std::size_t pe_count = cache().size();

	ModuleView view{};
	if (!fill_module_view(module, view)) {
		path_label() = pe_count ? "pe-export" : "none";
		return;
	}

	std::unordered_map<std::uintptr_t, const char*> strings;
	collect_il2cpp_strings(view, strings);
	collect_pointer_tables(view, strings);

	if (!cache().empty() && pe_count > 0) {
		path_label() = "pe+table";
	} else if (!cache().empty()) {
		path_label() = "table-scan";
	} else if (pe_count > 0) {
		path_label() = "pe-export";
	} else {
		path_label() = "none";
	}
}

void* lookup(const char* api_name) {
	if (!api_name) {
		return nullptr;
	}
	const auto it = cache().find(api_name);
	return it != cache().end() ? it->second : nullptr;
}

std::size_t export_count() {
	return cache().size();
}

const char* last_path_label() {
	return path_label().c_str();
}

} // namespace export_scan
