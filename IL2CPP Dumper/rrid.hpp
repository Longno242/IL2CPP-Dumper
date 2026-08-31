#ifndef INCLUDE_RRID_HPP
#define INCLUDE_RRID_HPP

#define RRID_FIELD_ACCESS_LEVEL_MASK			    0x0007
#define RRID_FIELD_ACCESS_LEVEL_PRIVATE			    0x0001
#define RRID_FIELD_ACCESS_LEVEL_PRIVATE_PROTECTED   0x0002
#define RRID_FIELD_ACCESS_LEVEL_INTERNAL            0x0003
#define RRID_FIELD_ACCESS_LEVEL_PROTECTED           0x0004
#define RRID_FIELD_ACCESS_LEVEL_PROTECTED_INTERNAL  0x0005
#define RRID_FIELD_ACCESS_LEVEL_PUBLIC			    0x0006
#define RRID_FIELD_ATTRIBUTE_STATIC                 0x0010
#define RRID_FIELD_ATTRIBUTE_READONLY               0x0020
#define RRID_FIELD_ATTRIBUTE_CONST                  0x0040
#define RRID_METHOD_ACCESS_LEVEL_MASK				  0x0007
#define RRID_METHOD_ACCESS_LEVEL_PRIVATE              0x0001
#define RRID_METHOD_ACCESS_LEVEL_PRIVATE_PROTECTED    0x0002
#define RRID_METHOD_ACCESS_LEVEL_INTERNAL             0x0003
#define RRID_METHOD_ACCESS_LEVEL_PROTECTED            0x0004
#define RRID_METHOD_ACCESS_LEVEL_PROTECTED_INTERNAL   0x0005
#define RRID_METHOD_ACCESS_LEVEL_PUBLIC               0x0006
#define RRID_METHOD_ATTRIBUTE_STATIC                  0x0010
#define RRID_METHOD_ATTRIBUTE_FINAL                   0x0020
#define RRID_METHOD_ATTRIBUTE_VIRTUAL                 0x0040
#define RRID_METHOD_ATTRIBUTE_ABSTRACT                0x0400
#define RRID_METHOD_ATTRIBUTE_NEW_SLOT                0x0100
#define RRID_METHOD_ATTRIBUTE_PINVOKE_IMPL            0x2000

#include <memory>
#include <string>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include "experimental/renamed_exports.h"
#include "experimental/export_scan.h"
#include "experimental/module_discovery.h"

#include <Windows.h>

class RridImage;
class RridClass;
class RridField;
class RridMethod;

inline uint64_t GetModuleBaseAddress(const char* moduleName) {
	HMODULE hModule = GetModuleHandleA(moduleName);
	if (!hModule) return 0;
	return (uint64_t)hModule;
}

inline uint64_t CalculateRVA(uint64_t address, uint64_t moduleBase) {
	if (address > moduleBase) {
		return address - moduleBase;
	}
	return address;
}

namespace rrid {
namespace detail {
	inline std::string& module_name_storage() {
		static std::string name = "GameAssembly.dll";
		return name;
	}

	inline uint64_t& module_base_storage() {
		static uint64_t base = 0;
		return base;
	}

	inline void invalidate_module_cache() {
		module_base_storage() = 0;
	}

	inline uint64_t get_module_base() {
		auto& base = module_base_storage();
		if (!base) {
			base = GetModuleBaseAddress(module_name_storage().c_str());
		}
		return base;
	}

	inline const char* module_name_cstr() {
		return module_name_storage().c_str();
	}
}
}

namespace rrid {
	bool init();
	void set_module_name(const std::string& name);
	const std::string& get_module_name();
	bool auto_detect_module();
	const char* get_spoofer_backend();
	const char* get_init_error();
	const char* get_api_scan_path();

	[[nodiscard]] RridImage* get_image(const std::string& name);
	[[nodiscard]] const std::vector<RridImage*>& get_images();
}

class RridImage {
public:
	RridImage(const std::string& name, const void* image)
		: name_(name), image_(image) {

		module_base_ = rrid::detail::get_module_base();
	}

	inline const std::string& get_name() const { return name_; }
	inline const void* get_raw() const { return image_; }
	inline uint64_t get_module_base() const { return module_base_; }

	inline uint64_t get_image_rva() const {
		return CalculateRVA((uint64_t)image_, module_base_);
	}

	RridClass* get_class(const std::string& name, const std::string& namespaze = "");
	const std::vector<RridClass*>& get_classes();
private:
	std::string name_;
	const void* image_ = nullptr;
	uint64_t module_base_ = 0;

	std::vector<RridClass*> classes_pointers_;
	std::vector<std::unique_ptr<RridClass>> classes_;
	std::unordered_map<uint64_t, RridClass*> class_lookup_;

	bool classes_initialized_ = false;
};

class RridClass {
public:
	RridClass(const std::string& name, const std::string& namespaze, const void* klass)
		: name_(name), namespace_(namespaze), class_(klass) {

		module_base_ = rrid::detail::get_module_base();
	}

	inline const std::string& get_name() const { return name_; }
	inline const std::string& get_namespace() const { return namespace_; }
	inline const void* get_raw() const { return class_; }

	inline uint64_t get_class_rva() const {
		return CalculateRVA((uint64_t)class_, module_base_);
	}

	inline uint64_t get_module_base() const { return module_base_; }

	bool is_enum();
	bool is_generic();
	bool is_valuetype();

	std::string get_parent_full_name();
	const std::vector<std::string>& get_interface_full_names();

	RridField* get_field(const std::string& name);
	const std::vector<RridField*>& get_fields();

	RridMethod* get_method(const std::string& name, int args_count);
	const std::vector<RridMethod*>& get_methods();

	RridClass* get_nested_type(const std::string& name);
	const std::vector<RridClass*>& get_nested_types();
private:
	static std::string get_type_name(const void* type);

	std::string name_;
	std::string namespace_;
	const void* class_ = nullptr;
	uint64_t module_base_ = 0;

	std::vector<RridField*> fields_pointers_;
	std::vector<std::unique_ptr<RridField>> fields_;
	std::unordered_map<uint64_t, RridField*> fields_lookup_;
	std::vector<RridMethod*> methods_pointers_;
	std::vector<std::unique_ptr<RridMethod>> methods_;
	std::vector<RridClass*> nested_types_pointers_;
	std::vector<std::unique_ptr<RridClass>> nested_types_;

	std::string parent_name_;
	std::vector<std::string> interface_names_;
	bool parent_initialized_ = false;
	bool interfaces_initialized_ = false;
	bool fields_initialized_ = false;
	bool methods_initialized_ = false;
	bool nested_types_initialized_ = false;
};

