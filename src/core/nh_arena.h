/* Night Heist - linear arena allocator.
 * All per-level memory comes from arenas that are reset wholesale, so the hot
 * loop never calls malloc/free. Allocations are 32-byte aligned by default
 * because GX DMA (textures, vertex arrays, display lists, ASND buffers)
 * requires it. */
#ifndef NH_ARENA_H
#define NH_ARENA_H

#include "nh_types.h"

#define NH_ARENA_ALIGN 32

typedef struct NhArena {
    uint8_t *base;
    size_t cap;
    size_t used;
    size_t peak;
} NhArena;

/* buffer must be NH_ARENA_ALIGN-aligned; the arena does not own it. */
void nh_arena_init(NhArena *a, void *buffer, size_t cap);
void *nh_arena_alloc(NhArena *a, size_t size); /* NULL when out of space */
void *nh_arena_alloc_zero(NhArena *a, size_t size);
size_t nh_arena_mark(const NhArena *a);
void nh_arena_reset_to(NhArena *a, size_t mark);
void nh_arena_reset(NhArena *a);

#endif
