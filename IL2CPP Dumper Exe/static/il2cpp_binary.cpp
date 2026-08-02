#define _CRT_SECURE_NO_WARNINGS
#include "il2cpp_binary.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace static_dump {
namespace {

enum Il2CppTypeEnum : uint8_t {
    IL2CPP_TYPE_END = 0x00,
    IL2CPP_TYPE_VOID = 0x01,
    IL2CPP_TYPE_BOOLEAN = 0x02,
    IL2CPP_TYPE_CHAR = 0x03,
    IL2CPP_TYPE_I1 = 0x04,
    IL2CPP_TYPE_U1 = 0x05,
    IL2CPP_TYPE_I2 = 0x06,
    IL2CPP_TYPE_U2 = 0x07,
    IL2CPP_TYPE_I4 = 0x08,
    IL2CPP_TYPE_U4 = 0x09,
    IL2CPP_TYPE_I8 = 0x0a,
    IL2CPP_TYPE_U8 = 0x0b,
    IL2CPP_TYPE_R4 = 0x0c,
    IL2CPP_TYPE_R8 = 0x0d,
    IL2CPP_TYPE_STRING = 0x0e,
    IL2CPP_TYPE_PTR = 0x0f,
    IL2CPP_TYPE_BYREF = 0x10,
    IL2CPP_TYPE_VALUETYPE = 0x11,
    IL2CPP_TYPE_CLASS = 0x12,
    IL2CPP_TYPE_VAR = 0x13,
    IL2CPP_TYPE_ARRAY = 0x14,
    IL2CPP_TYPE_GENERICINST = 0x15,
    IL2CPP_TYPE_TYPEDBYREF = 0x16,
    IL2CPP_TYPE_I = 0x18,
    IL2CPP_TYPE_U = 0x19,
    IL2CPP_TYPE_FNPTR = 0x1b,
    IL2CPP_TYPE_OBJECT = 0x1c,
    IL2CPP_TYPE_SZARRAY = 0x1d,
    IL2CPP_TYPE_MVAR = 0x1e,
};

const uint8_t kMscorlibFeature[] = {
    'm','s','c','o','r','l','i','b','.','d','l','l',0
};

std::vector<size_t> FindPattern(const uint8_t* hay, size_t hayLen, const uint8_t* needle, size_t nlen) {
    std::vector<size_t> hits;
    if (nlen == 0 || hayLen < nlen) return hits;
    for (size_t i = 0; i + nlen <= hayLen; ++i) {
        if (std::memcmp(hay + i, needle, nlen) == 0) hits.push_back(i);
    }
    return hits;
}

} // namespace

Il2CppBinary::Il2CppBinary(std::vector<uint8_t> buf) : BinaryStream(std::move(buf)) {
    ParsePE();
}