class RridField {
public:
	RridField(const std::string& name, const std::string& type, uint64_t offset, uint32_t flags, const void* field)
		: name_(name), type_(type), offset_(offset), flags_(flags), field_(field) {
		module_base_ = rrid::detail::get_module_base();

		if (flags & RRID_FIELD_ATTRIBUTE_STATIC && field) {
			static_rva_ = CalculateRVA((uint64_t)field, module_base_);
		}
		else {
			static_rva_ = 0;
		}
	}

	inline const std::string& get_name() const { return name_; }
	inline const std::string& get_type() const { return type_; }
	inline uint32_t get_flags() const { return flags_; }
	inline uint64_t get_offset() const { return offset_; }
	inline const void* get_raw() const { return field_; }
	inline uint64_t get_static_rva() const { return static_rva_; }
	inline uint64_t get_static_address() const { return (uint64_t)field_; }
	inline bool has_rva() const { return (flags_ & RRID_FIELD_ATTRIBUTE_STATIC) && static_rva_ != 0; }

	inline std::string get_offset_info() const {
		char buf[64];
		if (flags_ & RRID_FIELD_ATTRIBUTE_STATIC) {
			snprintf(buf, sizeof(buf), "RVA: 0x%llX (Absolute: 0x%llX)", static_rva_, (uint64_t)field_);
		}
		else {
			snprintf(buf, sizeof(buf), "Instance Offset: 0x%llX", offset_);
		}
		return std::string(buf);
	}

private:
	std::string name_;
	std::string type_;
	uint64_t offset_;
	uint32_t flags_;
	const void* field_ = nullptr;
	uint64_t static_rva_ = 0;
	uint64_t module_base_ = 0;
};

class RridMethod {
public:
	RridMethod(const std::string& name, const std::string& return_type, const std::vector<std::pair<std::string, std::string>>& params, uint32_t flags, void* address, const void* method)
		: name_(name), return_type_(return_type), flags_(flags), params_(params), address_(address), method_(method) {
		module_base_ = rrid::detail::get_module_base();

		if (address) {
			method_rva_ = CalculateRVA((uint64_t)address, module_base_);
		}

		if (method) {
			method_info_rva_ = CalculateRVA((uint64_t)method, module_base_);
		}
	}

	inline const std::string& get_name() const { return name_; }
	inline const std::string& get_return_type() const { return return_type_; }
	inline uint32_t get_flags() const { return flags_; }
	inline void* get_address() const { return address_; }
	inline const void* get_raw() const { return method_; }

	inline size_t get_param_count() const { return params_.size(); }
	const std::vector<std::pair<std::string, std::string>>& get_params() const { return params_; }
	const std::pair<std::string, std::string>& get_param(size_t index) const { return params_.at(index); }

	inline uint64_t get_method_rva() const { return method_rva_; }
	inline uint64_t get_method_info_rva() const { return method_info_rva_; }
	inline uint64_t get_absolute_address() const { return (uint64_t)address_; }

	inline std::string get_rva_info() const {
		char buf[128];
		snprintf(buf, sizeof(buf), "Code RVA: 0x%llX | MethodInfo RVA: 0x%llX | Absolute: 0x%llX",
			method_rva_, method_info_rva_, (uint64_t)address_);
		return std::string(buf);
	}

private:
	std::string name_;
	std::string return_type_;
	uint32_t flags_;
	std::vector<std::pair<std::string, std::string>> params_;
	void* address_ = nullptr;
	const void* method_ = nullptr;
	uint64_t method_rva_ = 0;
	uint64_t method_info_rva_ = 0;
	uint64_t module_base_ = 0;
};

namespace rrid {
	namespace detail {
		struct RridContext {
			void* jmp_rdx = nullptr;
			bool initialized = false;
			std::string module_name = "GameAssembly.dll";
			uint64_t module_base = 0;

			uint32_t method_pointer_offset = 0;

			std::vector<std::unique_ptr<RridImage>> images;
			std::unordered_map<std::string, RridImage*> image_lookup;

			// [ il2cpp api functions ]
			struct {
				void* (*domain_get)() = nullptr;
				void* (*thread_attach)(void* domain) = nullptr;
				const void** (*get_assemblies)(const void* domain, size_t* size) = nullptr;
				const void* (*get_image)(const void* assembly) = nullptr;
				const char* (*get_image_name)(const void* image) = nullptr;
				size_t(*get_class_count)(const void* image) = nullptr;
				const void* (*get_class)(const void* image, size_t index) = nullptr;
				void* (*get_class_by_name)(const void* image, const char* namespaze, const char* name) = nullptr;
				const char* (*get_class_name)(void* klass) = nullptr;
				const char* (*get_class_namespace)(void* klass) = nullptr;
				bool(*is_class_enum)(const void* klass) = nullptr;
				bool(*is_class_valuetype)(const void* klass) = nullptr;
				void* (*get_fields)(void* klass, void** iter) = nullptr;
				void* (*get_field_by_name)(void* klass, const char* name) = nullptr;
				const char* (*get_field_name)(void* field) = nullptr;
				int(*get_field_flags)(void* field) = nullptr;
				size_t(*get_field_offset)(void* field) = nullptr;
				const void* (*get_field_type)(void* field) = nullptr;
				char* (*get_type_name)(const void* type) = nullptr;
				const void* (*get_method_by_name)(void* klass, const char* name, int argsCount) = nullptr;
				const void* (*get_methods)(void* klass, void** iter) = nullptr;
				const char* (*get_method_name)(const void* method) = nullptr;
				const void* (*get_method_return_type)(const void* method) = nullptr;
				uint32_t(*get_method_param_count)(const void* method) = nullptr;
				const char* (*get_method_param_name)(const void* method, uint32_t index) = nullptr;
				bool (*is_class_generic)(const void* klass) = nullptr;
				const void* (*get_method_param_type)(const void* method, uint32_t index) = nullptr;
				void(*free_memory)(void* ptr) = nullptr;
				uint32_t(*get_method_flags)(const void* method, uint32_t* iflags) = nullptr;
				bool (*is_method_instance)(const void* method) = nullptr;
				void* (*get_class_nested_types)(void* klass, void** iter) = nullptr;
				void* (*get_class_parent)(void* klass) = nullptr;
				void* (*get_class_interfaces)(void* klass, void** iter) = nullptr;
			} api;

			const char* spoofer_backend_name = "unknown";
			std::string init_error;
			bool use_direct_calls = false;
		};

		struct ApiScanCache {
			std::unordered_map<std::string, void*> functions;
			bool scanned = false;
		};

		inline ApiScanCache& api_scan_cache() {
			static ApiScanCache cache;
			return cache;
		}

		inline void reset_api_scan_cache() {
			auto& cache = api_scan_cache();
			cache.functions.clear();
			cache.scanned = false;
		}

		inline void* resolve_export(const char* name) {
			HMODULE mod = GetModuleHandleA(module_name_cstr());
			if (!mod) {
				return nullptr;
			}
			return reinterpret_cast<void*>(GetProcAddress(mod, name));
		}

