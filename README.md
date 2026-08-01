# INTRODUCING C+

Shortcut to the license : [LICENSE](./LICENSE)

Shortcut to libc+       : [libc+](./libc+/)

Shortcut to the source  : [source](./source/)

## What is C+?
Simply put, **C+ is my C++ dialect made a language**. It gives a lot of functionality beyond C's simple structs, as it introduces proper namespaces, scoped enums, and better structs that can actually have methods with the lowering being C-like (more on that below).

Not only does C+ introduce the concept of deferring to the low-level languages, but it also changes it quite the bit from Go's `defer`. Instead of being executed as soon as this code block stops, it simply is moved to the end of its lexical scope, meaning an early return will leak memory if not handled in that branch. This approach to deferring was done, so that the programmer can see what's gonna happen at the end, assuming everything went fine, directly at declaration of an instance.
> Why did I choose 'defer'? Because I didn't know what else to call it.

## Why choose C+?
There is no reason as to why you shouldn't just use C++ is something you might say, and I agree with that. But let's be honest, we've all been there when we wanted to use C but just didn't because of all its historical quirks such as no namespaces, and so much more, that's why I made C+, to not carry around C++ baggage and to fix C's old mistakes! You might agree or disagree, but in the end, it's your decision afterall.
> Why am I not sounding more 'USE C+!!!'? Because I'm being honest and value your opinion and choices.

## Code examples in C+

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
}
```

## Any questions?
Maybe your question can be answered by the official C+ specification? Give it a shot and dive into [spec-c+](./spec-c+.md).