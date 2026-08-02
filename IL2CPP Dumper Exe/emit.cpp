#define _CRT_SECURE_NO_WARNINGS
#include "dumper.h"
#include "dump_model.h"
#include "dump_flags.h"
#include "static/static_collector.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::string CurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return os.str();
}

bool IsKeyword(const std::string& s) {
    static const std::unordered_set<std::string> kws = {
        "alignas","alignof","and","and_eq","asm","auto","bitand","bitor","bool","break","case",
        "catch","char","class","compl","concept","const","consteval","constexpr","constinit",
        "continue","co_await","co_return","co_yield","decltype","default","delete","do","double",
        "dynamic_cast","else","enum","explicit","export","extern","false","float","for","friend",
        "goto","if","inline","int","long","mutable","namespace","new","noexcept","not","not_eq",
        "nullptr","operator","or","or_eq","private","protected","public","register","reinterpret_cast",
        "requires","return","short","signed","sizeof","static","static_assert","static_cast","struct",
        "switch","template","this","thread_local","throw","true","try","typedef","typeid","typename",
        "union","unsigned","using","virtual","void","volatile","wchar_t","while","xor","xor_eq",
        "event","params","base","object","string","decimal","checked","unchecked","lock","nameof",
        "record","var","when","with","yield","async","await","fn","let","mut","mod","crate","self",
        "super","type","where","use","pub","ref","match","loop","move","dyn","impl","trait","as",
        "in","is","from","import","pass","None","True","False","lambda","nonlocal","global","def"
    };
    return kws.count(s) > 0;
}

std::string SanitizeIdent(const std::string& in) {
    if (in.empty()) return "_";
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')
            out += c;
        else
            out += '_';
    }
    if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    if (IsKeyword(out)) out += '_';
    return out;
}

std::string ToSnakeCase(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (c >= 'A' && c <= 'Z') {
            if (!out.empty()) out += '_';
            out += static_cast<char>(c - 'A' + 'a');
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
        } else if (c == '_') {
            if (!out.empty() && out.back() != '_') out += '_';
        }
    }
    if (out.empty()) return "_";
    if (out[0] >= '0' && out[0] <= '9') out.insert(out.begin(), '_');
    if (IsKeyword(out)) out += '_';
    return out;
}

std::string Hex(uint64_t v) {
    std::ostringstream os;
    os << "0x" << std::uppercase << std::hex << v;
    return os.str();
}

std::string HexJson(uint64_t v) {
    std::ostringstream os;
    os << "\"0x" << std::uppercase << std::hex << v << "\"";
    return os.str();
}

std::string Indent(int level) {
    return std::string(static_cast<size_t>(level) * 4, ' ');
}

std::string JsonEscape(const std::string& s) {
    std::ostringstream os;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b"; break;
            case '\f': os << "\\f"; break;
            case '\n': os << "\\n"; break;
            case '\r': os << "\\r"; break;
            case '\t': os << "\\t"; break;
            default:
                if (c < 0x20) {
                    os << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c)
                       << std::dec << std::setfill(' ');
                } else {
                    os << static_cast<char>(c);
                }
                break;
        }
    }
    return os.str();
}

std::string PrettyType(const std::string& t) {
    if (t == "System.Void") return "void";
    if (t == "System.Boolean") return "bool";
    if (t == "System.Byte") return "byte";
    if (t == "System.SByte") return "sbyte";
    if (t == "System.Char") return "char";
    if (t == "System.Int16") return "short";
    if (t == "System.UInt16") return "ushort";
    if (t == "System.Int32") return "int";
    if (t == "System.UInt32") return "uint";
    if (t == "System.Int64") return "long";
    if (t == "System.UInt64") return "ulong";
    if (t == "System.Single") return "float";
    if (t == "System.Double") return "double";
    if (t == "System.Decimal") return "decimal";
    if (t == "System.String") return "string";
    if (t == "System.Object") return "object";
    return t;
}

std::string CppType(const std::string& t) {
    if (t == "System.Void") return "void";
    if (t == "System.Boolean") return "bool";
    if (t == "System.Byte") return "uint8_t";
    if (t == "System.SByte") return "int8_t";
    if (t == "System.Char") return "char16_t";
    if (t == "System.Int16") return "int16_t";
    if (t == "System.UInt16") return "uint16_t";
    if (t == "System.Int32") return "int32_t";
    if (t == "System.UInt32") return "uint32_t";
    if (t == "System.Int64") return "int64_t";
    if (t == "System.UInt64") return "uint64_t";
    if (t == "System.Single") return "float";
    if (t == "System.Double") return "double";
    if (t == "System.IntPtr" || t == "System.UIntPtr") return "void*";
    return "void*";
}

std::string UniqueName(std::unordered_set<std::string>& used, std::string base) {
    if (base.empty()) base = "_";
    std::string candidate = base;
    int n = 2;
    while (used.count(candidate)) {
        candidate = base + "_" + std::to_string(n++);
    }
    used.insert(candidate);
    return candidate;
}

std::string ClassKindLine(const DumpClass& cls) {
    std::ostringstream os;
    os << cls.kind << " " << cls.full_name << "  (ClassRVA " << Hex(cls.class_rva) << ")";
    if (!cls.parent.empty()) os << "  extends " << cls.parent;
    if (!cls.interfaces.empty()) {
        os << "  implements ";
        for (size_t i = 0; i < cls.interfaces.size(); ++i) {
            if (i) os << ", ";
            os << cls.interfaces[i];
        }
    }
    if (cls.is_generic) os << "  [generic]";
    if (cls.is_inflated) os << "  [inflated]";
    if (!cls.attributes.empty()) {
        os << "  attrs=";
        for (size_t i = 0; i < cls.attributes.size(); ++i) {
            if (i) os << ",";
            os << cls.attributes[i];
        }
    }
    return os.str();
}

