//
// Created by Maguire Krist on 8/9/25.
//

#include "player_input_component.h"

#include "glm/detail/func_geometric.inl"
#include "glm/detail/func_trigonometric.inl"

PlayerInputComponent::PlayerInputComponent(const std::function<bool(glm::vec3)>& check_collision) : _check_collision(check_collision)
{
}

void PlayerInputComponent::move_forward(GameObject& object)
{
    const auto next_pos = object._position + (object._front * object._moveSpeed);
    if (!_check_collision(next_pos))
        object._position = next_pos;
}

void PlayerInputComponent::move_backward(GameObject& object)
{
    const auto next_pos = object._position - (object._front * object._moveSpeed);
    if (!_check_collision(next_pos))
        object._position = next_pos;
}

void PlayerInputComponent::move_left(GameObject& object)
{
    const auto next_pos = object._position - (glm::normalize(glm::cross(object._front, object._up)) * object._moveSpeed);
    if (!_check_collision(next_pos))
        object._position = next_pos;
}

void PlayerInputComponent::move_right(GameObject& object)
{
    const auto next_pos = object._position + (glm::normalize(glm::cross(object._front, object._up)) * object._moveSpeed);
    if (!_check_collision(next_pos))
        object._position = next_pos;
}

void PlayerInputComponent::handle_mouse_move(GameObject& object, float xChange, float yChange)
{
    float sensitivity = 0.1f;
    xChange *= sensitivity;
    yChange *= -sensitivity;

    object._yaw += xChange;
    object._pitch += yChange;

    if (object._pitch > 89.0f)
        object._pitch = 89.0f;
    if (object._pitch < -89.0f)
        object._pitch = -89.0f;

    glm::vec3 direction;
    direction.x = cos(glm::radians(object._yaw)) * cos(glm::radians(object._pitch));
    direction.y = sin(glm::radians(object._pitch));
    direction.z = sin(glm::radians(object._yaw)) * cos(glm::radians(object._pitch));

    object._front = glm::normalize(direction);
}
