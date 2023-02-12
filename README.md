# ccgc

 A conservative mark-and-sweep garbage collector implementation in C on x86 and x64. This implementation implements its own  heap & allocator, conservative scanning of the program stack as well as the CPU registers, and recursive scanning of referenced blocks. Note that this is just a hobby project; this is not necessarily production-suitable. Like with any conservative garbage collector implementation, it's not able to scan for misaligned pointers or malformed pointers (for example, by using the XOR pointer trick in linked lists). Run `build.sh` on Linux or Mac, or `build.bat` on Windows, in order to test the `example.c` program.

## Functions

 Further documentation is available through docstring comments in `ccgc.h`.

- `ccgc_malloc` - allocates GC heap memory
- `ccgc_free` - frees GC heap memory
- `ccgc_collect` - collects and frees unreachable blocks
- `ccgc_desegment` - desegments/merges consecutive free blocks in the GC heap
- `ccgc_resetPage` - resets GC heap with zeroes, removing every block in the heap
- `ccgc_dumpPage` - prints every block in the GC heap, with its use-status, size, and content