// ---- emitters ----

void WriteClassCpp(std::ostream& out, const DumpClass& cls, int indent) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);

    out << pad << "// " << ClassKindLine(cls) << "\n";
    if (!cls.properties.empty()) {
        out << pad << "// properties:";
        for (size_t i = 0; i < cls.properties.size(); ++i) {
            if (i) out << ",";
            out << " " << cls.properties[i].name;
        }
        out << "\n";
    }
    out << pad << "namespace " << cls.ident << " {\n";
    out << inner << "constexpr uint64_t ClassRVA = " << Hex(cls.class_rva) << ";\n";

    if (!cls.fields.empty()) {
        out << "\n" << inner << (cls.kind == "enum" ? "// enum values\n" : "// fields\n");
        for (const auto& f : cls.fields) {
            if (f.is_enum_value) {
                out << inner << "constexpr int64_t " << f.symbol << " = " << f.enum_value
                    << "; // " << f.signature << "\n";
            } else {
                const uint64_t value = f.is_static ? f.rva : f.offset;
                out << inner << "constexpr uint64_t " << f.symbol << " = " << Hex(value)
                    << "; // " << f.signature << "\n";
            }
        }
    }

    if (!cls.methods.empty()) {
        out << "\n" << inner << "// methods\n";
        for (const auto& m : cls.methods) {
            out << inner << "constexpr uint64_t " << m.symbol << " = " << Hex(m.rva)
                << "; // " << m.signature;
            if (m.token) out << "  token=" << Hex(m.token);
            if (m.is_getter || m.is_setter) out << "  [" << (m.is_getter ? "get" : "set") << " " << m.property_name << "]";
            out << "\n";
            if (m.method_info_rva) {
                out << inner << "constexpr uint64_t " << m.symbol << "_MethodInfo = "
                    << Hex(m.method_info_rva) << "; // MethodInfo for " << m.name << "\n";
            }
            if (m.invoker_rva) {
                out << inner << "constexpr uint64_t " << m.symbol << "_Invoker = "
                    << Hex(m.invoker_rva) << "; // invoker for " << m.name << "\n";
            }
        }
    }

    if (!cls.nested.empty()) {
        out << "\n" << inner << "// nested types\n";
        for (const auto& nested : cls.nested) {
            out << "\n";
            WriteClassCpp(out, nested, indent + 1);
        }
    }
    out << pad << "}\n\n";
}

void WriteImageCpp(std::ostream& out, const DumpImage& img, int indent) {
    const std::string pad = Indent(indent);
    out << pad << "// ==== image: " << img.name << "   ImageRVA " << Hex(img.image_rva) << "\n";
    out << pad << "namespace " << img.ident << " {\n\n";
    out << pad << "    constexpr uint64_t ImageRVA = " << Hex(img.image_rva) << ";\n\n";
    for (const auto& cls : img.classes) WriteClassCpp(out, cls, indent + 1);
    out << pad << "} // " << img.ident << "\n\n";
}

void WriteClassCSharp(std::ostream& out, const DumpClass& cls, int indent) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);
    out << pad << "// " << ClassKindLine(cls) << "\n";
    out << pad << "public static class " << cls.ident << "\n" << pad << "{\n";
    out << inner << "public const ulong ClassRVA = " << Hex(cls.class_rva) << ";\n";

    if (!cls.fields.empty()) {
        out << "\n" << inner << (cls.kind == "enum" ? "// enum values\n" : "// fields\n");
        for (const auto& f : cls.fields) {
            if (f.is_enum_value) {
                out << inner << "public const long " << f.symbol << " = " << f.enum_value
                    << "; // " << f.signature << "\n";
            } else {
                out << inner << "public const ulong " << f.symbol << " = "
                    << Hex(f.is_static ? f.rva : f.offset) << "; // " << f.signature << "\n";
            }
        }
    }
    if (!cls.methods.empty()) {
        out << "\n" << inner << "// methods\n";
        for (const auto& m : cls.methods) {
            out << inner << "public const ulong " << m.symbol << " = " << Hex(m.rva)
                << "; // " << m.signature << "\n";
            if (m.method_info_rva) {
                out << inner << "public const ulong " << m.symbol << "_MethodInfo = "
                    << Hex(m.method_info_rva) << ";\n";
            }
        }
    }
    if (!cls.nested.empty()) {
        out << "\n" << inner << "// nested types\n";
        for (const auto& nested : cls.nested) {
            out << "\n";
            WriteClassCSharp(out, nested, indent + 1);
        }
    }
    out << pad << "}\n\n";
}

void WriteImageCSharp(std::ostream& out, const DumpImage& img, int indent) {
    const std::string pad = Indent(indent);
    out << pad << "// ==== image: " << img.name << "   ImageRVA " << Hex(img.image_rva) << "\n";
    out << pad << "public static class " << img.ident << "\n" << pad << "{\n";
    out << pad << "    public const ulong ImageRVA = " << Hex(img.image_rva) << ";\n\n";
    for (const auto& cls : img.classes) WriteClassCSharp(out, cls, indent + 1);
    out << pad << "}\n\n";
}

