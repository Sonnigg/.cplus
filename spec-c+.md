# STATUS - WIP (1st August 2026)

# Information regarding "(DATE)" (3rd August 2026)
Every header (#, ##, and ###) in the Markdown documentation has the date of its last modification appended in parentheses.

## About the macro CMPL__PTR_SIZE (3rd August 2026)
On every non-Windows OS, this would be the generated version in C code of what CMPL__PTR_SIZE is.
```c
#ifndef CMPL__PTR_SIZE
#if defined(__x86_64__) || defined(__aarch64__) || defined(__riscv64) || defined(__loongarch64)
#define CMPL__PTR_SIZE 8
#elif defined(__i386__) || defined(__arm__) || defined(__riscv) || defined(__mips__) || defined(__powerpc__)
#define CMPL__PTR_SIZE 4
#elif defined(__MSP430__) || defined(__AVR__) || defined(__m16c__) || defined(__RL78__)
#define CMPL__PTR_SIZE 2
#else
#define CMPL__PTR_SIZE 4
#endif
#endif
```

On Windows, this would be the generated version in C code of what CMPL__PTR_SIZE is.
```c
#ifndef CMPL__PTR_SIZE
#if defined(_WIN64) || defined(__x86_64__)
#define CMPL__PTR_SIZE 8
#elif defined(_WIN16) || defined(__I86__)
#define CMPL__PTR_SIZE 2
#else
#define CMPL__PTR_SIZE 4
#endif
#endif
```

When generating C code without explicit target information, the generated fallback defaults to `CMPL__PTR_SIZE == 4` (32-bit). This fallback exists for compatibility and should not be interpreted as support for architectures with non-standard pointer sizes.