		inline RridContext& get_context() {
			static RridContext ctx;
			return ctx;
		}

		namespace pattern {
			inline void* find_pattern(size_t start, size_t end, const char* pattern, const char* mask) {
				if (!pattern || !mask) {
					return nullptr;
				}

				const size_t pattern_size = strlen(mask);
				if (end <= start || pattern_size > (end - start)) {
					return nullptr;
				}

				for (auto curr = (uint8_t*)start; curr <= (uint8_t*)end - pattern_size; curr++) {
					bool found = true;

					for (size_t i = 0; i < pattern_size; i++) {
						if (mask[i] != '?' && (uint8_t)pattern[i] != *(uint8_t*)(curr + i)) {
							found = false;
							break;
						}
					}

					if (found) {
						return curr;
					}
				}

				return nullptr;
			}

			inline void* find_pattern(const char* module, const char* pattern, const char* mask) {
				auto module_base = (size_t)GetModuleHandleA(module);
				if (!module_base) {
					return nullptr;
				}

				auto nt = (PIMAGE_NT_HEADERS)(module_base + ((PIMAGE_DOS_HEADER)module_base)->e_lfanew);
				return find_pattern(module_base, module_base + nt->OptionalHeader.SizeOfImage, pattern, mask);
			}
		}

		inline void* find_jmp_gadget() {
			static const char* patterns[] = {
				"\xFF\x27", // jmp [rdi] - legacy stub
				"\xFF\x23", // jmp [rbx] - v2 stub
				"\xFF\x26", // jmp [rsi]
				"\xFF\x21", // jmp [rcx]
			};

			const char* modules[] = {
				module_name_cstr(),
				"UnityPlayer.dll",
				nullptr
			};

			for (const char* mod : modules) {
				if (!mod || !GetModuleHandleA(mod)) {
					continue;
				}
				for (const char* pat : patterns) {
					if (void* hit = pattern::find_pattern(mod, pat, "xx")) {
						return hit;
					}
				}
			}
			return nullptr;
		}

		namespace il2cpp {
			namespace detail {

				inline void* find_api_function(const std::string& name) {
					if (void* exported = resolve_export(name.c_str())) {
						return exported;
					}

					if (void* scanned = export_scan::lookup(name.c_str())) {
						return scanned;
					}

					if (renamed_exports::Active()) {
						if (void* p = renamed_exports::Resolve(module_name_cstr(), name.c_str())) {
							return p;
						}
					}

					auto& cache = api_scan_cache();
					if (cache.scanned) {
						auto it = cache.functions.find(name);
						return (it != cache.functions.end()) ? it->second : nullptr;
					}

					void* first_time_addr = nullptr;

					if (GetModuleHandleA("UnityPlayer.dll")) {
						void* start = pattern::find_pattern("UnityPlayer.dll", "\x48\x89\x5C\x24\x00\x48\x8D\x15\x00\x00\x00\x00\xB3", "xxxx?xxx????x");
						void* end = pattern::find_pattern("UnityPlayer.dll", "\xE8\x00\x00\x00\x00\x48\x8B\x5C\x24\x00\x32\xC0\x48\xC7\x05", "x????xxxx?xxxxx");
						if (start && end) {
							auto curr = (uint8_t*)start;
							while (curr < (uint8_t*)end) {
								auto lea = (uint8_t*)pattern::find_pattern((size_t)curr, (size_t)end, "\x48\x8D\x15", "xxx");
								if (!lea) {
									break;
								}

								auto func_name = std::string((char*)(lea + *(int32_t*)(lea + 3) + 7));

								auto mov = (uint8_t*)pattern::find_pattern((size_t)(lea + 1), (size_t)end, "\x48\x89\x05", "xxx");
								if (!mov) {
									break;
								}

								void* func_addr = *(void**)(mov + *(int32_t*)(mov + 3) + 7);
								cache.functions[func_name] = func_addr;

								if (name == func_name) {
									first_time_addr = func_addr;
								}

								curr = mov + 1;
							}
						}
					}

					cache.scanned = true;
					return first_time_addr;
				}

				inline const char* first_missing_api(const RridContext& ctx) {
					struct Entry { const char* name; const void* ptr; };
					const Entry required[] = {
						{ "il2cpp_domain_get", (const void*)ctx.api.domain_get },
						{ "il2cpp_thread_attach", (const void*)ctx.api.thread_attach },
						{ "il2cpp_domain_get_assemblies", (const void*)ctx.api.get_assemblies },
						{ "il2cpp_assembly_get_image", (const void*)ctx.api.get_image },
						{ "il2cpp_image_get_name", (const void*)ctx.api.get_image_name },
						{ "il2cpp_image_get_class_count", (const void*)ctx.api.get_class_count },
						{ "il2cpp_class_from_name", (const void*)ctx.api.get_class_by_name },
						{ "il2cpp_image_get_class", (const void*)ctx.api.get_class },
						{ "il2cpp_class_get_name", (const void*)ctx.api.get_class_name },
						{ "il2cpp_class_get_namespace", (const void*)ctx.api.get_class_namespace },
						{ "il2cpp_class_is_enum", (const void*)ctx.api.is_class_enum },
						{ "il2cpp_class_is_valuetype", (const void*)ctx.api.is_class_valuetype },
						{ "il2cpp_class_get_fields", (const void*)ctx.api.get_fields },
						{ "il2cpp_class_get_field_from_name", (const void*)ctx.api.get_field_by_name },
						{ "il2cpp_field_get_name", (const void*)ctx.api.get_field_name },
						{ "il2cpp_field_get_flags", (const void*)ctx.api.get_field_flags },
						{ "il2cpp_field_get_offset", (const void*)ctx.api.get_field_offset },
						{ "il2cpp_field_get_type", (const void*)ctx.api.get_field_type },
						{ "il2cpp_type_get_name", (const void*)ctx.api.get_type_name },
						{ "il2cpp_class_get_method_from_name", (const void*)ctx.api.get_method_by_name },
						{ "il2cpp_class_get_methods", (const void*)ctx.api.get_methods },
						{ "il2cpp_method_get_name", (const void*)ctx.api.get_method_name },
						{ "il2cpp_method_get_return_type", (const void*)ctx.api.get_method_return_type },
						{ "il2cpp_method_get_param_count", (const void*)ctx.api.get_method_param_count },
						{ "il2cpp_method_get_param_name", (const void*)ctx.api.get_method_param_name },
						{ "il2cpp_class_is_generic", (const void*)ctx.api.is_class_generic },
						{ "il2cpp_method_get_param", (const void*)ctx.api.get_method_param_type },
						{ "il2cpp_free", (const void*)ctx.api.free_memory },
						{ "il2cpp_method_get_flags", (const void*)ctx.api.get_method_flags },
						{ "il2cpp_method_is_instance", (const void*)ctx.api.is_method_instance },
						{ "il2cpp_class_get_nested_types", (const void*)ctx.api.get_class_nested_types },
					};

					for (const auto& entry : required) {
						if (!entry.ptr) {
							return entry.name;
						}
					}
					return nullptr;
				}
			}

