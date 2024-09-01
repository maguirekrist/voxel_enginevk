
#pragma once
#include <vk_types.h>

class Camera
{
public:
    glm::vec3 _position;
    glm::vec3 _front;
    glm::vec3 _up;

    glm::mat4 _view;
    float _cameraSpeed = 10.0f;

    bool _firstMouse = true;
    float _lastMouseX;
    float _lastMouseY;
    float _yaw = 90.0f;
    float _pitch;

    Camera(glm::vec3 defaultPos);

    void moveForward();
    void moveBackward();
    void moveLeft();
    void moveRight();
    void update_view();
    void handle_mouse_move_v2(float xChange, float yChange);
    void handle_mouse_move(double xpos, double ypos);
};