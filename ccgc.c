#include <assert.h>
#include <ctype.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ccgc.h"


// GC heap
#define PAGE_SIZE 65536
static char page[PAGE_SIZE] = {0};

static void* stack_base;
static void initializeStackBase(void) __attribute__((constructor));
static void initializeStackBase(void) {
    // Ideally, we should get the actual address of where the entire program stack starts. But since
    // there aren't necessarily any clean cross-platform way to do that, this hack should be good
    // enough. This constructor is run before the `main` function in approximately the same local
    // address space, or before that. From the address of `dummy` to where `ccgc_collect`'s function
    // frame is, it should cover `main`'s local variables, and everything called from there in this
    // thread.

    int dummy;
    stack_base = &dummy;
}

// Heap block header
typedef struct {
    size_t size;
    bool allocated;
    bool marked; // For the GC
    alignas(sizeof(size_t)) char content[0];
} Block;

void* ccgc_malloc(size_t size) {
    Block* block = &((Block*)page)[0];

    // When `ccgc_malloc` is ran for the first time
    if (block->size == 0) {
        block->size = PAGE_SIZE - sizeof(Block);
        block->allocated = false;
    }

    // Allocating the size of 0 is not possible.
    if (size == 0) {
        return NULL;
    }

    // The allocated size should be in alignment of `Block`.
    // Thus, it rounds up the `size` to the nearest multiple of the alignment.
    size = __alignof(Block) * (size / __alignof(Block) + (size % __alignof(Block) > 0));

    // Finds a free sufficient block
    while (block->allocated || block->size < size) {
        Block* next_block = (Block*)(block->content + block->size);

        // Assertion fails if it reaches the last block, yet it's unable to allocate sufficient memory.
        assert((char*)next_block < page + PAGE_SIZE);

        // Merges multiple consecutive free blocks
        while (!block->allocated && !next_block->allocated) {
            block->size += sizeof(Block) + next_block->size;
            next_block = (Block*)(block->content + block->size);
            if ((char*)next_block >= page + PAGE_SIZE) {
                break;
            }
        }

        block = next_block;
    }

    // We'll only allocate as much as we actually need.
    // Makes a block after for the excess size, unless if it would have a content size of 0
    if (block->size > size + sizeof(Block)) {
        Block* next_block = (Block*)((char*)block + sizeof(Block) + size);
        next_block->size = block->size - size - sizeof(Block);
        next_block->allocated = false;

        block->size = size;
    }

    block->allocated = true;

    return block->content;
}

void ccgc_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }

    Block* block = (Block*)ptr - 1;

    // Assertion fails if the given pointer does not actually point in our heap page.
    assert((char*)block >= page && (char*)block < page + PAGE_SIZE - sizeof(Block));

    // Assertion fails if the user is trying to free an irregular block.
    // A healthy block from `ccgc_malloc` should have a size of >0 and is marked `allocated`.
    assert(block->size && block->allocated);

    block->allocated = false;

    // Merges multiple consecutive free blocks
    while (true) {
        Block* next_block = (Block*)(block->content + block->size);
        if ((char*)next_block >= page + PAGE_SIZE) {
            break;
        }

        if (next_block->allocated) {
            break;
        }
        else {
            block->size += sizeof(Block) + next_block->size;
        }
    }
}


typedef struct {
#if __i386__ || __x86_64__
    // Reference: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/x86-architecture
    size_t ax, bx, cx, dx, si, di, bp, sp;
#if __x86_64__
    // Reference: https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/x64-architecture
    size_t r8, r9, r10, r11, r12, r13, r14, r15;
#endif
#endif
} Registers;


static inline Registers getRegisters(void) {
    Registers registers;

#if __x86_64__
    asm("\t movq %%rax,%0" : "=r"(registers.ax));
    asm("\t movq %%rbx,%0" : "=r"(registers.bx));
    asm("\t movq %%rcx,%0" : "=r"(registers.cx));
    asm("\t movq %%rdx,%0" : "=r"(registers.dx));
    asm("\t movq %%rsi,%0" : "=r"(registers.si));
    asm("\t movq %%rdi,%0" : "=r"(registers.di));
    asm("\t movq %%rbp,%0" : "=r"(registers.bp));
    asm("\t movq %%rsp,%0" : "=r"(registers.sp));

    asm("\t movq %%r8,%0" : "=r"(registers.r8));
    asm("\t movq %%r9,%0" : "=r"(registers.r9));
    asm("\t movq %%r10,%0" : "=r"(registers.r10));
    asm("\t movq %%r11,%0" : "=r"(registers.r11));
    asm("\t movq %%r12,%0" : "=r"(registers.r12));
    asm("\t movq %%r13,%0" : "=r"(registers.r13));
    asm("\t movq %%r14,%0" : "=r"(registers.r14));
    asm("\t movq %%r15,%0" : "=r"(registers.r15));
#elif __i386__
    asm("\t mov %%eax,%0" : "=r"(registers.ax));
    asm("\t mov %%ebx,%0" : "=r"(registers.bx));
    asm("\t mov %%ecx,%0" : "=r"(registers.cx));
    asm("\t mov %%edx,%0" : "=r"(registers.dx));
    asm("\t mov %%esi,%0" : "=r"(registers.si));
    asm("\t mov %%edi,%0" : "=r"(registers.di));
    asm("\t mov %%ebp,%0" : "=r"(registers.bp));
    asm("\t mov %%esp,%0" : "=r"(registers.sp));
#else
#error "architecture not supported"
#endif

    return registers;
}

