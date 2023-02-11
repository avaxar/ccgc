#pragma once

#include <stddef.h>


/// @brief Allocates memory in the CCGC heap
/// @param size Amount of memory in bytes to be allocated (Size may be internally changed to the nearest
/// block header alignment size)
/// @return Pointer to the allocated block with the size of `size` byte(s). Function returns a `NULL`
/// pointer if the given `size` is 0. Program crashes if the CCGC heap is not sufficient to allocate
/// `size` bytes.
void* ccgc_malloc(size_t size);

/// @brief Frees a block in the CCGC heap to be used for other `ccgc_malloc` calls in need
/// @param ptr Pointer first given by `ccgc_malloc` whose block is to be freed. If given a `NULL`
/// pointer, this function does nothing.
void ccgc_free(void* ptr);

/// @brief Collects the allocated blocks that are no longer accessible from the program through
/// pointers. The program stack is scanned conservatively and recursively for any pointers pointing to
/// blocks in the CCGC memory, not to be collected.
/// @return Bytes of memory reclaimed from the collection
size_t ccgc_collect(void);

/// @brief Merges consecutive free blocks in the CCGC heap for faster `ccgc_malloc` calls in the future
void ccgc_desegment(void);

/// @brief Logs every block in the CCGC heap, along with its use-status, size, and content, to `stdout`
void ccgc_dumpPage(void);
