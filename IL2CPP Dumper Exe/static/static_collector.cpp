#define _CRT_SECURE_NO_WARNINGS
#include "static_collector.h"
#include "il2cpp_binary.h"
#include "metadata.h"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace static_dump {
namespace {

// Match .NET / RRID access & attribute bits used by the runtime dumper emitters.
constexpr uint32_t FIELD_STATIC = 0x0010;
constexpr uint32_t FIELD_LITERAL = 0x0040;
constexpr uint32_t METHOD_ACCESS_MASK = 0x0007;
constexpr uint32_t METHOD_STATIC = 0x0010;
constexpr uint32_t METHOD_FINAL = 0x0020;
constexpr uint32_t METHOD_VIRTUAL = 0x0040;
constexpr uint32_t METHOD_ABSTRACT = 0x0400;
constexpr uint32_t METHOD_NEWSLOT = 0x0100;
constexpr uint32_t METHOD_PINVOKE = 0x2000;

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

std::string UniqueName(std::unordered_set<std::string>& used, std::string base) {
    if (base.empty()) base = "_";
    std::string candidate = base;
    int n = 2;
    while (used.count(candidate)) candidate = base + "_" + std::to_string(n++);
    used.insert(candidate);
    return candidate;
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

std::string AccessLevel(uint32_t flags) {
    switch (flags & METHOD_ACCESS_MASK) {
        case 0x0001: return "private";
        case 0x0002: return "private protected";
        case 0x0003: return "internal";
        case 0x0004: return "protected";
        case 0x0005: return "protected internal";
        case 0x0006: return "public";
        default: return "private";
    }
}

std::string FieldModifiers(uint32_t flags) {
    std::string m;
    if (flags & FIELD_STATIC) m += "static ";
    if (flags & FIELD_LITERAL) m += "const ";
    if (flags & 0x0020) m += "readonly ";
    return m;
}

std::string MethodModifiers(uint32_t flags) {
    std::string m;
    if (flags & METHOD_STATIC) m += "static ";
    if (flags & METHOD_ABSTRACT) m += "abstract ";
    if (flags & METHOD_VIRTUAL) m += "virtual ";
    if (flags & METHOD_FINAL) m += "sealed ";
    if (flags & METHOD_NEWSLOT) m += "new ";
    if (flags & METHOD_PINVOKE) m += "extern ";
    return m;
}

std::vector<uint8_t> ReadFileBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const auto sz = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if (sz > 0) in.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

std::string TypeFullName(const Metadata& meta, const TypeDef& td) {
    const std::string ns = meta.GetString(td.namespaceIndex);
    const std::string name = meta.GetString(td.nameIndex);
    return ns.empty() ? name : (ns + "." + name);
}

int64_t ReadEnumDefault(const Metadata& meta, const Il2CppBinary& bin, const FieldDefaultValue& dv) {
    if (dv.dataIndex < 0) return 0;
    const size_t off = static_cast<size_t>(meta.defaultValueDataOffset) + static_cast<size_t>(dv.dataIndex);
    if (off + 8 > bin.data.size()) return 0;
    // Prefer type name; fall back to 4-byte int
    std::string ty = bin.GetTypeName(dv.typeIndex, meta);
    int64_t v = 0;
    if (ty == "System.Byte" || ty == "System.SByte" || ty == "System.Boolean") {
        v = static_cast<int8_t>(bin.data[off]);
    } else if (ty == "System.Int16" || ty == "System.UInt16" || ty == "System.Char") {
        int16_t x; std::memcpy(&x, bin.data.data() + off, 2); v = x;
    } else if (ty == "System.Int64" || ty == "System.UInt64") {
        std::memcpy(&v, bin.data.data() + off, 8);
    } else {
        int32_t x; std::memcpy(&x, bin.data.data() + off, 4); v = x;
    }
    return v;
}

void AnnotateProperties(DumpClass& cls) {
    std::unordered_map<std::string, DumpProperty> props;
    for (auto& m : cls.methods) {
        const auto& n = m.name;
        if (n.size() > 4 && n.compare(0, 4, "get_") == 0 && m.params.empty()) {
            m.is_getter = true;
            m.property_name = n.substr(4);
            props[m.property_name].name = m.property_name;
            props[m.property_name].getter_symbol = m.symbol;
        } else if (n.size() > 4 && n.compare(0, 4, "set_") == 0 && m.params.size() == 1) {
            m.is_setter = true;
            m.property_name = n.substr(4);
            props[m.property_name].name = m.property_name;
            props[m.property_name].setter_symbol = m.symbol;
        }
    }
    for (auto& kv : props) cls.properties.push_back(std::move(kv.second));
}

DumpClass CollectClass(int typeIndex,
                       const std::string& imageName,
                       const std::string& classPath,
                       Metadata& meta,
                       Il2CppBinary& bin,
                       std::vector<DumpIndexEntry>& index,
                       const DumpConfig& cfg,
                       const std::unordered_set<int>& nestedSet) {
    const TypeDef& td = meta.typeDefs[static_cast<size_t>(typeIndex)];
    DumpClass out;
    out.name = meta.GetString(td.nameIndex);
    out.namespaze = meta.GetString(td.namespaceIndex);
    out.full_name = TypeFullName(meta, td);
    out.type_name = out.full_name;
    out.kind = td.IsEnum() ? "enum" : (td.IsValueType() ? "struct" : "class");
    out.is_generic = td.genericContainerIndex >= 0;
    out.class_rva = 0; // static dump has no live Il2CppClass*
    out.ident = SanitizeIdent(out.namespaze.empty() ? out.name : (out.namespaze + "_" + out.name));
    out.path = classPath.empty() ? out.ident : (classPath + "::" + out.ident);

    if (td.parentIndex >= 0) {
        out.parent = bin.GetTypeName(td.parentIndex, meta);
    }
    for (uint16_t i = 0; i < td.interfaces_count; ++i) {
        const int idx = td.interfacesStart + i;
        if (idx >= 0 && idx < static_cast<int>(meta.interfaceIndices.size())) {
            out.interfaces.push_back(bin.GetTypeName(meta.interfaceIndices[static_cast<size_t>(idx)], meta));
        }
    }

    {
        DumpIndexEntry e;
        e.kind = "class";
        e.full_name = out.full_name;
        e.symbol = out.path + "::ClassRVA";
        e.image = imageName;
        e.rva = out.class_rva;
        index.push_back(std::move(e));
    }

    std::unordered_set<std::string> used_fields{"ClassRVA"};
    for (uint16_t fi = 0; fi < td.field_count; ++fi) {
        const int fidx = td.fieldStart + fi;
        if (fidx < 0 || fidx >= static_cast<int>(meta.fieldDefs.size())) continue;
        const FieldDef& fd = meta.fieldDefs[static_cast<size_t>(fidx)];

        DumpField df;
        df.name = meta.GetString(fd.nameIndex);
        df.type = bin.GetTypeName(fd.typeIndex, meta);
        // Field attrs live on Il2CppType.attrs in binary; approximate from type bits
        if (fd.typeIndex >= 0 && fd.typeIndex < static_cast<int>(bin.types.size())) {
            df.flags = bin.types[static_cast<size_t>(fd.typeIndex)].attrs;
        }
        df.is_enum_value = out.kind == "enum" && (df.flags & FIELD_STATIC) && (df.flags & FIELD_LITERAL);
        df.is_static = (df.flags & FIELD_STATIC) != 0;
        df.offset = 0;
        df.rva = 0;

        const int32_t off = bin.GetFieldOffset(typeIndex, fi, meta);
        if (off >= 0) {
            if (df.is_static) {
                // Offline: static field "offset" is often an index into static_fields; keep as offset, RVA unknown
                df.offset = static_cast<uint64_t>(off);
            } else {
                df.offset = static_cast<uint64_t>(off);
            }
        }

        FieldDefaultValue dv;
        if (df.is_enum_value && meta.GetFieldDefault(fidx, dv)) {
            df.enum_value = ReadEnumDefault(meta, bin, dv);
        }

        {
            std::ostringstream sig;
            sig << AccessLevel(df.flags) << ' ' << FieldModifiers(df.flags)
                << PrettyType(df.type) << ' ' << df.name;
            df.signature = sig.str();
        }

        if (df.is_enum_value) {
            df.symbol = UniqueName(used_fields, SanitizeIdent(df.name));
        } else {
            const std::string suffix = df.is_static ? "_RVA" : "_Offset";
            df.symbol = UniqueName(used_fields, SanitizeIdent(df.name) + suffix);
            // For static offline dumps, emit the static field data offset under _RVA slot when positive
            if (df.is_static && df.rva == 0 && df.offset) {
                df.rva = df.offset;
            }
        }
        out.fields.push_back(df);

        if (!df.is_enum_value) {
            DumpIndexEntry e;
            e.kind = df.is_static ? "staticField" : "instanceField";
            e.full_name = out.full_name + "." + df.name;
            e.symbol = out.path + "::" + df.symbol;
            e.image = imageName;
            e.signature = df.signature;
            e.rva = df.rva;
            e.offset = df.offset;
            index.push_back(std::move(e));
        }
    }

    std::unordered_set<std::string> used_methods{"ClassRVA"};
    for (uint16_t mi = 0; mi < td.method_count; ++mi) {
        const int midx = td.methodStart + mi;
        if (midx < 0 || midx >= static_cast<int>(meta.methodDefs.size())) continue;
        const MethodDef& md = meta.methodDefs[static_cast<size_t>(midx)];

        DumpMethod dm;
        dm.name = meta.GetString(md.nameIndex);
        dm.return_type = bin.GetTypeName(md.returnType, meta);
        dm.flags = md.flags;
        dm.token = md.token;
        dm.vtable_slot = md.slot == 0xFFFF ? -1 : static_cast<int32_t>(md.slot);
        dm.is_generic = md.genericContainerIndex >= 0;
        dm.rva = bin.GetMethodRVA(imageName, md);

        for (uint16_t pi = 0; pi < md.parameterCount; ++pi) {
            const int pidx = md.parameterStart + pi;
            if (pidx < 0 || pidx >= static_cast<int>(meta.parameterDefs.size())) continue;
            const ParamDef& pd = meta.parameterDefs[static_cast<size_t>(pidx)];
            DumpParam p;
            p.type = bin.GetTypeName(pd.typeIndex, meta);
            p.name = meta.GetString(pd.nameIndex);
            if (p.name.empty()) p.name = "p" + std::to_string(pi);
            dm.params.push_back(std::move(p));
        }

        {
            std::ostringstream sig;
            sig << AccessLevel(dm.flags) << ' ' << MethodModifiers(dm.flags)
                << PrettyType(dm.return_type) << ' ' << dm.name << '(';
            for (size_t i = 0; i < dm.params.size(); ++i) {
                if (i) sig << ", ";
                sig << PrettyType(dm.params[i].type) << ' ' << dm.params[i].name;
            }
            sig << ')';
            dm.signature = sig.str();
        }

        std::string base = SanitizeIdent(dm.name);
        if (dm.params.empty()) base += "_0";
        for (const auto& p : dm.params) base += "_" + SanitizeIdent(PrettyType(p.type));
        base += "_RVA";
        dm.symbol = UniqueName(used_methods, base);
        out.methods.push_back(dm);

        DumpIndexEntry e;
        e.kind = "method";
        e.full_name = out.full_name + "." + dm.name;
        e.symbol = out.path + "::" + dm.symbol;
        e.image = imageName;
        e.signature = dm.signature;
        e.rva = dm.rva;
        e.token = dm.token;
        index.push_back(std::move(e));
    }

    AnnotateProperties(out);

    for (uint16_t ni = 0; ni < td.nested_type_count; ++ni) {
        const int nidx = td.nestedTypesStart + ni;
        if (nidx < 0 || nidx >= static_cast<int>(meta.nestedTypeIndices.size())) continue;
        const int nestedType = meta.nestedTypeIndices[static_cast<size_t>(nidx)];
        if (nestedType < 0 || nestedType >= static_cast<int>(meta.typeDefs.size())) continue;
        if (!nestedSet.count(nestedType)) continue;
        out.nested.push_back(CollectClass(nestedType, imageName, out.path, meta, bin, index, cfg, nestedSet));
    }

    (void)cfg;
    return out;
}

size_t CountClasses(const DumpClass& cls) {
    size_t n = 1;
    for (const auto& nested : cls.nested) n += CountClasses(nested);
    return n;
}

size_t CountMethods(const DumpClass& cls) {
    size_t n = cls.methods.size();
    for (const auto& nested : cls.nested) n += CountMethods(nested);
    return n;
}

} // namespace

bool Collect(const std::string& assembly_path,
             const std::string& metadata_path,
             const DumpConfig& cfg,
             DumpRoot& out,
             std::string& error_out,
             std::function<void(const std::string&)> log) {
    auto say = [&](const std::string& m) { if (log) log(m); };
    try {
        say("[*] loading metadata: " + metadata_path);
        Metadata meta(ReadFileBytes(metadata_path));
        say("[+] metadata version " + std::to_string(meta.version));
        say("[*] types=" + std::to_string(meta.typeDefs.size()) +
            " methods=" + std::to_string(meta.methodDefs.size()) +
            " images=" + std::to_string(meta.images.size()));

        say("[*] loading binary: " + assembly_path);
        Il2CppBinary bin(ReadFileBytes(assembly_path));
        say("[*] searching CodeRegistration / MetadataRegistration...");
        if (!bin.PlusSearch(meta)) {
            error_out = "failed to locate IL2CPP registrations in binary (unsupported packer or encrypted metadata)";
            return false;
        }
        say("[+] registrations ready (effective version " + std::to_string(bin.Version) + ")");

        out = DumpRoot{};
        out.timestamp = CurrentTimestamp();
        out.module = std::filesystem::path(assembly_path).filename().string();
        out.spoofer = "static";
        out.fingerprint.module_name = out.module;
        out.fingerprint.size_of_image = bin.SizeOfImage;
        out.fingerprint.time_date_stamp = bin.TimeDateStamp;
        {
            std::ostringstream os;
            os << "metadata v" << meta.version;
            out.fingerprint.unity_hint = os.str();
        }
        out.fingerprint.image_count = meta.images.size();

        // Nested type indices for skipping nested at image root
        std::unordered_set<int> allNested;
        for (const auto& td : meta.typeDefs) {
            for (uint16_t i = 0; i < td.nested_type_count; ++i) {
                const int nidx = td.nestedTypesStart + i;
                if (nidx >= 0 && nidx < static_cast<int>(meta.nestedTypeIndices.size())) {
                    allNested.insert(meta.nestedTypeIndices[static_cast<size_t>(nidx)]);
                }
            }
        }

        for (const auto& img : meta.images) {
            const std::string imageName = meta.GetString(img.nameIndex);
            if (!dump_config::ImageAllowed(cfg, imageName)) continue;

            say("[*] image " + imageName + " (" + std::to_string(img.typeCount) + " types)");
            DumpImage di;
            di.name = imageName;
            di.ident = SanitizeIdent(imageName);
            di.image_rva = 0;

            for (uint32_t ti = 0; ti < img.typeCount; ++ti) {
                const int typeIndex = img.typeStart + static_cast<int>(ti);
                if (typeIndex < 0 || typeIndex >= static_cast<int>(meta.typeDefs.size())) continue;
                if (allNested.count(typeIndex)) continue; // emitted under declaring type
                di.classes.push_back(CollectClass(typeIndex, imageName, di.ident, meta, bin, out.index, cfg, allNested));
            }
            out.images.push_back(std::move(di));
        }

        out.fingerprint.filtered_image_count = out.images.size();
        for (const auto& img : out.images) {
            for (const auto& cls : img.classes) {
                out.fingerprint.class_count += CountClasses(cls);
                out.fingerprint.method_count += CountMethods(cls);
            }
        }

        if (cfg.emit_strings) {
            for (const auto& lit : meta.stringLiterals) {
                if (lit.length == 0) continue;
                if (lit.length < cfg.string_min_length) continue;
                if (out.strings.size() >= cfg.string_max_count) break;
                const size_t off = static_cast<size_t>(meta.stringLiteralDataOffset) + static_cast<size_t>(lit.dataIndex);
                if (off + lit.length > bin.data.size() && off + lit.length > meta.stream.data.size()) {
                    // literal data is in metadata file
                }
                std::string value;
                if (off + lit.length <= meta.stream.data.size()) {
                    value.assign(reinterpret_cast<const char*>(meta.stream.data.data() + off), lit.length);
                }
                if (value.empty()) continue;
                DumpStringEntry se;
                se.value = std::move(value);
                se.source = "metadataLiteral";
                out.strings.push_back(std::move(se));
            }
        }

        say("[*] collected " + std::to_string(out.images.size()) + " images, " +
            std::to_string(out.index.size()) + " index entries");
        return true;
    } catch (const std::exception& ex) {
        error_out = ex.what();
        return false;
    }
}

} // namespace static_dump