static inline void markPointer(void* ptr);
static void markPointers(void** memory, size_t size) {
    // We treat `memory` as a pointer array of possible pointers, and iterate over them.
    for (void** p_ptr = memory; (char*)p_ptr < (char*)memory + size; p_ptr++) {
        markPointer(*p_ptr);
    }
}

static inline void markPointer(void* ptr) {
    // Checks if the value is likely a pointer pointing to a block in the page
    if ((char*)ptr < page || page + PAGE_SIZE <= (char*)ptr) {
        return;
    }

    // Iterates over every block to find which block the pointer is pointing to
    Block* block = (Block*)page;
    for (; (char*)block < page + PAGE_SIZE; block = (Block*)(block->content + block->size)) {
        if ((char*)block <= (char*)ptr && (char*)ptr < block->content + block->size) {
            break;
        }
    }

    // Free blocks don't get marked.
    if (!block->allocated) {
        return;
    }

    // Sets the block as marked and recursively conservatively searches for more possible pointers
    // in the block
    block->marked = true;
    markPointers((void**)block->content, block->size);
}


size_t ccgc_collect(void) {
    // When `ccgc_malloc` has not been run to initialize the first block.
    if (((Block*)page)[0].size == 0) {
        return 0;
    }

    // Marks all of the blocks in the page as unmarked (haven't been referenced)
    for (Block* block = (Block*)page; (char*)block < page + PAGE_SIZE;
         block = (Block*)(block->content + block->size)) {
        block->marked = false;
    }

    // Marks the blocks that are pointed to by any pointers in the CPU's registers
    Registers registers = getRegisters();
    markPointers((void**)&registers, sizeof(registers));

    // Marks the blocks that are pointed to by any pointers in the stack
    volatile size_t dummy;
    markPointers((void**)&dummy, (size_t)stack_base - (size_t)&dummy); // The stack goes downwards.

    // Sweeps (frees) all of the the unmarked blocks that are no longer accessible from the user code
    size_t collected = 0;
    for (Block* block = (Block*)page; (char*)block < page + PAGE_SIZE;
         block = (Block*)(block->content + block->size)) {
        if (block->allocated && !block->marked) {
            collected += block->size;
            ccgc_free(block->content);
        }
    }

    return collected;
}

void ccgc_desegment(void) {
    // When `ccgc_malloc` has not been run to initialize the first block.
    if (((Block*)page)[0].size == 0) {
        return;
    }

    Block* block = (Block*)page;

    // Merges together every consecutive series of free blocks together,
    // a.k.a desegmenting the page
    while (true) {
        Block* next_block = (Block*)(block->content + block->size);
        if ((char*)next_block >= page + PAGE_SIZE) {
            break;
        }

        if (block->allocated || next_block->allocated) {
            block = next_block;
        }
        else {
            block->size += sizeof(Block) + next_block->size;
        }
    }
}

void ccgc_resetPage(void) {
    memset(page, 0, sizeof(page));
}

void ccgc_dumpPage(void) {
    printf("(ccgc) Dumping page...\n");

    // When `ccgc_malloc` has not been run to initialize the first block
    if (((Block*)page)[0].size == 0) {
        printf("Page has not been initialized.\n\n");
        return;
    }

    // Iterates over each block in the page
    size_t block_i = 0;
    for (Block* block = (Block*)page; (char*)block < page + PAGE_SIZE;
         block = (Block*)(block->content + block->size), block_i++) {
        printf("Block #%zu (%zuB + %zuB%s)\n", block_i, sizeof(Block), block->size,
               block->allocated ? "" : ", free");

        if (!block->allocated) {
            continue;
        }

        // Dumps the block's content
        putchar('\t');
        for (size_t i = 0; i < block->size; i++) {
            char c = block->content[i];

            switch (c) {
                default:
                    // Prints directly if it's printable
                    if (isprint(c)) {
                        putchar(c);
                    }
                    // For unprintable characters, give hex representation
                    else {
                        printf("\\x%02hhX", c);
                    }
                    break;

                // Exceptions for these characters, which have their own unique escape codes
                case '\0':
                    printf("\\0");
                    break;
                case '\n':
                    printf("\\n");
                    break;
                case '\r':
                    printf("\\r");
                    break;
                case '\t':
                    printf("\\t");
                    break;
                case '\v':
                    printf("\\v");
                    break;
                case '\b':
                    printf("\\b");
                    break;
                case '\a':
                    printf("\\a");
                    break;
                case '\f':
                    printf("\\f");
                    break;
                case '\\':
                    printf("\\\\");
                    break;
            }
        }
        putchar('\n');
    }
    putchar('\n');
}