void Il2CppBinary::ParsePE() {
    Seek(0);
    if (ReadU16() != 0x5A4D) throw std::runtime_error("not a PE file (missing MZ)");
    Seek(0x3C);
    const uint32_t lfanew = ReadU32();
    Seek(lfanew);
    if (ReadU32() != 0x00004550) throw std::runtime_error("not a PE file (missing PE signature)");

    const uint16_t machine = ReadU16();
    const uint16_t numberOfSections = ReadU16();
    TimeDateStamp = ReadU32();
    ReadU32(); // PointerToSymbolTable
    ReadU32(); // NumberOfSymbols
    const uint16_t sizeOfOptionalHeader = ReadU16();
    ReadU16(); // Characteristics

    const size_t optStart = Position;
    const uint16_t magic = ReadU16();
    Seek(optStart);
    if (magic == 0x10B) {
        Is32Bit = true;
        ReadU16(); // magic
        ReadU8(); ReadU8(); // linker version
        ReadU32(); ReadU32(); ReadU32(); // SizeOfCode / SizeOfInitializedData / SizeOfUninitializedData
        ReadU32(); // AddressOfEntryPoint
        ReadU32(); // BaseOfCode
        ReadU32(); // BaseOfData
        ImageBase = ReadU32();
        ReadU32(); ReadU32(); // SectionAlignment / FileAlignment
        ReadU16(); ReadU16(); ReadU16(); ReadU16(); ReadU16(); ReadU16();
        ReadU32(); // Win32VersionValue
        SizeOfImage = ReadU32();
    } else if (magic == 0x20B) {
        Is32Bit = false;
        ReadU16(); // magic
        ReadU8(); ReadU8();
        ReadU32(); ReadU32(); ReadU32();
        ReadU32(); // AddressOfEntryPoint
        ReadU32(); // BaseOfCode
        ImageBase = ReadU64();
        ReadU32(); ReadU32();
        ReadU16(); ReadU16(); ReadU16(); ReadU16(); ReadU16(); ReadU16();
        ReadU32();
        SizeOfImage = ReadU32();
    } else {
        throw std::runtime_error("unsupported PE optional header");
    }
    (void)machine;

    Seek(optStart + sizeOfOptionalHeader);
    sections.clear();
    sections.reserve(numberOfSections);
    execSecs.clear();
    dataSecs.clear();
    for (uint16_t i = 0; i < numberOfSections; ++i) {
        PeSection s;
        auto nameBytes = ReadBytes(8);
        std::memcpy(s.name, nameBytes.data(), 8);
        s.name[8] = 0;
        s.VirtualSize = ReadU32();
        s.VirtualAddress = ReadU32();
        s.SizeOfRawData = ReadU32();
        s.PointerToRawData = ReadU32();
        ReadU32(); // relocs
        ReadU32(); // linenumbers
        ReadU16(); ReadU16();
        s.Characteristics = ReadU32();
        sections.push_back(s);

        SearchSection ss;
        ss.offset = s.PointerToRawData;
        ss.offsetEnd = s.PointerToRawData + s.SizeOfRawData;
        ss.address = ImageBase + s.VirtualAddress;
        ss.addressEnd = ImageBase + s.VirtualAddress + s.VirtualSize;

        if (s.Characteristics == 0x60000020) {
            execSecs.push_back(ss);
        } else if (s.Characteristics == 0x40000040 || s.Characteristics == 0xC0000040) {
            dataSecs.push_back(ss);
        }
    }
}

uint64_t Il2CppBinary::MapVATR(uint64_t absAddr) const {
    if (absAddr < ImageBase) return 0;
    const uint64_t rva = absAddr - ImageBase;
    for (const auto& s : sections) {
        if (rva >= s.VirtualAddress && rva < s.VirtualAddress + std::max(s.VirtualSize, s.SizeOfRawData)) {
            return (rva - s.VirtualAddress) + s.PointerToRawData;
        }
    }
    return 0;
}

uint64_t Il2CppBinary::GetRVA(uint64_t pointer) const {
    if (pointer < ImageBase) return pointer;
    return pointer - ImageBase;
}

bool Il2CppBinary::InExecVa(uint64_t va) const {
    for (const auto& s : execSecs) if (va >= s.address && va < s.addressEnd) return true;
    return false;
}

bool Il2CppBinary::InDataVa(uint64_t va) const {
    for (const auto& s : dataSecs) if (va >= s.address && va < s.addressEnd) return true;
    return false;
}

bool Il2CppBinary::CheckPointerRangeExecVa(const std::vector<uint64_t>& ptrs) const {
    for (auto p : ptrs) if (p && !InExecVa(p)) return false;
    return true;
}

bool Il2CppBinary::CheckPointerRangeDataVa(const std::vector<uint64_t>& ptrs) const {
    for (auto p : ptrs) if (p && !InDataVa(p)) return false;
    return true;
}

bool Il2CppBinary::CheckPointerRangeDataRa(uint64_t fileOff) const {
    for (const auto& s : dataSecs) {
        if (fileOff >= s.offset && fileOff < s.offsetEnd) return true;
    }
    return false;
}

