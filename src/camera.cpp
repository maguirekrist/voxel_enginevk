#include "camera.h"

Camera::Camera(glm::vec3 defaultPos)
{
    _position = defaultPos;
    _front = glm::vec3(0.0f, 0.0f, 1.0f);
    _up = glm::vec3(0.0f, 1.0f, 0.0f);
    _view = glm::mat4(1.0f);
    update_view();
}

void Camera::moveForward()
{
    _position += _front * _cameraSpeed;
    update_view();
}

void Camera::moveBackward()
{
    _position -= _front * _cameraSpeed;
    update_view();
}

void Camera::moveLeft()
{
    _position -= glm::normalize(glm::cross(_front, _up)) * _cameraSpeed;
    update_view();
}

void Camera::moveRight()
{
    _position += glm::normalize(glm::cross(_front, _up)) * _cameraSpeed;
    update_view();
}

void Camera::update_view()
{
    _view = glm::lookAt(_position, _position + _front, _up);
}

void Camera::handle_mouse_move_v2(float xChange, float yChange)
{
    fmt::println("Mouse x: {}, y: {}", xChange, yChange);

    float sensitivity = 0.1f;
    xChange *= sensitivity;
    yChange *= -sensitivity;

    _yaw += xChange;
    _pitch += yChange;

    if (_pitch > 89.0f)
        _pitch = 89.0f;
    if (_pitch < -89.0f)
        _pitch = -89.0f;


    fmt::println("Pitch: {}", _pitch);

    glm::vec3 direction;
    direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    direction.y = sin(glm::radians(_pitch));
    direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));

    fmt::println("Direction, x: {}, y: {}, z: {}", direction.x, direction.y, direction.z);

    _front = glm::normalize(direction);
    update_view();

}

void Camera::handle_mouse_move(double xpos, double ypos)
{
    if (_firstMouse)
    {
        _lastMouseX = xpos;
        _lastMouseY = ypos;
        _firstMouse = false;
    }

    fmt::println("Mouse x: {}, y: {}", xpos, ypos);
  
    float xoffset = xpos - _lastMouseX;
    float yoffset = _lastMouseY - ypos; 
    _lastMouseX = xpos;
    _lastMouseY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    fmt::println("Pitch: {}, yoffset, {}", _pitch, yoffset);

    _yaw   += xoffset;
    _pitch += yoffset;

    if(_pitch > 89.0f)
        _pitch = 89.0f;
    if(_pitch < -89.0f)
        _pitch = -89.0f;


    fmt::println("Pitch: {}", _pitch);

    glm::vec3 direction;
    direction.x = cos(glm::radians(_yaw)) * cos(glm::radians(_pitch));
    direction.y = sin(glm::radians(_pitch));
    direction.z = sin(glm::radians(_yaw)) * cos(glm::radians(_pitch));

    fmt::println("Direction, x: {}, y: {}, z: {}", direction.x, direction.y, direction.z);

    _front = glm::normalize(direction);
    update_view();
}
