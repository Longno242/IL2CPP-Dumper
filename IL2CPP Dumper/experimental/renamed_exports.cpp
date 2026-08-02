#include "renamed_exports.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace renamed_exports {
namespace {

bool g_active = false;
std::unordered_map<std::string, void*> g_map;

struct Pat {
	const char* api;
	const uint8_t* bytes;
	const uint8_t* mask; // 0 = wildcard
	size_t len;
	int priority; // higher wins when multiple match
};

bool Match(const uint8_t* body, size_t body_len, const Pat& p) {
	if (body_len < p.len) return false;
	for (size_t i = 0; i < p.len; ++i) {
		if (p.mask[i] == 0) continue;
		if (body[i] != p.bytes[i]) return false;
	}
	return true;
}

uint8_t* FollowJumps(uint8_t* p, int depth = 8) {
	for (int i = 0; i < depth; ++i) {
		if (!p) return nullptr;
		if (p[0] == 0xE9) {
			const int32_t rel = *reinterpret_cast<int32_t*>(p + 1);
			p = p + 5 + rel;
			continue;
		}
		if (p[0] == 0xEB) {
			const int8_t rel = *reinterpret_cast<int8_t*>(p + 1);
			p = p + 2 + rel;
			continue;
		}
		return p;
	}
	return p;
}

bool InModule(HMODULE mod, const void* p) {
	if (!mod || !p) return false;
	auto base = reinterpret_cast<uint8_t*>(mod);
	auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
	auto end = base + nt->OptionalHeader.SizeOfImage;
	auto u = reinterpret_cast<const uint8_t*>(p);
	return u >= base && u < end;
}

std::vector<uint8_t*> EnumExportBodies(HMODULE mod) {
	std::vector<uint8_t*> out;
	auto base = reinterpret_cast<uint8_t*>(mod);
	auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) return out;

	auto exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	auto funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
	auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
	auto ords = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals);

	out.reserve(exp->NumberOfNames);
	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const DWORD rva = funcs[ords[i]];
		// skip forwarders in export dir
		if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) continue;
		uint8_t* body = FollowJumps(base + rva);
		if (body && InModule(mod, body)) out.push_back(body);
		(void)names;
	}
	return out;
}

// Pattern tables (x = exact via mask 0xFF, ? = 0x00)
#define B(...) reinterpret_cast<const uint8_t*>(__VA_ARGS__)

// domain_get: sub rsp,??; mov rax,[rip+??]; test rax,rax
static const uint8_t kDomainGet[] = { 0x48,0x83,0xEC,0x00, 0x48,0x8B,0x05,0,0,0,0, 0x48,0x85,0xC0 };
static const uint8_t kDomainGetM[] = { 0xFF,0xFF,0xFF,0x00, 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF };

// get_assemblies: mov rax,[a]; sub rax,[b]; shr rax,3; mov [rdx],rax
static const uint8_t kAssemblies[] = { 0x48,0x8B,0x05,0,0,0,0, 0x48,0x2B,0x05,0,0,0,0, 0x48,0xC1,0xF8,0x03, 0x48,0x89,0x02 };
static const uint8_t kAssembliesM[] = { 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF };

// thread_attach start
static const uint8_t kAttach[] = { 0x40,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1 };
static const uint8_t kAttachM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMovRaxRcxt[] = { 0x48,0x8B,0x01,0xC3 };
static const uint8_t kMovRaxRcxtM[] = { 0xFF,0xFF,0xFF,0xFF };

static const uint8_t kClassName[] = { 0x48,0x8B,0x41,0x10,0xC3 };
static const uint8_t kClassNameM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kClassNs[] = { 0x48,0x8B,0x41,0x18,0xC3 };
static const uint8_t kClassNsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kImgCount[] = { 0x8B,0x41,0x18,0xC3 };
static const uint8_t kImgCountM[] = { 0xFF,0xFF,0xFF,0xFF };

