#include "game_settings_config_repository.h"

#include "config_paths.h"

namespace config
{
    namespace
    {
        constexpr int GameSettingsConfigVersion = 3;

        nlohmann::json vec3_to_json(const glm::vec3& value)
        {
            return {
                { "x", value.x },
                { "y", value.y },
                { "z", value.z }
            };
        }

        void read_vec3(const nlohmann::json& node, const char* key, glm::vec3& value)
        {
            if (!node.contains(key) || !node.at(key).is_object())
            {
                return;
            }

            const nlohmann::json& vector = node.at(key);
            value.x = vector.value("x", value.x);
            value.y = vector.value("y", value.y);
            value.z = vector.value("z", value.z);
        }

        nlohmann::json serialize(const settings::GameSettingsPersistence& settings)
        {
            const settings::LightingTuningSettings& tuning = settings.dayNight.tuning;
            return {
                { "version", GameSettingsConfigVersion },
                { "world", {
                    { "viewDistance", settings.world.viewDistance },
                    { "ambientOcclusionEnabled", settings.world.ambientOcclusionEnabled }
                } },
                { "debug", {
                    { "showChunkBoundaries", settings.debug.showChunkBoundaries }
                } },
                { "player", {
                    { "moveSpeed", settings.player.moveSpeed },
                    { "airControl", settings.player.airControl },
                    { "gravity", settings.player.gravity },
                    { "jumpVelocity", settings.player.jumpVelocity },
                    { "maxFallSpeed", settings.player.maxFallSpeed },
                    { "cameraTargetOffset", vec3_to_json(settings.player.cameraTargetOffset) },
                    { "flyModeEnabled", settings.player.flyModeEnabled }
                } },
                { "dayNight", {
                    { "paused", settings.dayNight.paused },
                    { "timeOfDay", settings.dayNight.timeOfDay },
                    { "tuning", {
                        { "daySkyZenith", vec3_to_json(tuning.daySkyZenith) },
                        { "daySkyHorizon", vec3_to_json(tuning.daySkyHorizon) },
                        { "dayGround", vec3_to_json(tuning.dayGround) },
                        { "daySun", vec3_to_json(tuning.daySun) },
                        { "dayShadow", vec3_to_json(tuning.dayShadow) },
                        { "dayFog", vec3_to_json(tuning.dayFog) },
                        { "dayWaterShallow", vec3_to_json(tuning.dayWaterShallow) },
                        { "dayWaterDeep", vec3_to_json(tuning.dayWaterDeep) },
                        { "duskSkyHorizon", vec3_to_json(tuning.duskSkyHorizon) },
                        { "duskFog", vec3_to_json(tuning.duskFog) },
                        { "nightSkyZenith", vec3_to_json(tuning.nightSkyZenith) },
                        { "nightSkyHorizon", vec3_to_json(tuning.nightSkyHorizon) },
                        { "nightGround", vec3_to_json(tuning.nightGround) },
                        { "nightSun", vec3_to_json(tuning.nightSun) },
                        { "nightMoon", vec3_to_json(tuning.nightMoon) },
                        { "nightShadow", vec3_to_json(tuning.nightShadow) },
                        { "nightFog", vec3_to_json(tuning.nightFog) },
                        { "nightWaterShallow", vec3_to_json(tuning.nightWaterShallow) },
                        { "nightWaterDeep", vec3_to_json(tuning.nightWaterDeep) },
                        { "aoStrength", tuning.aoStrength },
                        { "shadowFloor", tuning.shadowFloor },
                        { "hemiStrength", tuning.hemiStrength },
                        { "skylightStrength", tuning.skylightStrength },
                        { "shadowStrength", tuning.shadowStrength },
                        { "localLightStrength", tuning.localLightStrength },
                        { "waterFogStrength", tuning.waterFogStrength },
                        { "cycleDurationSeconds", tuning.cycleDurationSeconds }
                    } }
                } }
            };
        }

