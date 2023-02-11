#include <stdio.h>
#include <string.h>

#include "ccgc.h"


void allocTest(void) {
    ccgc_dumpPage();

    const char* c_str = "Hello, world!\n";
    char* gc_str = ccgc_malloc(strlen(c_str) + 1);
    strcpy(gc_str, c_str);

    ccgc_dumpPage();

    ccgc_free(gc_str);

    ccgc_dumpPage();
}

void gcTest(void) {
    ccgc_dumpPage();

    volatile void* test = ccgc_malloc(256);

    for (int i = 0; i < 5; i++) {
        ccgc_malloc(128);
    }

    ccgc_dumpPage();

    ccgc_collect();
    ccgc_desegment();

    ccgc_dumpPage();
}

int main(int argc, const char* argv[]) {
    printf("Allocation test with `ccgc_malloc` and `ccgc_free`...\n\n");
    allocTest();

    printf("\n\n");

    printf("Garbage collection test with `ccgc_malloc`, `ccgc_collect`, and `ccgc_desegment`...\n\n");
    gcTest();
}