static const uint8_t kImgClass[] = { 0x48,0x8B,0x41,0x28,0x44,0x8B,0x00 };
static const uint8_t kImgClassM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kTypeName[] = { 0x40,0x53,0x48,0x83,0xEC,0x40,0x45,0x33,0xC0,0x48,0x8B,0xD1 };
static const uint8_t kTypeNameM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kParamCount[] = { 0x0F,0xB6,0x41,0x52,0xC3 };
static const uint8_t kParamCountM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kRetType[] = { 0x48,0x8B,0x41,0x28,0xC3 };
static const uint8_t kRetTypeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldOff[] = { 0x48,0x63,0x41,0x18,0xC3 };
static const uint8_t kFieldOffM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldType[] = { 0x48,0x8B,0x41,0x08,0xC3 };
static const uint8_t kFieldTypeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kParent[] = { 0x48,0x8B,0x41,0x58,0xC3 };
static const uint8_t kParentM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsEnum[] = { 0x0F,0xB6,0x81,0x35,0x01,0x00,0x00,0xC0,0xE8,0x02,0x24,0x01,0xC3 };
static const uint8_t kIsEnumM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsVT[] = { 0x8B,0x41,0x28,0xC1,0xE8,0x1F,0xC3 };
static const uint8_t kIsVTM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsGeneric[] = { 0x0F,0xB6,0x81,0x35,0x01,0x00,0x00,0xC0,0xE8,0x04,0x24,0x01,0xC3 };
static const uint8_t kIsGenericM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsInst[] = { 0x0F,0xB6,0x41,0x4C,0xC0,0xE8,0x04,0xF6,0xD0,0x24,0x01,0xC3 };
static const uint8_t kIsInstM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldFlags[] = { 0x48,0x8B,0x41,0x08,0x0F,0xB7,0x40,0x08,0xC3 };
static const uint8_t kFieldFlagsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMethodFlags[] = { 0x48,0x85,0xD2,0x74,0x06,0x0F,0xB7,0x41,0x4E,0x89,0x02,0x0F,0xB7,0x41,0x4C,0xC3 };
static const uint8_t kMethodFlagsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMethodParam[] = { 0x0F,0xB6,0x41,0x52,0x3B,0xD0,0x73,0x0B,0x48,0x8B,0x41,0x30 };
static const uint8_t kMethodParamM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

// fields iterator: contains cmp word [rbx+0x124],0
static const uint8_t kFields[] = { 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9 };
static const uint8_t kFieldsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t kFieldsMark[] = { 0x66,0x83,0xBB,0x24,0x01,0x00,0x00 };
static const uint8_t kMethodsMark[] = { 0x66,0x83,0xBB,0x20,0x01,0x00,0x00 };

bool Contains(const uint8_t* body, size_t n, const uint8_t* needle, size_t m) {
	if (n < m) return false;
	for (size_t i = 0; i + m <= n; ++i) {
		if (std::memcmp(body + i, needle, m) == 0) return true;
	}
	return false;
}

