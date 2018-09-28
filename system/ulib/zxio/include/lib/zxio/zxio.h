// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_ZXIO_H_
#define LIB_ZXIO_ZXIO_H_

#include <fuchsia/io/c/fidl.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

typedef fuchsia_io_NodeAttributes zxio_node_attr_t;
typedef fuchsia_io_SeekOrigin zxio_seek_origin_t;

// A IO object.
//
// Provides an ergnomic C interface to the fuchsia.io family of protocols.
// These protocols are optimized for efficiency at the cost of ergonomics. This
// object provides a more ergonomic interface to the same underlying protocol
// without sacrificing (much) performance.
//
// A zxio_t also abstracts over several related protocols (e.g., vmofile,
// file, and directory) to provide a uniform interface. Advanced clients can
// also provide their own implementation of the underlying ops table to
// provide drop-in replacements for zxio_t with different backends.
//
// # Threading model
//
// Most operations on zxio_t objects can be called from any thread with
// external synchronization. However, the caller needs to synchronize
// operations that consume the zxio_t with other operations.
typedef struct zxio zxio_t;

// Node

// Create a |zxio_t| object from |node|.
//
// To free the resources allocated by this function, either call
// |zxio_destroy| to retreive the |zx_handle_t| or |zxio_close| to
// destroy the |zx_handle_t| as well.
//
// The |node| must be a channel whose remote endpoint implements the
// |fuchsia.io.Node| protocol.
//
// Always consumes |node|.
zx_status_t zxio_acquire_node(zx_handle_t node, zxio_t** out_io);

// Create a |zxio_t| object from |socket|.
//
// To free the resources allocated by this function, either call
// |zxio_destroy| to retreive the |zx_handle_t| or |zxio_close| to
// destroy the |zx_handle_t| as well.
//
// The |socket| must be a socket.
//
// Always consumes |socket|.
zx_status_t zxio_acquire_socket(zx_handle_t socket, zxio_t** out_io);

// Destroy |io| and produce a |zx_handle_t|.
//
// Recovers the underlying |zx_handle_t| for |io|. Does not terminate the
// connection with the server.
//
// Always consumes |io|.
zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_handle);

// Terminates connection with the server.
//
// Always consumes |io|.
zx_status_t zxio_close(zxio_t* io);

// Creates another connection to the same remote object.
//
// The connection is represented as a |zxio_t|. This call blocks until the
// remote server is able to describe the new connection.
//
// See io.fidl for the available |flags|.
zx_status_t zxio_clone(zxio_t* io, uint32_t flags, zxio_t** out_io);

// Creates another connection to the same remote object.
//
// The connection is represented as a |zx_handle_t|. The caller is responsible
// for creating the |zx_handle_t|, which must be a channel. This call does not
// block on the remote server.
//
// See io.fidl for the available |flags|.
zx_status_t zxio_clone_async(zxio_t* io, uint32_t flags,
                             zx_handle_t request);

// Synchronizes updates to the file to the underlying media, if it exists.
zx_status_t zxio_sync(zxio_t* io);

// Returns information about the file.
zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attr_t* out_attr);

// Update information about the file.
//
// See io.fidl for the available |flags|.
zx_status_t zxio_attr_set(zxio_t* io, uint32_t flags,
                          const zxio_node_attr_t* attr);

// File

// Attempt to read |capacity| bytes from the file at the current seek offset.
//
// The seek offset is moved forward by the actual number of bytes read.
//
// The actual number of bytes read is returned in |out_actual|.
zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity,
                      size_t* out_actual);

// Attempt to read |capacity| bytes from the file at the provided offset.
//
// Does not affect the seek offset.
//
// The actual number of bytes read is returned in |out_actual|.
zx_status_t zxio_read_at(zxio_t* io, size_t offset, void* buffer,
                         size_t capacity, size_t* out_actual);

// Attempt to write data to the file at the current seek offset.
//
// The seek offset is moved forward by the actual number of bytes written.
//
// The actual number of bytes written is returned in |out_actual|.
zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity,
                       size_t* out_actual);

// Attempt to write data to the file at the provided offset.
//
// Does not affect the seek offset.
//
// The actual number of bytes written is returned in |out_actual|.
zx_status_t zxio_write_at(zxio_t* io, size_t offset, const void* buffer,
                          size_t capacity, size_t* out_actual);

// Modify the seek offset.
//
// The seek offset for the file is modified by |offset| relative to |start|.
//
// The resulting seek offset relative to the start of the file is returned in
// |out_offset|.
zx_status_t zxio_seek(zxio_t* io, size_t offset,
                      zxio_seek_origin_t start, size_t* out_offset);

// Shrink the file size to |length| bytes.
zx_status_t zxio_trucate(zxio_t* io, size_t length);

// Returns the flags associated with the file.
//
// These flags are typically set when the file is opened but can be modified by
// |zxio_flags_set|.
//
// See io.fidl for the available |flags|.
zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags);