			inline bool find_api_functions(std::string* error_out = nullptr) {
				auto& ctx = get_context();
				reset_api_scan_cache();
				export_scan::reset();
				export_scan::build_cache(module_name_cstr());
				renamed_exports::Reset();

				ctx.api = {};
				ctx.method_pointer_offset = 0;

				ctx.api.domain_get = (decltype(ctx.api.domain_get))resolve_export("il2cpp_domain_get");
				if (!ctx.api.domain_get) {
					ctx.api.domain_get = (decltype(ctx.api.domain_get))pattern::find_pattern(::rrid::detail::module_name_cstr(), "\x48\x83\xEC\x00\x48\x63\x05\x00\x00\x00\x00\x48\x8D\x0D\x00\x00\x00\x00\x4C\x8B\x05\x00\x00\x00\x00\x48\x8D\x15\x00\x00\x00\x00\x8B\x0C\x08\x48\x8B\x44\x24\x00\x48\xC1\xE1\x00\x48\x03\xCA\x48\x3B\xC1\x73\x00\x48\x8B\x44\x24\x00\x48\x3B\xC2\x73\x00\x49\x63\x40\x00\x42\x8B\x4C\x00\x00\x48\x8B\x44\x24\x00\x49\x8D\x14\x88\x48\x3B\xC2\x0F\x83", "xxx?xxx????xxx????xxx????xxx????xxxxxxx?xxx?xxxxxxx?xxxx?xxxx?xxx?xxxx?xxxx?xxxxxxxxx");
				}

				ctx.api.thread_attach = (decltype(ctx.api.thread_attach))resolve_export("il2cpp_thread_attach");
				if (!ctx.api.thread_attach) {
					ctx.api.thread_attach = (decltype(ctx.api.thread_attach))pattern::find_pattern(::rrid::detail::module_name_cstr(), "\x40\x56\x48\x83\xEC\x00\x48\x8B\xF1\x48\x8B\x0D\x00\x00\x00\x00\x8B\x09", "xxxxx?xxxxxx????xx");
				}
				if (!ctx.api.thread_attach) {
					ctx.api.thread_attach = (decltype(ctx.api.thread_attach))pattern::find_pattern(::rrid::detail::module_name_cstr(), "\x48\x89\x5C\x24\x00\x57\x48\x83\xEC\x00\x48\x8B\xF9\x48\x8B\x0D", "xxxx?xxxx?xxxxxx");
				}
				ctx.api.get_assemblies = (decltype(ctx.api.get_assemblies))detail::find_api_function("il2cpp_domain_get_assemblies");
				ctx.api.get_image = (decltype(ctx.api.get_image))detail::find_api_function("il2cpp_assembly_get_image");
				ctx.api.get_image_name = (decltype(ctx.api.get_image_name))detail::find_api_function("il2cpp_image_get_name");
				ctx.api.get_class_count = (decltype(ctx.api.get_class_count))detail::find_api_function("il2cpp_image_get_class_count");
				ctx.api.get_class_by_name = (decltype(ctx.api.get_class_by_name))detail::find_api_function("il2cpp_class_from_name");
				ctx.api.get_class = (decltype(ctx.api.get_class))detail::find_api_function("il2cpp_image_get_class");
				ctx.api.get_class_name = (decltype(ctx.api.get_class_name))detail::find_api_function("il2cpp_class_get_name");
				ctx.api.get_class_namespace = (decltype(ctx.api.get_class_namespace))detail::find_api_function("il2cpp_class_get_namespace");
				ctx.api.is_class_enum = (decltype(ctx.api.is_class_enum))detail::find_api_function("il2cpp_class_is_enum");
				ctx.api.is_class_valuetype = (decltype(ctx.api.is_class_valuetype))detail::find_api_function("il2cpp_class_is_valuetype");
				ctx.api.get_fields = (decltype(ctx.api.get_fields))detail::find_api_function("il2cpp_class_get_fields");
				ctx.api.get_field_by_name = (decltype(ctx.api.get_field_by_name))detail::find_api_function("il2cpp_class_get_field_from_name");
				ctx.api.get_field_name = (decltype(ctx.api.get_field_name))detail::find_api_function("il2cpp_field_get_name");
				ctx.api.get_field_flags = (decltype(ctx.api.get_field_flags))detail::find_api_function("il2cpp_field_get_flags");
				ctx.api.get_field_offset = (decltype(ctx.api.get_field_offset))detail::find_api_function("il2cpp_field_get_offset");
				ctx.api.get_field_type = (decltype(ctx.api.get_field_type))detail::find_api_function("il2cpp_field_get_type");
				ctx.api.get_type_name = (decltype(ctx.api.get_type_name))detail::find_api_function("il2cpp_type_get_name");
				ctx.api.get_method_by_name = (decltype(ctx.api.get_method_by_name))detail::find_api_function("il2cpp_class_get_method_from_name");
				ctx.api.get_methods = (decltype(ctx.api.get_methods))detail::find_api_function("il2cpp_class_get_methods");
				ctx.api.get_method_name = (decltype(ctx.api.get_method_name))detail::find_api_function("il2cpp_method_get_name");
				ctx.api.get_method_return_type = (decltype(ctx.api.get_method_return_type))detail::find_api_function("il2cpp_method_get_return_type");
				ctx.api.get_method_param_count = (decltype(ctx.api.get_method_param_count))detail::find_api_function("il2cpp_method_get_param_count");
				ctx.api.get_method_param_name = (decltype(ctx.api.get_method_param_name))detail::find_api_function("il2cpp_method_get_param_name");
				ctx.api.is_class_generic = (decltype(ctx.api.is_class_generic))detail::find_api_function("il2cpp_class_is_generic");
				ctx.api.get_method_param_type = (decltype(ctx.api.get_method_param_type))detail::find_api_function("il2cpp_method_get_param");
				ctx.api.free_memory = (decltype(ctx.api.free_memory))detail::find_api_function("il2cpp_free");
				ctx.api.get_method_flags = (decltype(ctx.api.get_method_flags))detail::find_api_function("il2cpp_method_get_flags");
				ctx.api.is_method_instance = (decltype(ctx.api.is_method_instance))detail::find_api_function("il2cpp_method_is_instance");
				ctx.api.get_class_nested_types = (decltype(ctx.api.get_class_nested_types))detail::find_api_function("il2cpp_class_get_nested_types");
				ctx.api.get_class_parent = (decltype(ctx.api.get_class_parent))detail::find_api_function("il2cpp_class_get_parent");
				ctx.api.get_class_interfaces = (decltype(ctx.api.get_class_interfaces))detail::find_api_function("il2cpp_class_get_interfaces");

				void* address = pattern::find_pattern(::rrid::detail::module_name_cstr(), "\xF3\x0F\x11\x4C\x24\x00\x4C\x8B\xDC\x48\x81\xEC\x00\x00\x00\x00\x49\x8D\x43\x00\x49\x89\x4B", "xxxxx?xxxxxx????xxx?xxx");
				if (!address) {
					address = pattern::find_pattern(::rrid::detail::module_name_cstr(), "\xF3\x0F\x11\x54\x24\x00\xF3\x0F\x11\x4C\x24\x00\x4C\x8B\xDC\x48\x83\xEC\x00\x49\x89\x4B\x00\x49\x8B\xC1\x49\x8D\x4B\x00\x49\xC7\x43\x00\x00\x00\x00\x00\x49\x89\x4B\x00\x4D\x8D\x4B\x00\x49\x8D\x4B\x00\x45\x33\xC0\x49\x89\x4B\x00\x48\x8B\xD0\x48\x8B\x48", "xxxxx?xxxxx?xxxxxx?xxx?xxxxxx?xxx?????xxx?xxx?xxx?xxxxxx?xxxxxx");
				}

				if (address) {
					auto mov = (uint8_t*)pattern::find_pattern((size_t)address, (size_t)address + 0x7E, "\x48\x8B\x48", "xxx");
					if (mov) {
						ctx.method_pointer_offset = mov[3];
					}
				}
				if (!ctx.method_pointer_offset) {
					ctx.method_pointer_offset = sizeof(void*);
				}

				auto apply_renamed_fallbacks = [&]() {
					renamed_exports::ApiSlots slots;
					slots.domain_get = reinterpret_cast<void**>(&ctx.api.domain_get);
					slots.thread_attach = reinterpret_cast<void**>(&ctx.api.thread_attach);
					slots.get_assemblies = reinterpret_cast<void**>(&ctx.api.get_assemblies);
					slots.get_image = reinterpret_cast<void**>(&ctx.api.get_image);
					slots.get_image_name = reinterpret_cast<void**>(&ctx.api.get_image_name);
					slots.get_class_count = reinterpret_cast<void**>(&ctx.api.get_class_count);
					slots.get_class = reinterpret_cast<void**>(&ctx.api.get_class);
					slots.get_class_name = reinterpret_cast<void**>(&ctx.api.get_class_name);
					slots.get_class_namespace = reinterpret_cast<void**>(&ctx.api.get_class_namespace);
					slots.get_class_by_name = reinterpret_cast<void**>(&ctx.api.get_class_by_name);
					slots.get_class_parent = reinterpret_cast<void**>(&ctx.api.get_class_parent);
					slots.get_fields = reinterpret_cast<void**>(&ctx.api.get_fields);
					slots.get_field_by_name = reinterpret_cast<void**>(&ctx.api.get_field_by_name);
					slots.get_field_name = reinterpret_cast<void**>(&ctx.api.get_field_name);
					slots.get_field_flags = reinterpret_cast<void**>(&ctx.api.get_field_flags);
					slots.get_field_offset = reinterpret_cast<void**>(&ctx.api.get_field_offset);
					slots.get_field_type = reinterpret_cast<void**>(&ctx.api.get_field_type);
					slots.get_methods = reinterpret_cast<void**>(&ctx.api.get_methods);
					slots.get_method_by_name = reinterpret_cast<void**>(&ctx.api.get_method_by_name);
					slots.get_method_name = reinterpret_cast<void**>(&ctx.api.get_method_name);
					slots.get_method_return_type = reinterpret_cast<void**>(&ctx.api.get_method_return_type);
					slots.get_method_param_count = reinterpret_cast<void**>(&ctx.api.get_method_param_count);
					slots.get_method_param_name = reinterpret_cast<void**>(&ctx.api.get_method_param_name);
					slots.get_method_param_type = reinterpret_cast<void**>(&ctx.api.get_method_param_type);
					slots.get_method_flags = reinterpret_cast<void**>(&ctx.api.get_method_flags);
					slots.get_type_name = reinterpret_cast<void**>(&ctx.api.get_type_name);
					slots.free_memory = reinterpret_cast<void**>(&ctx.api.free_memory);
					slots.is_class_enum = reinterpret_cast<void**>(&ctx.api.is_class_enum);
					slots.is_class_valuetype = reinterpret_cast<void**>(&ctx.api.is_class_valuetype);
					slots.is_class_generic = reinterpret_cast<void**>(&ctx.api.is_class_generic);
					slots.is_method_instance = reinterpret_cast<void**>(&ctx.api.is_method_instance);
					slots.get_class_nested_types = reinterpret_cast<void**>(&ctx.api.get_class_nested_types);
					slots.get_class_interfaces = reinterpret_cast<void**>(&ctx.api.get_class_interfaces);
					renamed_exports::InstallFallbacks(slots);
				};

				if (const char* missing = detail::first_missing_api(ctx)) {
					if (renamed_exports::DetectAndEnable(module_name_cstr())) {
						apply_renamed_fallbacks();
						if (const char* still = detail::first_missing_api(ctx)) {
							if (error_out) {
								*error_out = std::string("missing API: ") + still + " (renamed-export mode)";
							}
							return false;
						}
						return true;
					}
					if (error_out) {
						*error_out = std::string("missing API: ") + missing;
					}
					return false;
				}

				return true;
			}
		}

