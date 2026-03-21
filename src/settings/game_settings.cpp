#include "game_settings.h"

#include <algorithm>

namespace settings
{
    namespace
    {
        bool equal_vec3(const glm::vec3& lhs, const glm::vec3& rhs) noexcept
        {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool equal_lighting_tuning(const LightingTuningSettings& lhs, const LightingTuningSettings& rhs) noexcept
        {
            return equal_vec3(lhs.daySkyZenith, rhs.daySkyZenith) &&
                equal_vec3(lhs.daySkyHorizon, rhs.daySkyHorizon) &&
                equal_vec3(lhs.dayGround, rhs.dayGround) &&
                equal_vec3(lhs.daySun, rhs.daySun) &&
                equal_vec3(lhs.dayShadow, rhs.dayShadow) &&
                equal_vec3(lhs.dayFog, rhs.dayFog) &&
                equal_vec3(lhs.dayWaterShallow, rhs.dayWaterShallow) &&
                equal_vec3(lhs.dayWaterDeep, rhs.dayWaterDeep) &&
                equal_vec3(lhs.duskSkyHorizon, rhs.duskSkyHorizon) &&
                equal_vec3(lhs.duskFog, rhs.duskFog) &&
                equal_vec3(lhs.nightSkyZenith, rhs.nightSkyZenith) &&
                equal_vec3(lhs.nightSkyHorizon, rhs.nightSkyHorizon) &&
                equal_vec3(lhs.nightGround, rhs.nightGround) &&
                equal_vec3(lhs.nightSun, rhs.nightSun) &&
                equal_vec3(lhs.nightMoon, rhs.nightMoon) &&
                equal_vec3(lhs.nightShadow, rhs.nightShadow) &&
                equal_vec3(lhs.nightFog, rhs.nightFog) &&
                equal_vec3(lhs.nightWaterShallow, rhs.nightWaterShallow) &&
                equal_vec3(lhs.nightWaterDeep, rhs.nightWaterDeep) &&
                lhs.aoStrength == rhs.aoStrength &&
                lhs.shadowFloor == rhs.shadowFloor &&
                lhs.hemiStrength == rhs.hemiStrength &&
                lhs.skylightStrength == rhs.skylightStrength &&
                lhs.shadowStrength == rhs.shadowStrength &&
                lhs.localLightStrength == rhs.localLightStrength &&
                lhs.waterFogStrength == rhs.waterFogStrength &&
                lhs.cycleDurationSeconds == rhs.cycleDurationSeconds;
        }

        bool compare_persistence(const GameSettingsPersistence& lhs, const GameSettingsPersistence& rhs) noexcept
        {
            return lhs.world.viewDistance == rhs.world.viewDistance &&
                lhs.world.ambientOcclusionEnabled == rhs.world.ambientOcclusionEnabled &&
                lhs.debug.showChunkBoundaries == rhs.debug.showChunkBoundaries &&
                lhs.dayNight.paused == rhs.dayNight.paused &&
                lhs.dayNight.timeOfDay == rhs.dayNight.timeOfDay &&
                equal_lighting_tuning(lhs.dayNight.tuning, rhs.dayNight.tuning);
        }
    }

    SettingsManager::SettingsManager() = default;

    SettingsManager::SettingsManager(GameSettingsPersistence persistence) : _persistence(std::move(persistence))
    {
        normalize(_persistence);
    }

    const GameSettingsPersistence& SettingsManager::persistence() const noexcept
    {
        return _persistence;
    }

    ViewDistanceRuntimeSettings SettingsManager::view_distance_runtime_settings() const noexcept
    {
        return make_view_distance_runtime(_persistence);
    }

    AmbientOcclusionRuntimeSettings SettingsManager::ambient_occlusion_runtime_settings() const noexcept
    {
        return make_ambient_occlusion_runtime(_persistence);
    }

    bool SettingsManager::equal_persistence(const GameSettingsPersistence& lhs, const GameSettingsPersistence& rhs) noexcept
    {
        return compare_persistence(lhs, rhs);
    }

    void SettingsManager::bind_view_distance_handler(ViewDistanceHandler handler, const bool replayCurrent)
    {
        _viewDistanceHandlers.push_back(std::move(handler));
        if (replayCurrent)
        {
            _viewDistanceHandlers.back()(make_view_distance_runtime(_persistence));
        }
    }

    void SettingsManager::bind_ambient_occlusion_handler(AmbientOcclusionHandler handler, const bool replayCurrent)
    {
        _ambientOcclusionHandlers.push_back(std::move(handler));
        if (replayCurrent)
        {
            _ambientOcclusionHandlers.back()(make_ambient_occlusion_runtime(_persistence));
        }
    }

    void SettingsManager::normalize(GameSettingsPersistence& persistence)
    {
        persistence.world.viewDistance = std::max(1, persistence.world.viewDistance);
        persistence.dayNight.timeOfDay = std::clamp(persistence.dayNight.timeOfDay, 0.0f, 1.0f);
        persistence.dayNight.tuning.cycleDurationSeconds = std::max(1.0f, persistence.dayNight.tuning.cycleDurationSeconds);
    }

    ViewDistanceRuntimeSettings SettingsManager::make_view_distance_runtime(const GameSettingsPersistence& persistence) noexcept
    {
        const int viewDistance = std::max(1, persistence.world.viewDistance);
        return ViewDistanceRuntimeSettings{
            .viewDistance = viewDistance,
            .maximumResidentChunks = maximum_chunks_for_view_distance(viewDistance),
            .fogRadius = (CHUNK_SIZE * static_cast<float>(viewDistance)) - 60.0f
        };
    }

    AmbientOcclusionRuntimeSettings SettingsManager::make_ambient_occlusion_runtime(const GameSettingsPersistence& persistence) noexcept
    {
        return AmbientOcclusionRuntimeSettings{
            .enabled = persistence.world.ambientOcclusionEnabled
        };
    }

    void SettingsManager::dispatch_changes(const GameSettingsPersistence& previous, const GameSettingsPersistence& current)
    {
        if (previous.world.viewDistance != current.world.viewDistance)
        {
            const ViewDistanceRuntimeSettings runtime = make_view_distance_runtime(current);
            for (const ViewDistanceHandler& handler : _viewDistanceHandlers)
            {
                handler(runtime);
            }
        }

        if (previous.world.ambientOcclusionEnabled != current.world.ambientOcclusionEnabled)
        {
            const AmbientOcclusionRuntimeSettings runtime = make_ambient_occlusion_runtime(current);
            for (const AmbientOcclusionHandler& handler : _ambientOcclusionHandlers)
            {
                handler(runtime);
            }
        }
    }
}