void WriteClassRust(std::ostream& out, const DumpClass& cls, int indent) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);
    const std::string ident = ToSnakeCase(cls.ident);
    out << pad << "// " << cls.kind << " " << cls.full_name << "  (ClassRVA " << Hex(cls.class_rva) << ")\n";
    out << pad << "pub mod " << ident << " {\n";
    out << inner << "pub const CLASS_RVA: u64 = " << Hex(cls.class_rva) << ";\n";

    if (!cls.fields.empty()) {
        out << "\n" << inner << (cls.kind == "enum" ? "// enum values\n" : "// fields\n");
        std::unordered_set<std::string> used{"CLASS_RVA"};
        for (const auto& f : cls.fields) {
            if (f.is_enum_value) {
                const auto name = UniqueName(used, ToSnakeCase(f.symbol));
                out << inner << "pub const " << name << ": i64 = " << f.enum_value
                    << "; // " << f.signature << "\n";
            } else {
                const auto name = UniqueName(used, ToSnakeCase(f.symbol));
                out << inner << "pub const " << name << ": u64 = "
                    << Hex(f.is_static ? f.rva : f.offset) << "; // " << f.signature << "\n";
            }
        }
    }
    if (!cls.methods.empty()) {
        out << "\n" << inner << "// methods\n";
        std::unordered_set<std::string> used{"CLASS_RVA"};
        for (const auto& m : cls.methods) {
            const auto name = UniqueName(used, ToSnakeCase(m.symbol));
            out << inner << "pub const " << name << ": u64 = " << Hex(m.rva)
                << "; // " << m.signature << "\n";
            if (m.method_info_rva) {
                const auto mi = UniqueName(used, name + "_method_info");
                out << inner << "pub const " << mi << ": u64 = " << Hex(m.method_info_rva) << ";\n";
            }
        }
    }
    if (!cls.nested.empty()) {
        out << "\n" << inner << "// nested types\n";
        for (const auto& nested : cls.nested) {
            out << "\n";
            WriteClassRust(out, nested, indent + 1);
        }
    }
    out << pad << "}\n\n";
}

void WriteImageRust(std::ostream& out, const DumpImage& img, int indent) {
    const std::string pad = Indent(indent);
    const std::string img_ident = ToSnakeCase(img.ident);
    out << pad << "// ==== image: " << img.name << "   ImageRVA " << Hex(img.image_rva) << "\n";
    out << pad << "pub mod " << img_ident << " {\n";
    out << pad << "    pub const IMAGE_RVA: u64 = " << Hex(img.image_rva) << ";\n\n";
    for (const auto& cls : img.classes) WriteClassRust(out, cls, indent + 1);
    out << pad << "}\n\n";
}

void WriteClassPython(std::ostream& out, const DumpClass& cls, int indent) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);
    out << pad << "# " << cls.kind << " " << cls.full_name << "  (ClassRVA " << Hex(cls.class_rva) << ")\n";
    out << pad << "class " << cls.ident << ":\n";
    out << inner << "CLASS_RVA = " << Hex(cls.class_rva) << "\n";

    bool wrote = true;
    if (!cls.fields.empty()) {
        out << "\n" << inner << (cls.kind == "enum" ? "# enum values\n" : "# fields\n");
        for (const auto& f : cls.fields) {
            if (f.is_enum_value) {
                out << inner << f.symbol << " = " << f.enum_value << "  # " << f.signature << "\n";
            } else {
                out << inner << f.symbol << " = " << Hex(f.is_static ? f.rva : f.offset)
                    << "  # " << f.signature << "\n";
            }
        }
        wrote = true;
    }
    if (!cls.methods.empty()) {
        out << "\n" << inner << "# methods\n";
        for (const auto& m : cls.methods) {
            out << inner << m.symbol << " = " << Hex(m.rva) << "  # " << m.signature << "\n";
            if (m.method_info_rva) {
                out << inner << m.symbol << "_MethodInfo = " << Hex(m.method_info_rva) << "\n";
            }
        }
        wrote = true;
    }
    if (!cls.nested.empty()) {
        out << "\n" << inner << "# nested types\n";
        for (const auto& nested : cls.nested) {
            out << "\n";
            WriteClassPython(out, nested, indent + 1);
        }
        wrote = true;
    }
    if (!wrote) out << inner << "pass\n";
    out << "\n";
}

void WriteImagePython(std::ostream& out, const DumpImage& img, int indent) {
    const std::string pad = Indent(indent);
    out << pad << "# ==== image: " << img.name << "   ImageRVA " << Hex(img.image_rva) << "\n";
    out << pad << "class " << img.ident << ":\n";
    out << pad << "    IMAGE_RVA = " << Hex(img.image_rva) << "\n\n";
    if (img.classes.empty()) {
        out << pad << "    pass\n\n";
        return;
    }
    for (const auto& cls : img.classes) WriteClassPython(out, cls, indent + 1);
}

