#pragma once

#include <functional>
#include <vector>

#include "constants.h"
#include <glm/vec3.hpp>

namespace settings
{
    struct LightingTuningSettings
    {
        glm::vec3 daySkyZenith{0.58f, 0.80f, 1.0f};
        glm::vec3 daySkyHorizon{0.96f, 0.97f, 1.0f};
        glm::vec3 dayGround{0.60f, 0.48f, 0.36f};
        glm::vec3 daySun{1.18f, 1.06f, 0.92f};
        glm::vec3 dayShadow{0.78f, 0.86f, 1.0f};
        glm::vec3 dayFog{0.74f, 0.88f, 1.0f};
        glm::vec3 dayWaterShallow{0.42f, 0.82f, 0.95f};
        glm::vec3 dayWaterDeep{0.12f, 0.34f, 0.58f};

        glm::vec3 duskSkyHorizon{1.0f, 0.56f, 0.35f};
        glm::vec3 duskFog{0.93f, 0.56f, 0.42f};

        glm::vec3 nightSkyZenith{0.03f, 0.05f, 0.11f};
        glm::vec3 nightSkyHorizon{0.10f, 0.08f, 0.16f};
        glm::vec3 nightGround{0.10f, 0.08f, 0.11f};
        glm::vec3 nightSun{0.50f, 0.56f, 0.82f};
        glm::vec3 nightMoon{0.34f, 0.42f, 0.62f};
        glm::vec3 nightShadow{0.14f, 0.18f, 0.30f};
        glm::vec3 nightFog{0.06f, 0.10f, 0.18f};
        glm::vec3 nightWaterShallow{0.10f, 0.22f, 0.30f};
        glm::vec3 nightWaterDeep{0.02f, 0.05f, 0.10f};

        float aoStrength{0.10f};
        float shadowFloor{0.84f};
        float hemiStrength{0.50f};
        float skylightStrength{1.0f};
        float shadowStrength{1.0f};
        float localLightStrength{1.1f};
        float waterFogStrength{0.35f};
        float cycleDurationSeconds{180.0f};
    };

    struct WorldSettingsPersistence
    {
        int viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
        bool ambientOcclusionEnabled{false};
    };

    struct DebugSettingsPersistence
    {
        bool showChunkBoundaries{false};
    };

    struct DayNightSettingsPersistence
    {
        bool paused{false};
        float timeOfDay{0.32f};
        LightingTuningSettings tuning{};
    };

    struct PlayerSettingsPersistence
    {
        float moveSpeed{4.5f};
        float airControl{0.35f};
        float gravity{24.0f};
        float jumpVelocity{8.5f};
        float maxFallSpeed{30.0f};
        float collisionHalfWidth{0.35f};
        float collisionHalfDepth{0.35f};
        float collisionHeight{1.8f};
        glm::vec3 cameraTargetOffset{0.0f, 1.4f, 0.0f};
        bool flyModeEnabled{false};
    };

    struct GameSettingsPersistence
    {
        WorldSettingsPersistence world{};
        DebugSettingsPersistence debug{};
        DayNightSettingsPersistence dayNight{};
        PlayerSettingsPersistence player{};
    };

    struct ViewDistanceRuntimeSettings
    {
        int viewDistance{GameConfig::DEFAULT_VIEW_DISTANCE};
        int maximumResidentChunks{maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE)};
        float fogRadius{
            (CHUNK_SIZE * static_cast<float>(GameConfig::DEFAULT_VIEW_DISTANCE)) - 60.0f
        };
    };

    struct AmbientOcclusionRuntimeSettings
    {
        bool enabled{false};
    };

    struct PlayerRuntimeSettings
    {
        float moveSpeed{4.5f};
        float airControl{0.35f};
        float gravity{24.0f};
        float jumpVelocity{8.5f};
        float maxFallSpeed{30.0f};
        float collisionHalfWidth{0.35f};
        float collisionHalfDepth{0.35f};
        float collisionHeight{1.8f};
        glm::vec3 cameraTargetOffset{0.0f, 1.4f, 0.0f};
        bool flyModeEnabled{false};
    };

    class SettingsManager
    {
    public:
        using ViewDistanceHandler = std::function<void(const ViewDistanceRuntimeSettings&)>;
        using AmbientOcclusionHandler = std::function<void(const AmbientOcclusionRuntimeSettings&)>;
        using PlayerSettingsHandler = std::function<void(const PlayerRuntimeSettings&)>;

        SettingsManager();
        explicit SettingsManager(GameSettingsPersistence persistence);

        [[nodiscard]] const GameSettingsPersistence& persistence() const noexcept;
        [[nodiscard]] ViewDistanceRuntimeSettings view_distance_runtime_settings() const noexcept;
        [[nodiscard]] AmbientOcclusionRuntimeSettings ambient_occlusion_runtime_settings() const noexcept;
        [[nodiscard]] PlayerRuntimeSettings player_runtime_settings() const noexcept;

        template <typename Mutator>
        bool mutate(Mutator&& mutator)
        {
            GameSettingsPersistence next = _persistence;
            mutator(next);
            normalize(next);
            if (equal_persistence(next, _persistence))
            {
                return false;
            }

            const GameSettingsPersistence previous = _persistence;
            _persistence = std::move(next);
            dispatch_changes(previous, _persistence);
            return true;
        }

        void bind_view_distance_handler(ViewDistanceHandler handler, bool replayCurrent = true);
        void bind_ambient_occlusion_handler(AmbientOcclusionHandler handler, bool replayCurrent = true);
        void bind_player_settings_handler(PlayerSettingsHandler handler, bool replayCurrent = true);

    private:
        static bool equal_persistence(const GameSettingsPersistence& lhs, const GameSettingsPersistence& rhs) noexcept;
        static void normalize(GameSettingsPersistence& persistence);
        static ViewDistanceRuntimeSettings make_view_distance_runtime(const GameSettingsPersistence& persistence) noexcept;
        static AmbientOcclusionRuntimeSettings make_ambient_occlusion_runtime(const GameSettingsPersistence& persistence) noexcept;
        static PlayerRuntimeSettings make_player_runtime(const GameSettingsPersistence& persistence) noexcept;

        void dispatch_changes(const GameSettingsPersistence& previous, const GameSettingsPersistence& current);

        GameSettingsPersistence _persistence{};
        std::vector<ViewDistanceHandler> _viewDistanceHandlers{};
        std::vector<AmbientOcclusionHandler> _ambientOcclusionHandlers{};
        std::vector<PlayerSettingsHandler> _playerSettingsHandlers{};
    };
}