std::vector<uint64_t> Il2CppBinary::FindReferences(uint64_t va) const {
    std::vector<uint64_t> refs;
    const size_t step = PointerSize();
    for (const auto& sec : dataSecs) {
        if (sec.offsetEnd <= sec.offset) continue;
        const uint64_t end = std::min(sec.offsetEnd, static_cast<uint64_t>(data.size()));
        for (uint64_t off = sec.offset; off + step <= end; off += step) {
            uint64_t val = 0;
            if (Is32Bit) {
                uint32_t v;
                std::memcpy(&v, data.data() + off, 4);
                val = v;
            } else {
                std::memcpy(&val, data.data() + off, 8);
            }
            if (val == va) {
                refs.push_back(sec.address + (off - sec.offset));
            }
        }
    }
    return refs;
}

uint64_t Il2CppBinary::FindCodeRegistrationOld(int methodCount) {
    const size_t step = PointerSize();
    for (const auto& section : dataSecs) {
        for (uint64_t addr = section.offset; addr + step <= section.offsetEnd; addr += step) {
            Position = static_cast<size_t>(addr);
            try {
                if (ReadIntPtr() != methodCount) continue;
                const uint64_t pointer = MapVATR(ReadUIntPtr());
                if (!CheckPointerRangeDataRa(pointer)) continue;
                Position = static_cast<size_t>(pointer);
                std::vector<uint64_t> pointers;
                pointers.reserve(methodCount);
                for (int i = 0; i < methodCount; ++i) pointers.push_back(ReadUIntPtr());
                if (CheckPointerRangeExecVa(pointers)) {
                    return addr - section.offset + section.address;
                }
            } catch (...) {
            }
            Position = static_cast<size_t>(addr + step);
        }
    }
    return 0;
}

uint64_t Il2CppBinary::FindCodeRegistration2019(const std::vector<SearchSection>& secs, int imageCount, double ver) {
    for (const auto& sec : secs) {
        if (sec.offsetEnd <= sec.offset) continue;
        const size_t len = static_cast<size_t>(sec.offsetEnd - sec.offset);
        if (sec.offset + len > data.size()) continue;
        auto hits = FindPattern(data.data() + sec.offset, len, kMscorlibFeature, sizeof(kMscorlibFeature));
        for (size_t index : hits) {
            const uint64_t dllva = sec.address + index;
            for (uint64_t refva : FindReferences(dllva)) {
                for (uint64_t refva2 : FindReferences(refva)) {
                    if (ver >= 27.0) {
                        for (int i = imageCount - 1; i >= 0; --i) {
                            const uint64_t maybeModules = refva2 - static_cast<uint64_t>(i) * PointerSize();
                            for (uint64_t refva3 : FindReferences(maybeModules)) {
                                try {
                                    Position = static_cast<size_t>(MapVATR(refva3 - PointerSize()));
                                    if (ReadIntPtr() == imageCount) {
                                        if (ver >= 35.0) return refva3 - PointerSize() * 16;
                                        if (ver >= 29.0) return refva3 - PointerSize() * 14;
                                        return refva3 - PointerSize() * 13;
                                    }
                                } catch (...) {
                                }
                            }
                        }
                    } else {
                        for (int i = 0; i < imageCount; ++i) {
                            for (uint64_t refva3 : FindReferences(refva2 - static_cast<uint64_t>(i) * PointerSize())) {
                                return refva3 - PointerSize() * 13;
                            }
                        }
                    }
                }
            }
        }
    }
    return 0;
}

