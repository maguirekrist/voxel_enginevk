#include "world_geometry_config_repository.h"

#include "config_paths.h"

namespace config
{
    namespace
    {
        constexpr int WorldGeometryConfigVersion = 1;

        nlohmann::json serialize(WorldGeometrySettings settings)
        {
            normalize_world_geometry_settings(settings);
            return {
                { "version", WorldGeometryConfigVersion },
                { "chunkVoxelWidth", settings.chunkVoxelWidth },
                { "chunkVoxelHeight", settings.chunkVoxelHeight },
                { "chunkWorldWidth", settings.chunkWorldWidth },
                { "chunkWorldHeight", settings.chunkWorldHeight }
            };
        }

        WorldGeometrySettings deserialize(const nlohmann::json& document)
        {
            WorldGeometrySettings settings = default_world_geometry_settings();
            settings.chunkVoxelWidth = document.value("chunkVoxelWidth", settings.chunkVoxelWidth);
            settings.chunkVoxelHeight = document.value("chunkVoxelHeight", settings.chunkVoxelHeight);
            settings.chunkWorldWidth = document.value("chunkWorldWidth", settings.chunkWorldWidth);
            settings.chunkWorldHeight = document.value("chunkWorldHeight", settings.chunkWorldHeight);
            normalize_world_geometry_settings(settings);
            return settings;
        }
    }

    WorldGeometryConfigRepository::WorldGeometryConfigRepository(const IJsonDocumentStore& documentStore) :
        _documentStore(documentStore)
    {
    }

    WorldGeometrySettings WorldGeometryConfigRepository::load_or_default() const
    {
        try
        {
            if (const auto document = _documentStore.load(ConfigPaths::world_geometry()); document.has_value())
            {
                return deserialize(document.value());
            }
        }
        catch (const std::exception&)
        {
        }

        return default_world_geometry_settings();
    }

    void WorldGeometryConfigRepository::save(const WorldGeometrySettings& settings) const
    {
        _documentStore.save(ConfigPaths::world_geometry(), serialize(settings));
    }
}
