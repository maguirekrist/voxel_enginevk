#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <string_view>

template <std::size_t N>
inline void copy_cstr_truncating(char (&destination)[N], std::string_view source)
{
    static_assert(N > 0, "Destination buffer must not be empty.");

    const std::size_t copyLength = std::min(source.size(), N - 1);
    std::memcpy(destination, source.data(), copyLength);
    destination[copyLength] = '\0';
}
