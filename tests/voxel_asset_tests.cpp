#include <filesystem>
#include <memory>
#include <ranges>

#include <gtest/gtest.h>
#include <glm/ext/quaternion_trigonometric.hpp>

#include "test_support.h"
#include "components/voxel_assembly_component.h"
#include "voxel/voxel_assembly_asset_manager.h"
#include "voxel/voxel_assembly_repository.h"
#include "voxel/voxel_asset_manager.h"
#include "voxel/voxel_component_render_adapter.h"
#include "voxel/voxel_model_component_adapter.h"
#include "voxel/voxel_model_repository.h"
#include "voxel/voxel_render_instance.h"
#include "voxel/voxel_spatial_bounds.h"
#include "voxel/voxel_spatial_collider.h"

using test_support::TestJsonDocumentStore;

TEST(VoxelRenderInstanceTest, ModelMatrixAppliesPositionRotationAndScaleToPivotCenteredMesh)
{
    auto asset = std::make_shared<VoxelRuntimeAsset>();
    asset->model.pivot = glm::vec3(1.0f, 0.0f, 0.0f);

    const VoxelRenderInstance instance{
        .asset = asset,
        .position = glm::vec3(10.0f, 4.0f, -2.0f),
        .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
        .scale = 2.0f
    };

    const glm::vec3 worldPoint = instance.world_point_from_asset_local(glm::vec3(3.0f, 1.0f, 0.0f));
    EXPECT_NEAR(worldPoint.x, 10.0f, 0.0001f);
    EXPECT_NEAR(worldPoint.y, 6.0f, 0.0001f);
    EXPECT_NEAR(worldPoint.z, -6.0f, 0.0001f);
}

TEST(VoxelRenderInstanceTest, AttachmentWorldTransformUsesAttachmentPositionRelativeToPivot)
{
    auto asset = std::make_shared<VoxelRuntimeAsset>();
    asset->model.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    asset->model.attachments.push_back(VoxelAttachment{
        .name = "grip",
        .position = glm::vec3(1.5f, 0.5f, 0.5f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });

    const VoxelRenderInstance instance{
        .asset = asset,
        .position = glm::vec3(3.0f, 2.0f, 1.0f),
        .rotation = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)),
        .scale = 1.0f
    };

    const std::optional<glm::mat4> attachmentTransform = instance.attachment_world_transform("grip");
    ASSERT_TRUE(attachmentTransform.has_value());

    const glm::vec3 worldOrigin = glm::vec3(attachmentTransform.value()[3]);
    EXPECT_NEAR(worldOrigin.x, 3.0f, 0.0001f);
    EXPECT_NEAR(worldOrigin.y, 2.0f, 0.0001f);
    EXPECT_NEAR(worldOrigin.z, 0.0f, 0.0001f);
}

TEST(VoxelModelRepositoryTest, SavesAndLoadsVoxelAssetRoundTrip)
{
    VoxelModel model{};
    model.assetId = "Test Ogre";
    model.displayName = "Test Ogre";
    model.voxelSize = 1.0f / 16.0f;
    model.pivot = glm::vec3(0.5f, 0.0f, 0.5f);
    model.attachments.push_back(VoxelAttachment{
        .name = "right_hand",
        .position = glm::vec3(3.5f, 5.0f, 1.0f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 0, 0, 255 });
    model.set_voxel(VoxelCoord{ 3, 2, 1 }, VoxelColor{ 12, 34, 56, 255 });

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_model_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository repository(documentStore, "assets");

    repository.save(model);
    const std::optional<VoxelModel> loaded = repository.load("Test Ogre");
    std::filesystem::remove_all(tempRoot);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->assetId, "testogre");
    EXPECT_EQ(loaded->displayName, "Test Ogre");
    EXPECT_FLOAT_EQ(loaded->voxelSize, model.voxelSize);
    EXPECT_FLOAT_EQ(loaded->pivot.x, model.pivot.x);
    ASSERT_EQ(loaded->attachments.size(), 1u);
    EXPECT_EQ(loaded->attachments[0].name, "right_hand");
    EXPECT_FLOAT_EQ(loaded->attachments[0].position.x, 3.5f);
    EXPECT_FLOAT_EQ(loaded->attachments[0].position.y, 5.0f);
    ASSERT_EQ(loaded->voxel_count(), 2u);

    const VoxelColor* originColor = loaded->try_get(VoxelCoord{ 0, 0, 0 });
    ASSERT_NE(originColor, nullptr);
    EXPECT_EQ(originColor->r, 255);
    EXPECT_EQ(originColor->g, 0);
    EXPECT_EQ(originColor->b, 0);

    const VoxelColor* secondColor = loaded->try_get(VoxelCoord{ 3, 2, 1 });
    ASSERT_NE(secondColor, nullptr);
    EXPECT_EQ(secondColor->r, 12);
    EXPECT_EQ(secondColor->g, 34);
    EXPECT_EQ(secondColor->b, 56);
}

