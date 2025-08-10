//
// Created by Maguire Krist on 8/9/25.
//

#ifndef PLAYER_INPUT_COMPONENT_H
#define PLAYER_INPUT_COMPONENT_H
#include <functional>

#include "game_object.h"
#include "glm/vec3.hpp"

class PlayerInputComponent final : public Component {

    std::function<bool(glm::vec3)> _check_collision;
public:
    explicit PlayerInputComponent(const std::function<bool(glm::vec3)>& check_collision);

    void move_forward(GameObject& object);
    void move_backward(GameObject& object);
    void move_left(GameObject& object);
    void move_right(GameObject& object);
    void handle_mouse_move(GameObject& object, float xChange, float yChange);
};



#endif //PLAYER_INPUT_COMPONENT_H
