//
// Created by Maguire Krist on 8/23/25.
//

#include "mesh.h"

#include "constants.h"
#include "game/block.h"

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

std::shared_ptr<Mesh> Mesh::create_block_outline_mesh(const glm::vec3& blockMinCorner)
{
    auto debugMesh = std::make_shared<Mesh>();
    constexpr float epsilon = 0.0025f;
    const glm::vec3 color = glm::vec3(1.0f);

    const auto add_line = [&debugMesh, &color](const glm::vec3& start, const glm::vec3& end)
    {
        const uint32_t baseIndex = static_cast<uint32_t>(debugMesh->_vertices.size());
        debugMesh->_vertices.push_back(Vertex{ start, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_vertices.push_back(Vertex{ end, glm::vec3(0.0f), color, glm::vec2(1.0f, 1.0f), glm::vec3(0.0f) });
        debugMesh->_indices.push_back(baseIndex);
        debugMesh->_indices.push_back(baseIndex + 1);
    };

    const glm::vec3 p000 = blockMinCorner + glm::vec3{-epsilon, -epsilon, -epsilon};
    const glm::vec3 p100 = blockMinCorner + glm::vec3{1.0f + epsilon, -epsilon, -epsilon};
    const glm::vec3 p010 = blockMinCorner + glm::vec3{-epsilon, 1.0f + epsilon, -epsilon};
    const glm::vec3 p110 = blockMinCorner + glm::vec3{1.0f + epsilon, 1.0f + epsilon, -epsilon};
    const glm::vec3 p001 = blockMinCorner + glm::vec3{-epsilon, -epsilon, 1.0f + epsilon};
    const glm::vec3 p101 = blockMinCorner + glm::vec3{1.0f + epsilon, -epsilon, 1.0f + epsilon};
    const glm::vec3 p011 = blockMinCorner + glm::vec3{-epsilon, 1.0f + epsilon, 1.0f + epsilon};
    const glm::vec3 p111 = blockMinCorner + glm::vec3{1.0f + epsilon, 1.0f + epsilon, 1.0f + epsilon};

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