		namespace fnv1a {
			inline uint64_t hash_class(const char* namespaze, const char* name) {
				uint64_t hash = 0x14650FB0739D0383;
				while (*namespaze) {
					hash ^= (uint8_t)*namespaze++;
					hash *= 0x100000001B3;
				}

				hash ^= ':';
				hash *= 0x100000001B3;

				while (*name) {
					hash ^= (uint8_t)*name++;
					hash *= 0x100000001B3;
				}

				return hash;
			}
		}

		// Return-address spoofing: v2 rbx/r15 stub (default) with legacy rdi stub fallback.
		namespace return_spoofer {
			namespace detail {
#pragma section(".text")
				__declspec(allocate(".text")) __declspec(selectany) __declspec(align(16))
					uint8_t legacy_stub_asm[] = {
						0x41, 0x5B,
						0x48, 0x83, 0xC4, 0x08,
						0x48, 0x8B, 0x44, 0x24, 0x18,
						0x4C, 0x8B, 0x10,
						0x4C, 0x89, 0x14, 0x24,
						0x4C, 0x8B, 0x50, 0x08,
						0x4C, 0x89, 0x58, 0x08,
						0x48, 0x89, 0x78, 0x10,
						0x48, 0x8D, 0x3D, 0x09, 0x00, 0x00, 0x00,
						0x48, 0x89, 0x38,
						0x48, 0x8B, 0xF8,
						0x41, 0xFF, 0xE2,
						0x48, 0x83, 0xEC, 0x10,
						0x48, 0x8B, 0xCF,
						0x48, 0x8B, 0x79, 0x10,
						0xFF, 0x61, 0x08
				};

