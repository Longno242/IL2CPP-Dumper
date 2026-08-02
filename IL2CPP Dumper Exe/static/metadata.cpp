#define _CRT_SECURE_NO_WARNINGS
#include "metadata.h"
#include <stdexcept>
#include <sstream>

namespace static_dump {
namespace {

constexpr uint32_t kMetadataMagic = 0xFAB11BAFu;

} // namespace

Metadata::Metadata(std::vector<uint8_t> buf) : stream(std::move(buf)) {
    DetectAndLoad();
}

std::string Metadata::GetString(uint32_t index) const {
    return stream.ReadStringToNull(static_cast<size_t>(stringOffset) + index);
}

bool Metadata::GetFieldDefault(int32_t fieldIndex, FieldDefaultValue& out) const {
    auto it = fieldDefaultByIndex.find(fieldIndex);
    if (it == fieldDefaultByIndex.end()) return false;
    out = it->second;
    return true;
}

int Metadata::indexWidthFromCount(int count) const {
    if (count < 0) count = 0;
    if (count <= 0xFF) return 1;
    if (count <= 0xFFFF) return 2;
    return 4;
}

int32_t Metadata::ReadIndex(int width) {
    if (width == 1) return static_cast<int32_t>(stream.ReadU8());
    if (width == 2) return static_cast<int32_t>(stream.ReadU16());
    return stream.ReadI32();
}

ImageDef Metadata::ReadImageDef() {
    ImageDef d;
    d.nameIndex = stream.ReadU32();
    d.assemblyIndex = stream.ReadI32();
    d.typeStart = stream.ReadI32();
    d.typeCount = stream.ReadU32();
    if (version >= 24.0) {
        d.exportedTypeStart = stream.ReadI32();
        d.exportedTypeCount = stream.ReadU32();
    }
    d.entryPointIndex = stream.ReadI32();
    if (version >= 19.0) d.token = stream.ReadU32();
    if (version >= 24.1) {
        d.customAttributeStart = stream.ReadI32();
        d.customAttributeCount = stream.ReadU32();
    }
    return d;
}

TypeDef Metadata::ReadTypeDef() {
    TypeDef d;
    d.nameIndex = stream.ReadU32();
    d.namespaceIndex = stream.ReadU32();
    if (version <= 24.0) stream.ReadI32(); // customAttributeIndex
    d.byvalTypeIndex = stream.ReadI32();
    if (version <= 24.5) stream.ReadI32(); // byrefTypeIndex
    d.declaringTypeIndex = stream.ReadI32();
    d.parentIndex = stream.ReadI32();
    if (version <= 31.0) d.elementTypeIndex = stream.ReadI32();
    if (version <= 24.1) {
        stream.ReadI32(); // rgctxStart
        stream.ReadI32(); // rgctxCount
    }
    d.genericContainerIndex = stream.ReadI32();
    if (version <= 22.0) {
        stream.ReadI32(); // delegateWrapper
        stream.ReadI32(); // marshalingFunctions
        if (version >= 21.0) {
            stream.ReadI32(); // ccw
            stream.ReadI32(); // guid
        }
    }
    d.flags = stream.ReadU32();
    d.fieldStart = stream.ReadI32();
    d.methodStart = stream.ReadI32();
    stream.ReadI32(); // eventStart
    stream.ReadI32(); // propertyStart
    d.nestedTypesStart = stream.ReadI32();
    d.interfacesStart = stream.ReadI32();
    stream.ReadI32(); // vtableStart
    stream.ReadI32(); // interfaceOffsetsStart
    d.method_count = stream.ReadU16();
    stream.ReadU16(); // property_count
    d.field_count = stream.ReadU16();
    stream.ReadU16(); // event_count
    d.nested_type_count = stream.ReadU16();
    stream.ReadU16(); // vtable_count
    d.interfaces_count = stream.ReadU16();
    stream.ReadU16(); // interface_offsets_count
    d.bitfield = stream.ReadU32();
    if (version >= 19.0) d.token = stream.ReadU32();
    return d;
}

MethodDef Metadata::ReadMethodDef() {
    MethodDef d;
    d.nameIndex = stream.ReadU32();
    d.declaringType = stream.ReadI32();
    d.returnType = stream.ReadI32();
    if (version >= 31.0) stream.ReadI32(); // returnParameterToken
    d.parameterStart = stream.ReadI32();
    if (version <= 24.0) stream.ReadI32(); // customAttributeIndex
    d.genericContainerIndex = stream.ReadI32();
    if (version <= 24.1) {
        d.methodIndex = stream.ReadI32();
        stream.ReadI32(); // invokerIndex
        stream.ReadI32(); // delegateWrapperIndex
        stream.ReadI32(); // rgctxStart
        stream.ReadI32(); // rgctxCount
    }
    d.token = stream.ReadU32();
    d.flags = stream.ReadU16();
    d.iflags = stream.ReadU16();
    d.slot = stream.ReadU16();
    d.parameterCount = stream.ReadU16();
    return d;
}

FieldDef Metadata::ReadFieldDef() {
    FieldDef d;
    d.nameIndex = stream.ReadU32();
    d.typeIndex = stream.ReadI32();
    if (version <= 24.0) stream.ReadI32(); // customAttributeIndex
    if (version >= 19.0) d.token = stream.ReadU32();
    return d;
}

ParamDef Metadata::ReadParamDef() {
    ParamDef d;
    d.nameIndex = stream.ReadU32();
    d.token = stream.ReadU32();
    if (version <= 24.0) stream.ReadI32(); // customAttributeIndex
    d.typeIndex = stream.ReadI32();
    return d;
}

void Metadata::DetectAndLoad() {
    stream.Seek(0);
    const uint32_t sanity = stream.ReadU32();
    if (sanity != kMetadataMagic) {
        throw std::runtime_error("not a valid global-metadata.dat (bad sanity)");
    }
    const int32_t rawVersion = stream.ReadI32();
    if (rawVersion < 16 || rawVersion > 200) {
        std::ostringstream os;
        os << "unsupported metadata version " << rawVersion << " (supported 16-39+)";
        throw std::runtime_error(os.str());
    }
    version = static_cast<double>(rawVersion);
    stream.Version = version;

    if (rawVersion >= 38) {
        ReadHeaderV38();
        return;
    }

    ReadHeaderClassic();
}

void Metadata::ReadHeaderClassic() {
    // Re-read full header from 0 with version-gated fields.
    stream.Seek(0);
    stream.ReadU32(); // sanity
    stream.ReadI32(); // version

    auto readOffSize = [&](uint32_t& off, int32_t& size) {
        off = stream.ReadU32();
        size = stream.ReadI32();
    };

    uint32_t stringLiteralOffset = 0; int32_t stringLiteralSize = 0;
    uint32_t stringLiteralDataOffset = 0; int32_t stringLiteralDataSize = 0;
    uint32_t stringOff = 0; int32_t stringSize = 0;
    uint32_t eventsOffset = 0; int32_t eventsSize = 0;
    uint32_t propertiesOffset = 0; int32_t propertiesSize = 0;
    uint32_t methodsOffset = 0; int32_t methodsSize = 0;
    uint32_t parameterDefaultValuesOffset = 0; int32_t parameterDefaultValuesSize = 0;
    uint32_t fieldDefaultValuesOffset = 0; int32_t fieldDefaultValuesSize = 0;
    uint32_t fieldAndParameterDefaultValueDataOffset = 0; int32_t fieldAndParameterDefaultValueDataSize = 0;
    int32_t fieldMarshaledSizesOffset = 0; int32_t fieldMarshaledSizesSize = 0;
    uint32_t parametersOffset = 0; int32_t parametersSize = 0;
    uint32_t fieldsOffset = 0; int32_t fieldsSize = 0;
    uint32_t genericParametersOffset = 0; int32_t genericParametersSize = 0;
    uint32_t genericParameterConstraintsOffset = 0; int32_t genericParameterConstraintsSize = 0;
    uint32_t genericContainersOffset = 0; int32_t genericContainersSize = 0;
    uint32_t nestedTypesOffset = 0; int32_t nestedTypesSize = 0;
    uint32_t interfacesOffset = 0; int32_t interfacesSize = 0;
    uint32_t vtableMethodsOffset = 0; int32_t vtableMethodsSize = 0;
    int32_t interfaceOffsetsOffset = 0; int32_t interfaceOffsetsSize = 0;
    uint32_t typeDefinitionsOffset = 0; int32_t typeDefinitionsSize = 0;
    uint32_t rgctxEntriesOffset = 0; int32_t rgctxEntriesCount = 0;
    uint32_t imagesOffset = 0; int32_t imagesSize = 0;
    uint32_t assembliesOffset = 0; int32_t assembliesSize = 0;
    uint32_t metadataUsageListsOffset = 0; int32_t metadataUsageListsCount = 0;
    uint32_t metadataUsagePairsOffset = 0; int32_t metadataUsagePairsCount = 0;
    uint32_t fieldRefsOffset = 0; int32_t fieldRefsSize = 0;
    int32_t referencedAssembliesOffset = 0; int32_t referencedAssembliesSize = 0;
    uint32_t attributesInfoOffset = 0; int32_t attributesInfoCount = 0;
    uint32_t attributeTypesOffset = 0; int32_t attributeTypesCount = 0;
    uint32_t attributeDataOffset = 0; int32_t attributeDataSize = 0;
    uint32_t attributeDataRangeOffset = 0; int32_t attributeDataRangeSize = 0;
    int32_t unresolvedVirtualCallParameterTypesOffset = 0; int32_t unresolvedVirtualCallParameterTypesSize = 0;
    int32_t unresolvedVirtualCallParameterRangesOffset = 0; int32_t unresolvedVirtualCallParameterRangesSize = 0;
    int32_t windowsRuntimeTypeNamesOffset = 0; int32_t windowsRuntimeTypeNamesSize = 0;
    int32_t windowsRuntimeStringsOffset = 0; int32_t windowsRuntimeStringsSize = 0;
    int32_t exportedTypeDefinitionsOffset = 0; int32_t exportedTypeDefinitionsSize = 0;

    readOffSize(stringLiteralOffset, stringLiteralSize);
    readOffSize(stringLiteralDataOffset, stringLiteralDataSize);
    readOffSize(stringOff, stringSize);
    readOffSize(eventsOffset, eventsSize);
    readOffSize(propertiesOffset, propertiesSize);
    readOffSize(methodsOffset, methodsSize);
    readOffSize(parameterDefaultValuesOffset, parameterDefaultValuesSize);
    readOffSize(fieldDefaultValuesOffset, fieldDefaultValuesSize);
    readOffSize(fieldAndParameterDefaultValueDataOffset, fieldAndParameterDefaultValueDataSize);
    fieldMarshaledSizesOffset = stream.ReadI32();
    fieldMarshaledSizesSize = stream.ReadI32();
    readOffSize(parametersOffset, parametersSize);
    readOffSize(fieldsOffset, fieldsSize);
    readOffSize(genericParametersOffset, genericParametersSize);
    readOffSize(genericParameterConstraintsOffset, genericParameterConstraintsSize);
    readOffSize(genericContainersOffset, genericContainersSize);
    readOffSize(nestedTypesOffset, nestedTypesSize);
    readOffSize(interfacesOffset, interfacesSize);
    readOffSize(vtableMethodsOffset, vtableMethodsSize);
    interfaceOffsetsOffset = stream.ReadI32();
    interfaceOffsetsSize = stream.ReadI32();
    readOffSize(typeDefinitionsOffset, typeDefinitionsSize);

    if (version <= 24.1) {
        rgctxEntriesOffset = stream.ReadU32();
        rgctxEntriesCount = stream.ReadI32();
    }

    readOffSize(imagesOffset, imagesSize);
    readOffSize(assembliesOffset, assembliesSize);

    if (version >= 19.0 && version <= 24.5) {
        metadataUsageListsOffset = stream.ReadU32();
        metadataUsageListsCount = stream.ReadI32();
        metadataUsagePairsOffset = stream.ReadU32();
        metadataUsagePairsCount = stream.ReadI32();
    }
    if (version >= 19.0) {
        fieldRefsOffset = stream.ReadU32();
        fieldRefsSize = stream.ReadI32();
    }
    if (version >= 20.0) {
        referencedAssembliesOffset = stream.ReadI32();
        referencedAssembliesSize = stream.ReadI32();
    }
    if (version >= 21.0 && version <= 27.2) {
        attributesInfoOffset = stream.ReadU32();
        attributesInfoCount = stream.ReadI32();
        attributeTypesOffset = stream.ReadU32();
        attributeTypesCount = stream.ReadI32();
    }
    if (version >= 29.0) {
        attributeDataOffset = stream.ReadU32();
        attributeDataSize = stream.ReadI32();
        attributeDataRangeOffset = stream.ReadU32();
        attributeDataRangeSize = stream.ReadI32();
    }
    if (version >= 22.0) {
        unresolvedVirtualCallParameterTypesOffset = stream.ReadI32();
        unresolvedVirtualCallParameterTypesSize = stream.ReadI32();
        unresolvedVirtualCallParameterRangesOffset = stream.ReadI32();
        unresolvedVirtualCallParameterRangesSize = stream.ReadI32();
    }
    if (version >= 23.0) {
        windowsRuntimeTypeNamesOffset = stream.ReadI32();
        windowsRuntimeTypeNamesSize = stream.ReadI32();
    }
    if (version >= 27.0) {
        windowsRuntimeStringsOffset = stream.ReadI32();
        windowsRuntimeStringsSize = stream.ReadI32();
    }
    if (version >= 24.0) {
        exportedTypeDefinitionsOffset = stream.ReadI32();
        exportedTypeDefinitionsSize = stream.ReadI32();
    }

    stringOffset = stringOff;
    defaultValueDataOffset = fieldAndParameterDefaultValueDataOffset;
    stringLiteralDataOffset = stringLiteralDataOffset;
    (void)stringSize;
    (void)eventsOffset; (void)eventsSize;
    (void)propertiesOffset; (void)propertiesSize;
    (void)parameterDefaultValuesOffset; (void)parameterDefaultValuesSize;
    (void)fieldMarshaledSizesOffset; (void)fieldMarshaledSizesSize;
    (void)genericParametersOffset; (void)genericParametersSize;
    (void)genericParameterConstraintsOffset; (void)genericParameterConstraintsSize;
    (void)genericContainersOffset; (void)genericContainersSize;
    (void)vtableMethodsOffset; (void)vtableMethodsSize;
    (void)interfaceOffsetsOffset; (void)interfaceOffsetsSize;
    (void)rgctxEntriesOffset; (void)rgctxEntriesCount;
    (void)fieldRefsOffset; (void)fieldRefsSize;
    (void)referencedAssembliesOffset; (void)referencedAssembliesSize;
    (void)attributesInfoOffset; (void)attributesInfoCount;
    (void)attributeTypesOffset; (void)attributeTypesCount;
    (void)attributeDataOffset; (void)attributeDataSize;
    (void)attributeDataRangeOffset; (void)attributeDataRangeSize;
    (void)unresolvedVirtualCallParameterTypesOffset; (void)unresolvedVirtualCallParameterTypesSize;
    (void)unresolvedVirtualCallParameterRangesOffset; (void)unresolvedVirtualCallParameterRangesSize;
    (void)windowsRuntimeTypeNamesOffset; (void)windowsRuntimeTypeNamesSize;
    (void)windowsRuntimeStringsOffset; (void)windowsRuntimeStringsSize;
    (void)exportedTypeDefinitionsOffset; (void)exportedTypeDefinitionsSize;
    (void)stringLiteralDataSize;

    // --- 24.x subversion detection (Il2CppDumper heuristics) ---
    // If stringLiteralOffset is 264, the on-disk header already omitted RGCTX slots (24.2+).
    // Our first pass used version==24 (with RGCTX fields), so offsets are wrong ÔÇö re-read header at 24.2.
    if (static_cast<int>(version) == 24 && stringLiteralOffset == 264 && version < 24.15) {
        version = 24.2;
        stream.Version = version;
        ReadHeaderClassic();
        return;
    }

    // Provisional load images to detect 24.1 (token != 1)
    if (version == 24.0) {
        const int imgCount = imagesSize / 32;
        stream.Seek(imagesOffset);
        bool anyTokenNotOne = false;
        for (int i = 0; i < imgCount; ++i) {
            ImageDef img = ReadImageDef();
            if (img.token != 1) anyTokenNotOne = true;
        }
        if (anyTokenNotOne) {
            version = 24.1;
            stream.Version = version;
        }
    }

    if (version == 24.2 && imagesSize > 0) {
        const int imgCount = imagesSize / 40;
        if (imgCount > 0 && assembliesSize / 68 < imgCount) {
            version = 24.4;
            stream.Version = version;
        }
    }

    if (version >= 19.0 && version <= 24.5) {
        // metadataUsagesCount Ôëê max decoded usage index + 1; approximate with pair count for registration search
        metadataUsagesCount = metadataUsagePairsCount > 0 ? metadataUsagePairsCount : metadataUsageListsCount;
        if (metadataUsagesCount <= 0) metadataUsagesCount = 1;
    }

    LoadTablesClassic(imagesOffset, imagesSize,
                      typeDefinitionsOffset, typeDefinitionsSize,
                      methodsOffset, methodsSize,
                      fieldsOffset, fieldsSize,
                      parametersOffset, parametersSize,
                      nestedTypesOffset, nestedTypesSize,
                      interfacesOffset, interfacesSize,
                      fieldDefaultValuesOffset, fieldDefaultValuesSize,
                      stringLiteralOffset, stringLiteralSize);
}

void Metadata::LoadTablesClassic(uint32_t imagesOff, int32_t imagesSize,
                                 uint32_t typesOff, int32_t typesSize,
                                 uint32_t methodsOff, int32_t methodsSize,
                                 uint32_t fieldsOff, int32_t fieldsSize,
                                 uint32_t paramsOff, int32_t paramsSize,
                                 uint32_t nestedOff, int32_t nestedSize,
                                 uint32_t ifacesOff, int32_t ifacesSize,
                                 uint32_t fdvOff, int32_t fdvSize,
                                 uint32_t litOff, int32_t litSize) {
    // Measure one struct by reading until we know size from first element + remaining size.
    // Compute count via probing size of one record.
    auto countByProbe = [&](uint32_t off, int32_t size, auto reader) -> int {
        if (size <= 0) return 0;
        stream.Seek(off);
        const size_t start = stream.Position;
        reader();
        const size_t stride = stream.Position - start;
        if (stride == 0) return 0;
        return size / static_cast<int32_t>(stride);
    };

    const int imgCount = countByProbe(imagesOff, imagesSize, [&] { ReadImageDef(); });
    images.clear();
    images.reserve(imgCount);
    stream.Seek(imagesOff);
    for (int i = 0; i < imgCount; ++i) images.push_back(ReadImageDef());

    const int typeCount = countByProbe(typesOff, typesSize, [&] { ReadTypeDef(); });
    typeDefs.clear();
    typeDefs.reserve(typeCount);
    stream.Seek(typesOff);
    for (int i = 0; i < typeCount; ++i) typeDefs.push_back(ReadTypeDef());

    const int methodCount = countByProbe(methodsOff, methodsSize, [&] { ReadMethodDef(); });
    methodDefs.clear();
    methodDefs.reserve(methodCount);
    stream.Seek(methodsOff);
    for (int i = 0; i < methodCount; ++i) methodDefs.push_back(ReadMethodDef());

    const int fieldCount = countByProbe(fieldsOff, fieldsSize, [&] { ReadFieldDef(); });
    fieldDefs.clear();
    fieldDefs.reserve(fieldCount);
    stream.Seek(fieldsOff);
    for (int i = 0; i < fieldCount; ++i) fieldDefs.push_back(ReadFieldDef());

    const int paramCount = countByProbe(paramsOff, paramsSize, [&] { ReadParamDef(); });
    parameterDefs.clear();
    parameterDefs.reserve(paramCount);
    stream.Seek(paramsOff);
    for (int i = 0; i < paramCount; ++i) parameterDefs.push_back(ReadParamDef());

    nestedTypeIndices.clear();
    if (nestedSize >= 4) {
        stream.Seek(nestedOff);
        const int n = nestedSize / 4;
        nestedTypeIndices.reserve(n);
        for (int i = 0; i < n; ++i) nestedTypeIndices.push_back(stream.ReadI32());
    }

    interfaceIndices.clear();
    if (ifacesSize >= 4) {
        stream.Seek(ifacesOff);
        const int n = ifacesSize / 4;
        interfaceIndices.reserve(n);
        for (int i = 0; i < n; ++i) interfaceIndices.push_back(stream.ReadI32());
    }

    fieldDefaultValues.clear();
    fieldDefaultByIndex.clear();
    if (fdvSize >= 12) {
        stream.Seek(fdvOff);
        const int n = fdvSize / 12;
        for (int i = 0; i < n; ++i) {
            FieldDefaultValue v;
            v.fieldIndex = stream.ReadI32();
            v.typeIndex = stream.ReadI32();
            v.dataIndex = stream.ReadI32();
            fieldDefaultValues.push_back(v);
            fieldDefaultByIndex[v.fieldIndex] = v;
        }
    }

    stringLiterals.clear();
    if (litSize > 0) {
        stream.Seek(litOff);
        if (version >= 35.0) {
            // v35+: no length field; dataIndex only (8? actually just dataIndex int + padding depending)
            // Fork: Il2CppStringLiteral has only dataIndex for Max removed length.
            // Size is typically 4 or 8. Probe: use 8 if divisible else 4.
            int stride = (litSize % 8 == 0) ? 8 : 4;
            const int n = litSize / stride;
            stringLiterals.reserve(n);
            for (int i = 0; i < n; ++i) {
                StringLiteral s;
                s.dataIndex = stream.ReadI32();
                if (stride == 8) stream.ReadI32();
                s.length = 0;
                stringLiterals.push_back(s);
            }
            for (size_t i = 0; i + 1 < stringLiterals.size(); ++i) {
                stringLiterals[i].length = static_cast<uint32_t>(
                    stringLiterals[i + 1].dataIndex - stringLiterals[i].dataIndex);
            }
        } else {
            const int n = litSize / 8;
            stringLiterals.reserve(n);
            for (int i = 0; i < n; ++i) {
                StringLiteral s;
                s.length = stream.ReadU32();
                s.dataIndex = stream.ReadI32();
                stringLiterals.push_back(s);
            }
        }
    }
}

void Metadata::ReadHeaderV38() {
    stream.Seek(0);
    stream.ReadU32();
    stream.ReadI32();

    auto readSection = [&]() -> SectionMeta {
        SectionMeta s;
        s.offset = stream.ReadU32();
        s.sectionSize = stream.ReadI32();
        s.count = stream.ReadI32();
        return s;
    };

    const SectionMeta stringLiteralsSec = readSection();
    const SectionMeta stringLiteralDataSec = readSection();
    const SectionMeta stringsSec = readSection();
    readSection(); // events
    readSection(); // properties
    const SectionMeta methodsSec = readSection();
    readSection(); // parameterDefaultValues
    const SectionMeta fieldDefaultValuesSec = readSection();
    const SectionMeta defaultDataSec = readSection();
    readSection(); // fieldMarshaledSizes
    const SectionMeta parametersSec = readSection();
    const SectionMeta fieldsSec = readSection();
    readSection(); // genericParameters
    readSection(); // genericParameterConstraints
    readSection(); // genericContainers
    const SectionMeta nestedSec = readSection();
    const SectionMeta interfacesSec = readSection();
    readSection(); // vtableMethods
    readSection(); // interfaceOffsets
    const SectionMeta typeDefinitionsSec = readSection();
    const SectionMeta imagesSec = readSection();
    readSection(); // assemblies
    readSection(); // fieldRefs
    readSection(); // referencedAssemblies
    readSection(); // attributeData
    readSection(); // attributeDataRanges
    readSection(); // unresolvedIndirectCallParameterTypes
    readSection(); // unresolvedIndirectCallParameterRanges
    readSection(); // windowsRuntimeTypeNames
    readSection(); // windowsRuntimeStrings
    readSection(); // exportedTypeDefinitions

    stringOffset = stringsSec.offset;
    defaultValueDataOffset = defaultDataSec.offset;
    stringLiteralDataOffset = stringLiteralDataSec.offset;

    const int typeIdxW = indexWidthFromCount(typeDefinitionsSec.count);
    const int typeDefIdxW = indexWidthFromCount(typeDefinitionsSec.count);
    (void)typeDefIdxW;

    // Images (v38 layout similar to late classic without compact indices on image)
    images.clear();
    images.reserve(imagesSec.count);
    stream.Seek(imagesSec.offset);
    for (int i = 0; i < imagesSec.count; ++i) {
        ImageDef d;
        d.nameIndex = stream.ReadU32();
        d.assemblyIndex = stream.ReadI32();
        d.typeStart = stream.ReadI32();
        d.typeCount = stream.ReadU32();
        d.exportedTypeStart = stream.ReadI32();
        d.exportedTypeCount = stream.ReadU32();
        d.entryPointIndex = stream.ReadI32();
        d.token = stream.ReadU32();
        d.customAttributeStart = stream.ReadI32();
        d.customAttributeCount = stream.ReadU32();
        images.push_back(d);
    }

    // Type definitions ÔÇö v38 uses variable-width indices; use 4-byte fallback when section size matches
    typeDefs.clear();
    typeDefs.reserve(typeDefinitionsSec.count);
    stream.Seek(typeDefinitionsSec.offset);
    for (int i = 0; i < typeDefinitionsSec.count; ++i) {
        TypeDef d;
        d.nameIndex = stream.ReadU32();
        d.namespaceIndex = stream.ReadU32();
        d.byvalTypeIndex = ReadIndex(typeIdxW);
        d.declaringTypeIndex = ReadIndex(typeIdxW);
        d.parentIndex = ReadIndex(typeIdxW);
        d.genericContainerIndex = stream.ReadI32(); // may be compact in fork; keep int32 for robustness when width unknown
        // If compact generic container ÔÇö fork uses GenericContainerIndex width. Try: if remaining looks wrong, user may need updates.
        d.flags = stream.ReadU32();
        d.fieldStart = stream.ReadI32();
        d.methodStart = stream.ReadI32();
        stream.ReadI32(); // eventStart
        stream.ReadI32(); // propertyStart
        d.nestedTypesStart = stream.ReadI32();
        d.interfacesStart = stream.ReadI32();
        stream.ReadI32(); // vtableStart
        stream.ReadI32(); // interfaceOffsetsStart
        d.method_count = stream.ReadU16();
        stream.ReadU16();
        d.field_count = stream.ReadU16();
        stream.ReadU16();
        d.nested_type_count = stream.ReadU16();
        stream.ReadU16();
        d.interfaces_count = stream.ReadU16();
        stream.ReadU16();
        d.bitfield = stream.ReadU32();
        d.token = stream.ReadU32();
        typeDefs.push_back(d);
    }

    methodDefs.clear();
    methodDefs.reserve(methodsSec.count);
    stream.Seek(methodsSec.offset);
    for (int i = 0; i < methodsSec.count; ++i) {
        MethodDef d;
        d.nameIndex = stream.ReadU32();
        d.declaringType = ReadIndex(typeIdxW);
        d.returnType = ReadIndex(typeIdxW);
        d.parameterStart = stream.ReadI32();
        d.genericContainerIndex = stream.ReadI32();
        d.token = stream.ReadU32();
        d.flags = stream.ReadU16();
        d.iflags = stream.ReadU16();
        d.slot = stream.ReadU16();
        d.parameterCount = stream.ReadU16();
        methodDefs.push_back(d);
    }

    fieldDefs.clear();
    fieldDefs.reserve(fieldsSec.count);
    stream.Seek(fieldsSec.offset);
    for (int i = 0; i < fieldsSec.count; ++i) {
        FieldDef d;
        d.nameIndex = stream.ReadU32();
        d.typeIndex = ReadIndex(typeIdxW);
        d.token = stream.ReadU32();
        fieldDefs.push_back(d);
    }

    parameterDefs.clear();
    parameterDefs.reserve(parametersSec.count);
    stream.Seek(parametersSec.offset);
    const int paramIdxW = (version >= 39.0) ? indexWidthFromCount(parametersSec.count) : 4;
    (void)paramIdxW;
    for (int i = 0; i < parametersSec.count; ++i) {
        ParamDef d;
        d.nameIndex = stream.ReadU32();
        d.token = stream.ReadU32();
        d.typeIndex = ReadIndex(typeIdxW);
        parameterDefs.push_back(d);
    }

    nestedTypeIndices.clear();
    stream.Seek(nestedSec.offset);
    for (int i = 0; i < nestedSec.count; ++i) nestedTypeIndices.push_back(ReadIndex(typeIdxW));

    interfaceIndices.clear();
    stream.Seek(interfacesSec.offset);
    for (int i = 0; i < interfacesSec.count; ++i) interfaceIndices.push_back(ReadIndex(typeIdxW));

    fieldDefaultValues.clear();
    fieldDefaultByIndex.clear();
    stream.Seek(fieldDefaultValuesSec.offset);
    for (int i = 0; i < fieldDefaultValuesSec.count; ++i) {
        FieldDefaultValue v;
        v.fieldIndex = stream.ReadI32();
        v.typeIndex = ReadIndex(typeIdxW);
        v.dataIndex = stream.ReadI32();
        fieldDefaultValues.push_back(v);
        fieldDefaultByIndex[v.fieldIndex] = v;
    }

    stringLiterals.clear();
    stream.Seek(stringLiteralsSec.offset);
    for (int i = 0; i < stringLiteralsSec.count; ++i) {
        StringLiteral s;
        s.dataIndex = stream.ReadI32();
        s.length = 0;
        stringLiterals.push_back(s);
    }
    for (size_t i = 0; i + 1 < stringLiterals.size(); ++i) {
        stringLiterals[i].length = static_cast<uint32_t>(
            stringLiterals[i + 1].dataIndex - stringLiterals[i].dataIndex);
    }
}

} // namespace static_dump
