#include "camera.h"

Camera::Camera(const glm::vec3& position) :
    GameObject(position),
    _view(glm::mat4(1.0f))
{
    _projection[1][1] *= -1;
}

void Camera::update(const float dt)
{
    _view = glm::lookAt(_position, _position + _front, _up);
    GameObject::update(dt);
}


std::optional<RaycastResult> Camera::get_target_block(World& world, GameObject& player)
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
        auto current_chunk = world.get_chunk(voxelPos);

        if (current_chunk == nullptr) return std::nullopt;

        if(distance == 0.0f)
            std::println("Voxel Position: x:{}, y:{}, z:{}", voxelPos.x, voxelPos.y, voxelPos.z);

        const auto localPos = World::get_local_coordinates(voxelPos);
        auto block = current_chunk->_blocks[localPos.x][localPos.y][localPos.z];


        if (block._solid)
        {
            auto worldPos = current_chunk->get_world_pos(localPos);
            auto faceDir = get_face_direction(faceNormal);

            //TODO: re-add chunk to raycast result.
            return RaycastResult{ block, faceDir.value_or(FaceDirection::FRONT_FACE), worldPos, distance };
        }

        // Advance to next voxel
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            voxelPos.x += static_cast<int>(stepSize.x);
            distance = tMax.x;
            tMax.x += tDelta.x;
            faceNormal = glm::ivec3(-stepSize.x, 0, 0);
        }
        else if (tMax.y < tMax.z) {
            voxelPos.y += static_cast<int>(stepSize.y);
            distance = tMax.y;
            tMax.y += tDelta.y;
            faceNormal = glm::ivec3(0, -stepSize.y, 0);
        }
        else {
            voxelPos.z += static_cast<int>(stepSize.z);
            distance = tMax.z;
            tMax.z += tDelta.z;
            faceNormal = glm::ivec3(0, 0, -stepSize.z);
        }
    }

    // No voxel hit within maxDistance
    return std::nullopt;
}
