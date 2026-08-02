#pragma once
#include "binary_stream.h"
#include "metadata.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace static_dump {

struct SearchSection {
    uint64_t offset = 0;
    uint64_t offsetEnd = 0;
    uint64_t address = 0;
    uint64_t addressEnd = 0;
};

struct PeSection {
    char name[9]{};
    uint32_t VirtualSize = 0;
    uint32_t VirtualAddress = 0;
    uint32_t SizeOfRawData = 0;
    uint32_t PointerToRawData = 0;
    uint32_t Characteristics = 0;
};

struct Il2CppTypeData {
    uint64_t datapoint = 0;
    uint32_t bits = 0;
    uint32_t attrs = 0;
    uint8_t type = 0;
    uint8_t byref = 0;
};

class Il2CppBinary : public BinaryStream {
public:
    uint64_t ImageBase = 0;
    uint32_t SizeOfImage = 0;
    uint32_t TimeDateStamp = 0;
    std::vector<PeSection> sections;
    std::vector<SearchSection> execSecs;
    std::vector<SearchSection> dataSecs;

    std::vector<uint64_t> methodPointers; // <=24.1
    std::unordered_map<std::string, std::vector<uint64_t>> moduleMethodPointers; // >=24.2
    std::vector<Il2CppTypeData> types;
    std::vector<uint64_t> fieldOffsetPointers; // >21: per-type pointer
    std::vector<uint32_t> fieldOffsetsFlat;    // <=21
    bool fieldOffsetsArePointers = true;

    explicit Il2CppBinary(std::vector<uint8_t> buf);

    uint64_t MapVATR(uint64_t absAddr) const;
    uint64_t GetRVA(uint64_t pointer) const;

    bool PlusSearch(Metadata& meta);
    bool InitRegistrations(uint64_t codeRegistration, uint64_t metadataRegistration, Metadata& meta);

    uint64_t GetMethodPointer(const std::string& imageName, const MethodDef& method) const;
    uint64_t GetMethodRVA(const std::string& imageName, const MethodDef& method) const;
    int32_t GetFieldOffset(int typeDefIndex, int fieldIndexInType, const Metadata& meta) const;
    std::string GetTypeName(int32_t typeIndex, const Metadata& meta, int depth = 0) const;

private:
    void ParsePE();
    uint64_t FindCodeRegistration(const Metadata& meta, bool& pointerInExec);
    uint64_t FindMetadataRegistration(const Metadata& meta, bool pointerInExec);
    uint64_t FindCodeRegistrationOld(int methodCount);
    uint64_t FindCodeRegistration2019(const std::vector<SearchSection>& secs, int imageCount, double ver);
    uint64_t FindMetadataRegistrationOld(int typeDefinitionsCount, long usagesCount);
    uint64_t FindMetadataRegistrationV21(int typeDefinitionsCount, bool pointerInExec);
    std::vector<uint64_t> FindReferences(uint64_t va) const;
    bool CheckPointerRangeExecVa(const std::vector<uint64_t>& ptrs) const;
    bool CheckPointerRangeDataVa(const std::vector<uint64_t>& ptrs) const;
    bool CheckPointerRangeDataRa(uint64_t fileOff) const;
    bool InExecVa(uint64_t va) const;
    bool InDataVa(uint64_t va) const;

    // CodeRegistration field readers
    struct CodeReg {
        uint64_t genericMethodPointersCount = 0;
        uint64_t genericMethodPointers = 0;
        uint64_t invokerPointersCount = 0;
        uint64_t invokerPointers = 0;
        uint64_t codeGenModulesCount = 0;
        uint64_t codeGenModules = 0;
        uint64_t methodPointersCount = 0;
        uint64_t methodPointers = 0;
        uint64_t reversePInvokeWrapperCount = 0;
        uint64_t unresolvedVirtualCallCount = 0;
        uint64_t customAttributeCount = 0;
        uint64_t customAttributeGenerators = 0;
        uint64_t interopDataCount = 0;
    };
    struct MetaReg {
        int64_t genericClassesCount = 0;
        uint64_t genericClasses = 0;
        int64_t genericInstsCount = 0;
        uint64_t genericInsts = 0;
        int64_t genericMethodTableCount = 0;
        uint64_t genericMethodTable = 0;
        int64_t typesCount = 0;
        uint64_t types = 0;
        int64_t methodSpecsCount = 0;
        uint64_t methodSpecs = 0;
        int64_t fieldOffsetsCount = 0;
        uint64_t fieldOffsets = 0;
        int64_t typeDefinitionsSizesCount = 0;
        uint64_t typeDefinitionsSizes = 0;
        uint64_t metadataUsagesCount = 0;
        uint64_t metadataUsages = 0;
    };

    CodeReg ReadCodeRegistration(uint64_t va);
    MetaReg ReadMetadataRegistration(uint64_t va);
    Il2CppTypeData ReadTypeAt(uint64_t fileOff) const;
};

} // namespace static_dump
