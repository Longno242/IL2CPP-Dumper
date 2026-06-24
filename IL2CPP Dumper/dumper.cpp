#define _CRT_SECURE_NO_WARNINGS
#include "dumper.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

    std::string CurrentTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
        localtime_s(&tm, &t);
        std::ostringstream os;
        os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return os.str();
    }

    std::string SanitizeIdent(const std::string& in) {
        if (in.empty()) return "_";

        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '_') {
                out += c;
            } else {
                out += '_';
            }
        }
        if (out[0] >= '0' && out[0] <= '9') {
            out.insert(out.begin(), '_');
        }
        return out;
    }

    std::string ToSnakeCase(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (char c : in) {
            if (c >= 'A' && c <= 'Z') {
                if (!out.empty()) out += '_';
                out += (char)(c - 'A' + 'a');
            } else if (c >= 'a' && c <= 'z') {
                out += c;
            } else if (c >= '0' && c <= '9') {
                out += c;
            } else if (c == '_') {
                if (!out.empty() && out.back() != '_') out += '_';
            }
        }
        if (out.empty()) return "_";
        if (out[0] >= '0' && out[0] <= '9') {
            out.insert(out.begin(), '_');
        }
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
        return std::string((size_t)level * 4, ' ');
    }

    std::string JsonEscape(const std::string& s) {
        std::ostringstream os;
        for (char c : s) {
            switch (c) {
                case '"':  os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\n': os << "\\n";  break;
                case '\r': os << "\\r";  break;
                case '\t': os << "\\t";  break;
                default:   os << c;      break;
            }
        }
        return os.str();
    }

    std::string PrettyType(const std::string& t) {
        if (t == "System.Void")    return "void";
        if (t == "System.Boolean") return "bool";
        if (t == "System.Byte")    return "byte";
        if (t == "System.SByte")   return "sbyte";
        if (t == "System.Char")    return "char";
        if (t == "System.Int16")   return "short";
        if (t == "System.UInt16")  return "ushort";
        if (t == "System.Int32")   return "int";
        if (t == "System.UInt32")  return "uint";
        if (t == "System.Int64")   return "long";
        if (t == "System.UInt64")  return "ulong";
        if (t == "System.Single")  return "float";
        if (t == "System.Double")  return "double";
        if (t == "System.Decimal") return "decimal";
        if (t == "System.String")  return "string";
        if (t == "System.Object")  return "object";
        return t;
    }

    std::string AccessLevel(uint32_t flags) {
        switch (flags & RRID_METHOD_ACCESS_LEVEL_MASK) {
            case RRID_METHOD_ACCESS_LEVEL_PRIVATE:             return "private";
            case RRID_METHOD_ACCESS_LEVEL_PRIVATE_PROTECTED:   return "private protected";
            case RRID_METHOD_ACCESS_LEVEL_INTERNAL:            return "internal";
            case RRID_METHOD_ACCESS_LEVEL_PROTECTED:           return "protected";
            case RRID_METHOD_ACCESS_LEVEL_PROTECTED_INTERNAL:  return "protected internal";
            case RRID_METHOD_ACCESS_LEVEL_PUBLIC:              return "public";
            default:                                           return "private";
        }
    }

    std::string FieldModifiers(uint32_t flags) {
        std::string m;
        if (flags & RRID_FIELD_ATTRIBUTE_STATIC)   m += "static ";
        if (flags & RRID_FIELD_ATTRIBUTE_CONST)    m += "const ";
        if (flags & RRID_FIELD_ATTRIBUTE_READONLY) m += "readonly ";
        return m;
    }

    std::string MethodModifiers(uint32_t flags) {
        std::string m;
        if (flags & RRID_METHOD_ATTRIBUTE_STATIC)       m += "static ";
        if (flags & RRID_METHOD_ATTRIBUTE_ABSTRACT)     m += "abstract ";
        if (flags & RRID_METHOD_ATTRIBUTE_VIRTUAL)      m += "virtual ";
        if (flags & RRID_METHOD_ATTRIBUTE_FINAL)        m += "sealed ";
        if (flags & RRID_METHOD_ATTRIBUTE_NEW_SLOT)     m += "new ";
        if (flags & RRID_METHOD_ATTRIBUTE_PINVOKE_IMPL) m += "extern ";
        return m;
    }

    std::string BuildSignature(RridMethod* m) {
        std::ostringstream os;
        os << AccessLevel(m->get_flags()) << ' '
           << MethodModifiers(m->get_flags())
           << PrettyType(m->get_return_type()) << ' '
           << m->get_name() << '(';

        const auto& params = m->get_params();
        for (size_t i = 0; i < params.size(); ++i) {
            if (i) os << ", ";
            os << PrettyType(params[i].first) << ' ' << params[i].second;
        }
        os << ')';
        return os.str();
    }

    std::string BuildFieldSignature(RridField* f) {
        std::ostringstream os;
        os << AccessLevel(f->get_flags()) << ' '
           << FieldModifiers(f->get_flags())
           << PrettyType(f->get_type()) << ' '
           << f->get_name();
        return os.str();
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

    std::string MethodIdentBase(RridMethod* m) {
        return SanitizeIdent(m->get_name()) + "_" + std::to_string(m->get_param_count()) + "_RVA";
    }

    std::string ClassIdent(RridClass* cls) {
        const std::string ns = cls->get_namespace();
        const std::string cname = cls->get_name();
        return SanitizeIdent(ns.empty() ? cname : (ns + "_" + cname));
    }

    std::string ClassFullName(RridClass* cls) {
        const std::string ns = cls->get_namespace();
        const std::string cname = cls->get_name();
        return ns.empty() ? cname : (ns + "." + cname);
    }

    // ---- C++ ----

    void WriteClassCpp(std::ostream& out, RridClass* cls, int indent) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);
        const bool is_enum      = cls->is_enum();

        const char* kind = "class";
        if (is_enum)                  kind = "enum";
        else if (cls->is_valuetype()) kind = "struct";

        out << pad << "// " << kind << " " << ClassFullName(cls)
            << "  (ClassRVA " << Hex(cls->get_class_rva()) << ")\n";
        out << pad << "namespace " << ClassIdent(cls) << " {\n";
        out << inner << "constexpr uint64_t ClassRVA = " << Hex(cls->get_class_rva()) << ";\n";

        auto fields = cls->get_fields();
        if (!fields.empty()) {
            if (is_enum) {
                out << "\n" << inner << "// enum values\n";
                std::unordered_set<std::string> used{"ClassRVA"};
                for (auto* f : fields) {
                    const std::string ident = UniqueName(used, SanitizeIdent(f->get_name()));
                    out << inner << "constexpr int64_t " << ident
                        << " = " << f->get_offset() << "; // " << BuildFieldSignature(f) << "\n";
                }
            } else {
                out << "\n" << inner << "// fields\n";
                std::unordered_set<std::string> used{"ClassRVA"};
                for (auto* f : fields) {
                    const bool        is_static = f->has_rva();
                    const std::string suffix    = is_static ? "_RVA" : "_Offset";
                    const std::string ident     = UniqueName(used, SanitizeIdent(f->get_name()) + suffix);
                    const uint64_t    value     = is_static ? f->get_static_rva() : f->get_offset();
                    out << inner << "constexpr uint64_t " << ident
                        << " = " << Hex(value) << "; // " << BuildFieldSignature(f) << "\n";
                }
            }
        }

        auto methods = cls->get_methods();
        if (!methods.empty()) {
            out << "\n" << inner << "// methods\n";
            std::unordered_set<std::string> used{"ClassRVA"};
            for (auto* m : methods) {
                const std::string ident = UniqueName(used, MethodIdentBase(m));
                out << inner << "constexpr uint64_t " << ident
                    << " = " << Hex(m->get_method_rva()) << "; // " << BuildSignature(m) << "\n";
            }
        }

        auto nested = cls->get_nested_types();
        if (!nested.empty()) {
            out << "\n" << inner << "// nested types\n";
            for (auto* nested_cls : nested) {
                out << "\n";
                WriteClassCpp(out, nested_cls, indent + 1);
            }
        }

        out << pad << "}\n\n";
    }

    void WriteImageCpp(std::ostream& out, RridImage* img, int indent = 0) {
        const std::string pad = Indent(indent);
        out << pad << "// ==== image: " << img->get_name()
            << "   ModuleBase " << Hex(img->get_module_base())
            << "   ImageRVA " << Hex(img->get_image_rva()) << "\n";
        const std::string img_ident = SanitizeIdent(img->get_name());
        out << pad << "namespace " << img_ident << " {\n\n";
        out << pad << "    constexpr uint64_t ModuleBase = " << Hex(img->get_module_base()) << ";\n";
        out << pad << "    constexpr uint64_t ImageRVA   = " << Hex(img->get_image_rva())   << ";\n\n";
        for (auto* cls : img->get_classes()) {
            WriteClassCpp(out, cls, indent + 1);
        }
        out << pad << "} // " << img_ident << "\n\n";
    }

    // ---- C# ----

    void WriteClassCSharp(std::ostream& out, RridClass* cls, int indent) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);
        const bool is_enum      = cls->is_enum();

        out << pad << "// " << (is_enum ? "enum" : "class") << " " << ClassFullName(cls)
            << "  (ClassRVA " << Hex(cls->get_class_rva()) << ")\n";
        out << pad << "public static class " << ClassIdent(cls) << "\n" << pad << "{\n";
        out << inner << "public const ulong ClassRVA = " << Hex(cls->get_class_rva()) << ";\n";

        auto fields = cls->get_fields();
        if (!fields.empty()) {
            if (is_enum) {
                out << "\n" << inner << "// enum values\n";
                std::unordered_set<std::string> used{"ClassRVA"};
                for (auto* f : fields) {
                    const std::string ident = UniqueName(used, SanitizeIdent(f->get_name()));
                    out << inner << "public const long " << ident
                        << " = " << f->get_offset() << "; // " << BuildFieldSignature(f) << "\n";
                }
            } else {
                out << "\n" << inner << "// fields\n";
                std::unordered_set<std::string> used{"ClassRVA"};
                for (auto* f : fields) {
                    const bool        is_static = f->has_rva();
                    const std::string suffix    = is_static ? "_RVA" : "_Offset";
                    const std::string ident     = UniqueName(used, SanitizeIdent(f->get_name()) + suffix);
                    const uint64_t    value     = is_static ? f->get_static_rva() : f->get_offset();
                    out << inner << "public const ulong " << ident
                        << " = " << Hex(value) << "; // " << BuildFieldSignature(f) << "\n";
                }
            }
        }

        auto methods = cls->get_methods();
        if (!methods.empty()) {
            out << "\n" << inner << "// methods\n";
            std::unordered_set<std::string> used{"ClassRVA"};
            for (auto* m : methods) {
                const std::string ident = UniqueName(used, MethodIdentBase(m));
                out << inner << "public const ulong " << ident
                    << " = " << Hex(m->get_method_rva()) << "; // " << BuildSignature(m) << "\n";
            }
        }

        auto nested = cls->get_nested_types();
        if (!nested.empty()) {
            out << "\n" << inner << "// nested types\n";
            for (auto* nested_cls : nested) {
                out << "\n";
                WriteClassCSharp(out, nested_cls, indent + 1);
            }
        }

        out << pad << "}\n\n";
    }

    void WriteImageCSharp(std::ostream& out, RridImage* img, int indent = 0) {
        const std::string pad = Indent(indent);
        out << pad << "// ==== image: " << img->get_name()
            << "   ModuleBase " << Hex(img->get_module_base())
            << "   ImageRVA " << Hex(img->get_image_rva()) << "\n";
        const std::string img_ident = SanitizeIdent(img->get_name());
        out << pad << "public static class " << img_ident << "\n" << pad << "{\n";
        out << pad << "    public const ulong ModuleBase = " << Hex(img->get_module_base()) << ";\n";
        out << pad << "    public const ulong ImageRVA   = " << Hex(img->get_image_rva())   << ";\n\n";
        for (auto* cls : img->get_classes()) {
            WriteClassCSharp(out, cls, indent + 1);
        }
        out << pad << "}\n\n";
    }

    // ---- Rust ----

    void WriteClassRust(std::ostream& out, RridClass* cls, int indent) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);
        const bool is_enum      = cls->is_enum();
        const std::string ident = ToSnakeCase(ClassIdent(cls));

        out << pad << "// " << (is_enum ? "enum" : "class") << " " << ClassFullName(cls)
            << "  (ClassRVA " << Hex(cls->get_class_rva()) << ")\n";
        out << pad << "pub mod " << ident << " {\n";
        out << inner << "pub const CLASS_RVA: u64 = " << Hex(cls->get_class_rva()) << ";\n";

        auto fields = cls->get_fields();
        if (!fields.empty()) {
            if (is_enum) {
                out << "\n" << inner << "// enum values\n";
                std::unordered_set<std::string> used{"CLASS_RVA"};
                for (auto* f : fields) {
                    const std::string name = UniqueName(used, ToSnakeCase(SanitizeIdent(f->get_name())));
                    out << inner << "pub const " << name << ": i64 = "
                        << f->get_offset() << "; // " << BuildFieldSignature(f) << "\n";
                }
            } else {
                out << "\n" << inner << "// fields\n";
                std::unordered_set<std::string> used{"CLASS_RVA"};
                for (auto* f : fields) {
                    const bool        is_static = f->has_rva();
                    const std::string suffix    = is_static ? "_rva" : "_offset";
                    const std::string name      = UniqueName(used, ToSnakeCase(SanitizeIdent(f->get_name())) + suffix);
                    const uint64_t    value     = is_static ? f->get_static_rva() : f->get_offset();
                    out << inner << "pub const " << name << ": u64 = "
                        << Hex(value) << "; // " << BuildFieldSignature(f) << "\n";
                }
            }
        }

        auto methods = cls->get_methods();
        if (!methods.empty()) {
            out << "\n" << inner << "// methods\n";
            std::unordered_set<std::string> used{"CLASS_RVA"};
            for (auto* m : methods) {
                const std::string name = UniqueName(used, ToSnakeCase(MethodIdentBase(m)));
                out << inner << "pub const " << name << ": u64 = "
                    << Hex(m->get_method_rva()) << "; // " << BuildSignature(m) << "\n";
            }
        }

        auto nested = cls->get_nested_types();
        if (!nested.empty()) {
            out << "\n" << inner << "// nested types\n";
            for (auto* nested_cls : nested) {
                out << "\n";
                WriteClassRust(out, nested_cls, indent + 1);
            }
        }

        out << pad << "}\n\n";
    }

    void WriteImageRust(std::ostream& out, RridImage* img, int indent = 0) {
        const std::string pad = Indent(indent);
        const std::string img_ident = ToSnakeCase(SanitizeIdent(img->get_name()));
        out << pad << "// ==== image: " << img->get_name()
            << "   ModuleBase " << Hex(img->get_module_base())
            << "   ImageRVA " << Hex(img->get_image_rva()) << "\n";
        out << pad << "pub mod " << img_ident << " {\n";
        out << pad << "    pub const MODULE_BASE: u64 = " << Hex(img->get_module_base()) << ";\n";
        out << pad << "    pub const IMAGE_RVA: u64   = " << Hex(img->get_image_rva())   << ";\n\n";
        for (auto* cls : img->get_classes()) {
            WriteClassRust(out, cls, indent + 1);
        }
        out << pad << "}\n\n";
    }

    // ---- Python ----

    void WriteClassPython(std::ostream& out, RridClass* cls, int indent) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);
        const bool is_enum      = cls->is_enum();

        out << pad << "# " << (is_enum ? "enum" : "class") << " " << ClassFullName(cls)
            << "  (ClassRVA " << Hex(cls->get_class_rva()) << ")\n";
        out << pad << "class " << ClassIdent(cls) << ":\n";
        out << inner << "CLASS_RVA = " << Hex(cls->get_class_rva()) << "\n";

        auto fields = cls->get_fields();
        if (!fields.empty()) {
            if (is_enum) {
                out << "\n" << inner << "# enum values\n";
                std::unordered_set<std::string> used{"CLASS_RVA"};
                for (auto* f : fields) {
                    const std::string ident = UniqueName(used, SanitizeIdent(f->get_name()));
                    out << inner << ident << " = " << f->get_offset()
                        << "  # " << BuildFieldSignature(f) << "\n";
                }
            } else {
                out << "\n" << inner << "# fields\n";
                std::unordered_set<std::string> used{"CLASS_RVA"};
                for (auto* f : fields) {
                    const bool        is_static = f->has_rva();
                    const std::string suffix    = is_static ? "_RVA" : "_Offset";
                    const std::string ident     = UniqueName(used, SanitizeIdent(f->get_name()) + suffix);
                    const uint64_t    value     = is_static ? f->get_static_rva() : f->get_offset();
                    out << inner << ident << " = " << Hex(value)
                        << "  # " << BuildFieldSignature(f) << "\n";
                }
            }
        }

        auto methods = cls->get_methods();
        if (!methods.empty()) {
            out << "\n" << inner << "# methods\n";
            std::unordered_set<std::string> used{"CLASS_RVA"};
            for (auto* m : methods) {
                const std::string ident = UniqueName(used, MethodIdentBase(m));
                out << inner << ident << " = " << Hex(m->get_method_rva())
                    << "  # " << BuildSignature(m) << "\n";
            }
        }

        auto nested = cls->get_nested_types();
        if (!nested.empty()) {
            out << "\n" << inner << "# nested types\n";
            for (auto* nested_cls : nested) {
                out << "\n";
                WriteClassPython(out, nested_cls, indent + 1);
            }
        }

        out << "\n";
    }

    void WriteImagePython(std::ostream& out, RridImage* img, int indent = 0) {
        const std::string pad = Indent(indent);
        const std::string img_ident = SanitizeIdent(img->get_name());
        out << pad << "# ==== image: " << img->get_name()
            << "   ModuleBase " << Hex(img->get_module_base())
            << "   ImageRVA " << Hex(img->get_image_rva()) << "\n";
        out << pad << "class " << img_ident << ":\n";
        out << pad << "    MODULE_BASE = " << Hex(img->get_module_base()) << "\n";
        out << pad << "    IMAGE_RVA   = " << Hex(img->get_image_rva())   << "\n\n";
        for (auto* cls : img->get_classes()) {
            WriteClassPython(out, cls, indent + 1);
        }
        out << "\n";
    }

    // ---- JSON ----

    void WriteClassJson(std::ostream& out, RridClass* cls, int indent, bool& first_child) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);
        const bool is_enum      = cls->is_enum();

        if (!first_child) out << ",\n";
        first_child = false;

        out << pad << "{\n";
        out << inner << "\"name\": \"" << JsonEscape(cls->get_name()) << "\",\n";
        out << inner << "\"namespace\": \"" << JsonEscape(cls->get_namespace()) << "\",\n";
        out << inner << "\"fullName\": \"" << JsonEscape(ClassFullName(cls)) << "\",\n";
        out << inner << "\"kind\": \"" << (is_enum ? "enum" : (cls->is_valuetype() ? "struct" : "class")) << "\",\n";
        out << inner << "\"classRva\": " << HexJson(cls->get_class_rva()) << ",\n";

        out << inner << "\"fields\": [";
        auto fields = cls->get_fields();
        bool first_field = true;
        for (auto* f : fields) {
            if (!first_field) out << ',';
            first_field = false;
            const bool is_static = f->has_rva();
            out << "\n" << inner << "  {";
            out << "\"name\": \"" << JsonEscape(f->get_name()) << "\", ";
            out << "\"type\": \"" << JsonEscape(f->get_type()) << "\", ";
            out << "\"kind\": \"" << (is_enum ? "enumValue" : (is_static ? "static" : "instance")) << "\", ";
            if (is_enum) {
                out << "\"value\": " << f->get_offset() << ", ";
            } else if (is_static) {
                out << "\"rva\": " << HexJson(f->get_static_rva()) << ", ";
            } else {
                out << "\"offset\": " << HexJson(f->get_offset()) << ", ";
            }
            out << "\"signature\": \"" << JsonEscape(BuildFieldSignature(f)) << "\"}";
        }
        if (!fields.empty()) out << "\n" << inner;
        out << "],\n";

        out << inner << "\"methods\": [";
        auto methods = cls->get_methods();
        bool first_method = true;
        for (auto* m : methods) {
            if (!first_method) out << ',';
            first_method = false;
            out << "\n" << inner << "  {";
            out << "\"name\": \"" << JsonEscape(m->get_name()) << "\", ";
            out << "\"paramCount\": " << m->get_param_count() << ", ";
            out << "\"rva\": " << HexJson(m->get_method_rva()) << ", ";
            out << "\"signature\": \"" << JsonEscape(BuildSignature(m)) << "\"}";
        }
        if (!methods.empty()) out << "\n" << inner;
        out << "],\n";

        out << inner << "\"nestedTypes\": [";
        auto nested = cls->get_nested_types();
        bool first_nested = true;
        for (auto* nested_cls : nested) {
            WriteClassJson(out, nested_cls, indent + 2, first_nested);
        }
        if (!nested.empty()) out << "\n" << inner;
        out << "]\n";
        out << pad << "}";
    }

    void WriteImageJson(std::ostream& out, RridImage* img, int indent, bool& first_image) {
        const std::string pad   = Indent(indent);
        const std::string inner = Indent(indent + 1);

        if (!first_image) out << ",\n";
        first_image = false;

        out << pad << "{\n";
        out << inner << "\"name\": \"" << JsonEscape(img->get_name()) << "\",\n";
        out << inner << "\"moduleBase\": " << HexJson(img->get_module_base()) << ",\n";
        out << inner << "\"imageRva\": " << HexJson(img->get_image_rva()) << ",\n";
        out << inner << "\"classes\": [";

        bool first_class = true;
        for (auto* cls : img->get_classes()) {
            WriteClassJson(out, cls, indent + 2, first_class);
        }
        if (!img->get_classes().empty()) out << "\n" << inner;
        out << "]\n";
        out << pad << "}";
    }

    bool WriteFormat(const std::filesystem::path& path,
                     const std::vector<RridImage*>& images,
                     const std::function<void(std::ostream&, RridImage*, int)>& write_image,
                     const std::function<void(std::ostream&)>& write_preamble,
                     const std::function<void(std::ostream&)>& write_open,
                     const std::function<void(std::ostream&)>& write_close) {

        std::ofstream out(path);
        if (!out.is_open()) {
            return false;
        }

        write_preamble(out);
        write_open(out);
        for (auto* img : images) {
            write_image(out, img, 0);
        }
        write_close(out);
        out.close();
        return true;
    }

    bool WriteJson(const std::filesystem::path& path, const std::vector<RridImage*>& images) {
        std::ofstream out(path);
        if (!out.is_open()) {
            return false;
        }

        out << "{\n";
        out << "  \"timestamp\": \"" << JsonEscape(CurrentTimestamp()) << "\",\n";
        out << "  \"module\": \"" << JsonEscape(rrid::get_module_name()) << "\",\n";
        out << "  \"images\": [";

        bool first_image = true;
        for (auto* img : images) {
            WriteImageJson(out, img, 2, first_image);
        }

        if (!images.empty()) out << "\n  ";
        out << "]\n}\n";
        out.close();
        return true;
    }

    void WriteReadme(const std::filesystem::path& path) {
        std::ofstream out(path);
        if (!out.is_open()) {
            return;
        }

        out << "IL2CPP Dump - " << CurrentTimestamp() << "\n";
        out << "================================\n\n";
        out << "This folder contains the same IL2CPP metadata in multiple formats.\n";
        out << "Pick whichever file matches your project language.\n\n";
        out << "Files:\n";
        out << "  GameDump.hpp  - C/C++   (#include and use constexpr offsets)\n";
        out << "  GameDump.cs   - C#      (reference in your mod / tool project)\n";
        out << "  GameDump.rs   - Rust    (pub const values in nested modules)\n";
        out << "  GameDump.py   - Python  (class attributes for scripting)\n";
        out << "  GameDump.json - JSON    (for tools, scripts, or custom parsers)\n\n";
        out << "Suffix guide:\n";
        out << "  *_RVA on methods/static fields = offset from GameAssembly.dll base\n";
        out << "  *_Offset on instance fields    = offset from the object pointer\n";
        out << "  ClassRVA / CLASS_RVA           = Il2CppClass* address offset\n";
    }

}

