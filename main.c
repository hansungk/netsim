#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

typedef struct Memory {
    size_t size;
    uint8_t *data;
} Memory;

typedef uint64_t MemAddr;

typedef struct Cpu {
    Memory *mem;
    MemAddr program_counter;
    long cycle;
} Cpu;

static void fatal(const char *msg)
{
    fprintf(stderr, "fatal: %s\n", msg);
    exit(EXIT_FAILURE);
}

static void *cpu_alloc(size_t size)
{
    void *p = malloc(size);
    if (!p)
        fatal("out of memory");
    return p;
}

void memory_init(Memory *mem)
{
    mem->size = 1024 * 1024;
    mem->data = cpu_alloc(mem->size);
}

void memory_destroy(Memory *mem)
{
    free(mem->data);
}

void cpu_init(Cpu *cpu, Memory *mem)
{
    *cpu = (const Cpu){0};
    cpu->mem = mem;
}

void cpu_fetch(Cpu *cpu)
{
    if (cpu->program_counter < cpu->mem->size) {
        uint8_t byte = cpu->mem->data[cpu->program_counter];
        cpu->program_counter++;
        printf("fetched %d from 0x%08lx\n", byte, cpu->program_counter);
    }
}

void cpu_cycle(Cpu *cpu)
{
    cpu_fetch(cpu);
    cpu->cycle++;
}

int main()
{
    Cpu cpu;
    Memory mem;

    memory_init(&mem);
    cpu_init(&cpu, &mem);

    while (cpu.cycle < 10000) {
        cpu_cycle(&cpu);
    }

    memory_destroy(&mem);
    return 0;
}
