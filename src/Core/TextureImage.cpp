#include "TextureImage.h"

#include "Buffer.h"
#include <vector>
#include <iostream>
#include <stdexcept>

TextureImage::TextureImage(
    const PhysicalDevice& physicalDevice,
    const LogicalDevice& logicalDevice,
    const TextureData& textureData,
    bool srgb
)
    : m_PhysicalDevice(physicalDevice),
    m_LogicalDevice(logicalDevice)
{
    m_Format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

    if (!textureData.IsValid())
    {
        throw std::runtime_error(
            "Cannot create Vulkan texture from invalid TextureData."
        );
    }

    CreateImage(
        static_cast<uint32_t>(textureData.width),
        static_cast<uint32_t>(textureData.height),
        m_Format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT
    );

    UploadPixels(textureData);
    CreateImageView();
    CreateSampler();

    std::cout
        << "TextureImage uploaded successfully."
        << std::endl;
}

TextureImage::TextureImage(
    const PhysicalDevice& physicalDevice,
    const LogicalDevice& logicalDevice,
    const HDRTextureData& textureData
)
    : m_PhysicalDevice(physicalDevice),
    m_LogicalDevice(logicalDevice)
{
    m_Format = VK_FORMAT_R32G32B32A32_SFLOAT;

    if (!textureData.IsValid())
    {
        throw std::runtime_error(
            "Cannot create Vulkan texture from invalid HDRTextureData."
        );
    }

    CreateImage(
        static_cast<uint32_t>(textureData.width),
        static_cast<uint32_t>(textureData.height),
        m_Format,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT
    );

    UploadPixels(textureData);
    CreateImageView();
    CreateSampler();

    std::cout
        << "HDR TextureImage uploaded successfully."
        << std::endl;
}

TextureImage::TextureImage(
    const PhysicalDevice& physicalDevice,
    const LogicalDevice& logicalDevice,
    const EmptyTextureImageCreateInfo& createInfo
)
    : m_PhysicalDevice(physicalDevice),
    m_LogicalDevice(logicalDevice)
{
    if (createInfo.width == 0 ||
        createInfo.height == 0)
    {
        throw std::runtime_error(
            "Empty texture image dimensions must be greater than zero."
        );
    }

    if (createInfo.mipLevels == 0 ||
        createInfo.arrayLayers == 0)
    {
        throw std::runtime_error(
            "Empty texture image must contain at least one mip level and one array layer."
        );
    }

    if (createInfo.viewType == VK_IMAGE_VIEW_TYPE_CUBE &&
        createInfo.arrayLayers != 6)
    {
        throw std::runtime_error(
            "A cubemap texture image must contain exactly six array layers."
        );
    }

    m_Format =
        createInfo.format;

    m_MipLevels =
        createInfo.mipLevels;

    m_ArrayLayers =
        createInfo.arrayLayers;

    CreateImage(
        createInfo.width,
        createInfo.height,
        createInfo.format,
        createInfo.usage,
        createInfo.flags,
        createInfo.mipLevels,
        createInfo.arrayLayers
    );

    CreateImageView(
        createInfo.viewType,
        createInfo.mipLevels,
        createInfo.arrayLayers
    );
        if (createInfo.viewType == VK_IMAGE_VIEW_TYPE_CUBE)
        {
            CreateCubemapLayerImageViews(
                createInfo.mipLevels
            );
        }

    CreateSampler(
        createInfo.addressMode,
        static_cast<float>(
            createInfo.mipLevels - 1
        )
    );

    std::cout
        << "Empty TextureImage created: "
        << createInfo.width
        << "x"
        << createInfo.height
        << ", layers: "
        << createInfo.arrayLayers
        << ", mip levels: "
        << createInfo.mipLevels
        << std::endl;
}

