#pragma once

// Fallback resolver for GameAssembly builds that rename il2cpp_* exports.
// Activated only after the normal GetProcAddress / UnityPlayer path fails.

namespace renamed_exports {

bool DetectAndEnable(const char* module_name);
bool Active();
void Reset();

void* Resolve(const char* module_name, const char* api_name);
bool PreferDirectCalls();

struct ApiSlots {
	void** domain_get = nullptr;
	void** thread_attach = nullptr;
	void** get_assemblies = nullptr;
	void** get_image = nullptr;
	void** get_image_name = nullptr;
	void** get_class_count = nullptr;
	void** get_class = nullptr;
	void** get_class_name = nullptr;
	void** get_class_namespace = nullptr;
	void** get_class_by_name = nullptr;
	void** get_class_parent = nullptr;
	void** get_fields = nullptr;
	void** get_field_by_name = nullptr;
	void** get_field_name = nullptr;
	void** get_field_flags = nullptr;
	void** get_field_offset = nullptr;
	void** get_field_type = nullptr;
	void** get_methods = nullptr;
	void** get_method_by_name = nullptr;
	void** get_method_name = nullptr;
	void** get_method_return_type = nullptr;
	void** get_method_param_count = nullptr;
	void** get_method_param_name = nullptr;
	void** get_method_param_type = nullptr;
	void** get_method_flags = nullptr;
	void** get_type_name = nullptr;
	void** free_memory = nullptr;
	void** is_class_enum = nullptr;
	void** is_class_valuetype = nullptr;
	void** is_class_generic = nullptr;
	void** is_method_instance = nullptr;
	void** get_class_nested_types = nullptr;
	void** get_class_interfaces = nullptr;
};

void InstallFallbacks(ApiSlots& slots);

} // namespace renamed_exports
