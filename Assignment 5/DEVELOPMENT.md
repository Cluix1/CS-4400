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