// Modifies the flags associated with the file.
//
// This function can modify the following flags:
//
//  * |fuchsia_io_OPEN_FLAG_APPEND|.
//
// See io.fidl for the available |flags|.
zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags);

// Get a read-only VMO containing the whole contents of the file.
//
// This function creates a clone of the underlying VMO when possible. If the
// function cannot create a clone, the function will eagerly read the contents
// of the file into a freshly-created VMO.
zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size);

// Get a read-only VMO containing the whole contents of the file.
//
// This function creates a clone of the underlying VMO when possible. If the
// function cannot create a clone, the function will return an error.
zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size);

// Get a read-only handle to the exact VMO used by the file system server to
// represent the file.
//
// This function fails if the server does not have an exact VMO representation
// of the file.
zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size);

// Directory

// Open a new file relative to the given |directory|.
//
// The connection is represented as a |zxio_t|. This call blocks until the
// remote server is able to describe the new connection.
//
// See io.fidl for the available |flags| and |mode|.
zx_status_t zxio_open(zxio_t* directory, uint32_t flags, uint32_t mode,
                      const char* path, zxio_t** out_io);

// Open a new file relative to the given |directory|.
//
// The connection is represented as a |zx_handle_t|. The caller is responsible
// for creating the |zx_handle_t|, which must be a channel. This call does not
// block on the remote server.
//
// See io.fidl for the available |flags| and |mode|.
zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags,
                            uint32_t mode, const char* path,
                            zx_handle_t request);

// Remove an file relative to the given directory.
zx_status_t zxio_unlink(zxio_t* directory, const char* path);

// Attempts to rename |old_path| relative to |old_directory| to |new_path|
// relative to |new_directory|.
//
// |old_directory| and |new_directory| may be aliased.
zx_status_t zxio_rename(zxio_t* old_directory, const char* old_path,
                        zxio_t* new_directory, const char* new_path);

// Attempts to link |dst_path| relative to |dst_directory| to |src_path|
// relative to |src_directory|.
//
// |src_directory| and |dst_directory| may be aliased.
zx_status_t zxio_link(zxio_t* src_directory, const char* src_path,
                      zxio_t* dst_directory, const char* dst_path);

// Directory iterator

// An entry in a directory.
typedef struct zxio_dirent {
      // The inode number of the entry.
      uint64_t inode;

      // The length of the name of the entry.
      uint8_t size;

      // The type of the entry.
      //
      // Aligned with the POSIX d_type values.
      uint8_t type;

      // The name of the entry.
      //
      // This string is not null terminated. Instead, refer to |size| to
      // determine the length of the string.
      char name[0];
} zxio_dirent_t;

// An iterator for |zxio_dirent_t| objects.
//
// To start iterating directory entries, call |zxio_dirent_iterator_init| to
// initialize the |opaque| contents of the iterator. Then, call
// |zxio_dirent_iterator_next| to advance the iterator.
//
// Typically allocated on the stack.
typedef struct zxio_dirent_iterator {
    uint64_t opaque[8];
} zxio_dirent_iterator_t;

// A reasonable default capacity for |zxio_dirent_iterator_init|.
#define ZXIO_DIRENT_ITERATOR_DEFAULT_BUFFER_SIZE ((size_t)4096)

// Initializes a |zxio_dirent_iterator_t| for the given |directory|.
//
// At most one |zxio_dirent_iterator_t| can be active for a given |directory|
// at a time.
//
// |buffer| will be used internally by the iterator to cache chunks of directory
// entries from the remote server. The larger the buffer, the most entries can
// be fetched from the remote server in each chunk. The caller should not
// access or modify the contents of the buffer during iteration.
//
// |ZXIO_DIRENT_ITERATOR_DEFAULT_BUFFER_SIZE| is a reasonable capacity buffer to
// use for this operation. Larger buffers might improve performance. Smaller
// buffers are likely to degrade performance.
zx_status_t zxio_dirent_iterator_init(zxio_dirent_iterator_t* iterator,
                                      zxio_t* directory, void* buffer,
                                      size_t capacity);

// Read a |zxio_dirent_t| from the given |iterator|.
//
// The |zxio_dirent_t| returned via |out_entry| is valid until either (a) the
// next call to |zxio_dirent_iterator_next| or the |buffer| passed to
// |zxio_dirent_iterator_init| is modified or destroyed.
//
// This function |zxio_directory_entry_t| from the server in chunks, but this
// function returns the entries one at a time. When this function crosses into
// a new chunk, the function will block on the remote server to retrieve the
// next chunk.
//
// When there are no more directory entries to enumerate, this function will
// return |ZX_ERR_NOT_FOUND|.
//
// |iterator| must have been previously initialized via
// |zxio_dirent_iterator_init|.
zx_status_t zxio_dirent_iterator_next(zxio_dirent_iterator_t* iterator,
                                      zxio_dirent_t** out_entry);

__END_CDECLS

#endif // LIB_ZXIO_ZXIO_H_
