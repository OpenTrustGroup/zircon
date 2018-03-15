// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_AST_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_AST_H_

#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "types.h"

// The types in this file define structures that much more closely map
// the coding tables (i.e., fidl_type_t) for (de)serialization,
// defined at ulib/fidl/include/coding.h and so on.

// In particular, compared to the flat_ast version:
// - All files in the library are resolved together
// - Names have been unnested and fully qualified
// - All data structure sizes and layouts have been computed

namespace fidl {
namespace coded {

enum struct CodingNeeded {
    // There is interesting coding information about the location of
    // pointers, allocations, or handles for this type.
    kNeeded,

    // There is no coding information needed for this type. That is,
    // it contains no pointers or handles, and is just primitive
    // types, or fixed size aggregates thereof.
    kNotNeeded,
};

struct Type;

struct Field {
    Field(const Type* type, uint32_t offset)
        : type(type), offset(offset) {}

    const Type* type;
    const uint32_t offset;
};

struct Type {
    virtual ~Type() = default;

    enum struct Kind {
        kPrimitive,
        kHandle,
        kInterfaceHandle,
        kRequestHandle,
        kStruct,
        kUnion,
        kArray,
        kString,
        kVector,
    };

    Type(Kind kind, std::string coded_name, CodingNeeded coding_needed)
        : kind(kind), coded_name(std::move(coded_name)), coding_needed(coding_needed) {}

    const Kind kind;
    const std::string coded_name;
    const CodingNeeded coding_needed;
};

inline CodingNeeded SomeFieldIsNeeded(const std::vector<Field>& fields) {
    for (const auto& field : fields) {
        if (field.type->coding_needed == CodingNeeded::kNeeded)
            return CodingNeeded::kNeeded;
    }
    return CodingNeeded::kNotNeeded;
}

inline CodingNeeded SomeTypeIsNeeded(const std::vector<const Type*>& types) {
    for (const auto& type : types) {
        if (type->coding_needed == CodingNeeded::kNeeded)
            return CodingNeeded::kNeeded;
    }
    return CodingNeeded::kNotNeeded;
}

struct PrimitiveType : public Type {
    PrimitiveType(std::string name, types::PrimitiveSubtype subtype)
        : Type(Kind::kPrimitive, std::move(name), CodingNeeded::kNotNeeded), subtype(subtype) {}

    const types::PrimitiveSubtype subtype;
};

struct HandleType : public Type {
    HandleType(std::string name, types::HandleSubtype subtype, types::Nullability nullability)
        : Type(Kind::kHandle, std::move(name), CodingNeeded::kNeeded), subtype(subtype), nullability(nullability) {}

    const types::HandleSubtype subtype;
    const types::Nullability nullability;
};

struct InterfaceHandleType : public Type {
    InterfaceHandleType(std::string name, types::Nullability nullability)
        : Type(Kind::kInterfaceHandle, std::move(name), CodingNeeded::kNeeded), nullability(nullability) {}

    const types::Nullability nullability;
};

struct RequestHandleType : public Type {
    RequestHandleType(std::string name, types::Nullability nullability)
        : Type(Kind::kRequestHandle, std::move(name), CodingNeeded::kNeeded), nullability(nullability) {}

    const types::Nullability nullability;
};

struct StructType : public Type {
    StructType(std::string name, std::vector<Field> fields, uint32_t size)
        : Type(Kind::kStruct, std::move(name), SomeFieldIsNeeded(fields)), fields(std::move(fields)), size(size) {}

    const std::vector<Field> fields;
    const uint32_t size;
    bool referenced_by_pointer = false;
};

struct UnionType : public Type {
    UnionType(std::string name, std::vector<const Type*> types, uint32_t data_offset, uint32_t size)
        : Type(Kind::kUnion, std::move(name), SomeTypeIsNeeded(types)),
          types(std::move(types)), data_offset(data_offset), size(size) {}

    const std::vector<const Type*> types;
    const uint32_t data_offset;
    const uint32_t size;
    bool referenced_by_pointer = false;
};

struct ArrayType : public Type {
    ArrayType(std::string name, const Type* element_type, uint32_t array_size,
              uint32_t element_size)
        : Type(Kind::kArray, std::move(name), element_type->coding_needed), element_type(element_type), array_size(array_size),
          element_size(element_size) {}

    const Type* const element_type;
    const uint32_t array_size;
    const uint32_t element_size;
};

struct StringType : public Type {
    StringType(std::string name, uint32_t max_size, types::Nullability nullability)
        : Type(Kind::kString, std::move(name), CodingNeeded::kNeeded), max_size(max_size), nullability(nullability) {}

    const uint32_t max_size;
    const types::Nullability nullability;
};

struct VectorType : public Type {
    VectorType(std::string name, const Type* element_type, uint32_t max_count,
               uint32_t element_size, types::Nullability nullability)
        : Type(Kind::kVector, std::move(name), CodingNeeded::kNeeded),
          element_type(element_type), max_count(max_count),
          element_size(element_size), nullability(nullability) {}

    const Type* const element_type;
    const uint32_t max_count;
    const uint32_t element_size;
    const types::Nullability nullability;
};

} // namespace coded
} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_CODED_AST_H_