uint64_t Il2CppBinary::FindMetadataRegistrationOld(int typeDefinitionsCount, long usagesCount) {
    const size_t step = PointerSize();
    for (const auto& section : dataSecs) {
        const uint64_t end = std::min(section.offsetEnd, static_cast<uint64_t>(data.size())) - step;
        for (uint64_t addr = section.offset; addr < end; addr += step) {
            Position = static_cast<size_t>(addr);
            try {
                if (ReadIntPtr() != typeDefinitionsCount) continue;
                Position += PointerSize() * 2;
                const uint64_t pointer = MapVATR(ReadUIntPtr());
                if (!CheckPointerRangeDataRa(pointer)) continue;
                Position = static_cast<size_t>(pointer);
                std::vector<uint64_t> pointers;
                pointers.reserve(static_cast<size_t>(usagesCount));
                for (long i = 0; i < usagesCount; ++i) pointers.push_back(ReadUIntPtr());
                // BSS check softened: accept any non-exec pointers
                bool ok = true;
                for (auto p : pointers) {
                    if (p && InExecVa(p)) { ok = false; break; }
                }
                if (ok) return addr - PointerSize() * 12 - section.offset + section.address;
            } catch (...) {
            }
        }
    }
    return 0;
}

uint64_t Il2CppBinary::FindMetadataRegistrationV21(int typeDefinitionsCount, bool pointerInExec) {
    const size_t step = PointerSize();
    for (const auto& section : dataSecs) {
        const uint64_t end = std::min(section.offsetEnd, static_cast<uint64_t>(data.size())) - step;
        for (uint64_t addr = section.offset; addr < end; addr += step) {
            Position = static_cast<size_t>(addr);
            try {
                if (ReadIntPtr() != typeDefinitionsCount) continue;
                Position += PointerSize();
                if (ReadIntPtr() != typeDefinitionsCount) continue;
                const uint64_t pointer = MapVATR(ReadUIntPtr());
                if (!CheckPointerRangeDataRa(pointer)) continue;
                Position = static_cast<size_t>(pointer);
                std::vector<uint64_t> pointers;
                pointers.reserve(typeDefinitionsCount);
                for (int i = 0; i < typeDefinitionsCount; ++i) pointers.push_back(ReadUIntPtr());
                const bool flag = pointerInExec ? CheckPointerRangeExecVa(pointers) : CheckPointerRangeDataVa(pointers);
                if (flag) return addr - PointerSize() * 10 - section.offset + section.address;
            } catch (...) {
            }
        }
    }
    return 0;
}

uint64_t Il2CppBinary::FindCodeRegistration(const Metadata& meta, bool& pointerInExec) {
    pointerInExec = false;
    if (Version >= 24.2) {
        uint64_t cr = FindCodeRegistration2019(dataSecs, static_cast<int>(meta.images.size()), Version);
        if (cr == 0) {
            cr = FindCodeRegistration2019(execSecs, static_cast<int>(meta.images.size()), Version);
            if (cr) pointerInExec = true;
        }
        return cr;
    }
    return FindCodeRegistrationOld(static_cast<int>(meta.methodDefs.size()));
}

uint64_t Il2CppBinary::FindMetadataRegistration(const Metadata& meta, bool pointerInExec) {
    if (Version < 19.0) return 0;
    if (Version >= 27.0) {
        return FindMetadataRegistrationV21(static_cast<int>(meta.typeDefs.size()), pointerInExec);
    }
    long usages = meta.metadataUsagesCount > 0 ? meta.metadataUsagesCount : 1;
    return FindMetadataRegistrationOld(static_cast<int>(meta.typeDefs.size()), usages);
}

