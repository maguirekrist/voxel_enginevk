#include "voxel_mesher.h"

#include "game/block.h"

std::shared_ptr<Mesh> VoxelMesher::generate_mesh(const VoxelModel& model)
{
    auto mesh = std::make_shared<Mesh>();

    for (const auto& [coord, color] : model.voxels())
    {
        for (const auto face : faceDirections)
        {
            const VoxelCoord neighbor{
                .x = coord.x + faceOffsetX[face],
                .y = coord.y + faceOffsetY[face],
                .z = coord.z + faceOffsetZ[face]
            };
            if (model.contains(neighbor))
            {
                continue;
            }

            const uint32_t baseIndex = static_cast<uint32_t>(mesh->_vertices.size());
            const glm::vec3 basePos = glm::vec3(
                static_cast<float>(coord.x),
                static_cast<float>(coord.y),
                static_cast<float>(coord.z)) * model.voxelSize;
            const glm::vec3 normal = faceNormals[face];
            const glm::vec3 vertexColor = color.to_vec3();

            for (int vertexIndex = 0; vertexIndex < 4; ++vertexIndex)
            {
                const glm::vec3 faceVertex = glm::vec3(faceVertices[face][vertexIndex]) * model.voxelSize;
                const glm::vec3 position = (basePos + faceVertex) - model.pivot;
                mesh->_vertices.push_back(Vertex{
                    position,
                    normal,
                    vertexColor,
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

    return mesh;
}
