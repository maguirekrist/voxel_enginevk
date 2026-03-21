#pragma once

#include <filesystem>

namespace config
{
    class ConfigPaths
    {
    public:
        [[nodiscard]] static std::filesystem::path root();
        [[nodiscard]] static std::filesystem::path game_settings();
        [[nodiscard]] static std::filesystem::path world_gen();
    };
}
