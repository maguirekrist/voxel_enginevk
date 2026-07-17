#pragma once

#include "game_settings_config_repository.h"
#include "json_document_store.h"
#include "world_geometry_config_repository.h"
#include "world_gen_config_repository.h"

namespace config
{
    class ConfigService
    {
    public:
        ConfigService();

        [[nodiscard]] GameSettingsConfigRepository& game_settings() noexcept;
        [[nodiscard]] const GameSettingsConfigRepository& game_settings() const noexcept;
        [[nodiscard]] WorldGenConfigRepository& world_gen() noexcept;
        [[nodiscard]] const WorldGenConfigRepository& world_gen() const noexcept;
        [[nodiscard]] WorldGeometryConfigRepository& world_geometry() noexcept;
        [[nodiscard]] const WorldGeometryConfigRepository& world_geometry() const noexcept;

    private:
        JsonFileDocumentStore _documentStore{};
        GameSettingsConfigRepository _gameSettingsRepository;
        WorldGenConfigRepository _worldGenRepository;
        WorldGeometryConfigRepository _worldGeometryRepository;
    };
}
