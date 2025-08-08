//
// Created by Maguire Krist on 8/7/25.
//

#ifndef AABB_H
#define AABB_H
#include "glm/vec3.hpp"


struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    [[nodiscard]] bool intersects(const AABB& other) const noexcept
    {
        return  (max.x >= other.min.x && min.x <= other.max.x) &&
                (max.y >= other.min.y && min.y <= other.max.y) &&
                (max.z >= other.min.z && min.z <= other.max.z);
    }

    [[nodiscard]] AABB moved(const glm::vec3 delta) const noexcept
    {
        return { min + delta, max + delta};
    }
};



#endif //AABB_H
