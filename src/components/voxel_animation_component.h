#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <glm/ext/quaternion_float.hpp>
#include <glm/vec3.hpp>

#include "components/game_object.h"
#include "third_party/nlohmann/json.hpp"
#include "voxel/voxel_animation_asset.h"
#include "voxel/voxel_animation_pose.h"

struct VoxelAnimationQueuedEvent
{
    std::string clipAssetId{};
    std::string trackId{};
    std::string eventId{};
    nlohmann::json payload = nlohmann::json::object();
    float timeSeconds{0.0f};

    [[nodiscard]] bool operator==(const VoxelAnimationQueuedEvent& other) const = default;
};

struct VoxelAnimationRootMotionSample
{
    std::string sourcePartId{};
    glm::vec3 translationDeltaLocal{0.0f};
    glm::quat rotationDeltaLocal{1.0f, 0.0f, 0.0f, 0.0f};
    RootMotionMode mode{RootMotionMode::Ignore};
    bool valid{false};

    [[nodiscard]] bool operator==(const VoxelAnimationRootMotionSample& other) const = default;
};

struct VoxelAnimationLayerPlaybackState
{
    std::string layerId{};
    std::string activeStateId{};
    float stateTimeSeconds{0.0f};
    bool inTransition{false};
    std::string targetStateId{};
    float targetStateTimeSeconds{0.0f};
    float transitionElapsedSeconds{0.0f};
    float transitionDurationSeconds{0.0f};
    bool hasRootMotionReference{false};
    glm::vec3 rootMotionReferenceTranslation{0.0f};
    glm::quat rootMotionReferenceRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 previousExtractedTranslation{0.0f};
    glm::quat previousExtractedRotation{1.0f, 0.0f, 0.0f, 0.0f};

    [[nodiscard]] bool operator==(const VoxelAnimationLayerPlaybackState& other) const = default;
};

struct VoxelAnimationComponent final : Component
{
    std::string controllerAssetId{};
    std::unordered_map<std::string, float> floatParameters{};
    std::unordered_map<std::string, bool> boolParameters{};
    std::unordered_map<std::string, bool> triggerParameters{};
    std::vector<VoxelAnimationLayerPlaybackState> layerStates{};
    VoxelAssemblyPose currentPose{};
    std::vector<VoxelAnimationQueuedEvent> pendingEvents{};
    VoxelAnimationRootMotionSample rootMotion{};
    bool enabled{true};
};
