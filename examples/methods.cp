/*
    methods.cp - C+ example program for structs and methods
*/

#include <types>
#include <io>

// including string.h for malloc, memcpy, memmove, etc. and stdlib.h for malloc and free as-well
#include <string.h>
#include <stdlib.h>

struct String
{
    char *data;
    std::size_t length;

    static String new(const char *const string) // called via String::new("...")
    {
        std::size_t len = strlen(string); // using strlen from string.h
        char *ptr = static_cast<char *>(malloc(sizeof(char) * (len + 1))); // size dynamically depending on len
        
        if (ptr == static_cast<char *>(nullptr))
        {
            std::errputs("Out Of Memory, returning nullptr as data");
            return String {
                data: nullptr,
                length: 0,
            };
        }

        memcpy(ptr, string, len + 1);
        ptr[len] = '\0'; // ensure null termination
        
        return String {
            data: ptr,
            length: len,
        };
    }

    static void destroy(String *str)
    {
        free(str->data);
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