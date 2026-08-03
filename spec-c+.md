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

## About C+'s switch (3rd August 2026)
C+'s switch does not introduce performance loss and lowers into GNU (specifically TinyCC as it being the backend) compatible if-statements. There are multiple things that are given by C+ and are standardized.

The switch value placeholder (_ for your switch value)
```cx
switch (some_value)
{
    case _:
        foo(); // will always execute since _ is the switch value placeholder and is correctly lowered to __switch_value
        // whereas if you use some_value, it won't be lowered correctly
    default:
        bar(); // never executes
}
```
> _ is the switch value placeholder and is used everywhere you want your switch value to be used, e.g. in the next predicate case (predicate functions).

The predicate functions (lightweight pattern matching)
```cx
switch (some_value)
{
    case foo(_): // this will be lowered into a check if the function outputs something truthy, meaning only if foo(_) is true will this execute
        bar();
    default:
        baz();
}
```

The range operator (INEX and ININ)
```cx
switch (some_value)
{
    case 'a' => 'z': // INEX, lowers to __switch_value >= 'a' && __switch_value < 'z'
        foo();
    case 'a' ==> 'z': // ININ, lowers to __switch_value >= 'a' && __switch_value <= 'z'
        bar();
    default:
        baz();
}
```
> INEX stands for "INclusive-EXclusive", whereas ININ stands for "INclusive-INclusive".
> INEX == \[...\), ININ == \[...\]

The alternative operator (Shares syntax with the logical OR)
```cx
switch (some_value)
{
    case 1 || 2 || 4 || 8 || 16 || 32: // lowers to consecutive __switch_value == 1 || __switch_value == 2 || __switch_value == 4 || ...
        std::puts("Power of 2 under 6!"); // assuming #include <io> was used
    default:
        std::puts("Something else!");
}
```
> When designing the syntax, I thought about using '|' or ',' instead but ultimately decided on '||' since it already stands for an alternative (OR), therefore case 1 || 2 || 4 reads as "case 1 OR 2 OR 4".

The must-satisfy operator (Shares syntax with the logical AND)
```cx
switch (some_value)
{
    case _ > 0 && _ < 10: // lowers to consecutive __switch_value > 0 && __switch_value < 10 ...
        std::puts("Above 0 and below 10!"); // assuming #include <io> was used
    default:
        std::puts("Not above 0 nor below 10!");
}
```
> When designing the syntax, I thought about using '&' instead but ultimately decided on '&&' out of the same reason I chose '||' for the alternative, as it reads naturally as "case some_value > 0 AND some_value < 10"

### Regarding the precedence of operations in `case` (3rd August 2026)
The precedence is as follows:
    Highest\\
        ()\\
        predicate functions\\
        _\\
        literal equality\\
        =>   ==>\\
        &&\\
        ||\\
    Lowest