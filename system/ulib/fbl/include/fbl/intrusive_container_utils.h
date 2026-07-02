// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_pointer_traits.h>
#include <fbl/type_support.h>

namespace fbl {

// DefaultKeyedObjectTraits defines a default implementation of traits used to
// manage objects stored in associative containers such as hash-tables and
// trees.
//
// At a minimum, a class or a struct which is to be used to define the
// traits of a keyed object must define the following public members.
//
// GetKey   : A static method which takes a constant reference to an object (the
//            type of which is infered from PtrType) and returns a KeyType
//            instance corresponding to the key for an object.
// LessThan : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be less than key2 for
//            sorting purposes.
// EqualTo  : A static method which takes two keys (key1 and key2) and returns
//            true if-and-only-if key1 is considered to be equal to key2.
//
// Rules for keys:
// ++ The type of key returned by GetKey must be compatible with the key which
//    was specified for the container.
// ++ The key for an object must remain constant for as long as the object is
//    contained within a container.
// ++ When comparing keys, comparisons must obey basic transative and
//    commutative properties.  That is to say...
//    LessThan(A, B) and LessThan(B, C) implies LessThan(A, C)
//    EqualTo(A, B) and EqualTo(B, C) implies EqualTo(A, C)
//    EqualTo(A, B) if-and-only-if EqualTo(B, A)
//    LessThan(A, B) if-and-only-if EqualTo(B, A) or (not LessThan(B, A))
//
// DefaultKeyedObjectTraits is a helper class which allows an object to be
// treated as a keyed-object by implementing a const GetKey method which returns
// a key of the appropriate type.  The key type must be compatible with the
// container key type, and must have definitions of the < and == operators for
// the purpose of generating implementation of LessThan and EqualTo.
template <typename KeyType, typename ObjType>
struct DefaultKeyedObjectTraits {
    static KeyType GetKey(const ObjType& obj)                       { return obj.GetKey(); }
    static bool LessThan(const KeyType& key1, const KeyType& key2)  { return key1 <  key2; }
    static bool EqualTo (const KeyType& key1, const KeyType& key2)  { return key1 == key2; }
};

}  // namespace fbl

namespace fbl {
namespace internal {

// DirectEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) direct erase
// operation depending on whether or not the HashTable's bucket type supports
// O(k) erase.
template <typename ContainerType, typename Enable = void>
struct DirectEraseUtils;

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        typename enable_if<ContainerType::SupportsConstantOrderErase == false, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase_if(
            [&obj](const ValueType& other) -> bool {
                return &obj == &other;
            });
    }
};

template <typename ContainerType>
struct DirectEraseUtils<
        ContainerType,
        typename enable_if<ContainerType::SupportsConstantOrderErase == true, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    static PtrType erase(ContainerType& container, ValueType& obj) {
        return container.erase(obj);
    }
};

// KeyEraseUtils
//
// A utility class used by HashTable to implement an O(n) or O(k) erase-by-key
// operation depending on whether or not the HashTable's bucket type is
// associative or not.
template <typename ContainerType, typename KeyTraits, typename Enable = void>
struct KeyEraseUtils;

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        typename enable_if<ContainerType::IsAssociative == false, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;
    using ValueType = typename PtrTraits::ValueType;

    template <typename KeyType>
    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase_if(
            [key](const ValueType& other) -> bool {
                return KeyTraits::EqualTo(key, KeyTraits::GetKey(other));
            });
    }
};

template <typename ContainerType, typename KeyTraits>
struct KeyEraseUtils<
        ContainerType,
        KeyTraits,
        typename enable_if<ContainerType::IsAssociative == true, void>::type> {
    using PtrTraits = typename ContainerType::PtrTraits;
    using PtrType   = typename PtrTraits::PtrType;

    template <typename KeyType>
    static PtrType erase(ContainerType& container, const KeyType& key) {
        return container.erase(key);
    }
};

}  // namespace internal

