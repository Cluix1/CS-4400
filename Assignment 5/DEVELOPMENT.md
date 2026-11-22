# Allocator development

Tests run in Ubuntu WSL with the supplied driver and Makefile.

1. Added headers, footers, per-chunk boundary blocks, and page release.
   Both short traces passed (`./mdriver -V -f traces/short1-bal.rep` and
   `./mdriver -V -f traces/short2-bal.rep`). Each allocation still maps a chunk
   at this stage; the next step is to reuse and split free blocks.

2. Added a doubly linked free list and splitting (minimum block: 32 bytes).
   Both short traces and all nine default traces passed. The full driver
   reported 46% peak utilization, 27% instantaneous utilization, and index 23.
   Coalescing and chunk growth are still missing at this checkpoint.

3. Added boundary-tag coalescing and release of completely empty chunks.
   Both short traces and all nine default traces passed. Added a separate
   heap checker and focused tests for split/reuse, all coalescing cases, and
   page release. The tests passed with AddressSanitizer and UBSan enabled.
   The checker stays in tests, so it adds no work to submitted mm.c.

4. Added chunk growth with a bounded reserve, and reset growth when the heap
   becomes empty. Compared larger and smaller growth limits; retained the
   smaller limit for better utilization. Added overflow checks and expanded
   tests to cover 4,000 random operations, payload contents, chunk-list removal,
   tiny remainders, zero size, and repeated initialization. All passed with
   AddressSanitizer and UBSan. All nine full traces also passed with sanitizers.
   A normal full run gave 77% utilization, 45% instantaneous utilization,
   3,868 Kops/s, and index 57 on this WSL machine.

   Built a separate -pg driver and profiled random-bal.rep with gprof.
   mm_malloc accounted for 86% of sampled time, suggesting that first-fit
   searches through small fragments are worth improving.

5. Compared size buckets and a bounded first-fit search. Neither improved the
   combined score, so kept the simpler original first-fit list. Final checks:
   both short traces passed; all nine default traces passed; the custom heap
   checker and randomized tests passed with AddressSanitizer and UBSan.
   The final normal run reported 77% utilization, 45% instantaneous utilization,
   4,082 Kops/s, and performance index 58/100. This index is the driver's local
   performance metric, not the assignment grade. It is below the assignment's
   70-point full-credit threshold here; CADE performance has not been measured.
   No driver, header, or Makefile changes are needed. Submit only mm.c.

## Run the checks

From `Assignment 5/malloclab-handout` in Linux (or Ubuntu WSL):

```sh
make
./mdriver -V -f traces/short1-bal.rep
./mdriver -V -f traces/short2-bal.rep
./mdriver -l -v

gcc -std=c11 -D_DEFAULT_SOURCE -g -Wall -Wextra -Werror \
  -fsanitize=address,undefined ../tests/allocator_test.c memlib.c pagemap.c \
  -o /tmp/allocator_test
/tmp/allocator_test
```

The checker is in `tests/allocator_test.c`, which includes mm.c to inspect its
private state. It checks boundary tags, alignment, chunk links, free-list links,
coalescing, free-list membership, and total mapped bytes. Production allocator
calls do not run the checker.
