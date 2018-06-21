#pragma once
#include "glm/glm.hpp"
#include <limits>

namespace pandora {

struct SceneObject;

const float RAY_EPSILON = 0.000001f;

struct Ray {
public:
    Ray() = default;
    Ray(const glm::vec3& origin, const glm::vec3& direction)
        : origin(origin)
        , direction(direction)
        , tnear(0.0f)
        , tfar(std::numeric_limits<float>::max())
    {
    }
     Ray(const glm::vec3& origin, const glm::vec3& direction, float tnear, float tfar = std::numeric_limits<float>::max())
        : origin(origin)
        , direction(direction)
        , tnear(tnear)
        , tfar(tfar)
    {
    }


    glm::vec3 origin;
    glm::vec3 direction;
    float tnear;
    float tfar;
};

}
