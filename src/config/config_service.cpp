#include "config_service.h"

namespace config
{
    ConfigService::ConfigService() :
        _gameSettingsRepository(_documentStore),
        _worldGenRepository(_documentStore)
    {
    }

    GameSettingsConfigRepository& ConfigService::game_settings() noexcept
    {
        return _gameSettingsRepository;
    }

    const GameSettingsConfigRepository& ConfigService::game_settings() const noexcept
    {
        return _gameSettingsRepository;
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
