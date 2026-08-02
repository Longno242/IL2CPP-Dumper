#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct DumpParam {
    std::string type;
    std::string name;
};

struct DumpField {
    std::string name;
    std::string type;
    std::string signature;
    std::string symbol;
    bool is_static = false;
    bool is_enum_value = false;
    uint64_t rva = 0;
    uint64_t offset = 0;
    int64_t enum_value = 0;
    uint32_t flags = 0;
    std::vector<std::string> attributes;
    std::string string_value; // static System.String value when readable
};

struct DumpMethod {
    std::string name;
    std::string return_type;
    std::string signature;
    std::string symbol;
    std::vector<DumpParam> params;
    uint64_t rva = 0;
    uint64_t method_info_rva = 0;
    uint64_t invoker_rva = 0;
    uint32_t flags = 0;
    uint32_t token = 0;
    int32_t vtable_slot = -1;
    bool is_generic = false;
    bool is_inflated = false;
    bool is_getter = false;
    bool is_setter = false;
    std::string property_name;
    std::vector<std::string> attributes;
};

struct DumpProperty {
    std::string name;
    std::string getter_symbol;
    std::string setter_symbol;
};

struct DumpClass {
    std::string name;
    std::string namespaze;
    std::string full_name;
    std::string type_name; // may include generic args
    std::string kind; // class / struct / enum
    std::string parent;
    std::vector<std::string> interfaces;
    std::vector<std::string> attributes;
    uint64_t class_rva = 0;
    bool is_generic = false;
    bool is_inflated = false;
    std::string ident;
    std::string path;
    std::vector<DumpField> fields;
    std::vector<DumpMethod> methods;
    std::vector<DumpProperty> properties;
    std::vector<DumpClass> nested;
};

struct DumpImage {
    std::string name;
    std::string ident;
    uint64_t image_rva = 0;
    std::vector<DumpClass> classes;
};

struct DumpIndexEntry {
    std::string kind;
    std::string full_name;
    std::string symbol;
    std::string image;
    std::string signature;
    uint64_t rva = 0;
    uint64_t offset = 0;
    uint64_t method_info_rva = 0;
    uint64_t invoker_rva = 0;
    uint32_t token = 0;
};

struct DumpStringEntry {
    std::string value;
    uint64_t rva = 0;
    std::string source;
    std::string owner;
};

struct DumpFingerprint {
    std::string module_name;
    uint32_t size_of_image = 0;
    uint32_t time_date_stamp = 0;
    std::string file_version;
    std::string product_version;
    std::string unity_hint;
    size_t image_count = 0;
    size_t filtered_image_count = 0;
    size_t class_count = 0;
    size_t method_count = 0;
};

struct DumpRoot {
    std::string timestamp;
    std::string module;
    std::string spoofer;
    DumpFingerprint fingerprint;
    std::vector<DumpImage> images;
    std::vector<DumpIndexEntry> index;
    std::vector<DumpStringEntry> strings;
};
