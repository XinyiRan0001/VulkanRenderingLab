#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

// Power-of-two padding makes every parent cover exactly 2x2 children, including
// non-power-of-two viewport edges. Padded pixels contain far depth (1).
inline uint32_t HiZPowerOfTwo(uint32_t value)
{
    uint32_t n = 1;
    while (n < value)
        n *= 2;
    return n;
}
enum class HiZBypassReason
{
    None,
    NearOrEyePlane,
    Offscreen
};
struct HiZFootprint
{
    glm::ivec2 minimum{}, maximum{};
    glm::ivec2 pixelMinimum{}, pixelMaximum{};
    uint32_t mip = 0;
    float nearest = 1;
    bool valid = false;
    HiZBypassReason bypass = HiZBypassReason::None;
};
inline HiZFootprint ProjectHiZSphere(glm::vec4 sphere, const glm::mat4 &clip, uint32_t width, uint32_t height,
                                     uint32_t mipCount)
{
    HiZFootprint result;
    glm::vec2 lo(1e20f), hi(-1e20f);
    for (int i = 0; i < 8; ++i)
    {
        const glm::vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
        const auto p = clip * glm::vec4(glm::vec3(sphere) + sign * sphere.w, 1);
        // Projecting a bound that crosses the eye/near plane is not conservative.
        if (p.w <= 0 || p.z <= 0)
        {
            result.bypass = HiZBypassReason::NearOrEyePlane;
            return result;
        }
        const auto ndc = glm::vec3(p) / p.w;
        lo = glm::min(lo, glm::vec2(ndc));
        hi = glm::max(hi, glm::vec2(ndc));
        result.nearest = std::min(result.nearest, ndc.z);
    }
    const glm::vec2 extent{float(width), float(height)};
    lo = (lo * .5f + .5f) * extent;
    hi = (hi * .5f + .5f) * extent;
    if (hi.x < 0 || hi.y < 0 || lo.x >= width || lo.y >= height)
    {
        result.bypass = HiZBypassReason::Offscreen;
        return result;
    }
    const auto last = glm::ivec2(width - 1, height - 1);
    auto a = glm::clamp(glm::ivec2(glm::floor(lo)) - glm::ivec2(1), glm::ivec2(0), last);
    auto b = glm::clamp(glm::ivec2(glm::ceil(hi)) + glm::ivec2(1), glm::ivec2(0), last);
    uint32_t size = static_cast<uint32_t>(std::max(b.x - a.x + 1, b.y - a.y + 1));
    uint32_t texels = 1;
    while (texels < size && result.mip + 1 < mipCount)
    {
        texels *= 2;
        ++result.mip;
    }
    result.pixelMinimum = a;
    result.pixelMaximum = b;
    result.minimum = a / int(texels);
    result.maximum = b / int(texels);
    result.valid = true;
    return result;
}
inline float QueryHiZFarthest(const HiZFootprint &footprint, const std::vector<float> &mip, uint32_t width)
{
    float farthest = 0;
    for (int y : {footprint.minimum.y, footprint.maximum.y})
        for (int x : {footprint.minimum.x, footprint.maximum.x})
            farthest = std::max(farthest, mip.at(size_t(y) * width + x));
    return farthest;
}
inline bool QueryHiZOccluded(const HiZFootprint &footprint, const std::vector<float> &mip, uint32_t width)
{
    if (!footprint.valid)
        return false;
    const float farthest = QueryHiZFarthest(footprint, mip, width);
    return footprint.nearest > farthest + 1e-4f;
}
