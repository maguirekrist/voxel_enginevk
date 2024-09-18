#include "camera.h"

Camera::Camera()
{
    _view = glm::mat4(1.0f);
    _projection[1][1] *= -1;
}

void Camera::update_view(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up)
{
    _view = glm::lookAt(position, position + front, up);
}

std::optional<RaycastResult> Camera::get_target_block(World& world, Player& player)
{
    glm::vec3 rayStart = player._position;
    glm::vec3 rayDir = glm::normalize(player._front);

    glm::vec3 stepSize = glm::sign(rayDir);
    glm::vec3 tDelta = glm::abs(1.0f / rayDir);

    glm::ivec3 voxelPos = glm::floor(rayStart);
    // Calculate initial tMax values
    glm::vec3 tMax = (stepSize * (glm::vec3(voxelPos) - rayStart) + (stepSize * 0.5f) + 0.5f) * tDelta;

    // Initialize the face normal
    glm::ivec3 faceNormal(0);
    float distance = 0.0f;

    while (distance < CHUNK_SIZE)
    {
        //Get the current chunk we're in
        //voxel pos is worldPos
        Chunk* current_chunk = world.get_chunk(voxelPos);
        if(distance == 0.0f)
            fmt::println("Voxel Position: x:{}, y:{}, z:{}", voxelPos.x, voxelPos.y, voxelPos.z);
        if (current_chunk)
        {
            fmt::println("Current chunk: x:{}, y: {}", current_chunk->_position.x, current_chunk->_position.y);
            auto localPos = World::get_local_coordinates(voxelPos);
            auto block = current_chunk->get_block(localPos);
            if (block && block->_solid) {
                
                auto worldPos = current_chunk->get_world_pos(localPos);
                auto faceDir = get_face_direction(faceNormal);
                return RaycastResult{ block, faceDir.has_value() ? faceDir.value() : FaceDirection::FRONT_FACE, current_chunk, worldPos, distance };
            }
        }

        // Advance to next voxel
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            voxelPos.x += stepSize.x;
            distance = tMax.x;
            tMax.x += tDelta.x;
            faceNormal = glm::ivec3(-stepSize.x, 0, 0);
        }
        else if (tMax.y < tMax.z) {
            voxelPos.y += stepSize.y;
            distance = tMax.y;
            tMax.y += tDelta.y;
            faceNormal = glm::ivec3(0, -stepSize.y, 0);
        }
        else {
            voxelPos.z += stepSize.z;
            distance = tMax.z;
            tMax.z += tDelta.z;
            faceNormal = glm::ivec3(0, 0, -stepSize.z);
        }
    }

    // No voxel hit within maxDistance
    return std::nullopt;
}
