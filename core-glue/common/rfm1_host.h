/* SPDX-License-Identifier: GPL-3.0-only */
/* The host's file API for a Fantasy CFW (docs/VIRTUAL-CARTS.md section 4: import module rfm1_host_1, capability
 * fs1). In an .rfm1 build (FM1CORE_RFM1) the four functions are the module's imports; natively they are
 * rfm1_host.c's stand-ins (no folder yet: FS_ENOFOLDER), until FM1VST hosts the folder itself.
 *
 * Asynchronous: a request returns an id at once; the host completes it between two calls into the module (never
 * during one), and the cart polls. While a request is pending its buffer belongs to the host. Paths are UTF-8, not
 * NUL-terminated, virtual: /audio/.., /code/.. (the cart's own code/<id>/), /data/.. (its own data/<id>/). */
#ifndef FM1_RFM1_HOST_H
#define FM1_RFM1_HOST_H
#include <stdint.h>

enum {
    FS_STAT = 1,     /* buf: a 16-byte record, size (u64), mtime (u32 s), is_dir (u32), little-endian -> 16 */
    FS_LIST = 2,     /* buf: packed {u8 is_dir, u8 name_len, name} from entry `offset` on, sorted by name, never
                      * split -> entries written; 0 = the end. Hidden files (a leading '.') are not listed */
    FS_READ = 3,     /* len bytes at offset -> bytes read; 0 = the end */
    FS_WRITE = 4,    /* a file in /data, len bytes at offset (-1: truncate, then write) -> bytes written */
    FS_DELETE = 5,   /* a file or an empty folder in /data -> 0 */
    FS_MKDIR = 6,    /* a folder in /data -> 0 */
};
enum {
    FS_PENDING = -1,
    FS_ENOENT = -2,
    FS_EACCES = -3,  /* not allowed (section 3: /code and /audio read only, nothing outside the three) */
    FS_EIO = -4,
    FS_ENOSPC = -5,
    FS_EBUSY = -6,   /* too many requests in flight (at most 32) */
    FS_ECANCELED = -7,
    FS_ENOFOLDER = -8, /* no folder picked yet, or not allowed this visit */
};
#define FS_MAX_INFLIGHT 32
#define FS_MAX_LEN (1024 * 1024)   /* bytes per request */

#if defined FM1CORE_RFM1 && defined __wasm__
#define RFM1_HOST_IMPORT(n) __attribute__((import_module("rfm1_host_1"), import_name(#n)))
#else
#define RFM1_HOST_IMPORT(n)
#endif

/* [i32 i32 i32 i64 i32 i32] -> [i32]: a request's id (> 0), or an error */
RFM1_HOST_IMPORT(fs_request) int32_t rfm1_fs_request(int32_t op, const char *path, int32_t path_len, int64_t offset,
                                                     void *buf, int32_t len);
/* [i32] -> [i32]: FS_PENDING, or the result once (then the id is free) */
RFM1_HOST_IMPORT(fs_poll) int32_t rfm1_fs_poll(int32_t id);
/* [i32] -> []: the host stops writing into the buffer; a poll then says FS_ECANCELED */
RFM1_HOST_IMPORT(fs_cancel) void rfm1_fs_cancel(int32_t id);
/* [] -> [i32]: a counter the host moves when it sees the folder change */
RFM1_HOST_IMPORT(fs_changed) int32_t rfm1_fs_changed(void);

#endif