TextureImage::~TextureImage()
{
    VkDevice device = m_LogicalDevice.GetHandle();

    if (m_Sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(
            device,
            m_Sampler,
            nullptr
        );

        m_Sampler = VK_NULL_HANDLE;
    }

    for (VkImageView& layerImageView :
        m_LayerImageViews)
    {
        if (layerImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(
                device,
                layerImageView,
                nullptr
            );

            layerImageView =
                VK_NULL_HANDLE;
        }
    }

    if (m_ImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(
            device,
            m_ImageView,
            nullptr
        );

        m_ImageView = VK_NULL_HANDLE;
    }

    if (m_Image != VK_NULL_HANDLE)
    {
        vkDestroyImage(
            device,
            m_Image,
            nullptr
        );

        m_Image = VK_NULL_HANDLE;
    }

    if (m_ImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(
            device,
            m_ImageMemory,
            nullptr
        );

        m_ImageMemory = VK_NULL_HANDLE;
    }
}

void TextureImage::CreateImage(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageCreateFlags flags,
    uint32_t mipLevels,
    uint32_t arrayLayers
)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = flags;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;

    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;

    imageInfo.mipLevels =
        mipLevels;

    imageInfo.arrayLayers =
        arrayLayers;

    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;

    imageInfo.initialLayout =
        VK_IMAGE_LAYOUT_UNDEFINED;

    imageInfo.usage = usage;

    imageInfo.samples =
        VK_SAMPLE_COUNT_1_BIT;

    imageInfo.sharingMode =
        VK_SHARING_MODE_EXCLUSIVE;

    VkDevice device = m_LogicalDevice.GetHandle();

    if (vkCreateImage(
        device,
        &imageInfo,
        nullptr,
        &m_Image
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create Vulkan texture image."
        );
    }

    VkMemoryRequirements memoryRequirements{};

    vkGetImageMemoryRequirements(
        device,
        m_Image,
        &memoryRequirements
    );

    VkMemoryAllocateInfo allocationInfo{};
    allocationInfo.sType =
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;

    allocationInfo.allocationSize =
        memoryRequirements.size;

    allocationInfo.memoryTypeIndex =
        FindMemoryType(
            memoryRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

    if (vkAllocateMemory(
        device,
        &allocationInfo,
        nullptr,
        &m_ImageMemory
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to allocate Vulkan texture memory."
        );
    }

    if (vkBindImageMemory(
        device,
        m_Image,
        m_ImageMemory,
        0
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to bind Vulkan texture memory."
        );
    }
}

void TextureImage::CreateSampler()
{
    VkPhysicalDeviceProperties properties{};

    vkGetPhysicalDeviceProperties(
        m_PhysicalDevice.GetHandle(),
        &properties
    );

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType =
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeV =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;

    samplerInfo.maxAnisotropy =
        properties.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor =
        VK_BORDER_COLOR_INT_OPAQUE_BLACK;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode =
        VK_SAMPLER_MIPMAP_MODE_LINEAR;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(
        m_LogicalDevice.GetHandle(),
        &samplerInfo,
        nullptr,
        &m_Sampler
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create Vulkan texture sampler."
        );
    }

    std::cout
        << "Texture sampler created successfully."
        << std::endl;
}

void TextureImage::CreateSampler(
    VkSamplerAddressMode addressMode,
    float maximumLod
)
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType =
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter =
        VK_FILTER_LINEAR;

    samplerInfo.minFilter =
        VK_FILTER_LINEAR;

    samplerInfo.addressModeU =
        addressMode;

    samplerInfo.addressModeV =
        addressMode;

    samplerInfo.addressModeW =
        addressMode;

    samplerInfo.anisotropyEnable =
        VK_FALSE;

    samplerInfo.maxAnisotropy =
        1.0f;

    samplerInfo.borderColor =
        VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    samplerInfo.unnormalizedCoordinates =
        VK_FALSE;

    samplerInfo.compareEnable =
        VK_FALSE;

    samplerInfo.compareOp =
        VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode =
        VK_SAMPLER_MIPMAP_MODE_LINEAR;

    samplerInfo.mipLodBias =
        0.0f;

    samplerInfo.minLod =
        0.0f;

    samplerInfo.maxLod =
        maximumLod;

    if (vkCreateSampler(
        m_LogicalDevice.GetHandle(),
        &samplerInfo,
        nullptr,
        &m_Sampler
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create empty texture sampler."
        );
    }
}

