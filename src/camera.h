
#pragma once

#include <game/player.h>
#include <game/world.h>


struct RaycastResult {
    Block _block;
    FaceDirection _blockFace;
    //ChunkView _chunk;
    glm::ivec3 _worldPos;
    float _distance;
};

class Camera
{
    glm::vec3 _up;
    float _yaw = 0.0f;
    float _pitch = 0.0f;
public:
    glm::mat4 _view;
	glm::mat4 _projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 10000.0f);

    glm::vec3 _front;
    glm::vec3 _position;
    float _moveSpeed{GameConfig::DEFAULT_MOVE_SPEED};

    Camera();
    void handle_mouse_move(float xChange, float yChange);
    void update_view();
    void move_forward();
    void move_backward();
    void move_left();
    void move_right();
    static std::optional<RaycastResult> get_target_block(World& world, Player& player);
};