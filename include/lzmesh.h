/* SPDX-License-Identifier: 0BSD */
/*
 * lzmesh.h — public API of the standalone clean-room LZMESH port.
 *
 * STUB: declarations only. No bodies, no constants, no internals.
 * Implementer lanes provide definitions from the final spec prose.
 *
 * Shape mirrors Apple's public libcompression *buffer* API
 * (compression_encode_buffer / compression_decode_buffer /
 * compression_{encode,decode}_scratch_buffer_size), simplified where the
 * spec settles behaviour:
 *   - buffer API only; no streaming path exists in the format;
 *   - encode takes a level (0xE00/0xE01/0xE05/0xE09, full-form only);
 *     decode takes no selector — one decoder handles all levels;
 *   - a decoded-size walker is part of the API so callers cannot
 *     under-allocate silently (undersized destination truncates with
 *     no error signal).
 *
 * Naming: `lzmesh_` prefix. LZMESH / COMPRESSION_LZMESH is Apple's public
 * name. Upstream `mesh_*` / `MESH_*` and Apple-private `Msh*` / `msh_*`
 * identifiers are forbidden here (spec-source naming rule).
 *
 * There is no checksum anywhere in the format. Decoding never validates
 * integrity; applications needing integrity layer their own digest above.
 */

#ifndef LZMESH_H
#define LZMESH_H

#include <stddef.h>
#include <stdint.h>

/*
 * Provisional pre-release version marker (decoder-complete candidate,
 * 2026-09-19). The versioning scheme and first version number are
 * coordinator decisions (docs/RELEASING.md §1 OPEN — "do not invent
 * values"), so these macros are a 0.x dev placeholder for downstream
 * feature-gating, NOT a release version claim. Replaced by the real
 * version at release-prep time.
 */
#define LZMESH_VERSION_MAJOR 0
#define LZMESH_VERSION_MINOR 0
#define LZMESH_VERSION_PATCH 0
#define LZMESH_VERSION_STRING "0.0.0-dev"

/*
 * Encode src[0..src_size] into dst[0..dst_capacity].
 * level selects parse effort only (0xE00, 0xE01, 0xE05, 0xE09); it
 * changes no grammar. Bare 0/1/5/9 and every other value are rejected
 * (encode returns 0, scratch returns 0); full-integer match, no masking.
 * scratch may be NULL (library allocates) or point to at least
 * lzmesh_encode_scratch_size(level) bytes.
 * Returns bytes written, or 0 on failure. Deterministic: same input +
 * level yields identical bytes on every run.
 */
size_t lzmesh_encode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch, int level);

/*
 * Decode src[0..src_size] into dst[0..dst_capacity].
 * scratch may be NULL or point to at least lzmesh_decode_scratch_size().
 * Returns bytes written. Returns 0 on truncated/invalid input.
 * If dst_capacity is smaller than the decoded size, returns dst_capacity
 * with a correct prefix and NO error signal — size the destination with
 * lzmesh_decoded_size() first.
 */
size_t lzmesh_decode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch);

/* Scratch sizes. Decode scratch is level-independent. */
size_t lzmesh_encode_scratch_size(int level);
size_t lzmesh_decode_scratch_size(void);

/*
 * Walk the block framing of src[0..src_size] and return the total decoded
 * size (sum over blocks to the end marker). Returns 0 if framing invalid.
 * Call this to size the decode destination. Reads framing only; decodes
 * no entropy data.
 */
size_t lzmesh_decoded_size(const uint8_t *src, size_t src_size);

#endif /* LZMESH_H */
