#include "misc/utf8.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(1);
        }
    }
} // namespace

int main()
{
    check(toy3d::is_valid_utf8("plain text"), "ASCII must be valid UTF-8");
    check(toy3d::is_valid_utf8("\xe4\xb8\xad\xe6\x96\x87"), "multibyte text must be valid UTF-8");
    check(toy3d::is_valid_utf8(std::string("\0", 1)), "NUL is valid UTF-8; paths reject it separately");
    check(!toy3d::is_valid_utf8("\xc0\xaf"), "overlong encoding was accepted");
    check(!toy3d::is_valid_utf8("\xed\xa0\x80"), "surrogate was accepted");
    check(!toy3d::is_valid_utf8("\xf4\x90\x80\x80"), "out of range code point was accepted");
    check(!toy3d::is_valid_utf8("\xe4\xb8"), "truncated sequence was accepted");
    return 0;
}
