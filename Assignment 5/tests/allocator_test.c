/* Include the allocator so the checker can inspect its private lists. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../malloclab-handout/mm.c"

static void check(void)
{
  chunk *c, *prev_chunk = NULL;
  void *bp, *node, *prev = NULL;
  size_t blocks = 0, listed = 0, mapped = 0;
  for (c = chunk_head; c != NULL; c = c->next) {
    char *end = (char *)c + c->size;
    assert(c->prev == prev_chunk);
    assert((uintptr_t)c % page_size == 0);
    assert(c->size % page_size == 0);
    assert(GET((char *)c + sizeof(chunk)) == PACK(OVERHEAD, 1));
    assert(GET((char *)c + sizeof(chunk) + sizeof(size_t)) == PACK(OVERHEAD, 1));
    mapped += c->size;
    bp = (char *)c + CHUNK_OVERHEAD;
    while ((char *)bp < end) {
      size_t size = GET_SIZE(HDRP(bp));
      assert((uintptr_t)bp % ALIGNMENT == 0);
      assert(size >= MIN_BLOCK && size % ALIGNMENT == 0);
      assert(size <= (size_t)(end - (char *)bp));
      assert(GET(HDRP(bp)) == GET(FTRP(bp)));
      if (!GET_ALLOC(HDRP(bp))) {
        assert(GET_ALLOC(HDRP(NEXT_BLKP(bp))));
        blocks++;
      }
      bp = NEXT_BLKP(bp);
    }
    assert((char *)bp == end);
    assert(GET(HDRP(bp)) == PACK(0, 1));
    prev_chunk = c;
  }
  assert(mapped == mem_heapsize());
  for (node = free_head; node != NULL; node = NEXT_FREE(node)) {
    int found = 0;
    assert(++listed <= blocks); /* Also catches free-list cycles. */
    for (c = chunk_head; c != NULL; c = c->next) {
      for (bp = (char *)c + CHUNK_OVERHEAD;
           GET_SIZE(HDRP(bp)) != 0; bp = NEXT_BLKP(bp)) {
        if (node == bp) {
          assert(!GET_ALLOC(HDRP(bp)));
          found++;
        }
      }
    }
    assert(found == 1);
    assert(PREV_FREE(node) == prev);
    prev = node;
  }
  assert(listed == blocks);
}

int main(void)
{
  void *a, *b, *c, *d;
  mem_init();
  assert(mm_init() == 0);
  check();
  a = mm_malloc(64);
  b = mm_malloc(64);
  c = mm_malloc(64);
  d = mm_malloc(64);
  assert(a && b && c && d);
  check();
  mm_free(b); check();
  mm_free(c); check(); /* Merge with previous. */
  b = mm_malloc(144);
  assert(b == (char *)a + 80); /* Reuse the merged pair. */
  check();
  mm_free(b); check();
  mm_free(a); check(); /* Merge with next. */
  mm_free(d); check(); /* Merge both sides and release the chunk. */
  assert(mem_heapsize() == 0);
  puts("allocator tests passed");
  return 0;
}
