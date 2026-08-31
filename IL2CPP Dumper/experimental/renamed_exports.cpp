#include "renamed_exports.h"

#include <Windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace renamed_exports {
namespace {

constexpr size_t kMatchWindow = 96;

bool g_active = false;
std::unordered_map<std::string, void*> g_map;

struct BytePat {
	const uint8_t* bytes;
	const uint8_t* mask;
	size_t len;
};

bool MatchBytes(const uint8_t* body, size_t body_len, const BytePat& pat) {
	if (!body || body_len < pat.len) return false;
	for (size_t i = 0; i < pat.len; ++i) {
		if (pat.mask[i] == 0) continue;
		if (body[i] != pat.bytes[i]) return false;
	}
	return true;
}

bool ContainsBytes(const uint8_t* body, size_t body_len, const uint8_t* needle, size_t needle_len) {
	if (!body || !needle || body_len < needle_len) return false;
	for (size_t i = 0; i + needle_len <= body_len; ++i) {
		if (std::memcmp(body + i, needle, needle_len) == 0) return true;
	}
	return false;
}

uint8_t* FollowJumps(uint8_t* p, int depth = 8) {
	for (int i = 0; i < depth && p; ++i) {
		if (p[0] == 0xE9) {
			p = p + 5 + *reinterpret_cast<int32_t*>(p + 1);
			continue;
		}
		if (p[0] == 0xEB) {
			p = p + 2 + *reinterpret_cast<int8_t*>(p + 1);
			continue;
		}
		return p;
	}
	return p;
}

bool PointerInModule(HMODULE mod, const void* p) {
	if (!mod || !p) return false;
	auto* base = reinterpret_cast<uint8_t*>(mod);
	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(
		base + reinterpret_cast<PIMAGE_DOS_HEADER>(base)->e_lfanew);
	auto* end = base + nt->OptionalHeader.SizeOfImage;
	auto* cur = reinterpret_cast<const uint8_t*>(p);
	return cur >= base && cur < end;
}

bool ModuleLooksRenamed(HMODULE mod) {
	auto* base = reinterpret_cast<uint8_t*>(mod);
	auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) return false;

	auto* exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	if (exp->NumberOfNames < 20) return false;

	auto* names = reinterpret_cast<DWORD*>(base + exp->AddressOfNames);
	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const char* name = reinterpret_cast<const char*>(base + names[i]);
		if (std::strncmp(name, "il2cpp_", 7) == 0) return false;
	}
	return true;
}

std::vector<uint8_t*> CollectExportBodies(HMODULE mod) {
	std::vector<uint8_t*> out;
	std::unordered_set<uint8_t*> seen;

	auto* base = reinterpret_cast<uint8_t*>(mod);
	auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) return out;

	auto* exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	auto* funcs = reinterpret_cast<DWORD*>(base + exp->AddressOfFunctions);
	auto* ords = reinterpret_cast<WORD*>(base + exp->AddressOfNameOrdinals);

	out.reserve(exp->NumberOfNames);
	for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
		const DWORD rva = funcs[ords[i]];
		if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) continue;

		uint8_t* body = FollowJumps(base + rva);
		if (!body || !PointerInModule(mod, body)) continue;
		if (!seen.insert(body).second) continue;
		out.push_back(body);
	}
	return out;
}

void MapFirst(const char* api, void* fn) {
	if (!api || !fn) return;
	if (g_map.find(api) == g_map.end()) g_map.emplace(api, fn);
}

// Mask: 0xFF = compare, 0x00 = wildcard (typically rip-relative immediates).
#define PAT(name) name, name##M, sizeof(name)

static const uint8_t kDomainGet[]  = { 0x48,0x83,0xEC,0x00, 0x48,0x8B,0x05,0,0,0,0, 0x48,0x85,0xC0 };
static const uint8_t kDomainGetM[] = { 0xFF,0xFF,0xFF,0x00, 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF };

static const uint8_t kAssemblies[]  = { 0x48,0x8B,0x05,0,0,0,0, 0x48,0x2B,0x05,0,0,0,0, 0x48,0xC1,0xF8,0x03, 0x48,0x89,0x02 };
static const uint8_t kAssembliesM[] = { 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF,0,0,0,0, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF };

static const uint8_t kAttach[]  = { 0x40,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF1 };
static const uint8_t kAttachM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMovRaxRcx[]  = { 0x48,0x8B,0x01,0xC3 };
static const uint8_t kMovRaxRcxM[] = { 0xFF,0xFF,0xFF,0xFF };