void BuildMap(const char* module_name) {
	g_map.clear();
	HMODULE mod = GetModuleHandleA(module_name);
	if (!mod) return;

	auto bodies = EnumExportBodies(mod);

	auto set_if_empty = [](const char* api, void* p) {
		if (!p) return;
		if (!g_map.count(api)) g_map[api] = p;
	};

	for (uint8_t* body : bodies) {
		if (Match(body, 64, Pat{ "il2cpp_domain_get", kDomainGet, kDomainGetM, sizeof(kDomainGet), 10 }))
			set_if_empty("il2cpp_domain_get", body);
		if (Match(body, 64, Pat{ "il2cpp_domain_get_assemblies", kAssemblies, kAssembliesM, sizeof(kAssemblies), 10 }))
			set_if_empty("il2cpp_domain_get_assemblies", body);
		if (Match(body, 64, Pat{ "il2cpp_thread_attach", kAttach, kAttachM, sizeof(kAttach), 5 }))
			set_if_empty("il2cpp_thread_attach", body);
		if (Match(body, 64, Pat{ "il2cpp_assembly_get_image", kMovRaxRcxt, kMovRaxRcxtM, sizeof(kMovRaxRcxt), 1 })) {
			set_if_empty("il2cpp_assembly_get_image", body);
			set_if_empty("il2cpp_image_get_name", body);
			set_if_empty("il2cpp_field_get_name", body);
		}
		if (Match(body, 64, Pat{ "il2cpp_class_get_name", kClassName, kClassNameM, sizeof(kClassName), 1 }))
			set_if_empty("il2cpp_class_get_name", body);
		if (Match(body, 64, Pat{ "il2cpp_class_get_namespace", kClassNs, kClassNsM, sizeof(kClassNs), 1 })) {
			set_if_empty("il2cpp_class_get_namespace", body);
			set_if_empty("il2cpp_method_get_name", body);
		}
		if (Match(body, 64, Pat{ "il2cpp_image_get_class_count", kImgCount, kImgCountM, sizeof(kImgCount), 1 }))
			set_if_empty("il2cpp_image_get_class_count", body);
		if (Match(body, 64, Pat{ "il2cpp_image_get_class", kImgClass, kImgClassM, sizeof(kImgClass), 5 }))
			set_if_empty("il2cpp_image_get_class", body);
		if (Match(body, 64, Pat{ "il2cpp_type_get_name", kTypeName, kTypeNameM, sizeof(kTypeName), 5 }))
			set_if_empty("il2cpp_type_get_name", body);
		if (Match(body, 64, Pat{ "il2cpp_method_get_param_count", kParamCount, kParamCountM, sizeof(kParamCount), 5 }))
			set_if_empty("il2cpp_method_get_param_count", body);
		if (Match(body, 64, Pat{ "il2cpp_method_get_return_type", kRetType, kRetTypeM, sizeof(kRetType), 5 }))
			set_if_empty("il2cpp_method_get_return_type", body);
		if (Match(body, 64, Pat{ "il2cpp_field_get_offset", kFieldOff, kFieldOffM, sizeof(kFieldOff), 5 }))
			set_if_empty("il2cpp_field_get_offset", body);
		if (Match(body, 64, Pat{ "il2cpp_field_get_type", kFieldType, kFieldTypeM, sizeof(kFieldType), 5 }))
			set_if_empty("il2cpp_field_get_type", body);
		if (Match(body, 64, Pat{ "il2cpp_class_get_parent", kParent, kParentM, sizeof(kParent), 5 }))
			set_if_empty("il2cpp_class_get_parent", body);
		if (Match(body, 64, Pat{ "il2cpp_class_is_enum", kIsEnum, kIsEnumM, sizeof(kIsEnum), 10 }))
			set_if_empty("il2cpp_class_is_enum", body);
		if (Match(body, 64, Pat{ "il2cpp_class_is_valuetype", kIsVT, kIsVTM, sizeof(kIsVT), 10 }))
			set_if_empty("il2cpp_class_is_valuetype", body);
		if (Match(body, 64, Pat{ "il2cpp_class_is_generic", kIsGeneric, kIsGenericM, sizeof(kIsGeneric), 10 }))
			set_if_empty("il2cpp_class_is_generic", body);
		if (Match(body, 64, Pat{ "il2cpp_method_is_instance", kIsInst, kIsInstM, sizeof(kIsInst), 10 }))
			set_if_empty("il2cpp_method_is_instance", body);
		if (Match(body, 64, Pat{ "il2cpp_field_get_flags", kFieldFlags, kFieldFlagsM, sizeof(kFieldFlags), 10 }))
			set_if_empty("il2cpp_field_get_flags", body);
		if (Match(body, 64, Pat{ "il2cpp_method_get_flags", kMethodFlags, kMethodFlagsM, sizeof(kMethodFlags), 10 }))
			set_if_empty("il2cpp_method_get_flags", body);
		if (Match(body, 64, Pat{ "il2cpp_method_get_param", kMethodParam, kMethodParamM, sizeof(kMethodParam), 10 }))
			set_if_empty("il2cpp_method_get_param", body);

		if (Match(body, 64, Pat{ "iter", kFields, kFieldsM, sizeof(kFields), 1 })) {
			if (Contains(body, 96, kFieldsMark, sizeof(kFieldsMark)))
				set_if_empty("il2cpp_class_get_fields", body);
			if (Contains(body, 96, kMethodsMark, sizeof(kMethodsMark)))
				set_if_empty("il2cpp_class_get_methods", body);
		}
	}
}

