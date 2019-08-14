#include "sim.h"
#include "event.h"
#include <cstdarg>
#include <cstdio>
#include <elf.h>
#include <iostream>
#include <vector>

void fatal(const char *fmt, ...) {
    va_list ap;

    fprintf(stderr, "fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");

    exit(EXIT_FAILURE);
}

void Sim::run() {
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s EXEC-FILE\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Sim sim;
    sim.run();

    return 0;
}
