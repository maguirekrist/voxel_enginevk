#include "player.h"

// Player::Player(): _lastMouseX(0), _lastMouseY(0), _pitch(0), _check_collision([](glm::vec3) -> bool { return false; })
// {
//     _position = glm::vec3(0.0f, 120.0f, 0.0f);
//     _front = glm::vec3(1.0f, 0.0f, 0.0f);
//     _up = glm::vec3(0.0f, 1.0f, 0.0f);
// }

Player::Player(const std::function<bool(glm::vec3)>& check_collision) :
    _lastMouseX(0),
    _lastMouseY(0),
    _pitch(0),
    _check_collision(check_collision)
{
    _position = glm::vec3(0.0f, 120.0f, 0.0f);
    _front = glm::vec3(1.0f, 0.0f, 0.0f);
    _up = glm::vec3(0.0f, 1.0f, 0.0f);
}

void Player::move_forward()
{
    const auto next_pos = _position + (_front * _moveSpeed);
    if (!_check_collision(next_pos))
        _position = next_pos;
}

void Player::move_backward()
{
    const auto next_pos = _position - (_front * _moveSpeed);
    if (!_check_collision(next_pos))
        _position = next_pos;
}

void Player::move_left()
{
    const auto next_pos = _position - (glm::normalize(glm::cross(_front, _up)) * _moveSpeed);
    if (!_check_collision(next_pos))
        _position = next_pos;
}

void Player::move_right()
{
    const auto next_pos = _position + (glm::normalize(glm::cross(_front, _up)) * _moveSpeed);
    if (!_check_collision(next_pos))
        _position = next_pos;
}

void Player::handle_mouse_move(float xChange, float yChange)
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