        settings::GameSettingsPersistence deserialize(const nlohmann::json& document)
        {
            settings::GameSettingsPersistence settings{};

            if (document.contains("world") && document.at("world").is_object())
            {
                const auto& world = document.at("world");
                settings.world.viewDistance = world.value("viewDistance", settings.world.viewDistance);
                settings.world.ambientOcclusionEnabled = world.value("ambientOcclusionEnabled", settings.world.ambientOcclusionEnabled);
            }

            if (document.contains("debug") && document.at("debug").is_object())
            {
                const auto& debug = document.at("debug");
                settings.debug.showChunkBoundaries = debug.value("showChunkBoundaries", settings.debug.showChunkBoundaries);
            }

            if (document.contains("player") && document.at("player").is_object())
            {
                const auto& player = document.at("player");
                settings.player.moveSpeed = player.value("moveSpeed", settings.player.moveSpeed);
                settings.player.airControl = player.value("airControl", settings.player.airControl);
                settings.player.gravity = player.value("gravity", settings.player.gravity);
                settings.player.jumpVelocity = player.value("jumpVelocity", settings.player.jumpVelocity);
                settings.player.maxFallSpeed = player.value("maxFallSpeed", settings.player.maxFallSpeed);
                read_vec3(player, "cameraTargetOffset", settings.player.cameraTargetOffset);
                settings.player.flyModeEnabled = player.value("flyModeEnabled", settings.player.flyModeEnabled);
            }

            if (document.contains("dayNight") && document.at("dayNight").is_object())
            {
                const auto& dayNight = document.at("dayNight");
                settings.dayNight.paused = dayNight.value("paused", settings.dayNight.paused);
                settings.dayNight.timeOfDay = dayNight.value("timeOfDay", settings.dayNight.timeOfDay);

                if (dayNight.contains("tuning") && dayNight.at("tuning").is_object())
                {
                    const auto& tuningNode = dayNight.at("tuning");
                    settings::LightingTuningSettings& tuning = settings.dayNight.tuning;
                    read_vec3(tuningNode, "daySkyZenith", tuning.daySkyZenith);
                    read_vec3(tuningNode, "daySkyHorizon", tuning.daySkyHorizon);
                    read_vec3(tuningNode, "dayGround", tuning.dayGround);
                    read_vec3(tuningNode, "daySun", tuning.daySun);
                    read_vec3(tuningNode, "dayShadow", tuning.dayShadow);
                    read_vec3(tuningNode, "dayFog", tuning.dayFog);
                    read_vec3(tuningNode, "dayWaterShallow", tuning.dayWaterShallow);
                    read_vec3(tuningNode, "dayWaterDeep", tuning.dayWaterDeep);
                    read_vec3(tuningNode, "duskSkyHorizon", tuning.duskSkyHorizon);
                    read_vec3(tuningNode, "duskFog", tuning.duskFog);
                    read_vec3(tuningNode, "nightSkyZenith", tuning.nightSkyZenith);
                    read_vec3(tuningNode, "nightSkyHorizon", tuning.nightSkyHorizon);
                    read_vec3(tuningNode, "nightGround", tuning.nightGround);
                    read_vec3(tuningNode, "nightSun", tuning.nightSun);
                    read_vec3(tuningNode, "nightMoon", tuning.nightMoon);
                    read_vec3(tuningNode, "nightShadow", tuning.nightShadow);
                    read_vec3(tuningNode, "nightFog", tuning.nightFog);
                    read_vec3(tuningNode, "nightWaterShallow", tuning.nightWaterShallow);
                    read_vec3(tuningNode, "nightWaterDeep", tuning.nightWaterDeep);
                    tuning.aoStrength = tuningNode.value("aoStrength", tuning.aoStrength);
                    tuning.shadowFloor = tuningNode.value("shadowFloor", tuning.shadowFloor);
                    tuning.hemiStrength = tuningNode.value("hemiStrength", tuning.hemiStrength);
                    tuning.skylightStrength = tuningNode.value("skylightStrength", tuning.skylightStrength);
                    tuning.shadowStrength = tuningNode.value("shadowStrength", tuning.shadowStrength);
                    tuning.localLightStrength = tuningNode.value("localLightStrength", tuning.localLightStrength);
                    tuning.waterFogStrength = tuningNode.value("waterFogStrength", tuning.waterFogStrength);
                    tuning.cycleDurationSeconds = tuningNode.value("cycleDurationSeconds", tuning.cycleDurationSeconds);
                }
            }

            return settings;
        }
    }

    GameSettingsConfigRepository::GameSettingsConfigRepository(const IJsonDocumentStore& documentStore) :
        _documentStore(documentStore)
    {
    }

    settings::GameSettingsPersistence GameSettingsConfigRepository::load_or_default() const
    {
        try
        {
            if (const auto document = _documentStore.load(ConfigPaths::game_settings()); document.has_value())
            {
                return deserialize(document.value());
            }
        }
        catch (const std::exception&)
        {
        }

        return {};
    }

    void GameSettingsConfigRepository::save(const settings::GameSettingsPersistence& settings) const
    {
        _documentStore.save(ConfigPaths::game_settings(), serialize(settings));
    }
}
