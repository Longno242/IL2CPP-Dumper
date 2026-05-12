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

    std::string Hex(uint64_t v) {
        std::ostringstream os;
        os << "0x" << std::uppercase << std::hex << v;
        return os.str();
    }

    // C# short names for the common BCL types, just for readable comments.
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

    void WriteClass(std::ofstream& out, RridClass* cls) {
        const std::string ns    = cls->get_namespace();
        const std::string cname = cls->get_name();
        const std::string full  = ns.empty() ? cname : (ns + "." + cname);

        const char* kind = "class";
        if (cls->is_enum())           kind = "enum";
        else if (cls->is_valuetype()) kind = "struct";

        out << "    // " << kind << " " << full
            << "  (ClassRVA " << Hex(cls->get_class_rva()) << ")\n";

        const std::string cls_ident = SanitizeIdent(
            ns.empty() ? cname : (ns + "_" + cname));

        out << "    namespace " << cls_ident << " {\n";
        out << "        constexpr uint64_t ClassRVA = " << Hex(cls->get_class_rva()) << ";\n";

        auto fields = cls->get_fields();
        if (!fields.empty()) {
            out << "\n        // fields\n";
            std::unordered_set<std::string> used{"ClassRVA"};

            for (auto* f : fields) {
                const bool        is_static = f->has_rva();
                const std::string suffix    = is_static ? "_RVA" : "_Offset";
                const std::string ident     = UniqueName(used, SanitizeIdent(f->get_name()) + suffix);
                const uint64_t    value     = is_static ? f->get_static_rva() : f->get_offset();

                out << "        constexpr uint64_t " << ident
                    << " = " << Hex(value) << "; // " << BuildFieldSignature(f) << "\n";
            }
        }

        auto methods = cls->get_methods();
        if (!methods.empty()) {
            out << "\n        // methods\n";
            std::unordered_set<std::string> used{"ClassRVA"};

            for (auto* m : methods) {
                const std::string ident = UniqueName(used, SanitizeIdent(m->get_name()) + "_RVA");

                out << "        constexpr uint64_t " << ident
                    << " = " << Hex(m->get_method_rva()) << "; // " << BuildSignature(m) << "\n";
            }
        }

        out << "    }\n\n";
    }

    void WriteImage(std::ofstream& out, RridImage* img) {
        out << "// ==== image: " << img->get_name()
            << "   ModuleBase " << Hex(img->get_module_base())
            << "   ImageRVA " << Hex(img->get_image_rva()) << "\n";

        const std::string img_ident = SanitizeIdent(img->get_name());
        out << "namespace " << img_ident << " {\n\n";
        out << "    constexpr uint64_t ModuleBase = " << Hex(img->get_module_base()) << ";\n";
        out << "    constexpr uint64_t ImageRVA   = " << Hex(img->get_image_rva())   << ";\n\n";

        for (auto* cls : img->get_classes()) {
            WriteClass(out, cls);
        }

        out << "} // " << img_ident << "\n\n";
    }

}

bool GameDumper::DumpAll(const std::string& output_file,
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

    std::filesystem::path out_path(output_file);
    if (out_path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(out_path.parent_path(), ec);
    }

    std::ofstream out(out_path);
    if (!out.is_open()) {
        log("[!] cant open " + out_path.string());
        return false;
    }

    out << "// IL2CPP dump - " << CurrentTimestamp() << "\n";
    out << "// values are RVAs / offsets vs the image module base.\n";
    out << "#pragma once\n";
    out << "#include <cstdint>\n\n";
    out << "namespace GameDump {\n\n";

    auto images = rrid::get_images();
    log("[*] " + std::to_string(images.size()) + " images");

    size_t i = 0;
    for (auto* img : images) {
        ++i;
        log("[*] (" + std::to_string(i) + "/" + std::to_string(images.size()) +
            ") " + img->get_name());
        WriteImage(out, img);
    }

    out << "} // GameDump\n";
    out.close();

    log("[+] wrote " + out_path.string());
    return true;
}
