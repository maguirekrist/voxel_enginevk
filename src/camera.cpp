#include "camera.h"

Camera::Camera() :
    _view(glm::mat4(1.0f)),
    _projection(glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 10000.0f)),
    _position(glm::vec3(0.0f, 10.0f, 0.0f)),
    _front(glm::vec3(0.0f, 0.0f, 1.0f)),
    _up(glm::vec3(0.0f, 1.0f, 0.0f))
{
    _projection[1][1] *= -1;
    _view = glm::lookAt(_position, _position + _front, _up);

    fmt::println("Camera Constructed!");
}

void Camera::update_view(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up)
{
    _position = position;
    _front = front;
    _up = up;
    _view = glm::lookAt(position, position + front, up);
}

void Camera::update_front(float xChange, float yChange) {
    auto sensitivity = 0.1f;
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
    _view = glm::lookAt(_position, _position + _front, _up);
}
