#pragma once
#include "binary_stream.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace static_dump {

struct SectionMeta {
    uint32_t offset = 0;
    int32_t sectionSize = 0;
    int32_t count = 0;
};

struct ImageDef {
    uint32_t nameIndex = 0;
    int32_t assemblyIndex = 0;
    int32_t typeStart = 0;
    uint32_t typeCount = 0;
    int32_t exportedTypeStart = 0;
    uint32_t exportedTypeCount = 0;
    int32_t entryPointIndex = -1;
    uint32_t token = 0;
    int32_t customAttributeStart = 0;
    uint32_t customAttributeCount = 0;
};

struct TypeDef {
    uint32_t nameIndex = 0;
    uint32_t namespaceIndex = 0;
    int32_t byvalTypeIndex = -1;
    int32_t declaringTypeIndex = -1;
    int32_t parentIndex = -1;
    int32_t elementTypeIndex = -1;
    int32_t genericContainerIndex = -1;
    uint32_t flags = 0;
    int32_t fieldStart = 0;
    int32_t methodStart = 0;
    int32_t nestedTypesStart = 0;
    int32_t interfacesStart = 0;
    uint16_t method_count = 0;
    uint16_t field_count = 0;
    uint16_t nested_type_count = 0;
    uint16_t interfaces_count = 0;
    uint32_t bitfield = 0;
    uint32_t token = 0;

    bool IsValueType() const { return (bitfield & 0x1) == 1; }
    bool IsEnum() const { return ((bitfield >> 1) & 0x1) == 1; }
};

struct MethodDef {
    uint32_t nameIndex = 0;
    int32_t declaringType = -1;
    int32_t returnType = -1;
    int32_t parameterStart = 0;
    int32_t genericContainerIndex = -1;
    int32_t methodIndex = -1; // <=24.1
    uint32_t token = 0;
    uint16_t flags = 0;
    uint16_t iflags = 0;
    uint16_t slot = 0;
    uint16_t parameterCount = 0;
};

struct FieldDef {
    uint32_t nameIndex = 0;
    int32_t typeIndex = -1;
    uint32_t token = 0;
};

struct ParamDef {
    uint32_t nameIndex = 0;
    uint32_t token = 0;
    int32_t typeIndex = -1;
};

struct FieldDefaultValue {
    int32_t fieldIndex = 0;
    int32_t typeIndex = 0;
    int32_t dataIndex = 0;
};

struct StringLiteral {
    uint32_t length = 0;
    int32_t dataIndex = 0;
};

class Metadata {
public:
    BinaryStream stream;
    double version = 0;
    uint32_t stringOffset = 0;
    uint32_t defaultValueDataOffset = 0;
    long metadataUsagesCount = 0;

    std::vector<ImageDef> images;
    std::vector<TypeDef> typeDefs;
    std::vector<MethodDef> methodDefs;
    std::vector<FieldDef> fieldDefs;
    std::vector<ParamDef> parameterDefs;
    std::vector<int32_t> nestedTypeIndices;
    std::vector<int32_t> interfaceIndices;
    std::vector<FieldDefaultValue> fieldDefaultValues;
    std::unordered_map<int32_t, FieldDefaultValue> fieldDefaultByIndex;
    std::vector<StringLiteral> stringLiterals;
    uint32_t stringLiteralDataOffset = 0;

    explicit Metadata(std::vector<uint8_t> buf);

    std::string GetString(uint32_t index) const;
    bool GetFieldDefault(int32_t fieldIndex, FieldDefaultValue& out) const;

private:
    void DetectAndLoad();
    void ReadHeaderClassic();
    void ReadHeaderV38();
    void LoadTablesClassic(uint32_t imagesOff, int32_t imagesSize,
                           uint32_t typesOff, int32_t typesSize,
                           uint32_t methodsOff, int32_t methodsSize,
                           uint32_t fieldsOff, int32_t fieldsSize,
                           uint32_t paramsOff, int32_t paramsSize,
                           uint32_t nestedOff, int32_t nestedSize,
                           uint32_t ifacesOff, int32_t ifacesSize,
                           uint32_t fdvOff, int32_t fdvSize,
                           uint32_t litOff, int32_t litSize);
    ImageDef ReadImageDef();
    TypeDef ReadTypeDef();
    MethodDef ReadMethodDef();
    FieldDef ReadFieldDef();
    ParamDef ReadParamDef();
    int32_t ReadIndex(int width);
    int indexWidthFromCount(int count) const;
};

} // namespace static_dump