static const uint8_t kClassName[]  = { 0x48,0x8B,0x41,0x10,0xC3 };
static const uint8_t kClassNameM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kAtRcx18[]  = { 0x48,0x8B,0x41,0x18,0xC3 };
static const uint8_t kAtRcx18M[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kImgCount[]  = { 0x8B,0x41,0x18,0xC3 };
static const uint8_t kImgCountM[] = { 0xFF,0xFF,0xFF,0xFF };

static const uint8_t kImgClass[]  = { 0x48,0x8B,0x41,0x28,0x44,0x8B,0x00 };
static const uint8_t kImgClassM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kTypeName[]  = { 0x40,0x53,0x48,0x83,0xEC,0x40,0x45,0x33,0xC0,0x48,0x8B,0xD1 };
static const uint8_t kTypeNameM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kParamCount[]  = { 0x0F,0xB6,0x41,0x52,0xC3 };
static const uint8_t kParamCountM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kRetType[]  = { 0x48,0x8B,0x41,0x28,0xC3 };
static const uint8_t kRetTypeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldOff[]  = { 0x48,0x63,0x41,0x18,0xC3 };
static const uint8_t kFieldOffM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldType[]  = { 0x48,0x8B,0x41,0x08,0xC3 };
static const uint8_t kFieldTypeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kParent[]  = { 0x48,0x8B,0x41,0x58,0xC3 };
static const uint8_t kParentM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsEnum[]  = { 0x0F,0xB6,0x81,0x35,0x01,0x00,0x00,0xC0,0xE8,0x02,0x24,0x01,0xC3 };
static const uint8_t kIsEnumM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsValueType[]  = { 0x8B,0x41,0x28,0xC1,0xE8,0x1F,0xC3 };
static const uint8_t kIsValueTypeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsGeneric[]  = { 0x0F,0xB6,0x81,0x35,0x01,0x00,0x00,0xC0,0xE8,0x04,0x24,0x01,0xC3 };
static const uint8_t kIsGenericM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIsInstance[]  = { 0x0F,0xB6,0x41,0x4C,0xC0,0xE8,0x04,0xF6,0xD0,0x24,0x01,0xC3 };
static const uint8_t kIsInstanceM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFieldFlags[]  = { 0x48,0x8B,0x41,0x08,0x0F,0xB7,0x40,0x08,0xC3 };
static const uint8_t kFieldFlagsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMethodFlags[]  = { 0x48,0x85,0xD2,0x74,0x06,0x0F,0xB7,0x41,0x4E,0x89,0x02,0x0F,0xB7,0x41,0x4C,0xC3 };
static const uint8_t kMethodFlagsM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kMethodParam[]  = { 0x0F,0xB6,0x41,0x52,0x3B,0xD0,0x73,0x0B,0x48,0x8B,0x41,0x30 };
static const uint8_t kMethodParamM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kIteratorPrologue[]  = { 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9 };
static const uint8_t kIteratorPrologueM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

// Distinguishes class_get_fields vs class_get_methods (cmp word ptr [rbx+imm], 0).
static const uint8_t kFieldsMarker[]  = { 0x66,0x83,0xBB,0x24,0x01,0x00,0x00 };
static const uint8_t kMethodsMarker[] = { 0x66,0x83,0xBB,0x20,0x01,0x00,0x00 };
static const uint8_t kNestedMarker[]  = { 0x66,0x83,0xBB,0x1C,0x01,0x00,0x00 };
static const uint8_t kIfaceMarker[]   = { 0x66,0x83,0xBB,0x18,0x01,0x00,0x00 };

static const uint8_t kDomainGetAlt[]  = { 0x48,0x8B,0x05,0,0,0,0,0x48,0x85,0xC0,0x74 };
static const uint8_t kDomainGetAltM[] = { 0xFF,0xFF,0xFF,0,0,0,0,0xFF,0xFF,0xFF,0xFF };

static const uint8_t kFree[]  = { 0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9 };
static const uint8_t kFreeM[] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };

struct ApiPattern {
	const char* api;
	BytePat pat;
};

