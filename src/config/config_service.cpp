#include "config_service.h"

namespace config
{
    ConfigService::ConfigService() :
        _worldGenRepository(_documentStore)
    {
    }

    WorldGenConfigRepository& ConfigService::world_gen() noexcept
    {
        return _worldGenRepository;
    }

    const WorldGenConfigRepository& ConfigService::world_gen() const noexcept
    {
        return _worldGenRepository;
    }
}