void WriteClassJson(std::ostream& out, const DumpClass& cls, int indent, bool& first_child) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);
    if (!first_child) out << ",\n";
    first_child = false;

    out << pad << "{\n";
    out << inner << "\"name\": \"" << JsonEscape(cls.name) << "\",\n";
    out << inner << "\"namespace\": \"" << JsonEscape(cls.namespaze) << "\",\n";
    out << inner << "\"fullName\": \"" << JsonEscape(cls.full_name) << "\",\n";
    if (!cls.type_name.empty() && cls.type_name != cls.full_name) {
        out << inner << "\"typeName\": \"" << JsonEscape(cls.type_name) << "\",\n";
    }
    out << inner << "\"kind\": \"" << JsonEscape(cls.kind) << "\",\n";
    out << inner << "\"classRva\": " << HexJson(cls.class_rva) << ",\n";
    if (!cls.parent.empty()) out << inner << "\"parent\": \"" << JsonEscape(cls.parent) << "\",\n";
    if (!cls.interfaces.empty()) {
        out << inner << "\"interfaces\": [";
        for (size_t i = 0; i < cls.interfaces.size(); ++i) {
            if (i) out << ", ";
            out << "\"" << JsonEscape(cls.interfaces[i]) << "\"";
        }
        out << "],\n";
    }
    if (!cls.attributes.empty()) {
        out << inner << "\"attributes\": [";
        for (size_t i = 0; i < cls.attributes.size(); ++i) {
            if (i) out << ", ";
            out << "\"" << JsonEscape(cls.attributes[i]) << "\"";
        }
        out << "],\n";
    }
    if (cls.is_generic) out << inner << "\"isGeneric\": true,\n";
    if (cls.is_inflated) out << inner << "\"isInflated\": true,\n";
    if (!cls.properties.empty()) {
        out << inner << "\"properties\": [";
        for (size_t i = 0; i < cls.properties.size(); ++i) {
            if (i) out << ", ";
            out << "{\"name\": \"" << JsonEscape(cls.properties[i].name) << "\"";
            if (!cls.properties[i].getter_symbol.empty())
                out << ", \"getter\": \"" << JsonEscape(cls.properties[i].getter_symbol) << "\"";
            if (!cls.properties[i].setter_symbol.empty())
                out << ", \"setter\": \"" << JsonEscape(cls.properties[i].setter_symbol) << "\"";
            out << "}";
        }
        out << "],\n";
    }

    out << inner << "\"fields\": [";
    for (size_t i = 0; i < cls.fields.size(); ++i) {
        const auto& f = cls.fields[i];
        if (i) out << ',';
        out << "\n" << inner << "  {";
        out << "\"name\": \"" << JsonEscape(f.name) << "\", ";
        out << "\"type\": \"" << JsonEscape(f.type) << "\", ";
        out << "\"kind\": \"" << (f.is_enum_value ? "enumValue" : (f.is_static ? "static" : "instance")) << "\", ";
        if (f.is_enum_value) out << "\"value\": " << f.enum_value << ", ";
        else if (f.is_static) out << "\"rva\": " << HexJson(f.rva) << ", ";
        else out << "\"offset\": " << HexJson(f.offset) << ", ";
        if (!f.string_value.empty()) out << "\"stringValue\": \"" << JsonEscape(f.string_value) << "\", ";
        if (!f.attributes.empty()) {
            out << "\"attributes\": [";
            for (size_t ai = 0; ai < f.attributes.size(); ++ai) {
                if (ai) out << ", ";
                out << "\"" << JsonEscape(f.attributes[ai]) << "\"";
            }
            out << "], ";
        }
        out << "\"signature\": \"" << JsonEscape(f.signature) << "\"}";
    }
    if (!cls.fields.empty()) out << "\n" << inner;
    out << "],\n";

    out << inner << "\"methods\": [";
    for (size_t i = 0; i < cls.methods.size(); ++i) {
        const auto& m = cls.methods[i];
        if (i) out << ',';
        out << "\n" << inner << "  {";
        out << "\"name\": \"" << JsonEscape(m.name) << "\", ";
        out << "\"paramCount\": " << m.params.size() << ", ";
        out << "\"rva\": " << HexJson(m.rva) << ", ";
        if (m.method_info_rva) out << "\"methodInfoRva\": " << HexJson(m.method_info_rva) << ", ";
        if (m.invoker_rva) out << "\"invokerRva\": " << HexJson(m.invoker_rva) << ", ";
        if (m.token) out << "\"token\": " << HexJson(m.token) << ", ";
        if (m.is_generic) out << "\"isGeneric\": true, ";
        if (m.is_inflated) out << "\"isInflated\": true, ";
        if (m.is_getter || m.is_setter) {
            out << "\"property\": \"" << JsonEscape(m.property_name) << "\", ";
            out << "\"accessor\": \"" << (m.is_getter ? "get" : "set") << "\", ";
        }
        if (!m.attributes.empty()) {
            out << "\"attributes\": [";
            for (size_t ai = 0; ai < m.attributes.size(); ++ai) {
                if (ai) out << ", ";
                out << "\"" << JsonEscape(m.attributes[ai]) << "\"";
            }
            out << "], ";
        }
        out << "\"signature\": \"" << JsonEscape(m.signature) << "\"}";
    }
    if (!cls.methods.empty()) out << "\n" << inner;
    out << "],\n";

    out << inner << "\"nestedTypes\": [";
    bool first_nested = true;
    for (const auto& nested : cls.nested) WriteClassJson(out, nested, indent + 2, first_nested);
    if (!cls.nested.empty()) out << "\n" << inner;
    out << "]\n" << pad << "}";
}

void WriteFingerprintJson(std::ostream& out, const DumpFingerprint& fp, const std::string& indent) {
    out << indent << "\"fingerprint\": {\n";
    out << indent << "  \"module\": \"" << JsonEscape(fp.module_name) << "\",\n";
    out << indent << "  \"sizeOfImage\": " << HexJson(fp.size_of_image) << ",\n";
    out << indent << "  \"timeDateStamp\": " << HexJson(fp.time_date_stamp) << ",\n";
    if (!fp.file_version.empty())
        out << indent << "  \"fileVersion\": \"" << JsonEscape(fp.file_version) << "\",\n";
    if (!fp.product_version.empty())
        out << indent << "  \"productVersion\": \"" << JsonEscape(fp.product_version) << "\",\n";
    if (!fp.unity_hint.empty())
        out << indent << "  \"unityHint\": \"" << JsonEscape(fp.unity_hint) << "\",\n";
    out << indent << "  \"imageCount\": " << fp.image_count << ",\n";
    out << indent << "  \"filteredImageCount\": " << fp.filtered_image_count << ",\n";
    out << indent << "  \"classCount\": " << fp.class_count << ",\n";
    out << indent << "  \"methodCount\": " << fp.method_count << "\n";
    out << indent << "}";
}

