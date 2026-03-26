#include <filesystem>

#include <gtest/gtest.h>

#include "test_support.h"
#include "config/game_settings_config_repository.h"
#include "settings/game_settings.h"

using test_support::TestJsonDocumentStore;

TEST(GameSettingsConfigRepositoryTest, SavesAndLoadsSettingsRoundTrip)
{
    settings::GameSettingsPersistence persistence{};
    persistence.world.viewDistance = 14;
    persistence.world.ambientOcclusionEnabled = true;
    persistence.debug.showChunkBoundaries = true;
    persistence.player.moveSpeed = 7.5f;
    persistence.player.airControl = 0.6f;
    persistence.player.gravity = 18.0f;
    persistence.player.jumpVelocity = 11.0f;
    persistence.player.maxFallSpeed = 42.0f;
    persistence.player.cameraTargetOffset = glm::vec3(0.1f, 1.6f, -0.2f);
    persistence.player.flyModeEnabled = true;
    persistence.dayNight.paused = true;
    persistence.dayNight.timeOfDay = 0.71f;
    persistence.dayNight.tuning.daySkyZenith = { 0.25f, 0.44f, 0.93f };
    persistence.dayNight.tuning.dayFog = { 0.68f, 0.59f, 0.41f };
    persistence.dayNight.tuning.nightMoon = { 0.31f, 0.37f, 0.72f };
    persistence.dayNight.tuning.aoStrength = 0.22f;
    persistence.dayNight.tuning.shadowStrength = 2.2f;
    persistence.dayNight.tuning.cycleDurationSeconds = 420.0f;

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_game_settings_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const config::GameSettingsConfigRepository repository(documentStore);
    repository.save(persistence);
    const settings::GameSettingsPersistence loaded = repository.load_or_default();
    std::filesystem::remove_all(tempRoot);

    EXPECT_EQ(loaded.world.viewDistance, persistence.world.viewDistance);
    EXPECT_EQ(loaded.world.ambientOcclusionEnabled, persistence.world.ambientOcclusionEnabled);
    EXPECT_EQ(loaded.debug.showChunkBoundaries, persistence.debug.showChunkBoundaries);
    EXPECT_FLOAT_EQ(loaded.player.moveSpeed, persistence.player.moveSpeed);
    EXPECT_FLOAT_EQ(loaded.player.airControl, persistence.player.airControl);
    EXPECT_FLOAT_EQ(loaded.player.gravity, persistence.player.gravity);
    EXPECT_FLOAT_EQ(loaded.player.jumpVelocity, persistence.player.jumpVelocity);
    EXPECT_FLOAT_EQ(loaded.player.maxFallSpeed, persistence.player.maxFallSpeed);
    EXPECT_FLOAT_EQ(loaded.player.cameraTargetOffset.x, persistence.player.cameraTargetOffset.x);
    EXPECT_FLOAT_EQ(loaded.player.cameraTargetOffset.y, persistence.player.cameraTargetOffset.y);
    EXPECT_FLOAT_EQ(loaded.player.cameraTargetOffset.z, persistence.player.cameraTargetOffset.z);
    EXPECT_EQ(loaded.player.flyModeEnabled, persistence.player.flyModeEnabled);
    EXPECT_EQ(loaded.dayNight.paused, persistence.dayNight.paused);
    EXPECT_FLOAT_EQ(loaded.dayNight.timeOfDay, persistence.dayNight.timeOfDay);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.x, persistence.dayNight.tuning.daySkyZenith.x);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.y, persistence.dayNight.tuning.daySkyZenith.y);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.daySkyZenith.z, persistence.dayNight.tuning.daySkyZenith.z);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.dayFog.x, persistence.dayNight.tuning.dayFog.x);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.nightMoon.z, persistence.dayNight.tuning.nightMoon.z);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.aoStrength, persistence.dayNight.tuning.aoStrength);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.shadowStrength, persistence.dayNight.tuning.shadowStrength);
    EXPECT_FLOAT_EQ(loaded.dayNight.tuning.cycleDurationSeconds, persistence.dayNight.tuning.cycleDurationSeconds);
}

TEST(SettingsManagerTest, ViewDistanceHandlersReceiveDerivedRuntimeValues)
{
    settings::SettingsManager manager;
    settings::ViewDistanceRuntimeSettings last{};
    int notifications = 0;

    manager.bind_view_distance_handler([&](const settings::ViewDistanceRuntimeSettings& runtime)
    {
        last = runtime;
        ++notifications;
    });

    EXPECT_EQ(notifications, 1);
    EXPECT_EQ(last.viewDistance, GameConfig::DEFAULT_VIEW_DISTANCE);
    EXPECT_EQ(last.maximumResidentChunks, maximum_chunks_for_view_distance(GameConfig::DEFAULT_VIEW_DISTANCE));

    const bool changed = manager.mutate([](settings::GameSettingsPersistence& persistence)
    {
        persistence.world.viewDistance = 18;
    });

    EXPECT_TRUE(changed);
    EXPECT_EQ(notifications, 2);
    EXPECT_EQ(last.viewDistance, 18);
    EXPECT_EQ(last.maximumResidentChunks, maximum_chunks_for_view_distance(18));
}

TEST(SettingsManagerTest, AmbientOcclusionUpdatesOnlyRelevantSubscribers)
{
    settings::SettingsManager manager;
    int viewDistanceNotifications = 0;
    int ambientNotifications = 0;
    bool ambientEnabled = false;

    manager.bind_view_distance_handler([&](const settings::ViewDistanceRuntimeSettings&)
    {
        ++viewDistanceNotifications;
    });
    manager.bind_ambient_occlusion_handler([&](const settings::AmbientOcclusionRuntimeSettings& runtime)
    {
        ambientEnabled = runtime.enabled;
        ++ambientNotifications;
    });

    const bool changed = manager.mutate([](settings::GameSettingsPersistence& persistence)
    {
        persistence.world.ambientOcclusionEnabled = true;
    });

    EXPECT_TRUE(changed);
    EXPECT_TRUE(ambientEnabled);
    EXPECT_EQ(ambientNotifications, 2);
    EXPECT_EQ(viewDistanceNotifications, 1);
}

TEST(SettingsManagerTest, PlayerHandlersReceiveCameraRuntimeValues)
{
    settings::SettingsManager manager;
    settings::PlayerRuntimeSettings last{};
    int notifications = 0;

    manager.bind_player_settings_handler([&](const settings::PlayerRuntimeSettings& runtime)
    {
        last = runtime;
        ++notifications;
    });

    EXPECT_EQ(notifications, 1);
    EXPECT_FLOAT_EQ(last.cameraTargetOffset.y, 1.4f);

    const bool changed = manager.mutate([](settings::GameSettingsPersistence& persistence)
    {
        persistence.player.cameraTargetOffset = glm::vec3(0.1f, 1.8f, -0.3f);
    });

    EXPECT_TRUE(changed);
    EXPECT_EQ(notifications, 2);
    EXPECT_FLOAT_EQ(last.cameraTargetOffset.x, 0.1f);
    EXPECT_FLOAT_EQ(last.cameraTargetOffset.y, 1.8f);
    EXPECT_FLOAT_EQ(last.cameraTargetOffset.z, -0.3f);
}
