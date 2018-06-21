#include "pandora/core/interaction.h"
#include "pandora/core/bxdf.h"
#include "pandora/core/material.h"
#include "pandora/core/scene.h"
#include "pandora/utility/math.h"
#include "pandora/utility/memory_arena.h"

namespace pandora {

void SurfaceInteraction::setShadingGeometry(
    const glm::vec3& dpdus,
    const glm::vec3& dpdvs,
    const glm::vec3& dndus,
    const glm::vec3& dndvs,
    bool orientationIsAuthoritative)
{
    // Compute shading normal
    shading.normal = glm::normalize(glm::cross(dpdus, dpdvs));
    if (orientationIsAuthoritative)
        normal = faceForward(normal, shading.normal);
    else
        shading.normal = faceForward(shading.normal, normal);

    shading.dpdu = dpdus;
    shading.dpdv = dpdvs;
    shading.dndu = dndus;
    shading.dndv = dndvs;
}

void SurfaceInteraction::computeScatteringFunctions(const Ray& ray, MemoryArena& arena, TransportMode mode, bool allowMultipleLobes)
{
    // TODO: compute ray differentials

    sceneObject->material->computeScatteringFunctions(*this, arena, mode, allowMultipleLobes);
}

glm::vec3 SurfaceInteraction::lightEmitted(const glm::vec3& w) const
{
    if (sceneObject->areaLightPerPrimitive) {
        const auto& area = sceneObject->areaLightPerPrimitive[primitiveID];
        return area.light(*this, w);
    } else {
        return glm::vec3(0.0f);
    }
}

Ray SurfaceInteraction::spawnRay(const glm::vec3& dir) const
{
    return computeRayWithEpsilon(*this, dir);
}

Ray computeRayWithEpsilon(const Interaction& i1, const Interaction& i2)
{
    glm::vec3 direction = i2.position - i1.position;

    //glm::vec3 start = glm::dot(i1.normal, direction) > 0.0f ? i1.position + i1.normal * RAY_EPSILON : i1.position - i1.normal * RAY_EPSILON;
    //glm::vec3 end = glm::dot(i2.normal, -direction) > 0.0f ? i2.position + i2.normal * RAY_EPSILON : i2.position - i2.normal * RAY_EPSILON;

    return Ray(i1.position, glm::normalize(direction), RAY_EPSILON, direction.length() - RAY_EPSILON);
}

Ray computeRayWithEpsilon(const Interaction& i1, const glm::vec3& dir)
{
    /*if (glm::dot(i1.normal, dir) > 0.0f)
        return Ray(i1.position + i1.normal * RAY_EPSILON, dir);
    else
        return Ray(i1.position - i1.normal * RAY_EPSILON, dir);*/
    //return Ray(i1.position + i1.normal * RAY_EPSILON, dir);
    return Ray(i1.position, dir, RAY_EPSILON);
}

}
