#include "world_gen_config_repository.h"

#include "config_paths.h"

namespace config
{
    namespace
    {
        constexpr int WorldGenConfigVersion = 12;

        nlohmann::json serialize_noise_layer(const TerrainNoiseLayerSettings& settings)
        {
            return {
                { "basis", static_cast<uint32_t>(settings.basis) },
                { "frequency", settings.frequency },
                { "octaves", settings.octaves },
                { "lacunarity", settings.lacunarity },
                { "gain", settings.gain },
                { "weightedStrength", settings.weightedStrength },
                { "terraceStepCount", settings.terraceStepCount },
                { "terraceSmoothness", settings.terraceSmoothness },
                { "strength", settings.strength }
            };
        }

        void read_noise_layer(const nlohmann::json& node, const char* key, TerrainNoiseLayerSettings& settings)
        {
            if (!node.contains(key) || !node.at(key).is_object())
            {
                return;
            }

            const auto& layer = node.at(key);
            settings.basis = static_cast<TerrainNoiseBasis>(layer.value("basis", static_cast<uint32_t>(settings.basis)));
            settings.frequency = layer.value("frequency", settings.frequency);
            settings.octaves = layer.value("octaves", settings.octaves);
            settings.lacunarity = layer.value("lacunarity", settings.lacunarity);
            settings.gain = layer.value("gain", settings.gain);
            settings.weightedStrength = layer.value("weightedStrength", settings.weightedStrength);
            settings.terraceStepCount = layer.value("terraceStepCount", settings.terraceStepCount);
            settings.terraceSmoothness = layer.value("terraceSmoothness", settings.terraceSmoothness);
            settings.strength = layer.value("strength", settings.strength);
        }

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
                    { "seaLevel", settings.shape.seaLevel },
                    { "continental", serialize_noise_layer(settings.shape.continental) },
                    { "erosion", serialize_noise_layer(settings.shape.erosion) },
                    { "peaks", serialize_noise_layer(settings.shape.peaks) }
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
                settings.shape.seaLevel = shape.value("seaLevel", settings.shape.seaLevel);
                read_noise_layer(shape, "continental", settings.shape.continental);
                read_noise_layer(shape, "erosion", settings.shape.erosion);
                read_noise_layer(shape, "peaks", settings.shape.peaks);
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
