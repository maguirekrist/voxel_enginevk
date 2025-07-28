#pragma once

#include <vk_types.h>
#include <constants.h>

class Player {
public:
    glm::vec3 _position;
    glm::vec3 _front;
    glm::vec3 _up;
    float _moveSpeed = DEFAULT_MOVE_SPEED;
    
    Player();

    void move_forward();
    void move_backward();
    void move_left();
    void move_right();
    void handle_mouse_move(float xChange, float yChange);

private:
    bool _firstMouse = true;
    float _lastMouseX;
    float _lastMouseY;
    float _yaw = 90.0f;
    float _pitch;
};