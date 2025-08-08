#include "camera.h"

Camera::Camera() : _up(glm::vec3(0.0f, 1.0f, 0.0f)), _view(glm::mat4(1.0f)), _front(glm::vec3(1.0f, 0.0f, 0.0f)), _position(glm::vec3(0.0f, 120.0f, 0.0f))
{
    _projection[1][1] *= -1;
}

void Camera::handle_mouse_move(float xChange, float yChange)
{
    float sensitivity = 0.1f;
    xChange *= sensitivity;
    yChange *= -sensitivity;

    _yaw += xChange;
    _pitch += yChange;

    if (_pitch > 89.0f)
        _pitch = 89.0f;
    if (_pitch < -89.0f)
        _pitch = -89.0f;

    glm::vec3 direction;
    direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    direction.y = sin(glm::radians(_pitch));
    direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));

    _front = glm::normalize(direction);
}

void Camera::update_view()
{
    _view = glm::lookAt(_position, _position + _front, _up);
}

void Camera::move_forward()
{
    _position += _front * _moveSpeed;
}

void Camera::move_backward()
{
    _position -= _front * _moveSpeed;
}

void Camera::move_left()
{
    _position -= glm::normalize(glm::cross(_front, _up)) * _moveSpeed;
}

void Camera::move_right()
{
    _position += glm::normalize(glm::cross(_front, _up)) * _moveSpeed;
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
        auto current_chunk = world.get_chunk(voxelPos);

        if (current_chunk.expired()) return std::nullopt;

        if(distance == 0.0f)
            std::println("Voxel Position: x:{}, y:{}, z:{}", voxelPos.x, voxelPos.y, voxelPos.z);

        const auto localPos = World::get_local_coordinates(voxelPos);
        auto block = current_chunk.lock()->_blocks[localPos.x][localPos.y][localPos.z];


        if (block._solid)
        {
            auto worldPos = current_chunk.lock()->get_world_pos(localPos);
            auto faceDir = get_face_direction(faceNormal);

            //TODO: re-add chunk to raycast result.
            return RaycastResult{ block, faceDir.value_or(FaceDirection::FRONT_FACE), worldPos, distance };
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
