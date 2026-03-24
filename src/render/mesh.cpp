//
// Created by Maguire Krist on 8/23/25.
//

#include "mesh.h"

#include <algorithm>

#include "constants.h"
#include "game/block.h"

namespace
{
    void add_box_mesh(
        const std::shared_ptr<Mesh>& mesh,
        const glm::vec3& minCorner,
        const glm::vec3& maxCorner,
        const glm::vec3& color)
    {
        const glm::vec3 corners[8] = {
            { minCorner.x, minCorner.y, minCorner.z },
            { maxCorner.x, minCorner.y, minCorner.z },
            { maxCorner.x, maxCorner.y, minCorner.z },
            { minCorner.x, maxCorner.y, minCorner.z },
            { minCorner.x, minCorner.y, maxCorner.z },
            { maxCorner.x, minCorner.y, maxCorner.z },
            { maxCorner.x, maxCorner.y, maxCorner.z },
            { minCorner.x, maxCorner.y, maxCorner.z }
        };

        const int faceCornerIndices[6][4] = {
            { 4, 5, 6, 7 },
            { 1, 0, 3, 2 },
            { 0, 4, 7, 3 },
            { 5, 1, 2, 6 },
            { 3, 7, 6, 2 },
            { 0, 1, 5, 4 }
        };

        for (int face = 0; face < 6; ++face)
        {
            const uint32_t baseIndex = static_cast<uint32_t>(mesh->_vertices.size());
            for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
            {
                mesh->_vertices.push_back(Vertex{
                    corners[faceCornerIndices[face][vertexIndex]],
                    faceNormals[face],
                    color,
                    glm::vec2(1.0f, 1.0f),
                    glm::vec3(0.0f)
                });
            }

            mesh->_indices.push_back(baseIndex + 0);
            mesh->_indices.push_back(baseIndex + 1);
            mesh->_indices.push_back(baseIndex + 2);
            mesh->_indices.push_back(baseIndex + 2);
            mesh->_indices.push_back(baseIndex + 3);
            mesh->_indices.push_back(baseIndex + 0);
        }
    }

    void add_beam(
        const std::shared_ptr<Mesh>& mesh,
        const glm::vec3& start,
        const glm::vec3& end,
        const float thickness,
        const glm::vec3& color)
    {
        const glm::vec3 minCorner = glm::min(start, end) - glm::vec3(thickness * 0.5f);
        const glm::vec3 maxCorner = glm::max(start, end) + glm::vec3(thickness * 0.5f);
        add_box_mesh(mesh, minCorner, maxCorner, color);
    }
}