				__declspec(allocate(".text")) __declspec(selectany) __declspec(align(16))
					uint8_t v2_stub_asm[] = {
						0x41, 0x5B,
						0x48, 0x83, 0xC4, 0x08,
						0x48, 0x8B, 0x5C, 0x24, 0x18,
						0x41, 0x57,
						0x4C, 0x8B, 0xFB,
						0x4D, 0x8B, 0x17,
						0x4C, 0x89, 0x14, 0x24,
						0x49, 0x8B, 0x47, 0x08,
						0x4D, 0x89, 0x19,
						0x49, 0x89, 0x5F, 0x10,
						0x48, 0x8D, 0x1D, 0x08, 0x00, 0x00, 0x00,
						0x49, 0x89, 0x1F,
						0x49, 0x8B, 0xDF,
						0xFF, 0xE0,
						0x48, 0x83, 0xEC, 0x10,
						0x48, 0x8B, 0xCB,
						0x41, 0x5F,
						0x48, 0x8B, 0x59, 0x10,
						0xFF, 0x61, 0x08
				};

				enum class Backend {
					V2,
					Legacy
				};

				inline Backend& selected_backend() {
					static Backend backend = Backend::Legacy;
					return backend;
				}

				inline void* active_stub() {
					return selected_backend() == Backend::V2
						? (void*)v2_stub_asm
						: (void*)legacy_stub_asm;
				}

				struct shell_params {
					const void* trampoline;
					void* function;
					void* register_;
				};

				inline auto _spoofer_stub() {
					return (void(*)())active_stub();
				}

				template <typename Ret, typename... Args>
				static inline auto shellcode_stub_helper(
					const void* shell,
					Args... args
				) -> Ret {
					auto fn = (Ret(*)(Args...))(shell);
					return fn(args...);
				}

				template <std::size_t Argc, typename>
				struct argument_remapper {

					template<
						typename Ret,
						typename First,
						typename Second,
						typename Third,
						typename Fourth,
						typename... Pack
					>
					static auto do_call(
						const void* shell,
						void* shell_param,
						First first,
						Second second,
						Third third,
						Fourth fourth,
						Pack... pack
					) -> Ret {
						return shellcode_stub_helper<
							Ret,
							First,
							Second,
							Third,
							Fourth,
							void*,
							void*,
							Pack...
						>(
							shell,
							first,
							second,
							third,
							fourth,
							shell_param,
							nullptr,
							pack...
						);
					}
				};

				template <std::size_t Argc>
				struct argument_remapper<Argc, std::enable_if_t<Argc <= 4>> {

					template<
						typename Ret,
						typename First = void*,
						typename Second = void*,
						typename Third = void*,
						typename Fourth = void*
					>
					static auto do_call(
						const void* shell,
						void* shell_param,
						First first = First{},
						Second second = Second{},
						Third third = Third{},
						Fourth fourth = Fourth{}
					) -> Ret {
						return shellcode_stub_helper<
							Ret,
							First,
							Second,
							Third,
							Fourth,
							void*,
							void*
						>(
							shell,
							first,
							second,
							third,
							fourth,
							shell_param,
							nullptr
						);
					}
				};

				template <typename result, typename... arguments>
				static inline auto spoof_call_with_stub(
					const void* stub,
					const void* trampoline,
					result(*fn)(arguments...),
					arguments... args
				) -> result {
					shell_params p = { trampoline, reinterpret_cast<void*>(fn) };
					using mapper = argument_remapper<sizeof...(arguments), void>;
					return mapper::template do_call<result, arguments...>(stub, &p, args...);
				}

				inline bool probe_backend(Backend backend, void* jmp_rdx, void* (*domain_get)()) {
					selected_backend() = backend;
					void* domain = nullptr;
					__try {
						domain = spoof_call_with_stub<void*>(active_stub(), jmp_rdx, domain_get);
					}
					__except (EXCEPTION_EXECUTE_HANDLER) {
						return false;
					}
					return domain != nullptr;
				}
			}

			inline bool select_backend(void* jmp_rdx, void* (*domain_get)(), const char** backend_name_out = nullptr) {
				auto& ctx = get_context();

				if (renamed_exports::PreferDirectCalls()) {
					ctx.use_direct_calls = true;
					void* domain = nullptr;
					__try {
						domain = domain_get();
					}
					__except (EXCEPTION_EXECUTE_HANDLER) {
						domain = nullptr;
					}
					if (!domain) {
						if (backend_name_out) *backend_name_out = "failed";
						return false;
					}
					if (backend_name_out) *backend_name_out = "direct";
					return true;
				}

				ctx.use_direct_calls = false;

				const char* name = "legacy";
				if (detail::probe_backend(detail::Backend::Legacy, jmp_rdx, domain_get)) {
					detail::selected_backend() = detail::Backend::Legacy;
				}
				else if (detail::probe_backend(detail::Backend::V2, jmp_rdx, domain_get)) {
					detail::selected_backend() = detail::Backend::V2;
					name = "v2";
				}
				else {
					void* domain = nullptr;
					__try {
						domain = domain_get();
					}
					__except (EXCEPTION_EXECUTE_HANDLER) {
						domain = nullptr;
					}

					if (domain) {
						ctx.use_direct_calls = true;
						name = "direct";
					}
					else {
						if (backend_name_out) {
							*backend_name_out = "failed";
						}
						return false;
					}
				}

				if (backend_name_out) {
					*backend_name_out = name;
				}
				return true;
			}

			template <typename result, typename... arguments>
			static inline auto spoof_call(
				const void* trampoline,
				result(*fn)(arguments...),
				arguments... args
			) -> result {
				if (get_context().use_direct_calls) {
					return fn(args...);
				}
				return detail::spoof_call_with_stub(detail::active_stub(), trampoline, fn, args...);
			}
		};
	}
}

inline void rrid::set_module_name(const std::string& name) {
	auto& ctx = detail::get_context();
	if (ctx.initialized) {
		return;
	}
	detail::module_name_storage() = name.empty() ? "GameAssembly.dll" : name;
	detail::invalidate_module_cache();
	ctx.module_name = detail::module_name_storage();
	ctx.module_base = 0;
}

inline const std::string& rrid::get_module_name() {
	return detail::module_name_storage();
}

inline const char* rrid::get_spoofer_backend() {
	return detail::get_context().spoofer_backend_name;
}

inline const char* rrid::get_init_error() {
	const auto& err = detail::get_context().init_error;
	return err.empty() ? "unknown" : err.c_str();
}

