#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class LogicalDevice;

class DescriptorSetLayout
{
public:
    explicit DescriptorSetLayout(
        const LogicalDevice& logicalDevice
    );

    ~DescriptorSetLayout();

    DescriptorSetLayout(
        const DescriptorSetLayout&
    ) = delete;

    DescriptorSetLayout& operator=(
        const DescriptorSetLayout&
    ) = delete;

    VkDescriptorSetLayout GetHandle() const
    {
        return m_DescriptorSetLayout;
    }

private:
    const LogicalDevice& m_LogicalDevice;

    VkDescriptorSetLayout m_DescriptorSetLayout =
        VK_NULL_HANDLE;

private:
    void CreateDescriptorSetLayout();
};