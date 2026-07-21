#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "TextureData.h"

#include <iostream>
#include <stdexcept>

TextureData TextureLoader::LoadRGBA(const std::string& filepath)
{
    int width = 0;
    int height = 0;
    int sourceChannels = 0;

    stbi_uc* loadedPixels = stbi_load(
        filepath.c_str(),
        &width,
        &height,
        &sourceChannels,
        STBI_rgb_alpha
    );

    if (loadedPixels == nullptr)
    {
        const char* reason = stbi_failure_reason();

        throw std::runtime_error(
            "Failed to load texture: " + filepath +
            "\nReason: " +
            (reason != nullptr ? reason : "Unknown stb_image error")
        );
    }

    const std::size_t imageSize =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        4;

    TextureData textureData{};
    textureData.width = width;
    textureData.height = height;
    textureData.channels = 4;

    textureData.pixels.assign(
        loadedPixels,
        loadedPixels + imageSize
    );

    stbi_image_free(loadedPixels);

    std::cout << "Loaded texture: " << filepath << '\n'
        << "  Width:    " << textureData.width << '\n'
        << "  Height:   " << textureData.height << '\n'
        << "  Channels: " << textureData.channels << '\n'
        << "  Bytes:    " << textureData.pixels.size()
        << std::endl;

    return textureData;
}

HDRTextureData TextureLoader::LoadHDR(
    const std::string& filepath
)
{
    int width = 0;
    int height = 0;
    int sourceChannels = 0;

    stbi_set_flip_vertically_on_load(true);

    float* loadedPixels = stbi_loadf(
        filepath.c_str(),
        &width,
        &height,
        &sourceChannels,
        STBI_rgb
    );

    // Restore the default immediately so later LDR textures are unaffected.
    stbi_set_flip_vertically_on_load(false);

    if (loadedPixels == nullptr)
    {
        const char* reason = stbi_failure_reason();

        throw std::runtime_error(
            "Failed to load HDR texture: " +
            filepath +
            "\nReason: " +
            (reason != nullptr
                ? reason
                : "Unknown stb_image error")
        );
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        3;

    HDRTextureData textureData{};
    textureData.width = width;
    textureData.height = height;
    textureData.channels = 3;

    textureData.pixels.assign(
        loadedPixels,
        loadedPixels + pixelCount
    );

    stbi_image_free(loadedPixels);

    std::cout
        << "[HDR] Loaded " << filepath << '\n'
        << "Width: " << textureData.width << '\n'
        << "Height: " << textureData.height << '\n'
        << "Source channels: " << sourceChannels << '\n'
        << "Loaded channels: " << textureData.channels
        << std::endl;

    return textureData;
}