bool GameDumper::DumpAll(const std::string& output_dir,
                         std::function<void(const std::string&)> logCallback) {

    auto log = [&](const std::string& msg) {
        if (logCallback) logCallback(msg);
        std::cout << msg << std::endl;
    };

    if (!rrid::init()) {
        log("[!] rrid::init failed");
        return false;
    }
    log("[+] rrid ready");

    std::filesystem::path dir(output_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    auto images = rrid::get_images();
    log("[*] " + std::to_string(images.size()) + " images");

    size_t i = 0;
    for (auto* img : images) {
        ++i;
        log("[*] (" + std::to_string(i) + "/" + std::to_string(images.size()) +
            ") " + img->get_name());
    }

    const auto cpp_path    = dir / "GameDump.hpp";
    const auto cs_path     = dir / "GameDump.cs";
    const auto rs_path     = dir / "GameDump.rs";
    const auto py_path     = dir / "GameDump.py";
    const auto json_path   = dir / "GameDump.json";
    const auto readme_path = dir / "README.txt";

    log("[*] writing C++...");
    if (!WriteFormat(cpp_path, images,
        [](std::ostream& out, RridImage* img, int indent) { WriteImageCpp(out, img, indent); },
        [](std::ostream& out) {
            out << "// IL2CPP dump - " << CurrentTimestamp() << "\n";
            out << "// values are RVAs / offsets vs the image module base.\n";
            out << "#pragma once\n#include <cstdint>\n\n";
        },
        [](std::ostream& out) { out << "namespace GameDump {\n\n"; },
        [](std::ostream& out) { out << "} // GameDump\n"; })) {
        log("[!] cant open " + cpp_path.string());
        return false;
    }

    log("[*] writing C#...");
    if (!WriteFormat(cs_path, images,
        [](std::ostream& out, RridImage* img, int indent) { WriteImageCSharp(out, img, indent); },
        [](std::ostream& out) {
            out << "// IL2CPP dump - " << CurrentTimestamp() << "\n";
            out << "// values are RVAs / offsets vs the image module base.\n\n";
        },
        [](std::ostream& out) { out << "namespace GameDump\n{\n\n"; },
        [](std::ostream& out) { out << "}\n"; })) {
        log("[!] cant open " + cs_path.string());
        return false;
    }

    log("[*] writing Rust...");
    if (!WriteFormat(rs_path, images,
        [](std::ostream& out, RridImage* img, int indent) { WriteImageRust(out, img, indent); },
        [](std::ostream& out) {
            out << "// IL2CPP dump - " << CurrentTimestamp() << "\n";
            out << "// values are RVAs / offsets vs the image module base.\n\n";
        },
        [](std::ostream& out) { out << "pub mod gamedump {\n\n"; },
        [](std::ostream& out) { out << "}\n"; })) {
        log("[!] cant open " + rs_path.string());
        return false;
    }

    log("[*] writing Python...");
    if (!WriteFormat(py_path, images,
        [](std::ostream& out, RridImage* img, int indent) { WriteImagePython(out, img, indent); },
        [](std::ostream& out) {
            out << "# IL2CPP dump - " << CurrentTimestamp() << "\n";
            out << "# values are RVAs / offsets vs the image module base.\n\n";
        },
        [](std::ostream& out) { out << "class GameDump:\n\n"; },
        [](std::ostream& out) { (void)out; })) {
        log("[!] cant open " + py_path.string());
        return false;
    }

    log("[*] writing JSON...");
    if (!WriteJson(json_path, images)) {
        log("[!] cant open " + json_path.string());
        return false;
    }

    WriteReadme(readme_path);

    log("[+] wrote " + dir.string());
    log("    GameDump.hpp  (C++)");
    log("    GameDump.cs   (C#)");
    log("    GameDump.rs   (Rust)");
    log("    GameDump.py   (Python)");
    log("    GameDump.json (JSON)");
    log("    README.txt");
    return true;
}