bool WriteCpp(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "// IL2CPP dump - " << root.timestamp << "\n";
    out << "// values are RVAs / offsets vs " << root.module << " base.\n";
    if (!root.fingerprint.unity_hint.empty()) {
        out << "// unity hint: " << root.fingerprint.unity_hint << "\n";
    }
    out << "// image 0x" << std::hex << root.fingerprint.size_of_image << std::dec
        << "  stamp 0x" << std::hex << root.fingerprint.time_date_stamp << std::dec << "\n";
    out << "#pragma once\n#include <cstdint>\n\nnamespace GameDump {\n\n";
    for (const auto& img : root.images) WriteImageCpp(out, img, 0);
    out << "} // GameDump\n";
    return true;
}

bool WriteCSharp(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "// IL2CPP dump - " << root.timestamp << "\n";
    out << "// values are RVAs / offsets vs " << root.module << " base.\n\n";
    out << "namespace GameDump\n{\n\n";
    for (const auto& img : root.images) WriteImageCSharp(out, img, 0);
    out << "}\n";
    return true;
}

bool WriteRust(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "// IL2CPP dump - " << root.timestamp << "\n";
    out << "// values are RVAs / offsets vs " << root.module << " base.\n\n";
    out << "pub mod gamedump {\n\n";
    for (const auto& img : root.images) WriteImageRust(out, img, 0);
    out << "}\n";
    return true;
}

bool WritePython(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "# IL2CPP dump - " << root.timestamp << "\n";
    out << "# values are RVAs / offsets vs " << root.module << " base.\n\n";
    out << "class GameDump:\n";
    if (root.images.empty()) {
        out << "    pass\n";
        return true;
    }
    out << "\n";
    for (const auto& img : root.images) WriteImagePython(out, img, 1);
    return true;
}

bool WriteJson(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "{\n";
    out << "  \"timestamp\": \"" << JsonEscape(root.timestamp) << "\",\n";
    out << "  \"module\": \"" << JsonEscape(root.module) << "\",\n";
    out << "  \"spooferBackend\": \"" << JsonEscape(root.spoofer) << "\",\n";
    WriteFingerprintJson(out, root.fingerprint, "  ");
    out << ",\n  \"images\": [";
    bool first = true;
    for (const auto& img : root.images) {
        if (!first) out << ",\n";
        first = false;
        out << "\n  {\n";
        out << "    \"name\": \"" << JsonEscape(img.name) << "\",\n";
        out << "    \"imageRva\": " << HexJson(img.image_rva) << ",\n";
        out << "    \"classes\": [";
        bool first_cls = true;
        for (const auto& cls : img.classes) WriteClassJson(out, cls, 3, first_cls);
        if (!img.classes.empty()) out << "\n    ";
        out << "]\n  }";
    }
    if (!root.images.empty()) out << "\n  ";
    out << "]\n}\n";
    return true;
}

bool WriteIndexJson(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "{\n";
    out << "  \"timestamp\": \"" << JsonEscape(root.timestamp) << "\",\n";
    out << "  \"module\": \"" << JsonEscape(root.module) << "\",\n";
    out << "  \"spooferBackend\": \"" << JsonEscape(root.spoofer) << "\",\n";
    WriteFingerprintJson(out, root.fingerprint, "  ");
    out << ",\n  \"entries\": [";
    for (size_t i = 0; i < root.index.size(); ++i) {
        const auto& e = root.index[i];
        if (i) out << ',';
        out << "\n    {";
        out << "\"kind\": \"" << JsonEscape(e.kind) << "\", ";
        out << "\"fullName\": \"" << JsonEscape(e.full_name) << "\", ";
        out << "\"symbol\": \"" << JsonEscape(e.symbol) << "\", ";
        out << "\"image\": \"" << JsonEscape(e.image) << "\"";
        if (!e.signature.empty()) out << ", \"signature\": \"" << JsonEscape(e.signature) << "\"";
        if (e.rva) out << ", \"rva\": " << HexJson(e.rva);
        if (e.offset) out << ", \"offset\": " << HexJson(e.offset);
        if (e.method_info_rva) out << ", \"methodInfoRva\": " << HexJson(e.method_info_rva);
        if (e.invoker_rva) out << ", \"invokerRva\": " << HexJson(e.invoker_rva);
        if (e.token) out << ", \"token\": " << HexJson(e.token);
        out << "}";
    }
    if (!root.index.empty()) out << "\n  ";
    out << "]\n}\n";
    return true;
}

bool WriteStringsJson(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "{\n";
    out << "  \"timestamp\": \"" << JsonEscape(root.timestamp) << "\",\n";
    out << "  \"module\": \"" << JsonEscape(root.module) << "\",\n";
    WriteFingerprintJson(out, root.fingerprint, "  ");
    out << ",\n  \"count\": " << root.strings.size() << ",\n";
    out << "  \"strings\": [";
    for (size_t i = 0; i < root.strings.size(); ++i) {
        const auto& s = root.strings[i];
        if (i) out << ',';
        out << "\n    {";
        out << "\"value\": \"" << JsonEscape(s.value) << "\"";
        if (s.rva) out << ", \"rva\": " << HexJson(s.rva);
        if (!s.source.empty()) out << ", \"source\": \"" << JsonEscape(s.source) << "\"";
        if (!s.owner.empty()) out << ", \"owner\": \"" << JsonEscape(s.owner) << "\"";
        out << "}";
    }
    if (!root.strings.empty()) out << "\n  ";
    out << "]\n}\n";
    return true;
}