// ---------------------------------------------------------------------------
// Tagged intrusive-container support.
//
// A C++14 reimplementation of the subset of the newer fbl tagged-container API
// used by the pager. Objects that live in several intrusive containers
// distinguish them with empty "tag" types instead of multiple node-state
// members, letting upstream code such as
//   class T : public fbl::ContainableBaseClasses<
//                 fbl::TaggedDoublyLinkedListable<T*, TagA>,
//                 fbl::TaggedDoublyLinkedListable<T*, TagB>> { ... };
//   fbl::TaggedDoublyLinkedList<T*, TagA> list;
//   fbl::InContainer<TagA>(obj);
// compile unchanged. It is built additively on top of the existing
// DoublyLinkedList (the core containers are untouched). The upstream version
// relies on C++17 (if constexpr, fold expressions); this one uses ordinary
// C++14 templates.
// ---------------------------------------------------------------------------

// Tag used when an object lives in a single, untagged container.
struct DefaultObjectTag {};

namespace internal {
template <typename PtrType, typename TagType>
struct TaggedDoublyLinkedListTraits;
}  // namespace internal

// Base class contributing one tag-specific doubly-linked-list node state.
template <typename PtrType, typename TagType>
class TaggedDoublyLinkedListable {
public:
    using TagTypeAlias = TagType;
    bool InContainer() const { return tagged_dll_node_state_.InContainer(); }

private:
    friend struct internal::TaggedDoublyLinkedListTraits<PtrType, TagType>;
    DoublyLinkedListNodeState<PtrType> tagged_dll_node_state_;
};

namespace internal {
template <typename T>
struct TaggedContainableIdentity { using type = T; };

// Resolve, at compile time, which base in Bases... carries TagTypeAlias == Tag.
// Only the selected branch's ::type is instantiated, so a shorter chain need
// not name the terminator (which has no ::type and errors if a tag is absent).
template <typename Tag, typename... Bases>
struct TaggedContainableForTag;
template <typename Tag, typename First, typename... Rest>
struct TaggedContainableForTag<Tag, First, Rest...> {
    using type = typename conditional<
        is_same<Tag, typename First::TagTypeAlias>::value,
        TaggedContainableIdentity<First>,
        TaggedContainableForTag<Tag, Rest...>>::type::type;
};
template <typename Tag>
struct TaggedContainableForTag<Tag> {};
}  // namespace internal

// Combine several Tagged*Listable base classes and look one up by its tag.
template <typename... Bases>
class ContainableBaseClasses : public Bases... {
public:
    template <typename Tag>
    typename internal::TaggedContainableForTag<Tag, Bases...>::type& GetContainableByTag() {
        return static_cast<
            typename internal::TaggedContainableForTag<Tag, Bases...>::type&>(*this);
    }
    template <typename Tag>
    const typename internal::TaggedContainableForTag<Tag, Bases...>::type&
    GetContainableByTag() const {
        return static_cast<
            const typename internal::TaggedContainableForTag<Tag, Bases...>::type&>(*this);
    }
};

namespace internal {
// Node traits routing a tagged list to the object's tag-specific node state.
template <typename PtrType, typename TagType>
struct TaggedDoublyLinkedListTraits {
    using PtrTraits = ContainerPtrTraits<PtrType>;
    static DoublyLinkedListNodeState<PtrType>& node_state(typename PtrTraits::RefType obj) {
        return obj.template GetContainableByTag<TagType>().tagged_dll_node_state_;
    }
};
}  // namespace internal

// A doubly-linked list whose membership is keyed by a tag.
template <typename PtrType, typename TagType>
using TaggedDoublyLinkedList =
    DoublyLinkedList<PtrType, internal::TaggedDoublyLinkedListTraits<PtrType, TagType>>;

// Membership test for a tagged container: fbl::InContainer<Tag>(obj).
template <typename TagType, typename Containable>
bool InContainer(const Containable& obj) {
    return obj.template GetContainableByTag<TagType>().InContainer();
}

}  // namespace fbl
