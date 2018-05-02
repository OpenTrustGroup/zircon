// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/tables_generator.h"

#include "fidl/names.h"

namespace fidl {

namespace {

constexpr auto kIndent = "    ";

std::ostream& operator<<(std::ostream& stream, StringView view) {
    stream.rdbuf()->sputn(view.data(), view.size());
    return stream;
}

void Emit(std::ostream* file, StringView data) {
    *file << data;
}

void EmitNewlineAndIndent(std::ostream* file, size_t indent_level) {
    *file << "\n";
    while (indent_level--)
        *file << kIndent;
}

void EmitArrayBegin(std::ostream* file) {
    *file << "{";
}

void EmitArraySeparator(std::ostream* file, size_t indent_level) {
    *file << ",";
    EmitNewlineAndIndent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) {
    *file << "}";
}

void Emit(std::ostream* file, uint32_t value) {
    *file << value;
}

void Emit(std::ostream* file, types::HandleSubtype handle_subtype) {
    Emit(file, NameHandleZXObjType(handle_subtype));
}

void Emit(std::ostream* file, types::Nullability nullability) {
    switch (nullability) {
    case types::Nullability::kNullable:
        Emit(file, "::fidl::kNullable");
        break;
    case types::Nullability::kNonnullable:
        Emit(file, "::fidl::kNonnullable");
        break;
    }
}

} // namespace

void TablesGenerator::GenerateInclude(StringView filename) {
    Emit(&tables_file_, "#include ");
    Emit(&tables_file_, filename);
    Emit(&tables_file_, "\n");
}

void TablesGenerator::GenerateFilePreamble() {
    GenerateInclude("<lib/fidl/internal.h>");
    Emit(&tables_file_, "\nextern \"C\" {\n");
    Emit(&tables_file_, "\n");
}

void TablesGenerator::GenerateFilePostamble() {
    Emit(&tables_file_, "} // extern \"C\"\n");
}

template <typename Collection> void TablesGenerator::GenerateArray(const Collection& collection) {
    EmitArrayBegin(&tables_file_);

    if (!collection.empty())
        EmitNewlineAndIndent(&tables_file_, ++indent_level_);

    for (size_t i = 0; i < collection.size(); ++i) {
        if (i)
            EmitArraySeparator(&tables_file_, indent_level_);
        Generate(collection[i]);
    }

    if (!collection.empty())
        EmitNewlineAndIndent(&tables_file_, --indent_level_);

    EmitArrayEnd(&tables_file_);
}

void TablesGenerator::Generate(const coded::StructType& struct_type) {
    Emit(&tables_file_, "static const ::fidl::FidlField ");
    Emit(&tables_file_, NameFields(struct_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(struct_type.fields);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, NameTable(struct_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStruct(");
    Emit(&tables_file_, NameFields(struct_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.fields.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, struct_type.size);
    Emit(&tables_file_, ", \"");
    Emit(&tables_file_, struct_type.qname);
    Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::UnionType& union_type) {
    Emit(&tables_file_, "static const fidl_type_t* ");
    Emit(&tables_file_, NameMembers(union_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(union_type.types);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, NameTable(union_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnion(");
    Emit(&tables_file_, NameMembers(union_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.types.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.data_offset);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, union_type.size);
    Emit(&tables_file_, ", \"");
    Emit(&tables_file_, union_type.qname);
    Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::MessageType& message_type) {
    Emit(&tables_file_, "extern const fidl_type_t ");
    Emit(&tables_file_, NameTable(message_type.coded_name));
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "static const ::fidl::FidlField ");
    Emit(&tables_file_, NameFields(message_type.coded_name));
    Emit(&tables_file_, "[] = ");
    GenerateArray(message_type.fields);
    Emit(&tables_file_, ";\n");

    Emit(&tables_file_, "const fidl_type_t ");
    Emit(&tables_file_, NameTable(message_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStruct(");
    Emit(&tables_file_, NameFields(message_type.coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, message_type.fields.size());
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, message_type.size);
    Emit(&tables_file_, ", \"");
    Emit(&tables_file_, message_type.qname);
    Emit(&tables_file_, "\"));\n\n");
}

void TablesGenerator::Generate(const coded::HandleType& handle_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(handle_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, handle_type.subtype);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, handle_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::RequestHandleType& request_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(request_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, types::HandleSubtype::kChannel);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, request_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::InterfaceHandleType& interface_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(interface_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedHandle(");
    Emit(&tables_file_, types::HandleSubtype::kChannel);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, interface_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::ArrayType& array_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(array_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedArray(&");
    Emit(&tables_file_, NameTable(array_type.element_type->coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, array_type.element_size);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::StringType& string_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(string_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedString(");
    Emit(&tables_file_, string_type.max_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, string_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::VectorType& vector_type) {
    Emit(&tables_file_, "static const fidl_type_t ");
    Emit(&tables_file_, NameTable(vector_type.coded_name));
    Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedVector(");
    if (vector_type.element_type->coding_needed == coded::CodingNeeded::kNeeded) {
        Emit(&tables_file_, "&");
        Emit(&tables_file_, NameTable(vector_type.element_type->coded_name));
    } else {
        Emit(&tables_file_, "nullptr");
    }
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.max_count);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.element_size);
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, vector_type.nullability);
    Emit(&tables_file_, "));\n\n");
}

void TablesGenerator::Generate(const coded::Type* type) {
    if (type) {
        Emit(&tables_file_, "&");
        Emit(&tables_file_, NameTable(type->coded_name));
    } else {
        Emit(&tables_file_, "nullptr");
    }
}

void TablesGenerator::Generate(const coded::Field& field) {
    Emit(&tables_file_, "::fidl::FidlField(&");
    Emit(&tables_file_, NameTable(field.type->coded_name));
    Emit(&tables_file_, ", ");
    Emit(&tables_file_, field.offset);
    Emit(&tables_file_, ")");
}

void TablesGenerator::GeneratePointerIfNeeded(const coded::StructType& struct_type) {
    if (struct_type.referenced_by_pointer) {
        Emit(&tables_file_, "static const fidl_type_t ");
        Emit(&tables_file_, NameTable(struct_type.pointer_name));
        Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedStructPointer(&");
        Emit(&tables_file_, NameTable(struct_type.coded_name));
        Emit(&tables_file_, ".coded_struct));\n");
    }
}

void TablesGenerator::GeneratePointerIfNeeded(const coded::UnionType& union_type) {
    if (union_type.referenced_by_pointer) {
        Emit(&tables_file_, "static const fidl_type_t ");
        Emit(&tables_file_, NameTable(union_type.pointer_name));
        Emit(&tables_file_, " = fidl_type_t(::fidl::FidlCodedUnionPointer(&");
        Emit(&tables_file_, NameTable(union_type.coded_name));
        Emit(&tables_file_, ".coded_union));\n");
    }
}

void TablesGenerator::GenerateForward(const coded::StructType& struct_type) {
    Emit(&tables_file_, "extern const fidl_type_t ");
    Emit(&tables_file_, NameTable(struct_type.coded_name));
    Emit(&tables_file_, ";\n");
}

void TablesGenerator::GenerateForward(const coded::UnionType& union_type) {
    Emit(&tables_file_, "extern const fidl_type_t ");
    Emit(&tables_file_, NameTable(union_type.coded_name));
    Emit(&tables_file_, ";\n");
}

const coded::Type* TablesGenerator::CompileType(const flat::Type* type) {
    switch (type->kind) {
    case flat::Type::Kind::kArray: {
        auto array_type = static_cast<const flat::ArrayType*>(type);
        auto iter = array_type_map_.find(array_type);
        if (iter != array_type_map_.end())
            return iter->second;
        auto coded_element_type = CompileType(array_type->element_type.get());
        uint32_t array_size = array_type->size;
        uint32_t element_size = array_type->element_type->size;
        auto name = NameCodedArray(coded_element_type->coded_name, array_size);
        auto coded_array_type = std::make_unique<coded::ArrayType>(
            std::move(name), coded_element_type, array_size, element_size);
        array_type_map_[array_type] = coded_array_type.get();
        coded_types_.push_back(std::move(coded_array_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kVector: {
        auto vector_type = static_cast<const flat::VectorType*>(type);
        auto iter = vector_type_map_.find(vector_type);
        if (iter != vector_type_map_.end())
            return iter->second;
        auto coded_element_type = CompileType(vector_type->element_type.get());
        uint32_t max_count = vector_type->element_count.Value();
        uint32_t element_size = coded_element_type->size;
        StringView element_name = coded_element_type->coded_name;
        auto name = NameCodedVector(element_name, max_count, vector_type->nullability);
        auto coded_vector_type = std::make_unique<coded::VectorType>(
            std::move(name), coded_element_type, max_count, element_size, vector_type->nullability);
        vector_type_map_[vector_type] = coded_vector_type.get();
        coded_types_.push_back(std::move(coded_vector_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kString: {
        auto string_type = static_cast<const flat::StringType*>(type);
        auto iter = string_type_map_.find(string_type);
        if (iter != string_type_map_.end())
            return iter->second;
        uint32_t max_size = string_type->max_size.Value();
        auto name = NameCodedString(max_size, string_type->nullability);
        auto coded_string_type = std::make_unique<coded::StringType>(std::move(name), max_size,
                                                                     string_type->nullability);
        string_type_map_[string_type] = coded_string_type.get();
        coded_types_.push_back(std::move(coded_string_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kHandle: {
        auto handle_type = static_cast<const flat::HandleType*>(type);
        auto iter = handle_type_map_.find(handle_type);
        if (iter != handle_type_map_.end())
            return iter->second;
        auto name = NameCodedHandle(handle_type->subtype, handle_type->nullability);
        auto coded_handle_type = std::make_unique<coded::HandleType>(
            std::move(name), handle_type->subtype, handle_type->nullability);
        handle_type_map_[handle_type] = coded_handle_type.get();
        coded_types_.push_back(std::move(coded_handle_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kRequestHandle: {
        auto request_type = static_cast<const flat::RequestHandleType*>(type);
        auto iter = request_type_map_.find(request_type);
        if (iter != request_type_map_.end())
            return iter->second;
        auto name = NameCodedRequestHandle(NameName(request_type->name, "_", "_"), request_type->nullability);
        auto coded_request_type =
            std::make_unique<coded::RequestHandleType>(std::move(name), request_type->nullability);
        request_type_map_[request_type] = coded_request_type.get();
        coded_types_.push_back(std::move(coded_request_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kPrimitive: {
        auto primitive_type = static_cast<const flat::PrimitiveType*>(type);
        auto iter = primitive_type_map_.find(primitive_type);
        if (iter != primitive_type_map_.end())
            return iter->second;
        auto name = NamePrimitiveSubtype(primitive_type->subtype);
        auto coded_primitive_type = std::make_unique<coded::PrimitiveType>(
            std::move(name), primitive_type->subtype,
            flat::PrimitiveType::SubtypeSize(primitive_type->subtype));
        primitive_type_map_[primitive_type] = coded_primitive_type.get();
        coded_types_.push_back(std::move(coded_primitive_type));
        return coded_types_.back().get();
    }
    case flat::Type::Kind::kIdentifier: {
        auto identifier_type = static_cast<const flat::IdentifierType*>(type);
        auto iter = named_coded_types_.find(&identifier_type->name);
        if (iter == named_coded_types_.end()) {
            assert(false && "unknown type in named type map!");
        }
        // We may need to set the emit-pointer bit on structs and unions now.
        auto coded_type = iter->second.get();
        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct: {
            // Structs were compiled as part of decl compilation,
            // but we may now need to generate the StructPointer.
            if (identifier_type->nullability != types::Nullability::kNullable)
                break;
            auto coded_struct_type = static_cast<coded::StructType*>(coded_type);
            coded_struct_type->referenced_by_pointer = true;
            coded_types_.push_back(std::make_unique<coded::StructPointerType>(
                coded_struct_type->pointer_name, coded_struct_type));
            return coded_types_.back().get();
        }
        case coded::Type::Kind::kUnion: {
            // Unions were compiled as part of decl compilation,
            // but we may now need to generate the UnionPointer.
            if (identifier_type->nullability != types::Nullability::kNullable)
                break;
            auto coded_union_type = static_cast<coded::UnionType*>(coded_type);
            coded_union_type->referenced_by_pointer = true;
            coded_types_.push_back(std::make_unique<coded::UnionPointerType>(
                coded_union_type->pointer_name, coded_union_type));
            return coded_types_.back().get();
        }
        case coded::Type::Kind::kInterface: {
            auto iter = interface_type_map_.find(identifier_type);
            if (iter != interface_type_map_.end())
                return iter->second;
            auto name = NameCodedInterfaceHandle(NameName(identifier_type->name, "_", "_"),
                                                 identifier_type->nullability);
            auto coded_interface_type = std::make_unique<coded::InterfaceHandleType>(
                std::move(name), identifier_type->nullability);
            interface_type_map_[identifier_type] = coded_interface_type.get();
            coded_types_.push_back(std::move(coded_interface_type));
            return coded_types_.back().get();
        }
        case coded::Type::Kind::kPrimitive:
            // These are from enums. We don't need to do anything with them.
            break;
        case coded::Type::Kind::kInterfaceHandle:
        case coded::Type::Kind::kStructPointer:
        case coded::Type::Kind::kUnionPointer:
        case coded::Type::Kind::kMessage:
        case coded::Type::Kind::kRequestHandle:
        case coded::Type::Kind::kHandle:
        case coded::Type::Kind::kArray:
        case coded::Type::Kind::kVector:
        case coded::Type::Kind::kString:
            assert(false && "anonymous type in named type map!");
            break;
        }
        return coded_type;
    }
    }
}

void TablesGenerator::CompileFields(const flat::Decl* decl) {
    switch (decl->kind) {
    case flat::Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const flat::Interface*>(decl);
        coded::InterfaceType* coded_interface =
            static_cast<coded::InterfaceType*>(named_coded_types_[&decl->name].get());
        size_t i = 0;
        for (const auto& method : interface_decl->methods) {
            auto CompileMessage = [&](const flat::Interface::Method::Message& message) -> void {
                std::unique_ptr<coded::MessageType>& coded_message = coded_interface->messages[i++];
                std::vector<coded::Field>& request_fields = coded_message->fields;
                for (const auto& parameter : message.parameters) {
                    std::string parameter_name =
                        coded_message->coded_name + "_" + std::string(parameter.name.data());
                    auto coded_parameter_type = CompileType(parameter.type.get());
                    if (coded_parameter_type->coding_needed == coded::CodingNeeded::kNeeded)
                        request_fields.emplace_back(coded_parameter_type,
                                                    parameter.fieldshape.Offset());
                }
                // We move the coded_message to coded_types_ so that we'll generate tables for the
                // message
                // in the proper order.
                coded_types_.push_back(std::move(coded_message));
            };
            if (method.maybe_request) {
                CompileMessage(*method.maybe_request);
            }
            if (method.maybe_response) {
                CompileMessage(*method.maybe_response);
            }
        }
        break;
    }
    case flat::Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const flat::Struct*>(decl);
        coded::StructType* coded_struct =
            static_cast<coded::StructType*>(named_coded_types_[&decl->name].get());
        std::vector<coded::Field>& struct_fields = coded_struct->fields;
        for (const auto& member : struct_decl->members) {
            std::string member_name =
                coded_struct->coded_name + "_" + std::string(member.name.data());
            auto coded_member_type = CompileType(member.type.get());
            if (coded_member_type->coding_needed == coded::CodingNeeded::kNeeded)
                struct_fields.emplace_back(coded_member_type, member.fieldshape.Offset());
        }
        break;
    }
    case flat::Decl::Kind::kUnion: {
        auto union_decl = static_cast<const flat::Union*>(decl);
        coded::UnionType* union_struct =
            static_cast<coded::UnionType*>(named_coded_types_[&decl->name].get());
        std::vector<const coded::Type*>& union_members = union_struct->types;
        for (const auto& member : union_decl->members) {
            std::string member_name =
                union_struct->coded_name + "_" + std::string(member.name.data());
            auto coded_member_type = CompileType(member.type.get());
            if (coded_member_type->coding_needed == coded::CodingNeeded::kNeeded) {
                union_members.push_back(coded_member_type);
            } else {
                // We need union_members.size() to match union_decl->members.size() because
                // the coding tables will use the union |tag| to index into the member array.
                union_members.push_back(nullptr);
            }
        }
        break;
    }
    default: { break; }
    }
}

void TablesGenerator::Compile(const flat::Decl* decl) {
    switch (decl->kind) {
    case flat::Decl::Kind::kConst:
        // Nothing to do for const declarations.
        break;
    case flat::Decl::Kind::kEnum: {
        auto enum_decl = static_cast<const flat::Enum*>(decl);
        std::string enum_name = NameName(enum_decl->name, "_", "_");
        named_coded_types_.emplace(&enum_decl->name,
                                   std::make_unique<coded::PrimitiveType>(
                                       std::move(enum_name), enum_decl->type,
                                       flat::PrimitiveType::SubtypeSize(enum_decl->type)));
        break;
    }
    case flat::Decl::Kind::kInterface: {
        auto interface_decl = static_cast<const flat::Interface*>(decl);
        std::string interface_name = NameInterface(*interface_decl);
        std::vector<std::unique_ptr<coded::MessageType>> interface_messages;
        for (const auto& method : interface_decl->methods) {
            std::string method_name = NameMethod(interface_name, method);
            auto CreateMessage = [&](const flat::Interface::Method::Message& message,
                                     types::MessageKind kind) -> void {
                std::string message_name = NameMessage(method_name, kind);
                interface_messages.push_back(std::make_unique<coded::MessageType>(
                    std::move(message_name), std::vector<coded::Field>(), message.typeshape.Size(),
                    message_name));
            };
            if (method.maybe_request) {
                CreateMessage(*method.maybe_request, types::MessageKind::kRequest);
            }
            if (method.maybe_response) {
                auto kind = method.maybe_request ? types::MessageKind::kResponse
                                                 : types::MessageKind::kEvent;
                CreateMessage(*method.maybe_response, kind);
            }
        }
        named_coded_types_.emplace(
            &decl->name, std::make_unique<coded::InterfaceType>(std::move(interface_messages)));
        break;
    }
    case flat::Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const flat::Struct*>(decl);
        std::string struct_name = NameCodedStruct(struct_decl);
        std::string pointer_name = NamePointer(struct_name);
        named_coded_types_.emplace(
            &decl->name,
            std::make_unique<coded::StructType>(std::move(struct_name), std::vector<coded::Field>(),
                                                struct_decl->typeshape.Size(),
                                                std::move(pointer_name), NameName(struct_decl->name, ".", "/")));
        break;
    }
    case flat::Decl::Kind::kUnion: {
        auto union_decl = static_cast<const flat::Union*>(decl);
        std::string union_name = NameCodedUnion(union_decl);
        std::string pointer_name = NamePointer(union_name);
        named_coded_types_.emplace(
            &decl->name, std::make_unique<coded::UnionType>(
                             std::move(union_name), std::vector<const coded::Type*>(),
                             union_decl->membershape.Offset(), union_decl->typeshape.Size(),
                             std::move(pointer_name), NameName(union_decl->name, ".", "/")));
        break;
    }
    }
}

std::ostringstream TablesGenerator::Produce() {
    GenerateFilePreamble();

    for (const auto& decl : library_->declaration_order_) {
        Compile(decl);
    }

    for (const auto& decl : library_->declaration_order_) {
        if (decl->name.library() != library_)
            continue;
        CompileFields(decl);
    }

    for (const auto& decl : library_->declaration_order_) {
        coded::Type* coded_type = named_coded_types_[&decl->name].get();
        if (!coded_type)
            continue;
        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct:
            GenerateForward(*static_cast<const coded::StructType*>(coded_type));
            break;
        case coded::Type::Kind::kUnion:
            GenerateForward(*static_cast<const coded::UnionType*>(coded_type));
            break;
        default:
            break;
        }
    }

    Emit(&tables_file_, "\n");

    for (const auto& decl : library_->declaration_order_) {
        coded::Type* coded_type = named_coded_types_[&decl->name].get();
        if (!coded_type)
            continue;
        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct:
            GeneratePointerIfNeeded(*static_cast<const coded::StructType*>(coded_type));
            break;
        case coded::Type::Kind::kUnion:
            GeneratePointerIfNeeded(*static_cast<const coded::UnionType*>(coded_type));
            break;
        default:
            break;
        }
    }

    Emit(&tables_file_, "\n");

    for (const auto& coded_type : coded_types_) {
        if (coded_type->coding_needed == coded::CodingNeeded::kNotNeeded)
            continue;

        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct:
        case coded::Type::Kind::kStructPointer:
        case coded::Type::Kind::kUnion:
        case coded::Type::Kind::kUnionPointer:
            // These are generated in the next phase.
            break;
        case coded::Type::Kind::kInterface:
            // Nothing to generate for interfaces. We've already moved the
            // messages from the interface into coded_types_ directly.
            break;
        case coded::Type::Kind::kMessage:
            Generate(*static_cast<const coded::MessageType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kHandle:
            Generate(*static_cast<const coded::HandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kInterfaceHandle:
            Generate(*static_cast<const coded::InterfaceHandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kRequestHandle:
            Generate(*static_cast<const coded::RequestHandleType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kArray:
            Generate(*static_cast<const coded::ArrayType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kString:
            Generate(*static_cast<const coded::StringType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kVector:
            Generate(*static_cast<const coded::VectorType*>(coded_type.get()));
            break;
        case coded::Type::Kind::kPrimitive:
            // These are only around to provide size information to
            // vectors. There's never anything to generate, and this
            // should not be reached.
            assert(false && "Primitive types should never need coding tables");
            break;
        }
    }

    for (const auto& decl : library_->declaration_order_) {
        if (decl->name.library() != library_)
            continue;

        coded::Type* coded_type = named_coded_types_[&decl->name].get();
        if (!coded_type)
            continue;
        switch (coded_type->kind) {
        case coded::Type::Kind::kStruct:
            Generate(*static_cast<const coded::StructType*>(coded_type));
            break;
        case coded::Type::Kind::kUnion:
            Generate(*static_cast<const coded::UnionType*>(coded_type));
            break;
        default:
            continue;
        }
    }

    GenerateFilePostamble();

    return std::move(tables_file_);
}

} // namespace fidl
