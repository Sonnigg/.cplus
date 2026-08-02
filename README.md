# INTRODUCING C+

<img src="cx/cplus-icon.png" alt="C+ Logo" width="128" height="128">
<img src="cx/cplus-header-icon.png" alt="C+ Logo" width="128" height="128">

Shortcut to the license : [LICENSE](./LICENSE)

Shortcut to libc+       : [libc+](./libc+/)

Shortcut to the source  : [source](./source/)

## IMPORTANT

Currently, C+ provides pre-built compiler packages for:
- [Windows x86 and x86_64](./winzips/)
- [Linux x86 and x86_64](./tar-bz/)
- [Linux ARM64 and aarch64](./arm-arch/)

Windows ARM64 is currently not supported.

## What is C+?
Simply put, **C+ started as my C++ dialect, but grew into its own language**. It gives a lot of functionality beyond C's simple structs, as it introduces proper namespaces, scoped enums, and better structs that can actually have methods with the lowering being C-like (more on that below).

Not only does C+ introduce the concept of deferring to the low-level languages, but it also changes it quite a bit from Go's `defer`. Instead of being executed as soon as this code block stops, it simply is moved to the end of its lexical scope, meaning an early return will leak memory if not handled in that branch. This approach to deferring was done, so that the programmer can see what's gonna happen at the end, assuming everything went fine, directly at declaration of an instance.
> Why did I choose 'defer'? Because I didn't know what else to call it.
> Another note: defer might change its design to be Go-like, this change will be noted explicitly once done. Do not rely on defer too much before the mechanic is clear.

## Why choose C+?
Something you might say: "There is no reason as to why you shouldn't just use C++", and I agree with that. But let's be honest, we've all been there when we wanted to use C but just didn't because of all its historical quirks such as no namespaces, and so much more, that's why I made C+, to not carry around C++ baggage and to fix C's old mistakes! You might agree or disagree, but in the end, it's your decision afterall.
> Why am I not sounding more 'USE C+!!!'? Because I'm being honest and value your opinion and choices.

## Code examples in C+

General struct showcase:
```cx
namespace app
{
    struct Player
    {
        int health;

        void damage(Player *self, int amount) // no need to name the first parameter self, can also be this or whatever! But it has to be there
        {
            self->health -= amount;
        }
    }
}

int main()
{
    app::Player player = app::Player {
        health: 100,
    };
    player.damage(10);
    return 0;
}
```

"Hello, World!" showcase:
```cx
#include <libc+>

int main()
{
    std::hello();
    return 0;
}
```

## Any questions?
Maybe your question can be answered by the official C+ specification? Give it a shot and dive into [spec-c+](./spec-c+.md).