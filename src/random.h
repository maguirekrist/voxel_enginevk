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

    static uint64_t mix_seed(uint64_t value)
    {
        value += 0x9e3779b97f4a7c15ULL;
        value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
        value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
        return value ^ (value >> 31);
    }

    static uint64_t seed_from_ints(std::initializer_list<int64_t> values)
    {
        uint64_t seed = 0xcbf29ce484222325ULL;
        for (const int64_t value : values)
        {
            seed ^= mix_seed(static_cast<uint64_t>(value));
            seed *= 0x100000001b3ULL;
        }

        return mix_seed(seed);
    }

    template<typename T>
    static T generate_from_seed(uint64_t& seed, T min, T max)
    {
        seed = mix_seed(seed);
        if constexpr (std::is_integral_v<T>)
        {
            const auto range = static_cast<uint64_t>(max - min + 1);
            return static_cast<T>(min + static_cast<T>(seed % range));
        }
    }
}