// --- shims ---
using get_fields_t = void* (*)(void*, void**);
using get_field_name_t = const char* (*)(void*);
using get_methods_t = const void* (*)(void*, void**);
using get_method_name_t = const char* (*)(void*);
using get_param_count_t = int (*)(const void*);
using get_class_count_t = size_t (*)(const void*);
using get_class_t = const void* (*)(const void*, size_t);
using get_class_name_t = const char* (*)(void*);
using get_class_ns_t = const char* (*)(void*);

get_fields_t g_get_fields = nullptr;
get_field_name_t g_get_field_name = nullptr;
get_methods_t g_get_methods = nullptr;
get_method_name_t g_get_method_name = nullptr;
get_param_count_t g_get_param_count = nullptr;
get_class_count_t g_get_class_count = nullptr;
get_class_t g_get_class = nullptr;
get_class_name_t g_get_class_name = nullptr;
get_class_ns_t g_get_class_ns = nullptr;

void* __cdecl shim_field_from_name(void* klass, const char* name) {
	if (!klass || !name || !g_get_fields || !g_get_field_name) return nullptr;
	void* iter = nullptr;
	while (void* f = g_get_fields(klass, &iter)) {
		const char* n = g_get_field_name(f);
		if (n && std::strcmp(n, name) == 0) return f;
	}
	return nullptr;
}

const void* __cdecl shim_method_from_name(void* klass, const char* name, int argc) {
	if (!klass || !name || !g_get_methods || !g_get_method_name) return nullptr;
	void* iter = nullptr;
	while (const void* m = g_get_methods(klass, &iter)) {
		const char* n = g_get_method_name(const_cast<void*>(m));
		if (!n || std::strcmp(n, name) != 0) continue;
		if (argc >= 0 && g_get_param_count) {
			if (g_get_param_count(m) != argc) continue;
		}
		return m;
	}
	return nullptr;
}

void* __cdecl shim_class_from_name(const void* image, const char* namespaze, const char* name) {
	if (!image || !name || !g_get_class_count || !g_get_class || !g_get_class_name) return nullptr;
	const size_t n = g_get_class_count(image);
	for (size_t i = 0; i < n; ++i) {
		auto* cls = const_cast<void*>(g_get_class(image, i));
		if (!cls) continue;
		const char* cn = g_get_class_name(cls);
		if (!cn || std::strcmp(cn, name) != 0) continue;
		if (namespaze && *namespaze && g_get_class_ns) {
			const char* ns = g_get_class_ns(cls);
			if (!ns || std::strcmp(ns, namespaze) != 0) continue;
		}
		return cls;
	}
	return nullptr;
}

void __cdecl shim_free(void*) {}
bool __cdecl shim_false_v(void*) { return false; }
void* __cdecl shim_null_iter(void*, void** iter) { if (iter) *iter = nullptr; return nullptr; }
const char* __cdecl shim_param_name(const void*, uint32_t) { return ""; }

void set_slot(void** slot, void* fn) {
	if (slot && !*slot && fn) *slot = fn;
}

} // namespace

bool DetectAndEnable(const char* module_name) {
	g_active = false;
	g_map.clear();
	HMODULE mod = GetModuleHandleA(module_name);
	if (!mod) return false;

	auto base = reinterpret_cast<uint8_t*>(mod);
	auto dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) return false;

	auto exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	if (exp->NumberOfNames < 40) return false;

	auto names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const char* n = reinterpret_cast<const char*>(base + names[i]);
		if (std::strncmp(n, "il2cpp_", 7) == 0) return false;
	}

	g_active = true;
	BuildMap(module_name);
	return true;
}

bool Active() { return g_active; }

void Reset() {
	g_active = false;
	g_map.clear();
}

void* Resolve(const char* /*module_name*/, const char* api_name) {
	if (!g_active || !api_name) return nullptr;
	auto it = g_map.find(api_name);
	return it == g_map.end() ? nullptr : it->second;
}

