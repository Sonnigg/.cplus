/*
    methods.cp - C+ example program for structs and methods
*/

#include <types>
#include <io>
#include <chars>
#include <memory>

struct String
{
    char *data;
    std::size_t length;

    static String new(const char *const string) // called via String::new("...")
    {
        std::size_t len = std::stringLength(string); // using std::stringLength(char *) from chars in libc+
        char *ptr = static_cast<char *>(std::allocateMemory(sizeof(char) * (len + 1))); // size dynamically depending on len
        
        if (ptr == static_cast<char *>(nullptr))
        {
            std::errputs("Out Of Memory, returning nullptr as data");
            return String {
                data: nullptr,
                length: 0,
            };
        }

        std::copyString(ptr, string); // using std::copyString(char *, char *) from chars in libc+, this ensures null-termination
        
        return String {
            data: ptr,
            length: len,
        };
    }

    static void destroy(String *str)
    {
        std::freeMemory(str->data);
        str->data = nullptr;
        str->length = 0;
    }

    String copy(String *self) // called via str.copy()
    {
        return String::new(self->data); // we can just make a new String since a copy is just the same thing but a new owned String!
    }

    const char *chars(String *self)
    {
        return self->data;
    }

    std::size_t len(String *self)
    {
        return self->length;
    }
}

int main()
{
    String str = String::new("Hello, World!\n");
    String new_one = str.copy();
    defer {
        String::destroy(&str);
        String::destroy(&new_one);
    }

    std::puts(new_one.chars());
    return 0;
}