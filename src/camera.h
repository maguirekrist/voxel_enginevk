
#pragma once

#include <game/world.h>
#include "components/game_object.h"


struct RaycastResult {
    Block _block;
    FaceDirection _blockFace;
    glm::ivec3 _worldPos;
    float _distance;
};

class Camera final : public GameObject
{
public:
    glm::mat4 _view;
	glm::mat4 _projection = glm::perspective(glm::radians(70.f), 1700.f / 900.f, 0.1f, 10000.0f);

    explicit Camera(const glm::vec3& position);
    void update(float dt) override;
    static std::optional<RaycastResult> get_target_block(World& world, GameObject& player);
};