static const ApiPattern kSimplePatterns[] = {
	{ "il2cpp_domain_get",              { PAT(kDomainGet) } },
	{ "il2cpp_domain_get_assemblies",   { PAT(kAssemblies) } },
	{ "il2cpp_thread_attach",           { PAT(kAttach) } },
	{ "il2cpp_class_get_name",          { PAT(kClassName) } },
	{ "il2cpp_image_get_class_count",   { PAT(kImgCount) } },
	{ "il2cpp_image_get_class",         { PAT(kImgClass) } },
	{ "il2cpp_type_get_name",           { PAT(kTypeName) } },
	{ "il2cpp_method_get_param_count",  { PAT(kParamCount) } },
	{ "il2cpp_method_get_return_type",  { PAT(kRetType) } },
	{ "il2cpp_field_get_offset",        { PAT(kFieldOff) } },
	{ "il2cpp_field_get_type",          { PAT(kFieldType) } },
	{ "il2cpp_class_get_parent",        { PAT(kParent) } },
	{ "il2cpp_class_is_enum",           { PAT(kIsEnum) } },
	{ "il2cpp_class_is_valuetype",      { PAT(kIsValueType) } },
	{ "il2cpp_class_is_generic",        { PAT(kIsGeneric) } },
	{ "il2cpp_method_is_instance",      { PAT(kIsInstance) } },
	{ "il2cpp_field_get_flags",         { PAT(kFieldFlags) } },
	{ "il2cpp_method_get_flags",        { PAT(kMethodFlags) } },
	{ "il2cpp_method_get_param",        { PAT(kMethodParam) } },
	{ "il2cpp_free",                    { PAT(kFree) } },
};

void BuildMap(const char* module_name) {
	g_map.clear();
	HMODULE mod = GetModuleHandleA(module_name);
	if (!mod) return;

	const auto bodies = CollectExportBodies(mod);
	const BytePat mov_rax_rcx{ PAT(kMovRaxRcx) };
	const BytePat at_rcx_18{ PAT(kAtRcx18) };
	const BytePat iterator{ PAT(kIteratorPrologue) };
	const BytePat domain_alt{ PAT(kDomainGetAlt) };

	for (uint8_t* body : bodies) {
		for (const auto& entry : kSimplePatterns) {
			if (MatchBytes(body, kMatchWindow, entry.pat)) {
				MapFirst(entry.api, body);
			}
		}

		if (MatchBytes(body, kMatchWindow, domain_alt)) {
			MapFirst("il2cpp_domain_get", body);
		}

		// Shared tiny getters used by multiple APIs with identical layout offsets.
		if (MatchBytes(body, kMatchWindow, mov_rax_rcx)) {
			MapFirst("il2cpp_assembly_get_image", body);
			MapFirst("il2cpp_image_get_name", body);
			MapFirst("il2cpp_field_get_name", body);
		}
		if (MatchBytes(body, kMatchWindow, at_rcx_18)) {
			MapFirst("il2cpp_class_get_namespace", body);
			MapFirst("il2cpp_method_get_name", body);
		}
		if (MatchBytes(body, kMatchWindow, iterator)) {
			if (ContainsBytes(body, kMatchWindow, kFieldsMarker, sizeof(kFieldsMarker))) {
				MapFirst("il2cpp_class_get_fields", body);
			}
			if (ContainsBytes(body, kMatchWindow, kMethodsMarker, sizeof(kMethodsMarker))) {
				MapFirst("il2cpp_class_get_methods", body);
			}
			if (ContainsBytes(body, kMatchWindow, kNestedMarker, sizeof(kNestedMarker))) {
				MapFirst("il2cpp_class_get_nested_types", body);
			}
			if (ContainsBytes(body, kMatchWindow, kIfaceMarker, sizeof(kIfaceMarker))) {
				MapFirst("il2cpp_class_get_interfaces", body);
			}
		}
	}
}

using FnGetFields = void* (*)(void*, void**);
using FnGetFieldName = const char* (*)(void*);
using FnGetMethods = const void* (*)(void*, void**);
using FnGetMethodName = const char* (*)(void*);
using FnGetParamCount = int (*)(const void*);
using FnGetClassCount = size_t (*)(const void*);
using FnGetClass = const void* (*)(const void*, size_t);
using FnGetClassName = const char* (*)(void*);
using FnGetClassNamespace = const char* (*)(void*);

FnGetFields g_get_fields = nullptr;
FnGetFieldName g_get_field_name = nullptr;
FnGetMethods g_get_methods = nullptr;
FnGetMethodName g_get_method_name = nullptr;
FnGetParamCount g_get_param_count = nullptr;
FnGetClassCount g_get_class_count = nullptr;
FnGetClass g_get_class = nullptr;
FnGetClassName g_get_class_name = nullptr;
FnGetClassNamespace g_get_class_ns = nullptr;