bool WritePerImageHeaders(const std::filesystem::path& images_dir, const DumpRoot& root) {
    std::error_code ec;
    std::filesystem::create_directories(images_dir, ec);
    for (const auto& img : root.images) {
        const auto path = images_dir / (img.ident + ".hpp");
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << "// IL2CPP dump - " << root.timestamp << "\n";
        out << "// image: " << img.name << "\n";
        out << "#pragma once\n#include <cstdint>\n\nnamespace GameDump {\n\n";
        WriteImageCpp(out, img, 0);
        out << "} // GameDump\n";
    }
    return true;
}

void WriteStubClass(std::ostream& out, const DumpClass& cls, int indent) {
    const std::string pad = Indent(indent);
    const std::string inner = Indent(indent + 1);
    out << pad << "namespace " << cls.ident << " {\n";
    for (const auto& m : cls.methods) {
        if (!m.rva) continue;
        const bool is_static = (m.flags & RRID_METHOD_ATTRIBUTE_STATIC) != 0;
        std::ostringstream sig;
        sig << CppType(m.return_type) << "(__fastcall*)(";
        bool first = true;
        if (!is_static) {
            sig << "void* self";
            first = false;
        }
        for (const auto& p : m.params) {
            if (!first) sig << ", ";
            first = false;
            sig << CppType(p.type) << " /*" << SanitizeIdent(p.name) << "*/";
        }
        if (first) sig << "void";
        sig << ")";
        out << inner << "using " << SanitizeIdent(m.symbol.substr(0, m.symbol.size() > 4 ? m.symbol.size() - 4 : m.symbol.size()))
            << "_t = " << sig.str() << "; // " << m.signature << "\n";
    }
    for (const auto& nested : cls.nested) WriteStubClass(out, nested, indent + 1);
    out << pad << "}\n";
}

bool WriteStubs(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "// IL2CPP typed stubs - " << root.timestamp << "\n";
    out << "// Non-primitive types map to void*. Include GameDump.hpp for RVAs.\n";
    out << "#pragma once\n#include <cstdint>\n\nnamespace GameDump {\nnamespace Stubs {\n\n";
    for (const auto& img : root.images) {
        out << "namespace " << img.ident << " {\n";
        for (const auto& cls : img.classes) WriteStubClass(out, cls, 1);
        out << "} // " << img.ident << "\n\n";
    }
    out << "} // Stubs\n} // GameDump\n";
    return true;
}

bool WriteIdaScript(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out <<
R"py(# IDA Python script generated by IL2CPP Dumper
# File -> Script file...  OR  Alt+F7
# Set MODULE_NAME if your binary name differs.

import idaapi
import ida_name
import ida_funcs
import idc

MODULE_NAME = ")py" << JsonEscape(root.module) << R"py("

ENTRIES = [
)py";
    for (const auto& e : root.index) {
        if (e.kind != "method" || !e.rva) continue;
        std::string name = e.full_name;
        for (char& c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
                c = '_';
        }
        out << "  (0x" << std::hex << e.rva << std::dec << ", \"" << JsonEscape(name) << "\"),\n";
    }
    out <<
R"py(]

def module_base():
    for i in range(idaapi.get_module_qty()):
        m = idaapi.get_module_info(i)
        if m and MODULE_NAME.lower() in m.name.lower():
            return m.base
    return idaapi.get_imagebase()

def main():
    base = module_base()
    print("[IL2CPP Dumper] base=0x%X entries=%d" % (base, len(ENTRIES)))
    renamed = 0
    for rva, name in ENTRIES:
        ea = base + rva
        if not idc.is_code(idc.get_full_flags(ea)):
            ida_funcs.add_func(ea)
        if ida_name.set_name(ea, name.replace(".", "_"), ida_name.SN_FORCE):
            renamed += 1
    print("[IL2CPP Dumper] renamed %d functions" % renamed)

if __name__ == "__main__":
    main()
)py";
    return true;
}

bool WriteGhidraScript(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out <<
R"py(# Ghidra Python script generated by IL2CPP Dumper
# Window -> Script Manager -> Run
# @category IL2CPP

from ghidra.program.model.symbol import SourceType

ENTRIES = [
)py";
    for (const auto& e : root.index) {
        if (e.kind != "method" || !e.rva) continue;
        std::string name = e.full_name;
        for (char& c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
                c = '_';
        }
        out << "  (0x" << std::hex << e.rva << std::dec << ", \"" << JsonEscape(name) << "\"),\n";
    }
    out <<
R"py(]

def run():
    base = currentProgram.getImageBase().getOffset()
    listing = currentProgram.getListing()
    renamed = 0
    print("[IL2CPP Dumper] base=0x%x entries=%d" % (base, len(ENTRIES)))
    for rva, name in ENTRIES:
        addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(base + rva)
        name = name.replace(".", "_")
        try:
            createFunction(addr, name)
        except:
            pass
        try:
            currentProgram.getSymbolTable().createLabel(addr, name, SourceType.USER_DEFINED)
            renamed += 1
        except:
            pass
    print("[IL2CPP Dumper] labeled %d addresses" % renamed)

run()
)py";
    return true;
}

