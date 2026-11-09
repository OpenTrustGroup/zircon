// Copyright 2026 Open Trust Group. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Inline encryption for block devices: keys, per-request encryption contexts
// and the keyslot bookkeeping a block driver shares with its storage
// controller.  The model follows the Linux kernel's blk-crypto framework, so
// keys and capabilities pass between a guest and the host without
// translation.  The encryption modes, key types and struct blk_crypto_cap
// live in <zircon/device/block.h> because block_info_t carries them.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/device/block.h>
#include <zircon/listnode.h>

__BEGIN_CDECLS

// Key sizes, in bytes.  A standard key is held in plain form; a hardware
// wrapped key only in a form the storage controller can unwrap.  The spelling
// matches the Linux kernel so that drivers ported from it may repeat these
// definitions.
#define BLK_CRYPTO_MAX_STANDARD_KEY_SIZE 64
#define BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE 128

#define BLK_CRYPTO_MAX_ANY_KEY_SIZE \
    (BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE > \
     BLK_CRYPTO_MAX_STANDARD_KEY_SIZE ? \
     BLK_CRYPTO_MAX_HW_WRAPPED_KEY_SIZE : BLK_CRYPTO_MAX_STANDARD_KEY_SIZE)

// The largest initialization vector, in bytes, and the number of 64-bit words
// in a data unit number.
#define BLK_CRYPTO_MAX_IV_SIZE 32
#define BLK_CRYPTO_DUN_ARRAY_SIZE (BLK_CRYPTO_MAX_IV_SIZE / 8)

// How a key encrypts data.
struct blk_crypto_config {
    enum blk_crypto_mode_num crypto_mode;
    // The data unit size in bytes, a power of two.
    uint32_t data_unit_size;
    // The number of bytes of the data unit number the key needs.
    uint32_t dun_bytes;
    enum blk_crypto_key_type key_type;
};

// A key as a guest hands it over: its configuration and raw bytes.
struct blk_crypto_base_key {
    struct blk_crypto_config crypto_cfg;
    // log2 of crypto_cfg.data_unit_size.
    uint32_t data_unit_size_bits;
    // The number of valid bytes in raw.
    uint32_t size;
    uint8_t raw[BLK_CRYPTO_MAX_ANY_KEY_SIZE];
};

// A key in use by a block driver.  The leading fields are a struct
// blk_crypto_base_key, reachable both as a whole and member by member.
struct blk_crypto_key {
    union {
        struct blk_crypto_base_key base_key;
        struct {
            struct blk_crypto_config crypto_cfg;
            uint32_t data_unit_size_bits;
            uint32_t size;
            uint8_t raw[BLK_CRYPTO_MAX_ANY_KEY_SIZE];
        };
    };
    // Driver-private bits that keep keys of different owners apart in the
    // keyslot hash; see blk_crypto_key_set_priv().
    uint8_t priv;
};

// Records the owner bits of a key.  The block driver implements it.
void blk_crypto_key_set_priv(struct blk_crypto_key* key, uint8_t priv);

struct blk_crypto_profile;
struct blk_crypto_keyslot;

// The encryption context of one read or write.
struct bio_crypt_ctx {
    const struct blk_crypto_key* bc_key;
    // The data unit number of the first data unit, least significant word
    // first.
    uint64_t bc_dun[BLK_CRYPTO_DUN_ARRAY_SIZE];
    // The keyslot holding bc_key while the request is in flight.
    struct blk_crypto_keyslot* keyslot;
};

// Operations a storage controller provides to program its keyslots.  Both
// return zero on success.
struct blk_crypto_ll_ops {
    int (*keyslot_program)(struct blk_crypto_profile* profile,
                           const struct blk_crypto_key* key, unsigned int slot);
    int (*keyslot_evict)(struct blk_crypto_profile* profile,
                         const struct blk_crypto_key* key, unsigned int slot);
};

// What a storage controller declares about its inline encryption.
struct blk_crypto_profile_dev {
    struct blk_crypto_ll_ops ll_ops;
    // The number of hardware keyslots.
    unsigned int num_slots;
    // The capabilities, reachable both as a whole and member by member.
    union {
        struct blk_crypto_cap caps;
        struct {
            uint32_t max_dun_bytes_supported;
            uint32_t key_types_supported;
            uint32_t modes_supported[BLK_ENCRYPTION_MODE_MAX];
        };
    };
};

// A hash list, a singly linked list whose nodes can unlink themselves.
struct hlist_node {
    struct hlist_node* next;
    struct hlist_node** pprev;
};

struct hlist_head {
    struct hlist_node* first;
};

// The idle-keyslot list is a zircon list.
typedef list_node_t list_head;

#ifndef __cplusplus
// One hardware keyslot.  Reference counting needs C11 atomics, so only C
// drivers see this type.
struct blk_crypto_keyslot {
    _Atomic int slot_refs;
    // Links the slot into the profile's idle list while slot_refs is zero.
    list_node_t idle_slot_node;
    // Links the slot into the profile's hash table while it holds a key.
    struct hlist_node hash_node;
    const struct blk_crypto_key* key;
    struct blk_crypto_profile* profile;
};
#endif

#ifndef INIT_HLIST_HEAD
#define INIT_HLIST_HEAD(head) ((head)->first = NULL)
#endif

static inline void hlist_add_head(struct hlist_node* node, struct hlist_head* head) {
    node->next = head->first;
    if (node->next) {
        node->next->pprev = &node->next;
    }
    head->first = node;
    node->pprev = &head->first;
}

static inline void hlist_del(struct hlist_node* node) {
    if (node->pprev) {
        *node->pprev = node->next;
        if (node->next) {
            node->next->pprev = node->pprev;
        }
    }
    node->next = NULL;
    node->pprev = NULL;
}

// Iterates over the entries of type |type| on a hash list, |member| naming the
// struct hlist_node inside the entry.
#ifndef hlist_for_each_entry
#define hlist_entry_or_null(node, type, member) \
    ((node) ? (type*)((uintptr_t)(node) - offsetof(type, member)) : (type*)NULL)
#define hlist_for_each_entry(type, pos, head, member)                    \
    for ((pos) = hlist_entry_or_null((head)->first, type, member); (pos); \
         (pos) = hlist_entry_or_null((pos)->member.next, type, member))
#endif

// Hashes a pointer into |bits| bits with the 64-bit golden ratio multiplier.
static inline uint32_t hash_ptr(const void* ptr, unsigned int bits) {
    return (uint32_t)(((uint64_t)(uintptr_t)ptr * 0x61C8864680B583EBull) >> (64 - bits));
}

// Rounds |n| up to the next power of two; |n| must be nonzero.
#ifndef roundup_pow_of_two
#define roundup_pow_of_two(n) \
    ((n) <= 1 ? 1UL : 1UL << (64 - __builtin_clzl((unsigned long)(n) - 1)))
#endif

__END_CDECLS
