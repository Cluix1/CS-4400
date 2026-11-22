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
#define MIN_BLOCK (OVERHEAD + 2 * sizeof(void *))
#define PREV_FREE(bp) (*(void **)(bp))
#define NEXT_FREE(bp) (*(void **)((char *)(bp) + sizeof(void *)))

static size_t page_size;
static void *free_head;
static chunk *chunk_head;

static void insert_free(void *bp)
{
  PREV_FREE(bp) = NULL;
  NEXT_FREE(bp) = free_head;
  if (free_head != NULL)
    PREV_FREE(free_head) = bp;
  free_head = bp;
}

static void remove_free(void *bp)
{
  if (PREV_FREE(bp) != NULL)
    NEXT_FREE(PREV_FREE(bp)) = NEXT_FREE(bp);
  else
    free_head = NEXT_FREE(bp);
  if (NEXT_FREE(bp) != NULL)
    PREV_FREE(NEXT_FREE(bp)) = PREV_FREE(bp);
}

static void set_allocated(void *bp, size_t size)
{
  size_t old_size = GET_SIZE(HDRP(bp));
  remove_free(bp);
  if (old_size - size >= MIN_BLOCK) {
    PUT(HDRP(bp), PACK(size, 1));
    PUT(FTRP(bp), PACK(size, 1));
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(old_size - size, 0));
    PUT(FTRP(bp), PACK(old_size - size, 0));
    insert_free(bp);
  } else {
    PUT(HDRP(bp), PACK(old_size, 1));
    PUT(FTRP(bp), PACK(old_size, 1));
  }
}

int mm_init(void)
{
  page_size = mem_pagesize();
  free_head = NULL;
  chunk_head = NULL;
  return 0;
}

static void *extend(size_t size)
{
  chunk *c;
  void *bp;
  size_t mapped;
  mapped = PAGE_ALIGN(size + CHUNK_OVERHEAD);
  c = mem_map(mapped);
  if (c == NULL)
    return NULL;
  c->size = mapped;
  c->prev = NULL;
  c->next = chunk_head;
  if (chunk_head != NULL)
    chunk_head->prev = c;
  chunk_head = c;
  PUT((char *)c + sizeof(chunk), PACK(OVERHEAD, 1));
  PUT((char *)c + sizeof(chunk) + sizeof(size_t), PACK(OVERHEAD, 1));
  bp = (char *)c + CHUNK_OVERHEAD;
  PUT(HDRP(bp), PACK(mapped - CHUNK_OVERHEAD, 0));
  PUT(FTRP(bp), PACK(mapped - CHUNK_OVERHEAD, 0));
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
  insert_free(bp);
  return bp;
}

void *mm_malloc(size_t size)
{
  void *bp;
  if (size == 0 || size > SIZE_MAX - CHUNK_OVERHEAD - OVERHEAD - page_size)
    return NULL;
  size = ALIGN(size + OVERHEAD);
  if (size < MIN_BLOCK)
    size = MIN_BLOCK;
  for (bp = free_head; bp != NULL; bp = NEXT_FREE(bp)) {
    if (GET_SIZE(HDRP(bp)) >= size)
      break;
  }
  if (bp == NULL)
    bp = extend(size);
  if (bp != NULL)
    set_allocated(bp, size);
  return bp;
}

void mm_free(void *ptr)
{
  size_t size;
  if (ptr == NULL)
    return;
  size = GET_SIZE(HDRP(ptr));
  PUT(HDRP(ptr), PACK(size, 0));
  PUT(FTRP(ptr), PACK(size, 0));
  insert_free(ptr);
}