bool WriteBinaryNinjaScript(const std::filesystem::path& path, const DumpRoot& root) {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out <<
R"py(# Binary Ninja Python script generated by IL2CPP Dumper
# Script Console -> paste/run, or Tools -> Run Script

MODULE_NAME = ")py" << JsonEscape(root.module) << R"py("

ENTRIES = [
)py";
    for (const auto& e : root.index) {
        if (e.kind != "method" || !e.rva) continue;
        std::string name = e.full_name;
        for (char& c : name) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '.'))
                c = '_';
        }
        out << "  (0x" << std::hex << e.rva << std::dec << ", \"" << JsonEscape(name) << "\"),\n";
    }
    out <<
R"py(]

def module_base(bv):
    name = MODULE_NAME.lower()
    modules = getattr(bv, "modules", None)
    if modules:
        for m in modules:
            try:
                if name in m.name.lower():
                    return m.base
            except Exception:
                pass
    return bv.start

def run(bv):
    from binaryninja import Symbol, SymbolType
    base = module_base(bv)
    print("[IL2CPP Dumper] base=0x%x entries=%d" % (base, len(ENTRIES)))
    renamed = 0
    for rva, name in ENTRIES:
        addr = base + rva
        name = name.replace(".", "_")
        try:
            bv.create_user_function(addr)
        except Exception:
            try:
                bv.add_function(addr)
            except Exception:
                pass
        try:
            bv.define_user_symbol(Symbol(SymbolType.FunctionSymbol, addr, name))
            renamed += 1
        except Exception:
            pass
    print("[IL2CPP Dumper] labeled %d functions" % renamed)

try:
    run(bv)
except NameError:
    print("Open this in Binary Ninja Script Console (bv is provided).")
)py";
    return true;
}

std::string ExtractJsonString(const std::string& blob, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto pos = blob.find(needle);
    if (pos == std::string::npos) return {};
    pos = blob.find(':', pos);
    if (pos == std::string::npos) return {};
    pos = blob.find('"', pos);
    if (pos == std::string::npos) return {};
    auto end = blob.find('"', pos + 1);
    if (end == std::string::npos) return {};
    return blob.substr(pos + 1, end - pos - 1);
}

uint64_t ParseHexMaybe(const std::string& s) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return std::strtoull(s.c_str() + 2, nullptr, 16);
    }
    return std::strtoull(s.c_str(), nullptr, 10);
}

struct DiffEntry {
    std::string full_name;
    std::string kind;
    uint64_t old_rva = 0;
    uint64_t new_rva = 0;
};

bool ParseIndexEntries(const std::filesystem::path& path,
                       std::unordered_map<std::string, std::pair<std::string, uint64_t>>& out) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string data = buffer.str();

    size_t pos = 0;
    while (true) {
        auto kind_pos = data.find("\"kind\"", pos);
        if (kind_pos == std::string::npos) break;
        auto obj_end = data.find('}', kind_pos);
        if (obj_end == std::string::npos) break;
        const std::string obj = data.substr(kind_pos, obj_end - kind_pos);
        const std::string kind = ExtractJsonString(obj, "kind");
        const std::string full = ExtractJsonString(obj, "fullName");
        const std::string rva = ExtractJsonString(obj, "rva");
        if (!full.empty() && !rva.empty() && (kind == "method" || kind == "staticField" || kind == "class")) {
            out[kind + "|" + full] = {kind, ParseHexMaybe(rva)};
        }
        pos = obj_end + 1;
    }
    return !out.empty();
}

bool WriteDiff(const std::filesystem::path& prev_index, const std::filesystem::path& out_path,
               const DumpRoot& root) {
    std::unordered_map<std::string, std::pair<std::string, uint64_t>> old_map;
    if (!ParseIndexEntries(prev_index, old_map)) return false;

    std::unordered_map<std::string, std::pair<std::string, uint64_t>> new_map;
    for (const auto& e : root.index) {
        if (!e.rva) continue;
        if (e.kind != "method" && e.kind != "staticField" && e.kind != "class") continue;
        new_map[e.kind + "|" + e.full_name] = {e.kind, e.rva};
    }

    std::vector<DiffEntry> changed, added, removed;
    for (const auto& kv : new_map) {
        auto it = old_map.find(kv.first);
        if (it == old_map.end()) {
            DiffEntry d;
            d.full_name = kv.first.substr(kv.first.find('|') + 1);
            d.kind = kv.second.first;
            d.new_rva = kv.second.second;
            added.push_back(d);
        } else if (it->second.second != kv.second.second) {
            DiffEntry d;
            d.full_name = kv.first.substr(kv.first.find('|') + 1);
            d.kind = kv.second.first;
            d.old_rva = it->second.second;
            d.new_rva = kv.second.second;
            changed.push_back(d);
        }
    }
    for (const auto& kv : old_map) {
        if (!new_map.count(kv.first)) {
            DiffEntry d;
            d.full_name = kv.first.substr(kv.first.find('|') + 1);
            d.kind = kv.second.first;
            d.old_rva = kv.second.second;
            removed.push_back(d);
        }
    }

    std::ofstream out(out_path);
    if (!out.is_open()) return false;
    out << "IL2CPP Dump Diff - " << root.timestamp << "\n";
    out << "Compared against previous Index.json\n\n";
    out << "Changed: " << changed.size() << "\n";
    out << "Added:   " << added.size() << "\n";
    out << "Removed: " << removed.size() << "\n\n";

    out << "== Changed RVAs ==\n";
    for (const auto& d : changed) {
        out << "[" << d.kind << "] " << d.full_name << "  "
            << Hex(d.old_rva) << " -> " << Hex(d.new_rva) << "\n";
    }
    out << "\n== Added ==\n";
    for (const auto& d : added) {
        out << "[" << d.kind << "] " << d.full_name << "  " << Hex(d.new_rva) << "\n";
    }
    out << "\n== Removed ==\n";
    for (const auto& d : removed) {
        out << "[" << d.kind << "] " << d.full_name << "  " << Hex(d.old_rva) << "\n";
    }
    return true;
}