TEST(VoxelAssetManagerTest, LoadsAndCachesRuntimeAssets)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_runtime_asset_manager_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository repository(documentStore, "assets");

    VoxelModel model{};
    model.assetId = "Sword";
    model.displayName = "Sword";
    model.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    model.attachments.push_back(VoxelAttachment{
        .name = "grip",
        .position = glm::vec3(0.5f, 1.0f, 0.5f),
        .forward = glm::vec3(0.0f, 1.0f, 0.0f),
        .up = glm::vec3(1.0f, 0.0f, 0.0f)
    });
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    model.set_voxel(VoxelCoord{ 0, 1, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(model);

    VoxelAssetManager manager(repository);
    const std::shared_ptr<VoxelRuntimeAsset> firstLoad = manager.load_or_get("Sword");
    const std::shared_ptr<VoxelRuntimeAsset> secondLoad = manager.load_or_get("sword");

    std::filesystem::remove_all(tempRoot);

    ASSERT_NE(firstLoad, nullptr);
    ASSERT_NE(secondLoad, nullptr);
    EXPECT_EQ(firstLoad.get(), secondLoad.get());
    EXPECT_EQ(manager.loaded_asset_count(), 1u);
    EXPECT_EQ(firstLoad->assetId, "sword");
    ASSERT_TRUE(firstLoad->bounds.valid);
    ASSERT_NE(firstLoad->mesh, nullptr);
    EXPECT_FALSE(firstLoad->mesh->_indices.empty());
    ASSERT_EQ(firstLoad->attachments.size(), 1u);
    EXPECT_TRUE(firstLoad->attachments.contains("grip"));
    EXPECT_FLOAT_EQ(firstLoad->attachments.at("grip").position.y, 1.0f);
}

TEST(VoxelAssemblyRepositoryTest, SavesAndLoadsAssemblyAssetRoundTrip)
{
    VoxelAssemblyAsset asset{};
    asset.assetId = "Player Base";
    asset.displayName = "Player Base";
    asset.rootPartId = "torso";
    asset.collision.mode = VoxelAssemblyCollisionMode::CustomBounds;
    asset.collision.customBoundsMin = glm::vec3(-0.5f, 0.0f, -0.25f);
    asset.collision.customBoundsMax = glm::vec3(0.5f, 1.75f, 0.25f);

    VoxelAssemblyPartDefinition torso{};
    torso.partId = "torso";
    torso.displayName = "Torso";
    torso.defaultModelAssetId = "player_torso";
    torso.contributesToCollision = true;
    asset.parts.push_back(torso);

    VoxelAssemblyPartDefinition leftHand{};
    leftHand.partId = "left_hand";
    leftHand.displayName = "Left Hand";
    leftHand.defaultModelAssetId = "player_left_hand";
    leftHand.contributesToCollision = false;
    leftHand.slotId = "left_hand";
    leftHand.defaultStateId = "default";
    leftHand.bindingStates.push_back(VoxelAssemblyBindingState{
        .stateId = "default",
        .parentPartId = "torso",
        .parentAttachmentName = "left_hand",
        .localPositionOffset = glm::vec3(1.0f, 2.0f, 3.0f),
        .localRotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .localScale = glm::vec3(1.0f, 1.0f, 1.0f),
        .visible = true
    });
    leftHand.bindingStates.push_back(VoxelAssemblyBindingState{
        .stateId = "hidden",
        .parentPartId = "torso",
        .parentAttachmentName = "left_hand",
        .localPositionOffset = glm::vec3(0.0f),
        .localRotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
        .localScale = glm::vec3(1.0f),
        .visible = false
    });
    asset.parts.push_back(leftHand);

    asset.slots.push_back(VoxelAssemblySlotDefinition{
        .slotId = "left_hand",
        .displayName = "Left Hand",
        .fallbackPartId = "left_hand",
        .required = true
    });

    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_assembly_repo_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelAssemblyRepository repository(documentStore, "assemblies");

    repository.save(asset);
    const std::optional<VoxelAssemblyAsset> loaded = repository.load("Player Base");
    std::filesystem::remove_all(tempRoot);

    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->assetId, "playerbase");
    EXPECT_EQ(loaded->displayName, "Player Base");
    EXPECT_EQ(loaded->rootPartId, "torso");
    EXPECT_EQ(loaded->collision.mode, VoxelAssemblyCollisionMode::CustomBounds);
    EXPECT_FLOAT_EQ(loaded->collision.customBoundsMax.y, 1.75f);
    ASSERT_EQ(loaded->parts.size(), 2u);
    ASSERT_EQ(loaded->slots.size(), 1u);

    const VoxelAssemblyPartDefinition* const loadedTorso = loaded->find_part("torso");
    ASSERT_NE(loadedTorso, nullptr);
    EXPECT_EQ(loadedTorso->defaultModelAssetId, "player_torso");

    const VoxelAssemblyPartDefinition* const loadedLeftHand = loaded->find_part("left_hand");
    ASSERT_NE(loadedLeftHand, nullptr);
    EXPECT_FALSE(loadedLeftHand->contributesToCollision);
    EXPECT_EQ(loadedLeftHand->slotId, "left_hand");
    EXPECT_EQ(loadedLeftHand->defaultStateId, "default");
    ASSERT_EQ(loadedLeftHand->bindingStates.size(), 2u);

    const VoxelAssemblyBindingState* const defaultBinding = loaded->default_binding_state("left_hand");
    ASSERT_NE(defaultBinding, nullptr);
    EXPECT_EQ(defaultBinding->parentPartId, "torso");
    EXPECT_EQ(defaultBinding->parentAttachmentName, "left_hand");
    EXPECT_FLOAT_EQ(defaultBinding->localPositionOffset.x, 1.0f);
    EXPECT_FLOAT_EQ(defaultBinding->localPositionOffset.y, 2.0f);
    EXPECT_FLOAT_EQ(defaultBinding->localPositionOffset.z, 3.0f);

    const VoxelAssemblySlotDefinition* const leftHandSlot = loaded->find_slot("left_hand");
    ASSERT_NE(leftHandSlot, nullptr);
    EXPECT_EQ(leftHandSlot->fallbackPartId, "left_hand");
    EXPECT_TRUE(leftHandSlot->required);
}