bool PreferDirectCalls() { return g_active; }

void InstallFallbacks(ApiSlots& s) {
	if (!g_active) return;

	auto fill = [&](void** slot, const char* api) {
		if (slot && !*slot) {
			if (void* p = Resolve(nullptr, api)) *slot = p;
		}
	};

	fill(s.domain_get, "il2cpp_domain_get");
	fill(s.thread_attach, "il2cpp_thread_attach");
	fill(s.get_assemblies, "il2cpp_domain_get_assemblies");
	fill(s.get_image, "il2cpp_assembly_get_image");
	fill(s.get_image_name, "il2cpp_image_get_name");
	fill(s.get_class_count, "il2cpp_image_get_class_count");
	fill(s.get_class, "il2cpp_image_get_class");
	fill(s.get_class_name, "il2cpp_class_get_name");
	fill(s.get_class_namespace, "il2cpp_class_get_namespace");
	fill(s.get_class_parent, "il2cpp_class_get_parent");
	fill(s.get_fields, "il2cpp_class_get_fields");
	fill(s.get_field_name, "il2cpp_field_get_name");
	fill(s.get_field_flags, "il2cpp_field_get_flags");
	fill(s.get_field_offset, "il2cpp_field_get_offset");
	fill(s.get_field_type, "il2cpp_field_get_type");
	fill(s.get_methods, "il2cpp_class_get_methods");
	fill(s.get_method_name, "il2cpp_method_get_name");
	fill(s.get_method_return_type, "il2cpp_method_get_return_type");
	fill(s.get_method_param_count, "il2cpp_method_get_param_count");
	fill(s.get_method_param_type, "il2cpp_method_get_param");
	fill(s.get_method_flags, "il2cpp_method_get_flags");
	fill(s.get_type_name, "il2cpp_type_get_name");
	fill(s.is_class_enum, "il2cpp_class_is_enum");
	fill(s.is_class_valuetype, "il2cpp_class_is_valuetype");
	fill(s.is_class_generic, "il2cpp_class_is_generic");
	fill(s.is_method_instance, "il2cpp_method_is_instance");

	g_get_fields = s.get_fields ? reinterpret_cast<get_fields_t>(*s.get_fields) : nullptr;
	g_get_field_name = s.get_field_name ? reinterpret_cast<get_field_name_t>(*s.get_field_name) : nullptr;
	g_get_methods = s.get_methods ? reinterpret_cast<get_methods_t>(*s.get_methods) : nullptr;
	g_get_method_name = s.get_method_name ? reinterpret_cast<get_method_name_t>(*s.get_method_name) : nullptr;
	g_get_param_count = s.get_method_param_count ? reinterpret_cast<get_param_count_t>(*s.get_method_param_count) : nullptr;
	g_get_class_count = s.get_class_count ? reinterpret_cast<get_class_count_t>(*s.get_class_count) : nullptr;
	g_get_class = s.get_class ? reinterpret_cast<get_class_t>(*s.get_class) : nullptr;
	g_get_class_name = s.get_class_name ? reinterpret_cast<get_class_name_t>(*s.get_class_name) : nullptr;
	g_get_class_ns = s.get_class_namespace ? reinterpret_cast<get_class_ns_t>(*s.get_class_namespace) : nullptr;

	set_slot(s.get_field_by_name, reinterpret_cast<void*>(&shim_field_from_name));
	set_slot(s.get_method_by_name, reinterpret_cast<void*>(&shim_method_from_name));
	set_slot(s.get_class_by_name, reinterpret_cast<void*>(&shim_class_from_name));
	set_slot(s.free_memory, reinterpret_cast<void*>(&shim_free));
	set_slot(s.get_method_param_name, reinterpret_cast<void*>(&shim_param_name));
	set_slot(s.get_class_interfaces, reinterpret_cast<void*>(&shim_null_iter));
	set_slot(s.get_class_nested_types, reinterpret_cast<void*>(&shim_null_iter));
	if (s.is_class_generic && !*s.is_class_generic)
		*s.is_class_generic = reinterpret_cast<void*>(&shim_false_v);
}

} // namespace renamed_exports
