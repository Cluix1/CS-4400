/*
 * Explicit free list allocator. Blocks have size_t headers and footers.
 * Each mapped chunk has its own boundary blocks, since chunks can be apart.
 */
#include <stdint.h>
#include "mm.h"
#include "memlib.h"

#define ALIGNMENT 16
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~(size_t)(ALIGNMENT - 1))
#define PAGE_ALIGN(size) (((size) + (page_size - 1)) & ~(page_size - 1))
#define OVERHEAD (2 * sizeof(size_t))
#define GET(p) (*(size_t *)(p))
#define PUT(p, val) (*(size_t *)(p) = (val))
#define PACK(size, alloc) ((size) | (alloc))
#define GET_ALLOC(p) (GET(p) & 0x1)
#define GET_SIZE(p) (GET(p) & ~(size_t)0xF)
#define HDRP(bp) ((char *)(bp) - sizeof(size_t))
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - OVERHEAD)
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - OVERHEAD))

typedef struct chunk {
  struct chunk *prev;
  struct chunk *next;
  size_t size;
} chunk;

/* Chunk record, allocated prologue, and epilogue header. */
#define CHUNK_OVERHEAD (sizeof(chunk) + OVERHEAD + sizeof(size_t))

static size_t page_size;

int mm_init(void)
{
  page_size = mem_pagesize();
  return 0;
}

void *mm_malloc(size_t size)
{
  chunk *c;
  void *bp;
  size_t mapped;
  if (size == 0 || size > SIZE_MAX - CHUNK_OVERHEAD - OVERHEAD - page_size)
    return NULL;
  mapped = PAGE_ALIGN(ALIGN(size + OVERHEAD) + CHUNK_OVERHEAD);
  c = mem_map(mapped);
  if (c == NULL)
    return NULL;
  c->size = mapped;
  PUT((char *)c + sizeof(chunk), PACK(OVERHEAD, 1));
  PUT((char *)c + sizeof(chunk) + sizeof(size_t), PACK(OVERHEAD, 1));
  bp = (char *)c + CHUNK_OVERHEAD;
  PUT(HDRP(bp), PACK(mapped - CHUNK_OVERHEAD, 1));
  PUT(FTRP(bp), PACK(mapped - CHUNK_OVERHEAD, 1));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
  return bp;
}

void mm_free(void *ptr)
{
  chunk *c;
  if (ptr == NULL)
    return;
  c = (chunk *)((char *)ptr - CHUNK_OVERHEAD);
  mem_unmap(c, c->size);
}
