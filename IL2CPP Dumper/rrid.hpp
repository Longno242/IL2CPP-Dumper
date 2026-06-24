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
			} api;
		};

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

		namespace il2cpp {
			namespace detail {

				inline void* find_api_function(const std::string& name) {
					static std::unordered_map<std::string, void*> functions;
					static bool already_scanned = false;
					if (already_scanned) {
						auto it = functions.find(name);
						return (it != functions.end()) ? it->second : nullptr;
					}

					void* start = pattern::find_pattern("UnityPlayer.dll", "\x48\x89\x5C\x24\x00\x48\x8D\x15\x00\x00\x00\x00\xB3", "xxxx?xxx????x");
					void* end = pattern::find_pattern("UnityPlayer.dll", "\xE8\x00\x00\x00\x00\x48\x8B\x5C\x24\x00\x32\xC0\x48\xC7\x05", "x????xxxx?xxxxx");
					if (!start || !end) {
						return nullptr;
					}

					void* first_time_addr = nullptr;
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
						functions[func_name] = func_addr;

						if (name == func_name) {
							first_time_addr = func_addr;
						}

						curr = mov + 1;
					}

					already_scanned = true;
					return first_time_addr;
				}
			}

			inline bool find_api_functions() {
				auto& ctx = get_context();


				ctx.api.domain_get = (decltype(ctx.api.domain_get))pattern::find_pattern(::rrid::detail::module_name_cstr(), "\x48\x83\xEC\x00\x48\x63\x05\x00\x00\x00\x00\x48\x8D\x0D\x00\x00\x00\x00\x4C\x8B\x05\x00\x00\x00\x00\x48\x8D\x15\x00\x00\x00\x00\x8B\x0C\x08\x48\x8B\x44\x24\x00\x48\xC1\xE1\x00\x48\x03\xCA\x48\x3B\xC1\x73\x00\x48\x8B\x44\x24\x00\x48\x3B\xC2\x73\x00\x49\x63\x40\x00\x42\x8B\x4C\x00\x00\x48\x8B\x44\x24\x00\x49\x8D\x14\x88\x48\x3B\xC2\x0F\x83", "xxx?xxx????xxx????xxx????xxx????xxxxxxx?xxx?xxxxxxx?xxxx?xxxx?xxx?xxxx?xxxx?xxxxxxxxx");
				ctx.api.thread_attach = (decltype(ctx.api.thread_attach))pattern::find_pattern(::rrid::detail::module_name_cstr(), "\x40\x56\x48\x83\xEC\x00\x48\x8B\xF1\x48\x8B\x0D\x00\x00\x00\x00\x8B\x09", "xxxxx?xxxxxx????xx");
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

				return ctx.api.domain_get && ctx.api.thread_attach && ctx.api.get_assemblies && ctx.api.get_image && ctx.api.get_image_name && ctx.api.get_class_count &&
					ctx.api.get_class_by_name && ctx.api.get_class && ctx.api.get_class_name && ctx.api.get_class_namespace && ctx.api.is_class_enum && ctx.api.is_class_valuetype &&
					ctx.api.get_fields && ctx.api.get_field_by_name && ctx.api.get_field_name && ctx.api.get_field_flags && ctx.api.get_field_offset && ctx.api.get_field_type &&
					ctx.api.get_type_name && ctx.api.get_method_by_name && ctx.api.get_methods && ctx.api.get_method_name && ctx.api.get_method_return_type &&
					ctx.api.get_method_param_count && ctx.api.get_method_param_name && ctx.api.is_class_generic && ctx.api.get_method_param_type && ctx.api.free_memory &&
					ctx.api.get_method_flags && ctx.api.is_method_instance && ctx.api.get_class_nested_types && ctx.method_pointer_offset;
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

		// https://github.com/lennyRBLX/ret_addr_spoofer
		namespace return_spoofer {
			namespace detail {
#pragma section(".text")
				__declspec(allocate(".text")) __declspec(selectany) __declspec(align(16))
					uint8_t spoofer_stub_asm[] = {
						0x41, 0x5B,								  // pop r11
						0x48, 0x83, 0xC4, 0x08,                   // add rsp, 8
						0x48, 0x8B, 0x44, 0x24, 0x18,             // mov rax, [rsp+0x18]
						0x4C, 0x8B, 0x10,                         // mov r10, [rax]
						0x4C, 0x89, 0x14, 0x24,                   // mov [rsp], r10
						0x4C, 0x8B, 0x50, 0x08,                   // mov r10, [rax+0x8]
						0x4C, 0x89, 0x58, 0x08,                   // mov [rax+0x8], r11
						0x48, 0x89, 0x78, 0x10,                   // mov [rax+0x10], rdi
						0x48, 0x8D, 0x3D, 0x09, 0x00, 0x00, 0x00, // lea rdi, fixup
						0x48, 0x89, 0x38,						  // mov [rax], rdi
						0x48, 0x8B, 0xF8,                         // mov rdi, rax
						0x41, 0xFF, 0xE2,                         // jmp r10
						// fixup:						    
						0x48, 0x83, 0xEC, 0x10,                   // sub rsp, 0x10
						0x48, 0x8B, 0xCF,						  // mov rcx, rdi
						0x48, 0x8B, 0x79, 0x10,                   // mov rdi, [rcx+0x10]
						0xFF, 0x61, 0x08						  // jmp qword ptr [rcx+0x8]
				};

				inline auto _spoofer_stub() {
					return (void(*)()) & spoofer_stub_asm;
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
			}

			template <typename result, typename... arguments>
			static inline auto spoof_call(
				const void* trampoline,
				result(*fn)(arguments...),
				arguments... args
			) -> result {
				struct shell_params {
					const void* trampoline;
					void* function;
					void* register_;
				};

				shell_params p = { trampoline, reinterpret_cast<void*>(fn) };
				using mapper = detail::argument_remapper<sizeof...(arguments), void>;
				return mapper::template do_call<result, arguments...>((const void*)detail::_spoofer_stub(), &p, args...);
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

inline bool rrid::init() {
	auto& ctx = detail::get_context();
	if (ctx.initialized) {
		return true;
	}

	if (!detail::il2cpp::find_api_functions()) {
		return false;
	}

	ctx.jmp_rdx = detail::pattern::find_pattern(::rrid::detail::module_name_cstr(), "\xFF\x27", "xx");
	if (!ctx.jmp_rdx) {
		return false;
	}

	void* domain = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.domain_get);
	detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.thread_attach, domain);

	size_t assemblies_count = 0;
	const void** assemblies = detail::return_spoofer::spoof_call(ctx.jmp_rdx, ctx.api.get_assemblies, (const void*)domain, &assemblies_count);

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