//
// Created by Maguire Krist on 8/24/25.
//

#ifndef LOGGING_UTILS_H
#define LOGGING_UTILS_H
#include <string>

inline std::string bytes_to_mb_string(uint64_t bytes)
{
    auto mb = bytes / 1000 / 1000;
    // auto kb = (bytes / 1024) % 1024;
    // auto b = bytes % 1024;
    return std::to_string(mb) + " mb";
}

#endif //LOGGING_UTILS_H
