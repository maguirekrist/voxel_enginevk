#pragma once
#include <vk_types.h>

namespace Random {
    static std::random_device dev;
    static std::mt19937 rng(dev());

    template<typename T>
    static T generate(T min, T max) {
        std::uniform_int_distribution<std::mt19937::result_type> dist6(min,max);

        return dist6(rng);
    }
}