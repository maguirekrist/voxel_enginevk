#include <memory>

#include <gtest/gtest.h>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

#include "editing/document_command_history.h"
#include "render/render_primitives.h"
#include "voxel/voxel_mesher.h"
#include "voxel/voxel_picking.h"
#include "voxel/voxel_model_repository.h"

TEST(VoxelMesherTest, GeneratesOnlyExteriorFacesForAdjacentVoxels)
{
    VoxelModel model{};
    model.voxelSize = 1.0f / 16.0f;
    model.set_voxel(VoxelCoord{ 0, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });
    model.set_voxel(VoxelCoord{ 1, 0, 0 }, VoxelColor{ 255, 255, 255, 255 });

    const std::shared_ptr<Mesh> mesh = VoxelMesher::generate_mesh(model);
    ASSERT_NE(mesh, nullptr);

    constexpr uint32_t expectedVisibleFaces = 10;
    EXPECT_EQ(mesh->_vertices.size(), expectedVisibleFaces * 4);
    EXPECT_EQ(mesh->_indices.size(), expectedVisibleFaces * 6);
}

TEST(VoxelPickingTest, FaceFromOutwardNormalMatchesExpectedPlacementFace)
{
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(1, 0, 0)), LEFT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(-1, 0, 0)), RIGHT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 1, 0)), TOP_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, -1, 0)), BOTTOM_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 0, 1)), FRONT_FACE);
    EXPECT_EQ(voxel::picking::face_from_outward_normal(glm::ivec3(0, 0, -1)), BACK_FACE);
}

TEST(VoxelPickingTest, BuildRayFromCursorPointsForwardAtViewportCenter)
{
    const glm::vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    const glm::vec3 cameraFront = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), 800.0f / 600.0f, 0.1f, 10000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);

    const voxel::picking::Ray ray = voxel::picking::build_ray_from_cursor(
        399,
        299,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    EXPECT_NEAR(ray.direction.x, cameraFront.x, 0.02f);
    EXPECT_NEAR(ray.direction.y, cameraFront.y, 0.02f);
    EXPECT_NEAR(ray.direction.z, cameraFront.z, 0.02f);
}

TEST(VoxelPickingTest, BuildRayFromCursorTopOfViewportPointsUpward)
{
    const glm::vec3 cameraPosition{0.0f, 0.0f, 0.0f};
    const glm::vec3 cameraFront = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 cameraUp{0.0f, 1.0f, 0.0f};
    glm::mat4 projection = glm::perspective(glm::radians(70.0f), 800.0f / 600.0f, 0.1f, 10000.0f);
    projection[1][1] *= -1.0f;
    const glm::mat4 view = glm::lookAt(cameraPosition, cameraPosition + cameraFront, cameraUp);

    const voxel::picking::Ray topRay = voxel::picking::build_ray_from_cursor(
        399,
        0,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    const voxel::picking::Ray bottomRay = voxel::picking::build_ray_from_cursor(
        399,
        599,
        VkExtent2D{ 800, 600 },
        cameraPosition,
        glm::inverse(projection * view));

    EXPECT_GT(topRay.direction.y, 0.0f);
    EXPECT_LT(bottomRay.direction.y, 0.0f);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectOutwardNormal)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-2.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(-1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), RIGHT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectOutwardNormalForNegativeDirection)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(3.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(-1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), LEFT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsCorrectFrontFaceNormalOnZAxis)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, -2.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(0, 0, -1));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), BACK_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxMissesWhenParallelOutsideSlab)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(2.0f, 0.5f, -1.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    EXPECT_FALSE(hit.has_value());
}

TEST(VoxelPickingTest, IntersectRayBoxHitsWhenParallelInsideOtherSlabs)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, -2.0f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 2.0f, 0.0001f);
}

TEST(VoxelPickingTest, IntersectRayBoxRespectsMaximumDistance)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-2.0f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 0.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        1.5f);

    EXPECT_FALSE(hit.has_value());
}

TEST(VoxelPickingTest, IntersectRayBoxUsesDeterministicNormalForEdgeHits)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(-1.0f, -1.0f, 0.5f),
        .direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(-1, 0, 0));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), RIGHT_FACE);
}

TEST(VoxelPickingTest, IntersectRayBoxReturnsExitFaceWhenRayStartsInside)
{
    const voxel::picking::Ray ray{
        .origin = glm::vec3(0.5f, 0.5f, 0.5f),
        .direction = glm::normalize(glm::vec3(0.0f, 0.0f, 1.0f))
    };

    const auto hit = voxel::picking::intersect_ray_box(
        ray,
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        10.0f);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->distance, 0.5f, 0.0001f);
    EXPECT_EQ(hit->outwardNormal, glm::ivec3(0, 0, 1));
    EXPECT_EQ(voxel::picking::face_from_outward_normal(hit->outwardNormal), FRONT_FACE);
}

TEST(DocumentCommandHistoryTest, AppliesUndoAndRedoForSnapshotEdits)
{
    editing::DocumentCommandHistory<VoxelModel> history{5};
    VoxelModel model{};
    model.assetId = "test_model";
    model.set_voxel(VoxelCoord{0, 0, 0}, VoxelColor{255, 0, 0, 255});

    EXPECT_TRUE(editing::apply_snapshot_edit(history, model, "rename asset", [](VoxelModel& edited)
    {
        edited.assetId = "renamed_model";
    }));
    EXPECT_EQ(model.assetId, "renamed_model");
    EXPECT_TRUE(history.can_undo());
    EXPECT_FALSE(history.can_redo());

    ASSERT_TRUE(history.undo(model));
    EXPECT_EQ(model.assetId, "test_model");
    EXPECT_FALSE(history.can_undo());
    EXPECT_TRUE(history.can_redo());

    ASSERT_TRUE(history.redo(model));
    EXPECT_EQ(model.assetId, "renamed_model");
    EXPECT_TRUE(history.can_undo());
}

TEST(DocumentCommandHistoryTest, RespectsConfiguredHistoryCapacity)
{
    editing::DocumentCommandHistory<VoxelModel> history{2};
    VoxelModel model{};
    model.assetId = "state_0";

    EXPECT_TRUE(editing::apply_snapshot_edit(history, model, "state_1", [](VoxelModel& edited)
    {
        edited.assetId = "state_1";
    }));
    EXPECT_TRUE(editing::apply_snapshot_edit(history, model, "state_2", [](VoxelModel& edited)
    {
        edited.assetId = "state_2";
    }));
    EXPECT_TRUE(editing::apply_snapshot_edit(history, model, "state_3", [](VoxelModel& edited)
    {
        edited.assetId = "state_3";
    }));

    EXPECT_EQ(history.undo_count(), 2u);
    ASSERT_TRUE(history.undo(model));
    EXPECT_EQ(model.assetId, "state_2");
    ASSERT_TRUE(history.undo(model));
    EXPECT_EQ(model.assetId, "state_1");
    EXPECT_FALSE(history.undo(model));
}
