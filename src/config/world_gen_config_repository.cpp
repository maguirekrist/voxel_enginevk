#include "world_gen_config_repository.h"

#include "config_paths.h"

namespace config
{
    namespace
    {
        constexpr int WorldGenConfigVersion = 7;

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
                    { "continentalFrequency", settings.shape.continentalFrequency },
                    { "erosionFrequency", settings.shape.erosionFrequency },
                    { "peaksFrequency", settings.shape.peaksFrequency },
                    { "detailFrequency", settings.shape.detailFrequency },
                    { "seaLevel", settings.shape.seaLevel },
                    { "riversEnabled", settings.shape.riversEnabled },
                    { "riverFrequency", settings.shape.riverFrequency },
                    { "riverThreshold", settings.shape.riverThreshold },
                    { "continentalStrength", settings.shape.continentalStrength },
                    { "peaksStrength", settings.shape.peaksStrength },
                    { "erosionStrength", settings.shape.erosionStrength },
                    { "valleyStrength", settings.shape.valleyStrength },
                    { "detailStrength", settings.shape.detailStrength },
                    { "erosionSuppressionLow", settings.shape.erosionSuppressionLow },
                    { "erosionSuppressionHigh", settings.shape.erosionSuppressionHigh }
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
                settings.shape.continentalFrequency = shape.value("continentalFrequency", settings.shape.continentalFrequency);
                settings.shape.erosionFrequency = shape.value("erosionFrequency", settings.shape.erosionFrequency);
                settings.shape.peaksFrequency = shape.value("peaksFrequency", settings.shape.peaksFrequency);
                settings.shape.detailFrequency = shape.value("detailFrequency", settings.shape.detailFrequency);
                settings.shape.seaLevel = shape.value("seaLevel", settings.shape.seaLevel);
                settings.shape.riversEnabled = shape.value("riversEnabled", settings.shape.riversEnabled);
                settings.shape.riverFrequency = shape.value("riverFrequency", settings.shape.riverFrequency);
                settings.shape.riverThreshold = shape.value("riverThreshold", settings.shape.riverThreshold);
                settings.shape.continentalStrength = shape.value("continentalStrength", settings.shape.continentalStrength);
                settings.shape.peaksStrength = shape.value("peaksStrength", settings.shape.peaksStrength);
                settings.shape.erosionStrength = shape.value("erosionStrength", settings.shape.erosionStrength);
                settings.shape.valleyStrength = shape.value("valleyStrength", settings.shape.valleyStrength);
                settings.shape.detailStrength = shape.value("detailStrength", settings.shape.detailStrength);
                settings.shape.erosionSuppressionLow = shape.value("erosionSuppressionLow", settings.shape.erosionSuppressionLow);
                settings.shape.erosionSuppressionHigh = shape.value("erosionSuppressionHigh", settings.shape.erosionSuppressionHigh);
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