void* __cdecl ShimFieldFromName(void* klass, const char* name) {
	if (!klass || !name || !g_get_fields || !g_get_field_name) return nullptr;
	void* iter = nullptr;
	while (void* field = g_get_fields(klass, &iter)) {
		const char* field_name = g_get_field_name(field);
		if (field_name && std::strcmp(field_name, name) == 0) return field;
	}
	return nullptr;
}

const void* __cdecl ShimMethodFromName(void* klass, const char* name, int argc) {
	if (!klass || !name || !g_get_methods || !g_get_method_name) return nullptr;
	void* iter = nullptr;
	while (const void* method = g_get_methods(klass, &iter)) {
		const char* method_name = g_get_method_name(const_cast<void*>(method));
		if (!method_name || std::strcmp(method_name, name) != 0) continue;
		if (argc >= 0 && g_get_param_count && g_get_param_count(method) != argc) continue;
		return method;
	}
	return nullptr;
}

void* __cdecl ShimClassFromName(const void* image, const char* namespaze, const char* name) {
	if (!image || !name || !g_get_class_count || !g_get_class || !g_get_class_name) return nullptr;
	const size_t count = g_get_class_count(image);
	for (size_t i = 0; i < count; ++i) {
		void* klass = const_cast<void*>(g_get_class(image, i));
		if (!klass) continue;
		const char* class_name = g_get_class_name(klass);
		if (!class_name || std::strcmp(class_name, name) != 0) continue;
		if (namespaze && *namespaze && g_get_class_ns) {
			const char* ns = g_get_class_ns(klass);
			if (!ns || std::strcmp(ns, namespaze) != 0) continue;
		}
		return klass;
	}
	return nullptr;
}

void __cdecl ShimFree(void*) {}
bool __cdecl ShimFalse(void*) { return false; }
void* __cdecl ShimEmptyIter(void*, void** iter) {
	if (iter) *iter = nullptr;
	return nullptr;
}
const char* __cdecl ShimEmptyParamName(const void*, uint32_t) { return ""; }

void SetIfEmpty(void** slot, void* fn) {
	if (slot && !*slot && fn) *slot = fn;
}

void FillFromMap(void** slot, const char* api) {
	if (!slot || *slot) return;
	if (void* fn = Resolve(nullptr, api)) *slot = fn;
}

bool ShouldTryRecovery(HMODULE mod) {
	if (!mod) {
		return false;
	}
	if (ModuleLooksRenamed(mod)) {
		return true;
	}
	if (GetProcAddress(mod, "il2cpp_domain_get")) {
		return false;
	}

	auto* base = reinterpret_cast<uint8_t*>(mod);
	auto* dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
	auto* nt = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dos->e_lfanew);
	const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!dir.VirtualAddress) {
		return false;
	}
	auto* exp = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(base + dir.VirtualAddress);
	return exp->NumberOfNames >= 20;
}

} // namespace

bool DetectAndEnable(const char* module_name) {
	g_active = false;
	g_map.clear();

	HMODULE mod = GetModuleHandleA(module_name);
	if (!mod || !ShouldTryRecovery(mod)) {
		return false;
	}

	g_active = true;
	BuildMap(module_name);
	return !g_map.empty();
}

bool Active() { return g_active; }

void Reset() {
	g_active = false;
	g_map.clear();
	g_get_fields = nullptr;
	g_get_field_name = nullptr;
	g_get_methods = nullptr;
	g_get_method_name = nullptr;
	g_get_param_count = nullptr;
	g_get_class_count = nullptr;
	g_get_class = nullptr;
	g_get_class_name = nullptr;
	g_get_class_ns = nullptr;
}

void* Resolve(const char* /*module_name*/, const char* api_name) {
	if (!g_active || !api_name) return nullptr;
	auto it = g_map.find(api_name);
	return it == g_map.end() ? nullptr : it->second;
}

bool PreferDirectCalls() { return g_active; }

