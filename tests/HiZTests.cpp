#include "Core/HiZMath.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <iostream>
#include <stdexcept>

void Check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        Check(HiZPowerOfTwo(1280) == 2048 && HiZPowerOfTwo(720) == 1024, "NPOT padding incorrect");
        const auto clip = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.5f, .1f, 100.0f);
        auto projected = ProjectHiZSphere({0, 0, -5, .2f}, clip, 913, 517, 11);
        Check(projected.valid && projected.nearest < 1, "valid sphere projection rejected");
        Check(projected.maximum.x - projected.minimum.x <= 1 &&
                  projected.maximum.y - projected.minimum.y <= 1,
              "footprint needs more than 2x2 texels");
        Check(!ProjectHiZSphere({0, 0, -.12f, .1f}, clip, 913, 517, 11).valid,
              "near-plane crossing must bypass occlusion");
        Check(!ProjectHiZSphere({0, 0, 1, .2f}, clip, 913, 517, 11).valid,
              "behind-eye bound must bypass occlusion");
        Check(!ProjectHiZSphere({100, 0, -5, .2f}, clip, 913, 517, 11).valid,
              "offscreen bound must bypass occlusion");
        for (int i = 0; i < 1000; ++i)
        {
            auto p = ProjectHiZSphere({float(i % 29) - 14, float(i % 13) - 6, -float(i % 40 + 2), .2f}, clip,
                                      913, 517, 11);
            if (p.valid)
                Check(p.maximum.x - p.minimum.x <= 1 && p.maximum.y - p.minimum.y <= 1,
                      "2x2 coverage guarantee failed");
        }
        HiZFootprint query;
        query.valid = true;
        query.minimum = {0, 0};
        query.maximum = {1, 1};
        query.nearest = .8f;
        std::vector<float> depths{.2f, .3f, .4f, .7f};
        Check(QueryHiZOccluded(query, depths, 2), "fully covered farther bound should be occluded");
        depths[3] = 1;
        Check(!QueryHiZOccluded(query, depths, 2), "background hole must preserve visibility");
        depths[3] = .7f;
        query.nearest = .70005f;
        Check(!QueryHiZOccluded(query, depths, 2), "depth tolerance must preserve ambiguous objects");
        query.valid = false;
        Check(!QueryHiZOccluded(query, depths, 2), "invalid projection cannot be culled");
        std::cout << "Hi-Z padding, bounds, near/eye bypass, 2x2 coverage, max-depth and background-hole "
                     "tests passed.\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