Il2CppBinary::CodeReg Il2CppBinary::ReadCodeRegistration(uint64_t va) {
    CodeReg r;
    Position = static_cast<size_t>(MapVATR(va));
    if (Version <= 24.1) {
        r.methodPointersCount = static_cast<uint64_t>(ReadIntPtr());
        r.methodPointers = ReadUIntPtr();
    }
    if (Version <= 21.0) {
        ReadIntPtr(); ReadUIntPtr(); // delegateWrappersFromNativeToManaged
    }
    if (Version >= 22.0) {
        r.reversePInvokeWrapperCount = static_cast<uint64_t>(ReadIntPtr());
        ReadUIntPtr(); // reversePInvokeWrappers
    }
    if (Version <= 22.0) {
        ReadIntPtr(); ReadUIntPtr(); // managedToNative
        ReadIntPtr(); ReadUIntPtr(); // marshalingFunctions
        if (Version >= 21.0) {
            ReadIntPtr(); ReadUIntPtr(); // ccw
        }
    }
    r.genericMethodPointersCount = static_cast<uint64_t>(ReadIntPtr());
    r.genericMethodPointers = ReadUIntPtr();
    if ((Version >= 24.5 && Version <= 24.5) || Version >= 27.1) {
        ReadUIntPtr(); // genericAdjustorThunks
    }
    r.invokerPointersCount = static_cast<uint64_t>(ReadIntPtr());
    r.invokerPointers = ReadUIntPtr();
    if (Version <= 24.5) {
        r.customAttributeCount = static_cast<uint64_t>(ReadIntPtr());
        r.customAttributeGenerators = ReadUIntPtr();
    }
    if (Version >= 21.0 && Version <= 22.0) {
        ReadIntPtr(); ReadUIntPtr(); // guids
    }
    if (Version >= 22.0) {
        r.unresolvedVirtualCallCount = static_cast<uint64_t>(ReadIntPtr());
        ReadUIntPtr(); // unresolvedVirtualCallPointers
        if (Version >= 29.1) {
            ReadUIntPtr(); // unresolvedInstance
            ReadUIntPtr(); // unresolvedStatic
        }
    }
    if (Version >= 23.0) {
        r.interopDataCount = static_cast<uint64_t>(ReadIntPtr());
        ReadUIntPtr();
    }
    if (Version >= 24.3) {
        ReadIntPtr(); ReadUIntPtr(); // windowsRuntimeFactory
    }
    if (Version >= 24.2) {
        r.codeGenModulesCount = static_cast<uint64_t>(ReadIntPtr());
        r.codeGenModules = ReadUIntPtr();
    }
    return r;
}

Il2CppBinary::MetaReg Il2CppBinary::ReadMetadataRegistration(uint64_t va) {
    MetaReg r;
    Position = static_cast<size_t>(MapVATR(va));
    r.genericClassesCount = ReadIntPtr();
    r.genericClasses = ReadUIntPtr();
    r.genericInstsCount = ReadIntPtr();
    r.genericInsts = ReadUIntPtr();
    r.genericMethodTableCount = ReadIntPtr();
    r.genericMethodTable = ReadUIntPtr();
    r.typesCount = ReadIntPtr();
    r.types = ReadUIntPtr();
    r.methodSpecsCount = ReadIntPtr();
    r.methodSpecs = ReadUIntPtr();
    if (Version <= 16.0) {
        ReadIntPtr(); ReadUIntPtr(); // methodReferences
    }
    r.fieldOffsetsCount = ReadIntPtr();
    r.fieldOffsets = ReadUIntPtr();
    r.typeDefinitionsSizesCount = ReadIntPtr();
    r.typeDefinitionsSizes = ReadUIntPtr();
    if (Version >= 19.0) {
        r.metadataUsagesCount = static_cast<uint64_t>(ReadIntPtr());
        r.metadataUsages = ReadUIntPtr();
    }
    return r;
}

bool Il2CppBinary::PlusSearch(Metadata& meta) {
    Version = meta.version;
    bool pointerInExec = false;
    uint64_t codeRegistration = FindCodeRegistration(meta, pointerInExec);
    uint64_t metadataRegistration = FindMetadataRegistration(meta, pointerInExec);
    if (codeRegistration == 0 || (Version >= 19.0 && metadataRegistration == 0)) {
        return false;
    }
    return InitRegistrations(codeRegistration, metadataRegistration, meta);
}

