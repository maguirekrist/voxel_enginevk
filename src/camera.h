
#pragma once
#include <vk_types.h>
#include <block.h>
#include <world.h>
#include <player.h>

struct RaycastResult {
    Block* _block;
    FaceDirection _blockFace;
    Chunk* _chunk;
    glm::ivec3 _worldPos;
    float _distance;
};

class Camera
{
public:
    glm::mat4 _view;
	//camera projection
	glm::mat4 _projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 10000.0f);

    Camera();

    void update_view(const glm::vec3& position, const glm::vec3& front, const glm::vec3& up);
    std::optional<RaycastResult> get_target_block(World& world, Player& player);
};