TEST(VoxelComponentRenderAdapterTest, ResolvesAssemblyPartsRelativeToParentAttachments)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_assembly_runtime_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository modelRepository(documentStore, "models");
    const VoxelAssemblyRepository assemblyRepository(documentStore, "assemblies");

    VoxelModel torso{};
    torso.assetId = "torso";
    torso.voxelSize = 1.0f;
    torso.pivot = glm::vec3(0.0f);
    torso.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    torso.attachments.push_back(VoxelAttachment{
        .name = "hand",
        .position = glm::vec3(1.0f, 0.0f, 0.0f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });
    modelRepository.save(torso);

    VoxelModel dagger{};
    dagger.assetId = "dagger";
    dagger.voxelSize = 1.0f;
    dagger.pivot = glm::vec3(0.0f);
    dagger.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    modelRepository.save(dagger);

    VoxelAssemblyAsset assembly{};
    assembly.assetId = "player_base";
    assembly.displayName = "Player Base";
    assembly.rootPartId = "torso";
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "torso",
        .displayName = "Torso",
        .defaultModelAssetId = "torso",
        .visibleByDefault = true
    });
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "weapon",
        .displayName = "Weapon",
        .defaultModelAssetId = "dagger",
        .visibleByDefault = true,
        .slotId = "main_weapon",
        .defaultStateId = "equipped",
        .bindingStates = {
            VoxelAssemblyBindingState{
                .stateId = "equipped",
                .parentPartId = "torso",
                .parentAttachmentName = "hand",
                .localPositionOffset = glm::vec3(0.5f, 0.0f, 0.0f),
                .localRotationOffset = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                .localScale = glm::vec3(1.0f),
                .visible = true
            }
        }
    });
    assemblyRepository.save(assembly);

    VoxelAssetManager modelAssetManager(modelRepository);
    VoxelAssemblyAssetManager assemblyAssetManager(assemblyRepository);

    GameObject object(glm::vec3(10.0f, 0.0f, 0.0f));
    auto& component = object.Add<VoxelAssemblyComponent>();
    component.assetId = "player_base";
    component.position = glm::vec3(10.0f, 0.0f, 0.0f);
    component.scale = 2.0f;
    component.visible = true;

    const VoxelComponentRenderBundle bundle =
        build_voxel_component_render_bundle(object, assemblyAssetManager, modelAssetManager);

    std::filesystem::remove_all(tempRoot);

    EXPECT_FALSE(bundle.has_error());
    EXPECT_EQ(bundle.assetId, "player_base");
    ASSERT_EQ(bundle.entries.size(), 2u);

    const auto torsoIt = std::ranges::find_if(bundle.entries, [](const VoxelComponentRenderEntry& entry)
    {
        return entry.stableId == "torso";
    });
    ASSERT_NE(torsoIt, bundle.entries.end());
    EXPECT_FLOAT_EQ(torsoIt->renderInstance.position.x, 10.0f);

    const auto weaponIt = std::ranges::find_if(bundle.entries, [](const VoxelComponentRenderEntry& entry)
    {
        return entry.stableId == "weapon";
    });
    ASSERT_NE(weaponIt, bundle.entries.end());
    EXPECT_FLOAT_EQ(weaponIt->renderInstance.position.x, 13.0f);
    EXPECT_FLOAT_EQ(weaponIt->renderInstance.position.y, 0.0f);
    EXPECT_FLOAT_EQ(weaponIt->renderInstance.position.z, 0.0f);

    std::vector<VoxelRenderInstance> instances{};
    instances.reserve(bundle.entries.size());
    for (const VoxelComponentRenderEntry& entry : bundle.entries)
    {
        instances.push_back(entry.renderInstance);
    }

    const VoxelSpatialBounds aggregateBounds = evaluate_voxel_render_instances_bounds(instances);
    EXPECT_TRUE(aggregateBounds.valid);
    EXPECT_FLOAT_EQ(aggregateBounds.min.x, 10.0f);
    EXPECT_FLOAT_EQ(aggregateBounds.max.x, 15.0f);
}

