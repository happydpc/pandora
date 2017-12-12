#pragma once
#include "pandora/math/vec2.h"
#include "pandora/math/vec3.h"
#include <boost/multi_array.hpp>
#include <gsl/gsl>
#include <vector>

namespace pandora {

class Sensor {
public:
    using FramebufferType = boost::multi_array<Vec3f, 2>;
    using FrameBufferArrayView = boost::multi_array<Vec3f, 2>::array_view<2>::type;
    using FrameBufferConstArrayView = boost::multi_array<Vec3f, 2>::const_array_view<2>::type;

public:
    Sensor(int width, int height);

    void clear(Vec3f color);
    void addPixelContribution(Vec2i pixel, Vec3f value);

    int width() const { return m_width; }
    int height() const { return m_height; }
    const FrameBufferConstArrayView getFramebufferView() const;
    gsl::not_null<const Vec3f*> getFramebufferRaw() const;

private:
    int m_width, m_height;
    FramebufferType m_frameBuffer;
};
}