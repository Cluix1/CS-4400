# Allocator development

Tests run in Ubuntu WSL with the supplied driver and Makefile.

1. Added headers, footers, per-chunk boundary blocks, and page release.
   Both short traces passed (`./mdriver -V -f traces/short1-bal.rep` and
   `./mdriver -V -f traces/short2-bal.rep`). Each allocation still maps a chunk
   at this stage; the next step is to reuse and split free blocks.