inline const char* rrid::get_api_scan_path() {
	return export_scan::last_path_label();
}

inline bool rrid::auto_detect_module() {
	std::string detected;
	if (!module_discovery::auto_detect(&detected)) {
		return false;
	}
	set_module_name(detected);
	return true;
}

inline bool rrid::init() {
	auto& ctx = detail::get_context();
	if (ctx.initialized) {
		return true;
	}

	ctx.init_error.clear();

	if (!GetModuleHandleA(detail::module_name_cstr())) {
		if (!auto_detect_module()) {
			ctx.init_error = std::string(detail::module_name_cstr()) + " is not loaded";
			return false;
		}
	}

	std::string api_error;
	if (!detail::il2cpp::find_api_functions(&api_error)) {
		ctx.init_error = api_error.empty() ? "failed to resolve IL2CPP APIs" : api_error;
		if (!GetModuleHandleA("UnityPlayer.dll")) {
			ctx.init_error += " (UnityPlayer.dll not loaded)";
		}
		return false;
	}

	ctx.jmp_rdx = detail::find_jmp_gadget();
	if (!ctx.jmp_rdx) {
		ctx.use_direct_calls = true;
		ctx.spoofer_backend_name = "direct";
	}
	else if (!detail::return_spoofer::select_backend(ctx.jmp_rdx, ctx.api.domain_get, &ctx.spoofer_backend_name)) {
		ctx.init_error = "return spoofer probe failed (domain_get returned null)";
		return false;
	}

	void* domain = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.domain_get);
	if (!domain) {
		ctx.init_error = "il2cpp_domain_get returned null";
		return false;
	}

	detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.thread_attach, domain);

	size_t assemblies_count = 0;
	const void** assemblies = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_assemblies, (const void*)domain, &assemblies_count);
	if (!assemblies || assemblies_count == 0) {
		ctx.init_error = "no IL2CPP assemblies loaded yet";
		return false;
	}

	ctx.images.reserve(assemblies_count);
	ctx.image_lookup.reserve(assemblies_count);

	for (size_t i = 0; i < assemblies_count; i++) {
		auto assembly = assemblies[i];
		if (!assembly) {
			continue;
		}

		const void* image = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_image, assembly);
		if (!image) {
			continue;
		}

		const char* name = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_image_name, image);
		if (!name || !*name) {
			continue;
		}

		auto img = std::make_unique<RridImage>(name, image);
		ctx.image_lookup[name] = img.get();
		ctx.images.push_back(std::move(img));
	}

	ctx.initialized = true;
	return true;
}

inline const std::vector<RridImage*>& rrid::get_images() {
	auto& ctx = detail::get_context();
	static std::vector<RridImage*> result;

	if (!ctx.initialized) {
		return result;
	}

	if (result.empty()) {
		result.reserve(ctx.images.size());

		for (auto& img : ctx.images)
			result.push_back(img.get());
	}

	return result;
}

inline RridImage* rrid::get_image(const std::string& name) {
	auto& ctx = detail::get_context();
	if (!ctx.initialized) {
		return nullptr;
	}

	auto it = ctx.image_lookup.find(name);
	return (it != ctx.image_lookup.end()) ? it->second : nullptr;
}

inline RridClass* RridImage::get_class(const std::string& name, const std::string& namespaze) {
	uint64_t key = rrid::detail::fnv1a::hash_class(namespaze.c_str(), name.c_str());
	auto it = class_lookup_.find(key);
	if (it != class_lookup_.end()) {
		return it->second;
	}

	auto& ctx = rrid::detail::get_context();
	void* klass = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_by_name, image_, namespaze.c_str(), name.c_str());
	if (!klass) {
		return nullptr;
	}

	auto cls = std::make_unique<RridClass>(name, namespaze, klass);
	RridClass* ptr = cls.get();

	class_lookup_[key] = ptr;
	classes_.push_back(std::move(cls));
	classes_pointers_.push_back(ptr);

	return ptr;
}

inline const std::vector<RridClass*>& RridImage::get_classes() {
	if (!classes_initialized_) {
		auto& ctx = rrid::detail::get_context();
		size_t class_count = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_count, image_);
		classes_.reserve(class_count);
		class_lookup_.reserve(class_count);

		for (size_t i = 0; i < class_count; ++i) {
			const void* klass = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class, image_, i);
			if (!klass) {
				continue;
			}

			const char* name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_name, (void*)klass);
			if (!name || !*name) {
				continue;
			}

			const char* namespaze = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_namespace, (void*)klass);
			if (!namespaze) {
				continue;
			}

			uint64_t key = rrid::detail::fnv1a::hash_class(namespaze, name);
			if (class_lookup_.find(key) != class_lookup_.end()) {
				continue;
			}

			auto cls = std::make_unique<RridClass>(name, namespaze, klass);
			RridClass* ptr = cls.get();

			class_lookup_[key] = ptr;
			classes_.push_back(std::move(cls));
		}

		classes_pointers_.clear();
		classes_pointers_.reserve(classes_.size());
		for (auto& cls : classes_)
			classes_pointers_.push_back(cls.get());

		classes_initialized_ = true;
	}

	return classes_pointers_;
}

inline bool RridClass::is_enum() {
	auto& ctx = rrid::detail::get_context();
	return rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.is_class_enum, class_);
}

inline bool RridClass::is_generic() {
	auto& ctx = rrid::detail::get_context();
	return rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.is_class_generic, class_);
}

inline bool RridClass::is_valuetype() {
	auto& ctx = rrid::detail::get_context();
	return rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.is_class_valuetype, class_);
}

inline std::string RridClass::get_parent_full_name() {
	if (!parent_initialized_) {
		parent_initialized_ = true;
		auto& ctx = rrid::detail::get_context();
		if (!ctx.api.get_class_parent) {
			return parent_name_;
		}
		void* parent = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_parent, (void*)class_);
		if (parent) {
			const char* pname = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_name, parent);
			const char* pns = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_namespace, parent);
			if (pname && pns) {
				parent_name_ = (*pns && *pns) ? (std::string(pns) + "." + pname) : pname;
			}
		}
	}
	return parent_name_;
}

inline const std::vector<std::string>& RridClass::get_interface_full_names() {
	if (!interfaces_initialized_) {
		interfaces_initialized_ = true;
		auto& ctx = rrid::detail::get_context();
		if (!ctx.api.get_class_interfaces) {
			return interface_names_;
		}
		void* iter = nullptr;
		while (void* iface = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_interfaces, (void*)class_, &iter)) {
			const char* iname = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_name, iface);
			const char* ins = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_namespace, iface);
			if (!iname || !ins) {
				continue;
			}
			interface_names_.push_back((*ins && *ins) ? (std::string(ins) + "." + iname) : iname);
		}
	}
	return interface_names_;
}