TEST(VoxelModelComponentAdapterTest, BottomCenterPlacementAlignsVisualBoundsToWorldPosition)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_model_placement_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository modelRepository(documentStore, "models");

    VoxelModel model{};
    model.assetId = "crate";
    model.voxelSize = 1.0f;
    model.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    model.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    modelRepository.save(model);

    VoxelAssetManager assetManager(modelRepository);
    VoxelModelComponent component{};
    component.assetId = "crate";
    component.position = glm::vec3(8.0f, 3.0f, -2.0f);
    component.placementPolicy = VoxelPlacementPolicy::BottomCenter;

    const std::optional<VoxelRenderInstance> renderInstance = build_voxel_render_instance(component, assetManager);
    std::filesystem::remove_all(tempRoot);

    ASSERT_TRUE(renderInstance.has_value());
    const VoxelSpatialBounds worldBounds = evaluate_voxel_render_instance_bounds(renderInstance.value());
    ASSERT_TRUE(worldBounds.valid);
    EXPECT_FLOAT_EQ(worldBounds.min.x, 7.5f);
    EXPECT_FLOAT_EQ(worldBounds.min.y, 3.0f);
    EXPECT_FLOAT_EQ(worldBounds.min.z, -2.5f);
    EXPECT_FLOAT_EQ(worldBounds.max.x, 8.5f);
    EXPECT_FLOAT_EQ(worldBounds.max.y, 4.0f);
    EXPECT_FLOAT_EQ(worldBounds.max.z, -1.5f);
}

