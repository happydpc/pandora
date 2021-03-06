#pragma once
#include "pandora/graphics_core/transform.h"
#include "pandora/graphics_core/light.h"
#include "pandora/graphics_core/texture.h"

namespace pandora {

class DistantLight : public InfiniteLight {
public:
    DistantLight(const glm::mat4& lightToWorld, const Spectrum& L, const glm::vec3& wLight);

    //glm::vec3 power() const final;

    LightSample sampleLi(const Interaction& ref, PcgRng& rng) const final;
    float pdfLi(const Interaction& ref, const glm::vec3& wi) const final;

private:
    Transform m_transform;
    const Spectrum m_l;
    const glm::vec3 m_wLight;
};

}