inline std::string RridClass::get_type_name(const void* type) {
	auto& ctx = rrid::detail::get_context();

	char* name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_type_name, type);
	if (!name || !*name) {
		return "Unknown";
	}

	std::string result = name;
	rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.free_memory, (void*)name);
	return result;
}

inline RridField* RridClass::get_field(const std::string& name) {
	uint64_t key = rrid::detail::fnv1a::hash_class(namespace_.c_str(), name_.c_str()) ^ rrid::detail::fnv1a::hash_class("", name.c_str());
	auto it = fields_lookup_.find(key);
	if (it != fields_lookup_.end()) {
		return it->second;
	}

	auto& ctx = rrid::detail::get_context();
	void* field = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_by_name, (void*)class_, name.c_str());
	if (!field) {
		return nullptr;
	}

	uint32_t flags = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_flags, field);
	if (!flags) {
		return nullptr;
	}

	const void* type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_type, field);
	if (!type) {
		return nullptr;
	}

	uint64_t offset = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_offset, field);
	if (offset == (uint64_t)-1) {
		offset = 0;
	}

	auto fld = std::make_unique<RridField>(name, get_type_name(type), offset, flags, field);
	RridField* ptr = fld.get();

	fields_lookup_[key] = ptr;
	fields_.push_back(std::move(fld));

	return ptr;
}

inline const std::vector<RridField*>& RridClass::get_fields() {
	if (!fields_initialized_) {
		auto& ctx = rrid::detail::get_context();
		void* iter = nullptr;
		while (void* field = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_fields, (void*)class_, &iter)) {
			const char* name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_name, field);
			if (!name || !*name) {
				continue;
			}

			uint64_t key = rrid::detail::fnv1a::hash_class(namespace_.c_str(), name_.c_str()) ^ rrid::detail::fnv1a::hash_class("", name);
			if (fields_lookup_.find(key) != fields_lookup_.end()) {
				continue;
			}

			uint32_t flags = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_flags, field);
			if (!flags) {
				continue;
			}

			const void* type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_type, field);
			if (!type) {
				continue;
			}

			uint64_t offset = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_field_offset, field);
			if (offset == (uint64_t)-1) {
				offset = 0;
			}

			auto fld = std::make_unique<RridField>(name, get_type_name(type), offset, flags, field);
			RridField* ptr = fld.get();

			fields_lookup_[key] = ptr;
			fields_.push_back(std::move(fld));
		}

		fields_pointers_.clear();
		fields_pointers_.reserve(fields_.size());
		for (auto& fld : fields_) {
			fields_pointers_.push_back(fld.get());
		}

		fields_initialized_ = true;
	}

	return fields_pointers_;
}

inline RridMethod* RridClass::get_method(const std::string& name, int args_count) {
	auto& ctx = rrid::detail::get_context();
	const void* method = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_by_name, (void*)class_, name.c_str(), args_count);
	if (!method) {
		return nullptr;
	}

	auto it = std::find_if(methods_pointers_.begin(), methods_pointers_.end(), [&](RridMethod* m) {
		return m->get_raw() == method;
		});

	if (it != methods_pointers_.end()) {
		return *it;
	}

	uint32_t iflags = 0;
	uint32_t flags = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_flags, method, &iflags);
	if (!flags) {
		return nullptr;
	}

	const void* return_type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_return_type, method);
	if (!return_type) {
		return nullptr;
	}

	auto addr = *(void**)((uint64_t)method + ctx.method_pointer_offset);

	std::vector<std::pair<std::string, std::string>> params;
	params.reserve(args_count);

	for (uint32_t i = 0; i < (uint32_t)args_count; i++) {
		const void* param_type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_param_type, method, i);
		const char* param_name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_param_name, method, i);

		if (!param_type || !param_name) {
			continue;
		}

		params.emplace_back(get_type_name(param_type), param_name);
	}

	auto mtd = std::make_unique<RridMethod>(name, get_type_name(return_type), params, flags, addr, method);
	RridMethod* ptr = mtd.get();

	methods_pointers_.push_back(ptr);
	methods_.push_back(std::move(mtd));

	return ptr;
}

inline const std::vector<RridMethod*>& RridClass::get_methods() {
	if (!methods_initialized_) {
		auto& ctx = rrid::detail::get_context();
		void* iter = nullptr;
		while (const void* method = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_methods, (void*)class_, &iter)) {
			auto it = std::find_if(methods_pointers_.begin(), methods_pointers_.end(), [&](RridMethod* m) {
				return m->get_raw() == method;
				});

			if (it != methods_pointers_.end()) {
				continue;
			}

			const char* name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_name, method);
			if (!name || !*name) {
				continue;
			}

			uint32_t iflags = 0;
			uint32_t flags = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_flags, method, &iflags);
			if (!flags) {
				continue;
			}

			const void* return_type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_return_type, method);
			if (!return_type) {
				continue;
			}

			auto addr = *(void**)((uint64_t)method + ctx.method_pointer_offset);

			uint32_t param_count = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_param_count, method);
			std::vector<std::pair<std::string, std::string>> params;
			params.reserve(param_count);

			for (uint32_t i = 0; i < param_count; i++) {
				const void* param_type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_param_type, method, i);
				const char* param_name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_method_param_name, method, i);

				if (!param_type || !param_name) {
					continue;
				}

				params.emplace_back(get_type_name(param_type), param_name);
			}


			auto mtd = std::make_unique<RridMethod>(name, get_type_name(return_type), params, flags, addr, method);
			RridMethod* ptr = mtd.get();
			methods_.push_back(std::move(mtd));
		}

		methods_pointers_.clear();
		methods_pointers_.reserve(methods_.size());
		for (auto& mtd : methods_) {
			methods_pointers_.push_back(mtd.get());
		}

		methods_initialized_ = true;
	}

	return methods_pointers_;
}

inline RridClass* RridClass::get_nested_type(const std::string& name) {
	for (RridClass* cls : get_nested_types()) {
		if (cls->get_name() == name) {
			return cls;
		}
	}

	return nullptr;
}

inline const std::vector<RridClass*>& RridClass::get_nested_types() {
	if (!nested_types_initialized_) {
		auto& ctx = rrid::detail::get_context();
		void* iter = nullptr;
		while (void* nested_type = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_nested_types, (void*)class_, &iter)) {
			const char* name = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_name, nested_type);
			if (!name || !*name) {
				continue;
			}

			const char* namespaze = rrid::detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_class_namespace, nested_type);
			if (!namespaze) {
				continue;
			}

			auto cls = std::make_unique<RridClass>(name, namespaze, nested_type);
			RridClass* ptr = cls.get();
			nested_types_.push_back(std::move(cls));
			nested_types_pointers_.push_back(ptr);
		}

		nested_types_initialized_ = true;
	}

	return nested_types_pointers_;
}

#endif