bool Il2CppBinary::InitRegistrations(uint64_t codeRegistration, uint64_t metadataRegistration, Metadata& meta) {
    const uint64_t limit = 0x50000;
    auto bump = [&](double newVer, uint64_t adjustPtrs) {
        Version = newVer;
        meta.version = newVer;
        codeRegistration -= PointerSize() * adjustPtrs;
    };

    CodeReg cr = ReadCodeRegistration(codeRegistration);
    if (Version == 31.0) {
        if (cr.genericMethodPointersCount > limit) {
            codeRegistration -= PointerSize() * 2;
            cr = ReadCodeRegistration(codeRegistration);
        } else {
            Version = 29.0;
            meta.version = 29.0;
            cr = ReadCodeRegistration(codeRegistration);
        }
    }
    if (Version == 29.0 && cr.genericMethodPointersCount > limit) {
        bump(29.1, 2);
        cr = ReadCodeRegistration(codeRegistration);
    }
    if (Version == 27.0 && (cr.reversePInvokeWrapperCount > limit || cr.invokerPointersCount > limit)) {
        bump(27.1, 1);
        cr = ReadCodeRegistration(codeRegistration);
    }
    if (Version == 24.4 && cr.invokerPointersCount > limit) {
        bump(24.5, 0);
        // adjust address: Il2CppDumper subtracts then re-reads; approximate
        codeRegistration -= PointerSize() * 2;
        cr = ReadCodeRegistration(codeRegistration);
    }
    if (Version == 24.2 && cr.codeGenModules == 0) {
        bump(24.3, 2);
        cr = ReadCodeRegistration(codeRegistration);
    }

    MetaReg mr = ReadMetadataRegistration(metadataRegistration);

    methodPointers.clear();
    moduleMethodPointers.clear();
    if (Version <= 24.1) {
        const auto off = MapVATR(cr.methodPointers);
        Position = static_cast<size_t>(off);
        methodPointers.resize(static_cast<size_t>(cr.methodPointersCount));
        for (auto& p : methodPointers) p = ReadUIntPtr();
    } else if (cr.codeGenModules && cr.codeGenModulesCount) {
        const auto modsOff = MapVATR(cr.codeGenModules);
        Position = static_cast<size_t>(modsOff);
        std::vector<uint64_t> modPtrs(static_cast<size_t>(cr.codeGenModulesCount));
        for (auto& p : modPtrs) p = ReadUIntPtr();
        for (uint64_t mp : modPtrs) {
            Position = static_cast<size_t>(MapVATR(mp));
            const uint64_t moduleNamePtr = ReadUIntPtr();
            const int64_t methodPointerCount = ReadIntPtr();
            const uint64_t methodPointersPtr = ReadUIntPtr();
            // skip adjustor/invoker/etc. ÔÇö only need name + pointers
            std::string moduleName = ReadStringToNull(static_cast<size_t>(MapVATR(moduleNamePtr)));
            std::vector<uint64_t> ptrs;
            if (methodPointerCount > 0 && methodPointersPtr) {
                try {
                    Position = static_cast<size_t>(MapVATR(methodPointersPtr));
                    ptrs.resize(static_cast<size_t>(methodPointerCount));
                    for (auto& p : ptrs) p = ReadUIntPtr();
                } catch (...) {
                    ptrs.assign(static_cast<size_t>(methodPointerCount), 0);
                }
            }
            moduleMethodPointers.emplace(std::move(moduleName), std::move(ptrs));
        }
    }

    types.clear();
    if (mr.types && mr.typesCount > 0) {
        Position = static_cast<size_t>(MapVATR(mr.types));
        std::vector<uint64_t> typePtrs(static_cast<size_t>(mr.typesCount));
        for (auto& p : typePtrs) p = ReadUIntPtr();
        types.reserve(typePtrs.size());
        for (uint64_t tp : typePtrs) {
            types.push_back(ReadTypeAt(MapVATR(tp)));
        }
    }

    fieldOffsetsFlat.clear();
    fieldOffsetPointers.clear();
    fieldOffsetsArePointers = Version > 21.0;
    if (mr.fieldOffsets && mr.fieldOffsetsCount > 0) {
        if (!fieldOffsetsArePointers) {
            Position = static_cast<size_t>(MapVATR(mr.fieldOffsets));
            fieldOffsetsFlat.resize(static_cast<size_t>(mr.fieldOffsetsCount));
            for (auto& v : fieldOffsetsFlat) v = ReadU32();
        } else {
            Position = static_cast<size_t>(MapVATR(mr.fieldOffsets));
            fieldOffsetPointers.resize(static_cast<size_t>(mr.fieldOffsetsCount));
            for (auto& p : fieldOffsetPointers) p = ReadUIntPtr();
        }
    }

    meta.version = Version;
    return !types.empty() || !methodPointers.empty() || !moduleMethodPointers.empty();
}

