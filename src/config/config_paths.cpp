#include "config_paths.h"

namespace config
{
    std::filesystem::path ConfigPaths::root()
    {
        return "config";
    }

    std::filesystem::path ConfigPaths::world_gen()
    {
        return root() / "world_gen.json";
    }
}
