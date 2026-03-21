#include "world_gen_config_repository.h"

#include "config_paths.h"

namespace config
{
    namespace
    {
        constexpr int WorldGenConfigVersion = 1;

        nlohmann::json spline_to_json(const std::vector<SplinePoint>& spline)
        {
            nlohmann::json result = nlohmann::json::array();
            for (const SplinePoint& point : spline)
            {
                result.push_back({
                    { "noise", point.noiseValue },
                    { "height", point.heightValue }
                });
            }

            return result;
        }

        void read_spline(const nlohmann::json& node, const char* key, std::vector<SplinePoint>& spline)
        {
            if (!node.contains(key) || !node.at(key).is_array())
            {
                return;
            }

            std::vector<SplinePoint> parsed{};
            for (const nlohmann::json& point : node.at(key))
            {
                if (!point.is_object())
                {
                    continue;
                }

                parsed.push_back(SplinePoint{
                    .noiseValue = point.value("noise", 0.0f),
                    .heightValue = point.value("height", 0.0f)
                });
            }

            if (!parsed.empty())
            {
                spline = std::move(parsed);
            }
        }

        nlohmann::json serialize(const TerrainGeneratorSettings& settings)
        {
            return {
                { "version", WorldGenConfigVersion },
                { "seed", settings.seed },
                { "shape", {
                    { "terrainFrequency", settings.shape.terrainFrequency },
                    { "climateFrequency", settings.shape.climateFrequency },
                    { "riverFrequency", settings.shape.riverFrequency },
                    { "riverThreshold", settings.shape.riverThreshold },
                    { "erosionSuppressionLow", settings.shape.erosionSuppressionLow },
                    { "erosionSuppressionHigh", settings.shape.erosionSuppressionHigh }
                } },
                { "biome", {
                    { "oceanContinentalnessThreshold", settings.biome.oceanContinentalnessThreshold },
                    { "riverBlendThreshold", settings.biome.riverBlendThreshold },
                    { "riverMinBankHeightOffset", settings.biome.riverMinBankHeightOffset },
                    { "beachMinHeightOffset", settings.biome.beachMinHeightOffset },
                    { "beachMaxHeightOffset", settings.biome.beachMaxHeightOffset },
                    { "mountainHeightOffset", settings.biome.mountainHeightOffset },
                    { "mountainPeaksThreshold", settings.biome.mountainPeaksThreshold },
                    { "forestHumidityThreshold", settings.biome.forestHumidityThreshold },
                    { "forestTemperatureThreshold", settings.biome.forestTemperatureThreshold },
                    { "mountainStoneHeightOffset", settings.biome.mountainStoneHeightOffset }
                } },
                { "surface", {
                    { "riverTargetHeightOffset", settings.surface.riverTargetHeightOffset },
                    { "riverMinDepth", settings.surface.riverMinDepth },
                    { "riverMaxDepth", settings.surface.riverMaxDepth },
                    { "oceanFloorHeightOffset", settings.surface.oceanFloorHeightOffset },
                    { "shoreMinHeightOffset", settings.surface.shoreMinHeightOffset },
                    { "shoreMaxHeightOffset", settings.surface.shoreMaxHeightOffset },
                    { "riverStoneDepth", settings.surface.riverStoneDepth },
                    { "oceanStoneDepth", settings.surface.oceanStoneDepth },
                    { "shoreStoneDepth", settings.surface.shoreStoneDepth },
                    { "plainsStoneDepth", settings.surface.plainsStoneDepth },
                    { "mountainStoneDepth", settings.surface.mountainStoneDepth }
                } },
                { "splines", {
                    { "erosion", spline_to_json(settings.erosionSplines) },
                    { "peaks", spline_to_json(settings.peakSplines) },
                    { "continentalness", spline_to_json(settings.continentalSplines) }
                } }
            };
        }