void WriteReadme(const std::filesystem::path& path) {
    std::ofstream out(path);
    if (!out.is_open()) return;
    out << "IL2CPP Dump - " << CurrentTimestamp() << "\n";
    out << "================================\n\n";
    out << "Files:\n";
    out << "  GameDump.hpp       - C/C++ offsets\n";
    out << "  GameDump_Stubs.hpp - optional typed __fastcall aliases\n";
    out << "  GameDump.cs/rs/py  - other language bindings\n";
    out << "  GameDump.json      - full structured dump\n";
    out << "  Index.json         - flat search index\n";
    out << "  Diff.txt           - RVA changes vs previous Index.json (if present)\n";
    out << "  Strings.json       - managed string literals + static string fields\n";
    out << "  images/            - per-assembly C++ headers\n";
    out << "  scripts/ida_apply_names.py\n";
    out << "  scripts/ghidra_apply_names.py\n";
    out << "  scripts/binja_apply_names.py\n";
}

} // namespace

bool GameDumper::DumpFromFiles(const std::string& assembly_path,
                               const std::string& metadata_path,
                               const std::string& output_dir,
                               const DumpConfig& cfg,
                               std::function<void(const std::string&)> logCallback) {
    auto log = [&](const std::string& msg) {
        if (logCallback) logCallback(msg);
        else std::cout << msg << std::endl;
    };

    DumpRoot root;
    std::string err;
    if (!static_dump::Collect(assembly_path, metadata_path, cfg, root, err, log)) {
        log("[!] static dump failed: " + err);
        return false;
    }
    return EmitAll(root, output_dir, cfg, logCallback);
}

bool GameDumper::EmitAll(const DumpRoot& root,
                         const std::string& output_dir,
                         const DumpConfig& cfg,
                         std::function<void(const std::string&)> logCallback) {
    auto log = [&](const std::string& msg) {
        if (logCallback) logCallback(msg);
        else std::cout << msg << std::endl;
    };

    std::filesystem::path dir(output_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const auto index_path = dir / "Index.json";
    const auto prev_index = dir / "Index.prev.json";
    if (cfg.emit_diff && std::filesystem::exists(index_path)) {
        std::error_code cec;
        std::filesystem::remove(prev_index, cec);
        std::filesystem::rename(index_path, prev_index, cec);
    }

    auto fail = [&](const std::filesystem::path& p) {
        log("[!] cant write " + p.string());
        return false;
    };

    if (cfg.emit_cpp) {
        log("[*] writing C++...");
        if (!WriteCpp(dir / "GameDump.hpp", root)) return fail(dir / "GameDump.hpp");
    }
    if (cfg.emit_cs) {
        log("[*] writing C#...");
        if (!WriteCSharp(dir / "GameDump.cs", root)) return fail(dir / "GameDump.cs");
    }
    if (cfg.emit_rs) {
        log("[*] writing Rust...");
        if (!WriteRust(dir / "GameDump.rs", root)) return fail(dir / "GameDump.rs");
    }
    if (cfg.emit_py) {
        log("[*] writing Python...");
        if (!WritePython(dir / "GameDump.py", root)) return fail(dir / "GameDump.py");
    }
    if (cfg.emit_json) {
        log("[*] writing JSON...");
        if (!WriteJson(dir / "GameDump.json", root)) return fail(dir / "GameDump.json");
    }
    if (cfg.emit_index) {
        log("[*] writing index...");
        if (!WriteIndexJson(index_path, root)) return fail(index_path);
    }
    if (cfg.emit_strings) {
        log("[*] writing Strings.json...");
        if (!WriteStringsJson(dir / "Strings.json", root)) return fail(dir / "Strings.json");
    }
    if (cfg.emit_images) {
        log("[*] writing per-image headers...");
        if (!WritePerImageHeaders(dir / "images", root)) return fail(dir / "images");
    }
    if (cfg.emit_stubs) {
        log("[*] writing typed stubs...");
        if (!WriteStubs(dir / "GameDump_Stubs.hpp", root)) return fail(dir / "GameDump_Stubs.hpp");
    }
    if (cfg.emit_scripts) {
        log("[*] writing IDA/Ghidra/Binja scripts...");
        const auto scripts = dir / "scripts";
        std::filesystem::create_directories(scripts, ec);
        if (!WriteIdaScript(scripts / "ida_apply_names.py", root)) return fail(scripts / "ida_apply_names.py");
        if (!WriteGhidraScript(scripts / "ghidra_apply_names.py", root)) return fail(scripts / "ghidra_apply_names.py");
        if (!WriteBinaryNinjaScript(scripts / "binja_apply_names.py", root)) return fail(scripts / "binja_apply_names.py");
    }
    if (cfg.emit_diff && std::filesystem::exists(prev_index)) {
        log("[*] writing Diff.txt...");
        WriteDiff(prev_index, dir / "Diff.txt", root);
    }

    WriteReadme(dir / "README.txt");
    log("[+] wrote " + dir.string());
    return true;
}