void TextureImage::UploadPixels(
    const TextureData& textureData
)
{
    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(
            textureData.pixels.size()
            );

    Buffer stagingBuffer(
        m_PhysicalDevice,
        m_LogicalDevice,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.UploadData(
        textureData.pixels.data(),
        imageSize
    );

    TransitionImageLayout(
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    CopyBufferToImage(
        stagingBuffer.GetHandle(),
        static_cast<uint32_t>(textureData.width),
        static_cast<uint32_t>(textureData.height)
    );

    TransitionImageLayout(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}

void TextureImage::UploadPixels(
    const HDRTextureData& textureData
)
{
    const std::size_t pixelCount =
        static_cast<std::size_t>(textureData.width) *
        static_cast<std::size_t>(textureData.height);

    const std::size_t expectedFloatCount =
        pixelCount * 3;

    if (textureData.pixels.size() != expectedFloatCount)
    {
        throw std::runtime_error(
            "HDR texture pixel count does not match RGB dimensions."
        );
    }

    // Vulkan texture format is RGBA32F, while stb_image loaded RGB32F.
    std::vector<float> rgbaPixels(pixelCount * 4);

    for (std::size_t pixelIndex = 0;
        pixelIndex < pixelCount;
        ++pixelIndex)
    {
        const std::size_t rgbIndex =
            pixelIndex * 3;

        const std::size_t rgbaIndex =
            pixelIndex * 4;

        rgbaPixels[rgbaIndex + 0] =
            textureData.pixels[rgbIndex + 0];

        rgbaPixels[rgbaIndex + 1] =
            textureData.pixels[rgbIndex + 1];

        rgbaPixels[rgbaIndex + 2] =
            textureData.pixels[rgbIndex + 2];

        rgbaPixels[rgbaIndex + 3] = 1.0f;
    }

    const VkDeviceSize imageSize =
        static_cast<VkDeviceSize>(
            rgbaPixels.size() * sizeof(float)
            );

    Buffer stagingBuffer(
        m_PhysicalDevice,
        m_LogicalDevice,
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    stagingBuffer.UploadData(
        rgbaPixels.data(),
        imageSize
    );

    TransitionImageLayout(
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
    );

    CopyBufferToImage(
        stagingBuffer.GetHandle(),
        static_cast<uint32_t>(textureData.width),
        static_cast<uint32_t>(textureData.height)
    );

    TransitionImageLayout(
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );
}

void TextureImage::CreateImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = m_Format;

    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;

    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(
        m_LogicalDevice.GetHandle(),
        &viewInfo,
        nullptr,
        &m_ImageView
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create texture image view."
        );
    }
}

void TextureImage::CreateImageView(
    VkImageViewType viewType,
    uint32_t mipLevels,
    uint32_t arrayLayers
)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType =
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    viewInfo.image =
        m_Image;

    viewInfo.viewType =
        viewType;

    viewInfo.format =
        m_Format;

    viewInfo.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    viewInfo.subresourceRange.baseMipLevel =
        0;

    viewInfo.subresourceRange.levelCount =
        mipLevels;

    viewInfo.subresourceRange.baseArrayLayer =
        0;

    viewInfo.subresourceRange.layerCount =
        arrayLayers;

    if (vkCreateImageView(
        m_LogicalDevice.GetHandle(),
        &viewInfo,
        nullptr,
        &m_ImageView
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create empty texture image view."
        );
    }
}

void TextureImage::CreateCubemapLayerImageViews(
    uint32_t mipLevels
)
{
    constexpr uint32_t faceCount =
        6;

    if (mipLevels == 0)
    {
        throw std::runtime_error(
            "Cubemap image views require at least one mip level."
        );
    }

    VkDevice device =
        m_LogicalDevice.GetHandle();

    // One 2D image view is required for every:
    //
    // mip level × cubemap face
    //
    // Example:
    // 8 mip levels × 6 faces = 48 image views.
    m_LayerImageViews.clear();

    m_LayerImageViews.reserve(
        static_cast<size_t>(mipLevels) *
        faceCount
    );

    for (uint32_t mipLevel = 0;
        mipLevel < mipLevels;
        ++mipLevel)
    {
        for (uint32_t faceIndex = 0;
            faceIndex < faceCount;
            ++faceIndex)
        {
            VkImageViewCreateInfo viewInfo{};

            viewInfo.sType =
                VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

            viewInfo.image =
                m_Image;

            // A single cubemap face at one mip level
            // is used as a regular 2D framebuffer attachment.
            viewInfo.viewType =
                VK_IMAGE_VIEW_TYPE_2D;

            viewInfo.format =
                m_Format;

            viewInfo.subresourceRange.aspectMask =
                VK_IMAGE_ASPECT_COLOR_BIT;

            viewInfo.subresourceRange.baseMipLevel =
                mipLevel;

            viewInfo.subresourceRange.levelCount =
                1;

            viewInfo.subresourceRange.baseArrayLayer =
                faceIndex;

            viewInfo.subresourceRange.layerCount =
                1;

            VkImageView imageView =
                VK_NULL_HANDLE;

            if (vkCreateImageView(
                device,
                &viewInfo,
                nullptr,
                &imageView
            ) != VK_SUCCESS)
            {
                // Clean up any views already created before
                // propagating the error.
                for (VkImageView createdView :
                    m_LayerImageViews)
                {
                    if (createdView != VK_NULL_HANDLE)
                    {
                        vkDestroyImageView(
                            device,
                            createdView,
                            nullptr
                        );
                    }
                }

                m_LayerImageViews.clear();

                throw std::runtime_error(
                    "Failed to create cubemap face mip image view."
                );
            }

            m_LayerImageViews.push_back(
                imageView
            );
        }
    }

    std::cout
        << "Created "
        << m_LayerImageViews.size()
        << " cubemap face/mip image views."
        << std::endl;
}

VkImageView TextureImage::GetLayerImageView(
    uint32_t faceIndex,
    uint32_t mipLevel
) const
{
    constexpr uint32_t faceCount =
        6;

    if (faceIndex >= faceCount)
    {
        throw std::out_of_range(
            "Cubemap face index is out of range."
        );
    }

    const size_t imageViewIndex =
        static_cast<size_t>(mipLevel) *
        faceCount +
        faceIndex;

    if (imageViewIndex >=
        m_LayerImageViews.size())
    {
        throw std::out_of_range(
            "Cubemap mip level is out of range."
        );
    }

    return m_LayerImageViews[
        imageViewIndex
    ];
}

void TextureImage::TransitionImageLayout(
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t baseMipLevel,
    uint32_t mipLevelCount,
    uint32_t baseArrayLayer,
    uint32_t arrayLayerCount
)
{
    QueueFamilyIndices indices =
        m_PhysicalDevice.FindQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    poolInfo.queueFamilyIndex =
        indices.graphicsFamily.value();

    poolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice.GetHandle(),
        &poolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create temporary command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        BeginSingleTimeCommands(commandPool);

    VkImageMemoryBarrier barrier{};
    barrier.sType =
        VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;

    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;

    barrier.srcQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;

    barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;

    barrier.image = m_Image;

    barrier.subresourceRange.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    barrier.subresourceRange.baseMipLevel =
        baseMipLevel;

    barrier.subresourceRange.levelCount =
        mipLevelCount == VK_REMAINING_MIP_LEVELS
        ? m_MipLevels - baseMipLevel
        : mipLevelCount;

    barrier.subresourceRange.baseArrayLayer =
        baseArrayLayer;

    barrier.subresourceRange.layerCount =
        arrayLayerCount == VK_REMAINING_ARRAY_LAYERS
        ? m_ArrayLayers - baseArrayLayer
        : arrayLayerCount;

    if (baseMipLevel >= m_MipLevels)
    {
        throw std::out_of_range(
            "Texture image base mip level is out of range."
        );
    }

    if (baseArrayLayer >= m_ArrayLayers)
    {
        throw std::out_of_range(
            "Texture image base array layer is out of range."
        );
    }

    if (barrier.subresourceRange.levelCount >
        m_MipLevels - baseMipLevel)
    {
        throw std::out_of_range(
            "Texture image mip transition range is out of range."
        );
    }

    if (barrier.subresourceRange.layerCount >
        m_ArrayLayers - baseArrayLayer)
    {
        throw std::out_of_range(
            "Texture image layer transition range is out of range."
        );
    }

    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;

    if (
        oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        )
    {
        barrier.srcAccessMask = 0;

        barrier.dstAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage =
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

        destinationStage =
            VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (
        oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
        newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        )
    {
        barrier.srcAccessMask =
            VK_ACCESS_TRANSFER_WRITE_BIT;

        barrier.dstAccessMask =
            VK_ACCESS_SHADER_READ_BIT;

        sourceStage =
            VK_PIPELINE_STAGE_TRANSFER_BIT;

        destinationStage =
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        vkDestroyCommandPool(
            m_LogicalDevice.GetHandle(),
            commandPool,
            nullptr
        );

        throw std::runtime_error(
            "Unsupported texture image layout transition."
        );
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier
    );

    EndSingleTimeCommands(
        commandBuffer,
        commandPool
    );

    vkDestroyCommandPool(
        m_LogicalDevice.GetHandle(),
        commandPool,
        nullptr
    );
}

void TextureImage::CopyBufferToImage(
    VkBuffer buffer,
    uint32_t width,
    uint32_t height
)
{
    QueueFamilyIndices indices =
        m_PhysicalDevice.FindQueueFamilies();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    poolInfo.queueFamilyIndex =
        indices.graphicsFamily.value();

    poolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice.GetHandle(),
        &poolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create temporary copy command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        BeginSingleTimeCommands(commandPool);

    VkBufferImageCopy region{};

    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;

    region.imageSubresource.aspectMask =
        VK_IMAGE_ASPECT_COLOR_BIT;

    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = { 0, 0, 0 };

    region.imageExtent =
    {
        width,
        height,
        1
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        m_Image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    EndSingleTimeCommands(
        commandBuffer,
        commandPool
    );

    vkDestroyCommandPool(
        m_LogicalDevice.GetHandle(),
        commandPool,
        nullptr
    );
}

VkCommandBuffer TextureImage::BeginSingleTimeCommands(
    VkCommandPool commandPool
) const
{
    VkCommandBufferAllocateInfo allocationInfo{};
    allocationInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

    allocationInfo.level =
        VK_COMMAND_BUFFER_LEVEL_PRIMARY;

    allocationInfo.commandPool = commandPool;
    allocationInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    if (vkAllocateCommandBuffers(
        m_LogicalDevice.GetHandle(),
        &allocationInfo,
        &commandBuffer
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to allocate temporary command buffer."
        );
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    beginInfo.flags =
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(
        commandBuffer,
        &beginInfo
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to begin temporary command buffer."
        );
    }

    return commandBuffer;
}

void TextureImage::EndSingleTimeCommands(
    VkCommandBuffer commandBuffer,
    VkCommandPool commandPool
) const
{
    if (vkEndCommandBuffer(
        commandBuffer
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to finish temporary command buffer."
        );
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType =
        VK_STRUCTURE_TYPE_SUBMIT_INFO;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers =
        &commandBuffer;

    if (vkQueueSubmit(
        m_LogicalDevice.GetGraphicsQueue(),
        1,
        &submitInfo,
        VK_NULL_HANDLE
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to submit temporary command buffer."
        );
    }

    vkQueueWaitIdle(
        m_LogicalDevice.GetGraphicsQueue()
    );

    vkFreeCommandBuffers(
        m_LogicalDevice.GetHandle(),
        commandPool,
        1,
        &commandBuffer
    );
}

uint32_t TextureImage::FindMemoryType(
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties
) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};

    vkGetPhysicalDeviceMemoryProperties(
        m_PhysicalDevice.GetHandle(),
        &memoryProperties
    );

    for (
        uint32_t i = 0;
        i < memoryProperties.memoryTypeCount;
        ++i
        )
    {
        const bool typeSupported =
            (typeFilter & (1u << i)) != 0;

        const bool propertiesSupported =
            (
                memoryProperties
                .memoryTypes[i]
                .propertyFlags &
                properties
                ) == properties;

        if (typeSupported && propertiesSupported)
        {
            return i;
        }
    }

    throw std::runtime_error(
        "Failed to find texture image memory type."
    );
}