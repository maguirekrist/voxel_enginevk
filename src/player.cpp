#include "player.h"

Player::Player()
{
    _position = glm::vec3(0.0f, 120.0f, 0.0f);
    _front = glm::vec3(0.0f, 0.0f, 1.0f);
    _up = glm::vec3(0.0f, 1.0f, 0.0f);
}

void Player::move_forward()
{
    _position += _front * _moveSpeed;
}

void Player::move_backward()
{
    _position -= _front * _moveSpeed;
}

void Player::move_left()
{
    _position -= glm::normalize(glm::cross(_front, _up)) * _moveSpeed;
}

void Player::move_right()
{
    _position += glm::normalize(glm::cross(_front, _up)) * _moveSpeed;
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


    //fmt::println("Pitch: {}", _pitch);

    glm::vec3 direction;
    direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    direction.y = sin(glm::radians(_pitch));
    direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));

   // fmt::println("Direction, x: {}, y: {}, z: {}", direction.x, direction.y, direction.z);

    _front = glm::normalize(direction);
}
