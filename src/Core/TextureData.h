#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct TextureData
{
    int width = 0;
    int height = 0;
    int channels = 0;

    std::vector<std::uint8_t> pixels;

    bool IsValid() const
    {
        return width > 0 &&
            height > 0 &&
            !pixels.empty();
    }
};

struct HDRTextureData
{
    int width = 0;
    int height = 0;
    int channels = 0;

    std::vector<float> pixels;

    bool IsValid() const
    {
        return width > 0 &&
            height > 0 &&
            !pixels.empty();
    }
};

class TextureLoader
{
    public:
        static TextureData LoadRGBA(
            const std::string& filepath
        );

        static HDRTextureData LoadHDR(
            const std::string& filepath
        );
};