        TerrainGeneratorSettings deserialize(const nlohmann::json& document)
        {
            TerrainGeneratorSettings settings = TerrainGenerator::default_settings();
            settings.seed = document.value("seed", settings.seed);

            if (document.contains("shape") && document.at("shape").is_object())
            {
                const auto& shape = document.at("shape");
                settings.shape.terrainFrequency = shape.value("terrainFrequency", settings.shape.terrainFrequency);
                settings.shape.climateFrequency = shape.value("climateFrequency", settings.shape.climateFrequency);
                settings.shape.riverFrequency = shape.value("riverFrequency", settings.shape.riverFrequency);
                settings.shape.riverThreshold = shape.value("riverThreshold", settings.shape.riverThreshold);
                settings.shape.erosionSuppressionLow = shape.value("erosionSuppressionLow", settings.shape.erosionSuppressionLow);
                settings.shape.erosionSuppressionHigh = shape.value("erosionSuppressionHigh", settings.shape.erosionSuppressionHigh);
            }

            if (document.contains("biome") && document.at("biome").is_object())
            {
                const auto& biome = document.at("biome");
                settings.biome.oceanContinentalnessThreshold = biome.value("oceanContinentalnessThreshold", settings.biome.oceanContinentalnessThreshold);
                settings.biome.riverBlendThreshold = biome.value("riverBlendThreshold", settings.biome.riverBlendThreshold);
                settings.biome.riverMinBankHeightOffset = biome.value("riverMinBankHeightOffset", settings.biome.riverMinBankHeightOffset);
                settings.biome.beachMinHeightOffset = biome.value("beachMinHeightOffset", settings.biome.beachMinHeightOffset);
                settings.biome.beachMaxHeightOffset = biome.value("beachMaxHeightOffset", settings.biome.beachMaxHeightOffset);
                settings.biome.mountainHeightOffset = biome.value("mountainHeightOffset", settings.biome.mountainHeightOffset);
                settings.biome.mountainPeaksThreshold = biome.value("mountainPeaksThreshold", settings.biome.mountainPeaksThreshold);
                settings.biome.forestHumidityThreshold = biome.value("forestHumidityThreshold", settings.biome.forestHumidityThreshold);
                settings.biome.forestTemperatureThreshold = biome.value("forestTemperatureThreshold", settings.biome.forestTemperatureThreshold);
                settings.biome.mountainStoneHeightOffset = biome.value("mountainStoneHeightOffset", settings.biome.mountainStoneHeightOffset);
            }

            if (document.contains("surface") && document.at("surface").is_object())
            {
                const auto& surface = document.at("surface");
                settings.surface.riverTargetHeightOffset = surface.value("riverTargetHeightOffset", settings.surface.riverTargetHeightOffset);
                settings.surface.riverMinDepth = surface.value("riverMinDepth", settings.surface.riverMinDepth);
                settings.surface.riverMaxDepth = surface.value("riverMaxDepth", settings.surface.riverMaxDepth);
                settings.surface.oceanFloorHeightOffset = surface.value("oceanFloorHeightOffset", settings.surface.oceanFloorHeightOffset);
                settings.surface.shoreMinHeightOffset = surface.value("shoreMinHeightOffset", settings.surface.shoreMinHeightOffset);
                settings.surface.shoreMaxHeightOffset = surface.value("shoreMaxHeightOffset", settings.surface.shoreMaxHeightOffset);
                settings.surface.riverStoneDepth = surface.value("riverStoneDepth", settings.surface.riverStoneDepth);
                settings.surface.oceanStoneDepth = surface.value("oceanStoneDepth", settings.surface.oceanStoneDepth);
                settings.surface.shoreStoneDepth = surface.value("shoreStoneDepth", settings.surface.shoreStoneDepth);
                settings.surface.plainsStoneDepth = surface.value("plainsStoneDepth", settings.surface.plainsStoneDepth);
                settings.surface.mountainStoneDepth = surface.value("mountainStoneDepth", settings.surface.mountainStoneDepth);
            }

            if (document.contains("splines") && document.at("splines").is_object())
            {
                const auto& splines = document.at("splines");
                read_spline(splines, "erosion", settings.erosionSplines);
                read_spline(splines, "peaks", settings.peakSplines);
                read_spline(splines, "continentalness", settings.continentalSplines);
            }

            return settings;
        }
    }

    WorldGenConfigRepository::WorldGenConfigRepository(const IJsonDocumentStore& documentStore) :
        _documentStore(documentStore)
    {
    }

    TerrainGeneratorSettings WorldGenConfigRepository::load_or_default() const
    {
        try
        {
            if (const auto document = _documentStore.load(ConfigPaths::world_gen()); document.has_value())
            {
                return deserialize(document.value());
            }
        }
        catch (const std::exception&)
        {
        }

        return TerrainGenerator::default_settings();
    }

    void WorldGenConfigRepository::save(const TerrainGeneratorSettings& settings) const
    {
        _documentStore.save(ConfigPaths::world_gen(), serialize(settings));
    }
}