std::shared_ptr<Mesh> Mesh::create_cube_mesh()
{
    auto cubeMesh = std::make_shared<Mesh>();

    for (auto face : faceDirections)
    {
        //4 vertices per face
        for (int i = 0; i < 4; i++)
        {
            cubeMesh->_vertices.push_back({ faceVertices[face][i], glm::vec3(0.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        }
    }

    // Define the indices for the cube (two triangles per face)
    cubeMesh->_indices = {
        0, 1, 2, 2, 3, 0,   // Front face
        4, 5, 6, 6, 7, 4,   // Back face
        8, 9, 10, 10, 11, 8, // Left face
        12, 13, 14, 14, 15, 12, // Right face
        16, 17, 18, 18, 19, 16, // Top face
        20, 21, 22, 22, 23, 20  // Bottom face
    };

    return cubeMesh;
}

std::shared_ptr<Mesh> Mesh::create_quad_mesh()
{
    auto quadMesh = std::make_shared<Mesh>();

    // Define the four vertices of the quad in normalized coordinates
    // You can adjust the position values to scale the quad if needed.
    quadMesh->_vertices = {
        { glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) }, // Bottom-left
        { glm::vec3(0.5f, -0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) },  // Bottom-right
        { glm::vec3(0.5f, 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) },   // Top-right
        { glm::vec3(-0.5f, 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) }   // Top-left
    };

    // Define the indices for the quad (two triangles)
    quadMesh->_indices = {
        0, 1, 2, // First triangle
        2, 3, 0  // Second triangle
    };

    return quadMesh;
}

std::shared_ptr<Mesh> Mesh::create_chunk_boundary_mesh()
{
    auto debugMesh = std::make_shared<Mesh>();
    const glm::vec3 boundaryColor = glm::vec3(0.98f, 0.15f, 0.15f);
    const glm::vec3 axisColor = glm::vec3(0.1f, 0.95f, 0.95f);

    const auto add_line = [&debugMesh](const glm::vec3& start, const glm::vec3& end, const glm::vec3& color)
    {
        const uint32_t baseIndex = static_cast<uint32_t>(debugMesh->_vertices.size());
        debugMesh->_vertices.push_back(Vertex{ start, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_vertices.push_back(Vertex{ end, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_indices.push_back(baseIndex);
        debugMesh->_indices.push_back(baseIndex + 1);
    };

    const float maxX = static_cast<float>(CHUNK_SIZE);
    const float maxY = static_cast<float>(CHUNK_HEIGHT);
    const float maxZ = static_cast<float>(CHUNK_SIZE);

    const glm::vec3 p000{0.0f, 0.0f, 0.0f};
    const glm::vec3 p100{maxX, 0.0f, 0.0f};
    const glm::vec3 p010{0.0f, maxY, 0.0f};
    const glm::vec3 p110{maxX, maxY, 0.0f};
    const glm::vec3 p001{0.0f, 0.0f, maxZ};
    const glm::vec3 p101{maxX, 0.0f, maxZ};
    const glm::vec3 p011{0.0f, maxY, maxZ};
    const glm::vec3 p111{maxX, maxY, maxZ};

    add_line(p000, p100, axisColor);
    add_line(p001, p101, boundaryColor);
    add_line(p010, p110, boundaryColor);
    add_line(p011, p111, boundaryColor);

    add_line(p000, p001, axisColor);
    add_line(p100, p101, boundaryColor);
    add_line(p010, p011, boundaryColor);
    add_line(p110, p111, boundaryColor);

    add_line(p000, p010, boundaryColor);
    add_line(p100, p110, boundaryColor);
    add_line(p001, p011, boundaryColor);
    add_line(p101, p111, boundaryColor);

    return debugMesh;
}

std::shared_ptr<Mesh> Mesh::create_block_indicator_mesh(const glm::vec3& blockMinCorner, const float blockSize)
{
    auto debugMesh = std::make_shared<Mesh>();
    const float thickness = std::max(blockSize * 0.24f, 0.03f);
    const glm::vec3 color{1.0f, 0.95f, 0.25f};

    const glm::vec3 p000 = blockMinCorner;
    const glm::vec3 p100 = blockMinCorner + glm::vec3(blockSize, 0.0f, 0.0f);
    const glm::vec3 p010 = blockMinCorner + glm::vec3(0.0f, blockSize, 0.0f);
    const glm::vec3 p110 = blockMinCorner + glm::vec3(blockSize, blockSize, 0.0f);
    const glm::vec3 p001 = blockMinCorner + glm::vec3(0.0f, 0.0f, blockSize);
    const glm::vec3 p101 = blockMinCorner + glm::vec3(blockSize, 0.0f, blockSize);
    const glm::vec3 p011 = blockMinCorner + glm::vec3(0.0f, blockSize, blockSize);
    const glm::vec3 p111 = blockMinCorner + glm::vec3(blockSize, blockSize, blockSize);

    add_beam(debugMesh, p000, p100, thickness, color);
    add_beam(debugMesh, p001, p101, thickness, color);
    add_beam(debugMesh, p010, p110, thickness, color);
    add_beam(debugMesh, p011, p111, thickness, color);

    add_beam(debugMesh, p000, p001, thickness, color);
    add_beam(debugMesh, p100, p101, thickness, color);
    add_beam(debugMesh, p010, p011, thickness, color);
    add_beam(debugMesh, p110, p111, thickness, color);

    add_beam(debugMesh, p000, p010, thickness, color);
    add_beam(debugMesh, p100, p110, thickness, color);
    add_beam(debugMesh, p001, p011, thickness, color);
    add_beam(debugMesh, p101, p111, thickness, color);

    return debugMesh;
}

std::shared_ptr<Mesh> Mesh::create_point_marker_mesh(const glm::vec3& center, const float size, const glm::vec3& color)
{
    auto markerMesh = std::make_shared<Mesh>();
    const float clampedSize = std::max(size, 0.005f);
    const float coreHalfExtent = clampedSize * 0.28f;
    const float axisLength = clampedSize * 0.9f;
    const float axisThickness = std::max(clampedSize * 0.12f, 0.01f);

    add_box_mesh(
        markerMesh,
        center - glm::vec3(coreHalfExtent),
        center + glm::vec3(coreHalfExtent),
        color);
    add_beam(
        markerMesh,
        center - glm::vec3(axisLength, 0.0f, 0.0f),
        center + glm::vec3(axisLength, 0.0f, 0.0f),
        axisThickness,
        color);
    add_beam(
        markerMesh,
        center - glm::vec3(0.0f, axisLength, 0.0f),
        center + glm::vec3(0.0f, axisLength, 0.0f),
        axisThickness,
        color);
    add_beam(
        markerMesh,
        center - glm::vec3(0.0f, 0.0f, axisLength),
        center + glm::vec3(0.0f, 0.0f, axisLength),
        axisThickness,
        color);
    return markerMesh;
}

std::shared_ptr<Mesh> Mesh::create_box_preview_mesh(const glm::vec3& minCorner, const glm::vec3& maxCorner, const glm::vec3& color)
{
    auto previewMesh = std::make_shared<Mesh>();
    add_box_mesh(previewMesh, minCorner, maxCorner, color);
    return previewMesh;
}

std::shared_ptr<Mesh> Mesh::create_block_preview_mesh(const glm::vec3& blockMinCorner, const float blockSize)
{
    const float inset = std::max(blockSize * 0.08f, 0.004f);
    const glm::vec3 minCorner = blockMinCorner + glm::vec3(inset);
    const glm::vec3 maxCorner = blockMinCorner + glm::vec3(blockSize - inset);
    return create_box_preview_mesh(minCorner, maxCorner, glm::vec3(1.0f, 0.93f, 0.30f));
}

std::shared_ptr<Mesh> Mesh::create_block_outline_mesh(const glm::vec3& blockMinCorner)
{
    return create_block_outline_mesh(blockMinCorner, 1.0f);
}

std::shared_ptr<Mesh> Mesh::create_box_outline_mesh(const glm::vec3& minCorner, const glm::vec3& maxCorner, const glm::vec3& color)
{
    auto debugMesh = std::make_shared<Mesh>();

    const auto add_line = [&debugMesh, &color](const glm::vec3& start, const glm::vec3& end)
    {
        const uint32_t baseIndex = static_cast<uint32_t>(debugMesh->_vertices.size());
        debugMesh->_vertices.push_back(Vertex{ start, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_vertices.push_back(Vertex{ end, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_indices.push_back(baseIndex);
        debugMesh->_indices.push_back(baseIndex + 1);
    };

    const glm::vec3 p000{minCorner.x, minCorner.y, minCorner.z};
    const glm::vec3 p100{maxCorner.x, minCorner.y, minCorner.z};
    const glm::vec3 p010{minCorner.x, maxCorner.y, minCorner.z};
    const glm::vec3 p110{maxCorner.x, maxCorner.y, minCorner.z};
    const glm::vec3 p001{minCorner.x, minCorner.y, maxCorner.z};
    const glm::vec3 p101{maxCorner.x, minCorner.y, maxCorner.z};
    const glm::vec3 p011{minCorner.x, maxCorner.y, maxCorner.z};
    const glm::vec3 p111{maxCorner.x, maxCorner.y, maxCorner.z};

    add_line(p000, p100);
    add_line(p001, p101);
    add_line(p010, p110);
    add_line(p011, p111);

    add_line(p000, p001);
    add_line(p100, p101);
    add_line(p010, p011);
    add_line(p110, p111);

    add_line(p000, p010);
    add_line(p100, p110);
    add_line(p001, p011);
    add_line(p101, p111);

    return debugMesh;
}

std::shared_ptr<Mesh> Mesh::create_block_outline_mesh(const glm::vec3& blockMinCorner, const float blockSize)
{
    const float epsilon = std::max(0.0005f, blockSize * 0.025f);
    const glm::vec3 p000 = blockMinCorner + glm::vec3{-epsilon, -epsilon, -epsilon};
    const glm::vec3 p100 = blockMinCorner + glm::vec3{blockSize + epsilon, -epsilon, -epsilon};
    const glm::vec3 p010 = blockMinCorner + glm::vec3{-epsilon, blockSize + epsilon, -epsilon};
    const glm::vec3 p110 = blockMinCorner + glm::vec3{blockSize + epsilon, blockSize + epsilon, -epsilon};
    const glm::vec3 p001 = blockMinCorner + glm::vec3{-epsilon, -epsilon, blockSize + epsilon};
    const glm::vec3 p101 = blockMinCorner + glm::vec3{blockSize + epsilon, -epsilon, blockSize + epsilon};
    const glm::vec3 p011 = blockMinCorner + glm::vec3{-epsilon, blockSize + epsilon, blockSize + epsilon};
    const glm::vec3 p111 = blockMinCorner + glm::vec3{blockSize + epsilon, blockSize + epsilon, blockSize + epsilon};
    return create_box_outline_mesh(p000, p111, glm::vec3(1.0f));
}
