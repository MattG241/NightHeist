#include "nh_arena.h"

#include <string.h>

void nh_arena_init(NhArena *a, void *buffer, size_t cap)
{
    a->base = (uint8_t *)buffer;
    a->cap = cap;
    a->used = 0;
    a->peak = 0;
}

void *nh_arena_alloc(NhArena *a, size_t size)
{
    size_t start = NH_ALIGN_UP(a->used, NH_ARENA_ALIGN);
    if (start > a->cap || size > a->cap - start)
        return NULL;
    a->used = start + size;
    if (a->used > a->peak)
        a->peak = a->used;
    return a->base + start;
}

void *nh_arena_alloc_zero(NhArena *a, size_t size)
{
    void *p = nh_arena_alloc(a, size);
    if (p)
        memset(p, 0, size);
    return p;
}

size_t nh_arena_mark(const NhArena *a)
{
    return a->used;
}

void nh_arena_reset_to(NhArena *a, size_t mark)
{
    if (mark <= a->used)
        a->used = mark;
}

void nh_arena_reset(NhArena *a)
{
    a->used = 0;
}