Il2CppTypeData Il2CppBinary::ReadTypeAt(uint64_t fileOff) const {
    Il2CppTypeData t;
    if (fileOff + 12 > data.size()) return t;
    if (Is32Bit) {
        uint32_t dp;
        std::memcpy(&dp, data.data() + fileOff, 4);
        t.datapoint = dp;
        std::memcpy(&t.bits, data.data() + fileOff + 4, 4);
    } else {
        std::memcpy(&t.datapoint, data.data() + fileOff, 8);
        std::memcpy(&t.bits, data.data() + fileOff + 8, 4);
    }
    t.attrs = t.bits & 0xffff;
    t.type = static_cast<uint8_t>((t.bits >> 16) & 0xff);
    if (Version >= 27.2) {
        t.byref = (t.bits >> 29) & 1;
    } else {
        t.byref = (t.bits >> 30) & 1;
    }
    return t;
}

uint64_t Il2CppBinary::GetMethodPointer(const std::string& imageName, const MethodDef& method) const {
    if (Version >= 24.2) {
        auto it = moduleMethodPointers.find(imageName);
        if (it == moduleMethodPointers.end()) return 0;
        const uint32_t idx = (method.token & 0x00FFFFFFu);
        if (idx == 0 || idx > it->second.size()) return 0;
        return it->second[idx - 1];
    }
    if (method.methodIndex >= 0 && static_cast<size_t>(method.methodIndex) < methodPointers.size()) {
        return methodPointers[static_cast<size_t>(method.methodIndex)];
    }
    return 0;
}

uint64_t Il2CppBinary::GetMethodRVA(const std::string& imageName, const MethodDef& method) const {
    const uint64_t ptr = GetMethodPointer(imageName, method);
    return ptr ? GetRVA(ptr) : 0;
}

int32_t Il2CppBinary::GetFieldOffset(int typeDefIndex, int fieldIndexInType, const Metadata& meta) const {
    if (typeDefIndex < 0 || typeDefIndex >= static_cast<int>(meta.typeDefs.size())) return -1;
    const auto& td = meta.typeDefs[static_cast<size_t>(typeDefIndex)];
    const int globalField = td.fieldStart + fieldIndexInType;
    if (!fieldOffsetsArePointers) {
        if (globalField < 0 || globalField >= static_cast<int>(fieldOffsetsFlat.size())) return -1;
        return static_cast<int32_t>(fieldOffsetsFlat[static_cast<size_t>(globalField)]);
    }
    if (typeDefIndex >= static_cast<int>(fieldOffsetPointers.size())) return -1;
    const uint64_t ptr = fieldOffsetPointers[static_cast<size_t>(typeDefIndex)];
    if (!ptr) return -1;
    const uint64_t off = MapVATR(ptr);
    if (off + static_cast<uint64_t>((fieldIndexInType + 1) * 4) > data.size()) return -1;
    int32_t value = 0;
    std::memcpy(&value, data.data() + off + static_cast<size_t>(fieldIndexInType) * 4, 4);
    return value;
}

