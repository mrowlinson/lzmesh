# API.md — function reference

Source of truth: `include/lzmesh.h` (stub: declarations + one-line docs
only) and the API-shape record in `DECISIONS.md` D5. Nothing here goes
beyond those two sources; constants and internals are lane work.

Buffer API only; no streaming path exists in the format. Naming uses the
`lzmesh_` prefix; upstream `mesh_*`/`MESH_*` and Apple-private
`Msh*`/`msh_*` identifiers are forbidden here.

There is no checksum anywhere in the format. Decoding never validates
integrity; applications needing integrity layer their own digest above
the codec.

## `lzmesh_encode`

```c
size_t lzmesh_encode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch, int level);
```

Encodes `src[0..src_size]` into `dst[0..dst_capacity]`. `level` selects
parse effort only (0, 1, 5, 9); it changes no grammar. `scratch` may be
`NULL` (the library allocates) or point to at least
`lzmesh_encode_scratch_size(level)` bytes. Returns bytes written, or 0
on failure. Deterministic: same input + level yields identical bytes on
every run.

## `lzmesh_decode`

```c
size_t lzmesh_decode(uint8_t *dst, size_t dst_capacity,
                     const uint8_t *src, size_t src_size,
                     void *scratch);
```

Decodes `src[0..src_size]` into `dst[0..dst_capacity]`. Takes no
selector — one decoder handles all levels. `scratch` may be `NULL` or
point to at least `lzmesh_decode_scratch_size()` bytes. Returns bytes
written; returns 0 on truncated/invalid input. If `dst_capacity` is
smaller than the decoded size, returns `dst_capacity` with a correct
prefix and NO error signal — size the destination with
`lzmesh_decoded_size()` first.

## `lzmesh_encode_scratch_size` / `lzmesh_decode_scratch_size`

```c
size_t lzmesh_encode_scratch_size(int level);
size_t lzmesh_decode_scratch_size(void);
```

Scratch sizes. Decode scratch is level-independent.

## `lzmesh_decoded_size`

```c
size_t lzmesh_decoded_size(const uint8_t *src, size_t src_size);
```

Walks the block framing of `src[0..src_size]` and returns the total
decoded size (sum over blocks to the end marker). Returns 0 if framing
is invalid. Call this to size the decode destination. Reads framing
only; decodes no entropy data.

## `LZMESH_VERSION_*` (provisional)

```c
#define LZMESH_VERSION_MAJOR 0
#define LZMESH_VERSION_MINOR 0
#define LZMESH_VERSION_PATCH 0
#define LZMESH_VERSION_STRING "0.0.0-dev"
```

Pre-release dev placeholder for downstream feature-gating, added at
decoder-complete candidacy (2026-09-19). NOT a version claim: scheme +
first version are coordinator-OPEN (`docs/RELEASING.md` §1). Replaced
by the real version at release-prep time.

## Notes

- The simplified shape (level on encode only, no selector on decode)
  records measured behaviour over mimicry of Apple's 6-arg calls; the
  Apple buffer-API shape is the documented origin (DECISIONS.md D5).
  Shape confirmation is OPEN (O3): simplified vs Apple-exact 6-arg
  mirror — coordinator decides.
- Level representation is a bare `int` in the stub; a header enum would
  add spec values to the skeleton and stays deferred (O4).
