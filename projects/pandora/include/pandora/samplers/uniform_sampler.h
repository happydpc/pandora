#pragma once
#include "pandora/core/sampler.h"
#include "pandora/samplers/rng/pcg.h"
#include <random>

namespace pandora {
class UniformSampler : public Sampler {
public:
    UniformSampler(unsigned samplesPerPixel, unsigned rngSeed = 0);

    float get1D() final;
    glm::vec2 get2D() final;

private:
    PcgRng m_rng;
};
}