void InstallFallbacks(ApiSlots& slots) {
	if (!g_active) return;

	FillFromMap(slots.domain_get, "il2cpp_domain_get");
	FillFromMap(slots.thread_attach, "il2cpp_thread_attach");
	FillFromMap(slots.get_assemblies, "il2cpp_domain_get_assemblies");
	FillFromMap(slots.get_image, "il2cpp_assembly_get_image");
	FillFromMap(slots.get_image_name, "il2cpp_image_get_name");
	FillFromMap(slots.get_class_count, "il2cpp_image_get_class_count");
	FillFromMap(slots.get_class, "il2cpp_image_get_class");
	FillFromMap(slots.get_class_name, "il2cpp_class_get_name");
	FillFromMap(slots.get_class_namespace, "il2cpp_class_get_namespace");
	FillFromMap(slots.get_class_parent, "il2cpp_class_get_parent");
	FillFromMap(slots.get_fields, "il2cpp_class_get_fields");
	FillFromMap(slots.get_field_name, "il2cpp_field_get_name");
	FillFromMap(slots.get_field_flags, "il2cpp_field_get_flags");
	FillFromMap(slots.get_field_offset, "il2cpp_field_get_offset");
	FillFromMap(slots.get_field_type, "il2cpp_field_get_type");
	FillFromMap(slots.get_methods, "il2cpp_class_get_methods");
	FillFromMap(slots.get_method_name, "il2cpp_method_get_name");
	FillFromMap(slots.get_method_return_type, "il2cpp_method_get_return_type");
	FillFromMap(slots.get_method_param_count, "il2cpp_method_get_param_count");
	FillFromMap(slots.get_method_param_type, "il2cpp_method_get_param");
	FillFromMap(slots.get_method_flags, "il2cpp_method_get_flags");
	FillFromMap(slots.get_type_name, "il2cpp_type_get_name");
	FillFromMap(slots.is_class_enum, "il2cpp_class_is_enum");
	FillFromMap(slots.is_class_valuetype, "il2cpp_class_is_valuetype");
	FillFromMap(slots.is_class_generic, "il2cpp_class_is_generic");
	FillFromMap(slots.is_method_instance, "il2cpp_method_is_instance");
	FillFromMap(slots.get_class_nested_types, "il2cpp_class_get_nested_types");
	FillFromMap(slots.get_class_interfaces, "il2cpp_class_get_interfaces");
	FillFromMap(slots.free_memory, "il2cpp_free");

	g_get_fields = slots.get_fields ? reinterpret_cast<FnGetFields>(*slots.get_fields) : nullptr;
	g_get_field_name = slots.get_field_name ? reinterpret_cast<FnGetFieldName>(*slots.get_field_name) : nullptr;
	g_get_methods = slots.get_methods ? reinterpret_cast<FnGetMethods>(*slots.get_methods) : nullptr;
	g_get_method_name = slots.get_method_name ? reinterpret_cast<FnGetMethodName>(*slots.get_method_name) : nullptr;
	g_get_param_count = slots.get_method_param_count ? reinterpret_cast<FnGetParamCount>(*slots.get_method_param_count) : nullptr;
	g_get_class_count = slots.get_class_count ? reinterpret_cast<FnGetClassCount>(*slots.get_class_count) : nullptr;
	g_get_class = slots.get_class ? reinterpret_cast<FnGetClass>(*slots.get_class) : nullptr;
	g_get_class_name = slots.get_class_name ? reinterpret_cast<FnGetClassName>(*slots.get_class_name) : nullptr;
	g_get_class_ns = slots.get_class_namespace ? reinterpret_cast<FnGetClassNamespace>(*slots.get_class_namespace) : nullptr;

	SetIfEmpty(slots.get_field_by_name, reinterpret_cast<void*>(&ShimFieldFromName));
	SetIfEmpty(slots.get_method_by_name, reinterpret_cast<void*>(&ShimMethodFromName));
	SetIfEmpty(slots.get_class_by_name, reinterpret_cast<void*>(&ShimClassFromName));
	SetIfEmpty(slots.free_memory, reinterpret_cast<void*>(&ShimFree));
	SetIfEmpty(slots.get_method_param_name, reinterpret_cast<void*>(&ShimEmptyParamName));
	SetIfEmpty(slots.get_class_nested_types, reinterpret_cast<void*>(&ShimEmptyIter));
	SetIfEmpty(slots.get_class_interfaces, reinterpret_cast<void*>(&ShimEmptyIter));
	if (!slots.is_class_generic || !*slots.is_class_generic) {
		SetIfEmpty(slots.is_class_generic, reinterpret_cast<void*>(&ShimFalse));
	}
}

} // namespace renamed_exports