TEST(VoxelSpatialBoundsTest, EvaluatesModelBoundsRelativeToPivot)
{
    VoxelModel model{};
    model.voxelSize = 0.5f;
    model.pivot = glm::vec3(0.5f, 0.25f, 0.5f);
    model.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    model.set_voxel(VoxelCoord{1, 0, 0}, VoxelColor{255, 255, 255, 255});

    const VoxelSpatialBounds bounds = evaluate_voxel_model_local_bounds(model);
    ASSERT_TRUE(bounds.valid);
    EXPECT_FLOAT_EQ(bounds.min.x, -0.5f);
    EXPECT_FLOAT_EQ(bounds.min.y, -0.25f);
    EXPECT_FLOAT_EQ(bounds.min.z, -0.5f);
    EXPECT_FLOAT_EQ(bounds.max.x, 0.5f);
    EXPECT_FLOAT_EQ(bounds.max.y, 0.25f);
    EXPECT_FLOAT_EQ(bounds.max.z, 0.0f);
}

TEST(VoxelComponentRenderAdapterTest, AssemblyBottomCenterPlacementAlignsAggregateBoundsToWorldPosition)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_assembly_bottom_center_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository modelRepository(documentStore, "models");
    const VoxelAssemblyRepository assemblyRepository(documentStore, "assemblies");

    VoxelModel torso{};
    torso.assetId = "torso";
    torso.voxelSize = 1.0f;
    torso.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    torso.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    torso.attachments.push_back(VoxelAttachment{
        .name = "top",
        .position = glm::vec3(0.5f, 1.5f, 0.5f),
        .forward = glm::vec3(1.0f, 0.0f, 0.0f),
        .up = glm::vec3(0.0f, 1.0f, 0.0f)
    });
    modelRepository.save(torso);

    VoxelModel hat{};
    hat.assetId = "hat";
    hat.voxelSize = 1.0f;
    hat.pivot = glm::vec3(0.5f, 0.5f, 0.5f);
    hat.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    modelRepository.save(hat);

    VoxelAssemblyAsset assembly{};
    assembly.assetId = "stack";
    assembly.rootPartId = "torso";
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "torso",
        .defaultModelAssetId = "torso",
        .visibleByDefault = true
    });
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "hat",
        .defaultModelAssetId = "hat",
        .visibleByDefault = true,
        .defaultStateId = "default",
        .bindingStates = {
            VoxelAssemblyBindingState{
                .stateId = "default",
                .parentPartId = "torso",
                .parentAttachmentName = "top",
                .localScale = glm::vec3(1.0f),
                .visible = true
            }
        }
    });
    assemblyRepository.save(assembly);

    VoxelAssetManager modelAssetManager(modelRepository);
    VoxelAssemblyAssetManager assemblyAssetManager(assemblyRepository);

    GameObject object(glm::vec3(0.0f));
    auto& component = object.Add<VoxelAssemblyComponent>();
    component.assetId = "stack";
    component.position = glm::vec3(4.0f, 2.0f, 6.0f);
    component.placementPolicy = VoxelPlacementPolicy::BottomCenter;
    component.visible = true;

    const VoxelComponentRenderBundle bundle =
        build_voxel_component_render_bundle(object, assemblyAssetManager, modelAssetManager);
    std::filesystem::remove_all(tempRoot);

    ASSERT_FALSE(bundle.has_error());
    std::vector<VoxelRenderInstance> instances{};
    for (const VoxelComponentRenderEntry& entry : bundle.entries)
    {
        instances.push_back(entry.renderInstance);
    }

    const VoxelSpatialBounds aggregateBounds = evaluate_voxel_render_instances_bounds(instances);
    ASSERT_TRUE(aggregateBounds.valid);
    EXPECT_FLOAT_EQ(aggregateBounds.min.y, 2.0f);
}

