#pragma once
#include "pandora/graphics_core/pandora.h"
#include <EASTL/fixed_vector.h>
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <variant>

namespace pandora {

const float RAY_EPSILON = 0.000005f;

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

struct RayHit {
    glm::vec3 geometricNormal;
    glm::vec2 geometricUV;

	// Path taken through scene hierarchy to arrive at scene object
	eastl::fixed_vector<unsigned, 4, false> instanceIDs;
    const SceneObject* pSceneObject;
    unsigned primitiveID;
};

template <int N>
struct vec3SOA {
    float x[N];
    float y[N];
    float z[N];
};

template <int N>
struct RaySOA {
    vec3SOA<N> origin;
    vec3SOA<N> direction;
    float tnear[N];
    float tfar[N];
};

}
