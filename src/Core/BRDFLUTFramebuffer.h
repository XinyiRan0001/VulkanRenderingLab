#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class LogicalDevice;
class BRDFLUTRenderPass;
class TextureImage;

class BRDFLUTFramebuffer
{
public:
    BRDFLUTFramebuffer(
        const LogicalDevice& logicalDevice,
        const BRDFLUTRenderPass& renderPass,
        const TextureImage& brdfLUT,
        uint32_t width,
        uint32_t height
    );

    ~BRDFLUTFramebuffer();

    BRDFLUTFramebuffer(
        const BRDFLUTFramebuffer&
    ) = delete;

    BRDFLUTFramebuffer& operator=(
        const BRDFLUTFramebuffer&
    ) = delete;

    VkFramebuffer GetHandle() const
    {
        return m_Framebuffer;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkFramebuffer m_Framebuffer =
        VK_NULL_HANDLE;
};