TEST(VoxelSpatialColliderTest, TaggedAssemblyCollisionExcludesNonContributingParts)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_assembly_collision_test";
    std::filesystem::remove_all(tempRoot);
    const TestJsonDocumentStore documentStore(tempRoot);
    const VoxelModelRepository modelRepository(documentStore, "models");
    const VoxelAssemblyRepository assemblyRepository(documentStore, "assemblies");

    VoxelModel torso{};
    torso.assetId = "torso";
    torso.voxelSize = 1.0f;
    torso.pivot = glm::vec3(0.0f);
    torso.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    modelRepository.save(torso);

    VoxelModel sword{};
    sword.assetId = "sword";
    sword.voxelSize = 1.0f;
    sword.pivot = glm::vec3(0.0f);
    sword.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 255, 255, 255});
    modelRepository.save(sword);

    VoxelAssemblyAsset assembly{};
    assembly.assetId = "fighter";
    assembly.rootPartId = "torso";
    assembly.collision.mode = VoxelAssemblyCollisionMode::TaggedParts;
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "torso",
        .defaultModelAssetId = "torso",
        .visibleByDefault = true,
        .contributesToCollision = true
    });
    assembly.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "weapon",
        .defaultModelAssetId = "sword",
        .visibleByDefault = true,
        .contributesToCollision = false,
        .defaultStateId = "equipped",
        .bindingStates = {
            VoxelAssemblyBindingState{
                .stateId = "equipped",
                .parentPartId = "torso",
                .localPositionOffset = glm::vec3(4.0f, 0.0f, 0.0f),
                .localScale = glm::vec3(1.0f),
                .visible = true
            }
        }
    });
    assemblyRepository.save(assembly);

    VoxelAssetManager modelAssetManager(modelRepository);
    VoxelAssemblyAssetManager assemblyAssetManager(assemblyRepository);

    VoxelAssemblyComponent component{};
    component.assetId = "fighter";
    component.visible = true;

    const VoxelSpatialColliderEvaluation evaluation =
        evaluate_voxel_assembly_local_collider(component, assemblyAssetManager, modelAssetManager);

    std::filesystem::remove_all(tempRoot);

    ASSERT_TRUE(evaluation.valid);
    EXPECT_FLOAT_EQ(evaluation.localBounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(evaluation.localBounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(evaluation.localBounds.max.y, 1.0f);
}

TEST(VoxelModelRepositoryTest, ListsSavedVoxelAssets)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_model_list_test";
    std::filesystem::remove_all(tempRoot);
    const config::JsonFileDocumentStore documentStore;
    const VoxelModelRepository repository(documentStore, tempRoot / "assets");

    VoxelModel alpha{};
    alpha.assetId = "Alpha";
    alpha.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(alpha);

    VoxelModel beta{};
    beta.assetId = "beta";
    beta.set_voxel(VoxelCoord{ 1, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    repository.save(beta);

    const std::vector<std::string> assetIds = repository.list_asset_ids();
    std::filesystem::remove_all(tempRoot);

    ASSERT_EQ(assetIds.size(), 2u);
    EXPECT_EQ(assetIds[0], "alpha");
    EXPECT_EQ(assetIds[1], "beta");
}

TEST(VoxelAssemblyRepositoryTest, ListsSavedAssemblyAssets)
{
    const std::filesystem::path tempRoot = std::filesystem::temp_directory_path() / "voxel_enginevk_voxel_assembly_list_test";
    std::filesystem::remove_all(tempRoot);
    const config::JsonFileDocumentStore documentStore;
    const VoxelAssemblyRepository repository(documentStore, tempRoot / "assemblies");

    VoxelAssemblyAsset alpha{};
    alpha.assetId = "Alpha";
    alpha.rootPartId = "root";
    alpha.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "root",
        .displayName = "Root",
        .defaultModelAssetId = "alpha_root"
    });
    repository.save(alpha);

    VoxelAssemblyAsset beta{};
    beta.assetId = "beta";
    beta.rootPartId = "root";
    beta.parts.push_back(VoxelAssemblyPartDefinition{
        .partId = "root",
        .displayName = "Root",
        .defaultModelAssetId = "beta_root"
    });
    repository.save(beta);

    const std::vector<std::string> assetIds = repository.list_asset_ids();
    std::filesystem::remove_all(tempRoot);

    ASSERT_EQ(assetIds.size(), 2u);
    EXPECT_EQ(assetIds[0], "alpha");
    EXPECT_EQ(assetIds[1], "beta");
}
