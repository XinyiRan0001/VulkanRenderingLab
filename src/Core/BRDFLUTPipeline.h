#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <string>
#include <vector>

class LogicalDevice;
class BRDFLUTRenderPass;

class BRDFLUTPipeline
{
public:
    BRDFLUTPipeline(
        const LogicalDevice& logicalDevice,
        const BRDFLUTRenderPass& renderPass,
        uint32_t lutSize
    );

    ~BRDFLUTPipeline();

    BRDFLUTPipeline(
        const BRDFLUTPipeline&
    ) = delete;

    BRDFLUTPipeline& operator=(
        const BRDFLUTPipeline&
    ) = delete;

    VkPipeline GetPipeline() const
    {
        return m_Pipeline;
    }

    VkPipelineLayout GetLayout() const
    {
        return m_PipelineLayout;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkPipelineLayout m_PipelineLayout =
        VK_NULL_HANDLE;

    VkPipeline m_Pipeline =
        VK_NULL_HANDLE;

private:
    void CreatePipeline(
        const BRDFLUTRenderPass& renderPass,
        uint32_t lutSize
    );

    static std::vector<char> ReadFile(
        const std::string& filename
    );

    VkShaderModule CreateShaderModule(
        const std::vector<char>& code
    ) const;
};