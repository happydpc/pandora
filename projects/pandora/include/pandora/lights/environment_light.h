#pragma once
#include "pandora/graphics_core/light.h"
#include "pandora/graphics_core/texture.h"

namespace pandora {

class EnvironmentLight : public InfiniteLight {
public:
    EnvironmentLight(const glm::mat4& lightToWorld, const Spectrum& l, const std::shared_ptr<Texture<glm::vec3>>& texture);

    LightSample sampleLi(const Interaction& ref, PcgRng& rng) const final;
	float pdfLi(const Interaction& ref, const glm::vec3& wi) const final;

    Spectrum Le(const Ray& w) const final;

private:
    glm::vec3 lightToWorld(const glm::vec3& v) const;
    glm::vec3 worldToLight(const glm::vec3& v) const;

private:
    Spectrum m_l;
    std::shared_ptr<Texture<glm::vec3>> m_texture;

    const glm::mat4 m_lightToWorld, m_worldToLight;
};

}
