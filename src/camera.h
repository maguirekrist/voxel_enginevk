
#pragma once

#include <vk_types.h>


class Camera
{
public:
    glm::mat4 _view;
	glm::mat4 _projection;

    Camera();

    void update_view(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up);
    void update_front(float xChange, float yChange);
private:
    glm::vec3 _position;
    glm::vec3 _front;
    glm::vec3 _up;
    float _yaw = 90.0f;
    float _pitch = 0.0f;
};