std::string Il2CppBinary::GetTypeName(int32_t typeIndex, const Metadata& meta, int depth) const {
    if (depth > 8) return "?";
    if (typeIndex < 0 || typeIndex >= static_cast<int32_t>(types.size())) return "Unknown";
    const auto& t = types[static_cast<size_t>(typeIndex)];
    auto wrapByref = [&](std::string s) {
        if (t.byref) s += "&";
        return s;
    };
    switch (t.type) {
        case IL2CPP_TYPE_VOID: return wrapByref("System.Void");
        case IL2CPP_TYPE_BOOLEAN: return wrapByref("System.Boolean");
        case IL2CPP_TYPE_CHAR: return wrapByref("System.Char");
        case IL2CPP_TYPE_I1: return wrapByref("System.SByte");
        case IL2CPP_TYPE_U1: return wrapByref("System.Byte");
        case IL2CPP_TYPE_I2: return wrapByref("System.Int16");
        case IL2CPP_TYPE_U2: return wrapByref("System.UInt16");
        case IL2CPP_TYPE_I4: return wrapByref("System.Int32");
        case IL2CPP_TYPE_U4: return wrapByref("System.UInt32");
        case IL2CPP_TYPE_I8: return wrapByref("System.Int64");
        case IL2CPP_TYPE_U8: return wrapByref("System.UInt64");
        case IL2CPP_TYPE_R4: return wrapByref("System.Single");
        case IL2CPP_TYPE_R8: return wrapByref("System.Double");
        case IL2CPP_TYPE_STRING: return wrapByref("System.String");
        case IL2CPP_TYPE_OBJECT: return wrapByref("System.Object");
        case IL2CPP_TYPE_TYPEDBYREF: return wrapByref("System.TypedReference");
        case IL2CPP_TYPE_I: return wrapByref("System.IntPtr");
        case IL2CPP_TYPE_U: return wrapByref("System.UIntPtr");
        case IL2CPP_TYPE_CLASS:
        case IL2CPP_TYPE_VALUETYPE: {
            const int64_t idx = static_cast<int64_t>(t.datapoint);
            if (Version >= 27.0) {
                // runtime: typeHandle ÔÇö fall back to byval match
                for (size_t i = 0; i < meta.typeDefs.size(); ++i) {
                    if (meta.typeDefs[i].byvalTypeIndex == typeIndex) {
                        const auto& td = meta.typeDefs[i];
                        const std::string ns = meta.GetString(td.namespaceIndex);
                        const std::string name = meta.GetString(td.nameIndex);
                        return wrapByref(ns.empty() ? name : (ns + "." + name));
                    }
                }
                return wrapByref("TypeHandle");
            }
            if (idx >= 0 && idx < static_cast<int64_t>(meta.typeDefs.size())) {
                const auto& td = meta.typeDefs[static_cast<size_t>(idx)];
                const std::string ns = meta.GetString(td.namespaceIndex);
                const std::string name = meta.GetString(td.nameIndex);
                return wrapByref(ns.empty() ? name : (ns + "." + name));
            }
            return wrapByref("UnknownClass");
        }
        case IL2CPP_TYPE_SZARRAY: {
            // datapoint -> Il2CppType*
            try {
                auto inner = ReadTypeAt(MapVATR(t.datapoint));
                // Find index by scanning ÔÇö approximate via recursive name using temporary
                // Use type pointer match
                for (int32_t i = 0; i < static_cast<int32_t>(types.size()); ++i) {
                    if (types[static_cast<size_t>(i)].datapoint == inner.datapoint &&
                        types[static_cast<size_t>(i)].bits == inner.bits) {
                        return wrapByref(GetTypeName(i, meta, depth + 1) + "[]");
                    }
                }
                return wrapByref("Array");
            } catch (...) {
                return wrapByref("Array");
            }
        }
        case IL2CPP_TYPE_PTR:
            return wrapByref("Ptr");
        case IL2CPP_TYPE_GENERICINST:
            return wrapByref("GenericInst");
        case IL2CPP_TYPE_VAR:
            return wrapByref("T");
        case IL2CPP_TYPE_MVAR:
            return wrapByref("TM");
        case IL2CPP_TYPE_ARRAY:
            return wrapByref("Array");
        case IL2CPP_TYPE_FNPTR:
            return wrapByref("FnPtr");
        default:
            return wrapByref("Unknown");
    }
}

} // namespace static_dump
