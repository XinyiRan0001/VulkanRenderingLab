#include "VulkanContext.h"

#include "Window.h"
#include "PhysicalDevice.h"
#include "LogicalDevice.h"
#include "Swapchain.h"
#include "RenderPass.h"
#include "Framebuffer.h"
#include "CommandManager.h"
#include "Renderer.h"
#include "GraphicsPipeline.h"
#include "Buffer.h"
#include "Vertex.h"
#include "Image.h"
#include "UniformBufferObject.h"
#include "DescriptorManager.h"
#include "DescriptorSetLayout.h"
#include "OBJLoader.h"
#include "MeshData.h"
#include "TextureData.h"
#include "TextureImage.h"
#include "ShadowRenderPass.h"
#include "ShadowFramebuffer.h"
#include "ShadowPipeline.h"
#include "SkyboxPipeline.h"
#include <algorithm>

#include <limits>
#include <array>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

VulkanContext::VulkanContext(Window& window)
    : m_Window(window)
{
    // =========================================================
    // Vulkan base objects
    // =========================================================

    CreateInstance();
    CreateSurface();

    m_PhysicalDevice = std::make_unique<PhysicalDevice>(
        m_Instance,
        m_Surface
    );

    m_LogicalDevice = std::make_unique<LogicalDevice>(
        *m_PhysicalDevice
    );

    m_Swapchain = std::make_unique<Swapchain>(
        m_Window,
        *m_PhysicalDevice,
        *m_LogicalDevice,
        m_Surface
    );

    m_RenderPass = std::make_unique<RenderPass>(
        *m_LogicalDevice,
        *m_Swapchain
    );

    // =========================================================
    // Main depth buffer
    // =========================================================

    m_DepthImage = std::make_unique<Image>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        m_Swapchain->GetExtent().width,
        m_Swapchain->GetExtent().height,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    // =========================================================
// Shadow map depth image
// =========================================================

    constexpr uint32_t SHADOW_MAP_WIDTH = 2048;
    constexpr uint32_t SHADOW_MAP_HEIGHT = 2048;

    std::cout
        << "[Shadow] Creating shadow depth image..."
        << std::endl;

    m_ShadowDepthImage = std::make_unique<Image>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        SHADOW_MAP_WIDTH,
        SHADOW_MAP_HEIGHT,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT
    );

    std::cout
        << "[Shadow] Shadow depth image created successfully."
        << std::endl;

    std::cout
        << "[Shadow] Creating shadow render pass..."
        << std::endl;

    m_ShadowRenderPass =
        std::make_unique<ShadowRenderPass>(
            *m_LogicalDevice
        );

    std::cout
        << "[Shadow] Shadow render pass ready."
        << std::endl;

    std::cout
        << "[Shadow] Creating shadow framebuffer..."
        << std::endl;

    m_ShadowFramebuffer =
        std::make_unique<ShadowFramebuffer>(
            *m_LogicalDevice,
            *m_ShadowRenderPass,
            m_ShadowDepthImage->GetView(),
            SHADOW_MAP_WIDTH,
            SHADOW_MAP_HEIGHT
        );

    std::cout
        << "[Shadow] Shadow framebuffer ready."
        << std::endl;

    std::cout
    << "[Shadow] Creating shadow pipeline..."
    << std::endl;

    

    CreateShadowSampler();

    // =========================================================
    // Swapchain framebuffers
    // =========================================================

    m_Framebuffer = std::make_unique<Framebuffer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_RenderPass,
        m_DepthImage->GetView()
    );

    // =========================================================
// Shared descriptor set layout
// =========================================================

m_DescriptorSetLayout =
    std::make_unique<DescriptorSetLayout>(
        *m_LogicalDevice
    );

// =========================================================
// Main graphics pipeline
// =========================================================

m_GraphicsPipeline =
    std::make_unique<GraphicsPipeline>(
        *m_LogicalDevice,
        *m_RenderPass,
        *m_Swapchain,
        m_DescriptorSetLayout->GetHandle()
    );

    m_ShadowPipeline =
    std::make_unique<ShadowPipeline>(
        *m_LogicalDevice,
        *m_ShadowRenderPass,
        m_DescriptorSetLayout->GetHandle()
    );

    // =========================================================
    // Skybox pipeline
    // =========================================================

    m_SkyboxPipeline =
        std::make_unique<SkyboxPipeline>(
            *m_LogicalDevice,
            *m_RenderPass,
            *m_Swapchain,
            m_DescriptorSetLayout->GetHandle()
        );

        std::cout
        << "[Shadow] Shadow pipeline ready."
        << std::endl;
    // =========================================================
    // Load Marry model
    // =========================================================

    MeshData maryMesh = OBJLoader::Load(
        "assets/models/mary/Marry.obj"
    );

    // Character material defaults.
    for (MaterialData& material : maryMesh.materials)
    {
        material.metallic = 0.0f;
        material.roughness = 0.65f;
        material.ambientOcclusion = 1.0f;
    }

    // =========================================================
    // Load Floor model
    // =========================================================

    MeshData floorMesh = OBJLoader::Load(
        "assets/models/floor/floor.obj"
    );

    // =========================================================
// Load Material Lab sphere
// =========================================================

    MeshData materialBallMesh = OBJLoader::Load(
        "assets/models/materiallab/TestObj.obj"
    );

    if (materialBallMesh.vertices.empty())
    {
        throw std::runtime_error(
            "Material Lab sphere contains no vertices."
        );
    }

    // =========================================================
    // Normalize the sphere model
    //
    // TestObj.obj may use a different source scale from Marry.
    // Normalize it into a predictable local-space size before
    // applying the renderer's shared model matrix.
    // =========================================================

    glm::vec3 boundsMin(
        std::numeric_limits<float>::max()
    );

    glm::vec3 boundsMax(
        std::numeric_limits<float>::lowest()
    );

    for (const Vertex& vertex : materialBallMesh.vertices)
    {
        const glm::vec3 position(
            vertex.position[0],
            vertex.position[1],
            vertex.position[2]
        );

        boundsMin = glm::min(
            boundsMin,
            position
        );

        boundsMax = glm::max(
            boundsMax,
            position
        );
    }

    const glm::vec3 boundsCenter =
        (boundsMin + boundsMax) * 0.5f;

    const glm::vec3 boundsSize =
        boundsMax - boundsMin;

    const float largestDimension =
        std::max(
            boundsSize.x,
            std::max(
                boundsSize.y,
                boundsSize.z
            )
        );

    if (largestDimension <= 0.000001f)
    {
        throw std::runtime_error(
            "Material Lab sphere has invalid bounds."
        );
    }

    // The normalized sphere will be approximately two local-space
    // units across. The shared 0.35 model scale then makes it about
    // 0.7 world-space units across.
    const float normalizationScale =
        2.0f / largestDimension;

    // Position is specified before the shared 0.35 model scale.
    // This places the test sphere to the left of the character.
    const glm::vec3 materialBallPosition(
        -4.0f,
        1.35f,
        0.0f
    );

    for (Vertex& vertex : materialBallMesh.vertices)
    {
        glm::vec3 position(
            vertex.position[0],
            vertex.position[1],
            vertex.position[2]
        );

        position =
            (position - boundsCenter) *
            normalizationScale;

        position += materialBallPosition;

        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
    }

    if (materialBallMesh.materials.empty())
    {
        MaterialData material{};

        material.diffuseColor[0] = 0.72f;
        material.diffuseColor[1] = 0.72f;
        material.diffuseColor[2] = 0.72f;

        material.hasDiffuseTexture = false;
        material.metallic = 0.0f;
        material.roughness = 0.18f;
        material.ambientOcclusion = 1.0f;

        materialBallMesh.materials.push_back(
            material
        );

        for (SubMesh& subMesh :
            materialBallMesh.subMeshes)
        {
            subMesh.materialId = 0;
        }
    }

    // Temporarily bake a transform into the floor vertices.
    // These values can be adjusted after seeing the result.
    for (Vertex& vertex : floorMesh.vertices)
    {
        vertex.position[0] *= 4.0f;
        vertex.position[1] *= 4.0f;
        vertex.position[2] *= 4.0f;

        // Move the floor slightly behind/below the character.
        vertex.position[2] -= 1.5f;
    }

    // The current renderer only has one texture descriptor.
    // Do not let the floor accidentally sample Marry's texture.
    for (MaterialData& material : floorMesh.materials)
    {
        material.hasDiffuseTexture = false;

        material.metallic = 0.0f;
        material.roughness = 0.9f;
        material.ambientOcclusion = 1.0f;
    }

    // =========================================================
    // Combine Marry and Floor into one MeshData
    // =========================================================

    MeshData combinedMesh;

    // ---------------------------------------------------------
    // Copy Marry vertices and indices
    // ---------------------------------------------------------

    combinedMesh.vertices.insert(
        combinedMesh.vertices.end(),
        maryMesh.vertices.begin(),
        maryMesh.vertices.end()
    );

    combinedMesh.indices.insert(
        combinedMesh.indices.end(),
        maryMesh.indices.begin(),
        maryMesh.indices.end()
    );

    combinedMesh.materials = maryMesh.materials;

    for (const SubMesh& sourceSubMesh : maryMesh.subMeshes)
    {
        combinedMesh.subMeshes.push_back(sourceSubMesh);
    }

    // ---------------------------------------------------------
    // Record offsets before appending Floor
    // ---------------------------------------------------------

    const uint32_t floorVertexOffset =
        static_cast<uint32_t>(
            combinedMesh.vertices.size()
            );

    const uint32_t floorIndexOffset =
        static_cast<uint32_t>(
            combinedMesh.indices.size()
            );

    const int floorMaterialOffset =
        static_cast<int>(
            combinedMesh.materials.size()
            );

    // ---------------------------------------------------------
    // Append Floor vertices
    // ---------------------------------------------------------

    combinedMesh.vertices.insert(
        combinedMesh.vertices.end(),
        floorMesh.vertices.begin(),
        floorMesh.vertices.end()
    );

    // ---------------------------------------------------------
    // Append Floor indices
    //
    // Floor indices originally begin at vertex 0.
    // They must be shifted because Marry's vertices come first.
    // ---------------------------------------------------------

    for (uint32_t index : floorMesh.indices)
    {
        combinedMesh.indices.push_back(
            index + floorVertexOffset
        );
    }

    // ---------------------------------------------------------
    // Append Floor materials
    // ---------------------------------------------------------

    combinedMesh.materials.insert(
        combinedMesh.materials.end(),
        floorMesh.materials.begin(),
        floorMesh.materials.end()
    );

    // ---------------------------------------------------------
    // Append Floor SubMeshes
    // ---------------------------------------------------------

    for (const SubMesh& sourceSubMesh : floorMesh.subMeshes)
    {
        SubMesh floorSubMesh = sourceSubMesh;

        // Floor indices begin after all Marry indices.
        floorSubMesh.firstIndex += floorIndexOffset;

        // Floor material IDs begin after all Marry materials.
        if (floorSubMesh.materialId >= 0)
        {
            floorSubMesh.materialId += floorMaterialOffset;
        }

        combinedMesh.subMeshes.push_back(
            floorSubMesh
        );
    }
    for (int row = 0; row < 2; ++row)
    {
        for (int sphere = 0; sphere < 5; ++sphere)
        {
            MeshData sphereMesh = materialBallMesh;
            const float roughnessValues[5] =
            {
                0.02f,
                0.20f,
                0.45f,
                0.70f,
                1.00f
            };

            for (MaterialData& material : sphereMesh.materials)
            {
                material.hasDiffuseTexture = false;
                material.metallic = row == 0 ? 0.0f : 1.0f;
                material.roughness = roughnessValues[sphere];
                material.ambientOcclusion = 1.0f;

            }

            // ---------------------------------------------------------
            // Position the material test spheres
            // ---------------------------------------------------------

            const float sphereScale =
                0.65f;

            const float sphereSpacingX =
                1.65f;

            const float sphereSpacingY =
                1.65f;

            const glm::vec3 spherePosition =
                glm::vec3(
                    -5.2f +
                        static_cast<float>(sphere) *
                        1.45f,

                    1.0f -
                        static_cast<float>(row) *
                        1.55f,

                    -1.3f
                );

            for (Vertex& vertex :
                sphereMesh.vertices)
            {
                glm::vec3 position =
                    glm::vec3(
                        vertex.position[0],
                        vertex.position[1],
                        vertex.position[2]
                    );

                position *=
                    sphereScale;

                position +=
                    spherePosition;

                vertex.position[0] =
                    position.x;

                vertex.position[1] =
                    position.y;

                vertex.position[2] =
                    position.z;
            }
            // =========================================================
            // Append Material Lab sphere
            // =========================================================

            const uint32_t materialBallVertexOffset =
                static_cast<uint32_t>(
                    combinedMesh.vertices.size()
                    );

            const uint32_t materialBallIndexOffset =
                static_cast<uint32_t>(
                    combinedMesh.indices.size()
                    );

            const int materialBallMaterialOffset =
                static_cast<int>(
                    combinedMesh.materials.size()
                    );

            // Append vertices.
            combinedMesh.vertices.insert(
                combinedMesh.vertices.end(),
                sphereMesh.vertices.begin(),
                sphereMesh.vertices.end()
            );

            // Append indices and shift them to the sphere's vertex range.
            for (uint32_t index : sphereMesh.indices)
            {
                combinedMesh.indices.push_back(
                    index +
                    materialBallVertexOffset
                );
            }

            // Append materials.
            combinedMesh.materials.insert(
                combinedMesh.materials.end(),
                sphereMesh.materials.begin(),
                sphereMesh.materials.end()
            );

            // Append submeshes and shift index/material offsets.
            for (const SubMesh& sourceSubMesh :
                sphereMesh.subMeshes)
            {
                SubMesh materialBallSubMesh =
                    sourceSubMesh;

                materialBallSubMesh.firstIndex +=
                    materialBallIndexOffset;

                if (materialBallSubMesh.materialId >= 0)
                {
                    materialBallSubMesh.materialId +=
                        materialBallMaterialOffset;
                }

                combinedMesh.subMeshes.push_back(
                    materialBallSubMesh
                );
            }
        }
    }

    std::cout
        << "[Scene] Combined vertices: "
        << combinedMesh.vertices.size()
        << std::endl;

    std::cout
        << "[Scene] Combined indices: "
        << combinedMesh.indices.size()
        << std::endl;

    std::cout
        << "[Scene] Combined submeshes: "
        << combinedMesh.subMeshes.size()
        << std::endl;

    std::cout
        << "[Scene] Combined materials: "
        << combinedMesh.materials.size()
        << std::endl;

// =========================================================
// Test HDR loading
// =========================================================

    HDRTextureData hdrTextureData =
        TextureLoader::LoadHDR(
            "assets/textures/hdr/small_harbor_01_4k.hdr"
        );

    if (!hdrTextureData.IsValid())
    {
        throw std::runtime_error(
            "Loaded HDR texture is invalid."
        );
    }

    m_HDRTextureImage =
        std::make_unique<TextureImage>(
            *m_PhysicalDevice,
            *m_LogicalDevice,
            hdrTextureData
        );

    EmptyTextureImageCreateInfo cubemapCreateInfo{};

    constexpr uint32_t environmentCubemapSize =
        512;

    cubemapCreateInfo.width =
        environmentCubemapSize;

    cubemapCreateInfo.height =
        environmentCubemapSize;

    cubemapCreateInfo.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    cubemapCreateInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    cubemapCreateInfo.flags =
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    cubemapCreateInfo.viewType =
        VK_IMAGE_VIEW_TYPE_CUBE;

    cubemapCreateInfo.mipLevels =
        1;

    cubemapCreateInfo.arrayLayers =
        6;

    cubemapCreateInfo.addressMode =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    m_EnvironmentCubemap =
        std::make_unique<TextureImage>(
            *m_PhysicalDevice,
            *m_LogicalDevice,
            cubemapCreateInfo
        );

    // =========================================================
    // Create irradiance cubemap
    // =========================================================

    constexpr uint32_t irradianceCubemapSize =
        128;

    constexpr uint32_t irradianceMipLevels =
        1;

    EmptyTextureImageCreateInfo
        irradianceCubemapCreateInfo{};

    irradianceCubemapCreateInfo.width =
        irradianceCubemapSize;

    irradianceCubemapCreateInfo.height =
        irradianceCubemapSize;

    irradianceCubemapCreateInfo.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    irradianceCubemapCreateInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    irradianceCubemapCreateInfo.flags =
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    irradianceCubemapCreateInfo.viewType =
        VK_IMAGE_VIEW_TYPE_CUBE;

    irradianceCubemapCreateInfo.mipLevels =
        irradianceMipLevels;

    irradianceCubemapCreateInfo.arrayLayers =
        6;

    irradianceCubemapCreateInfo.addressMode =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    std::cout
        << "[IBL] About to create irradiance cubemap."
        << std::endl;

    m_IrradianceCubemap =
        std::make_unique<TextureImage>(
            *m_PhysicalDevice,
            *m_LogicalDevice,
            irradianceCubemapCreateInfo
        );

    std::cout
        << "[IBL] Irradiance cubemap created."
        << std::endl;

    // =========================================================
    // Create prefiltered environment cubemap
    //
    // Each mip level represents a different roughness:
    // mip 0            = sharp reflection
    // final mip level  = very rough reflection
    // =========================================================

    constexpr uint32_t prefilteredCubemapSize =
        128;

    const uint32_t prefilteredMipLevels =
        static_cast<uint32_t>(
            std::floor(
                std::log2(
                    static_cast<float>(
                        prefilteredCubemapSize
                    )
                )
            )
        ) + 1;

    EmptyTextureImageCreateInfo
        prefilteredCubemapCreateInfo{};

    prefilteredCubemapCreateInfo.width =
        prefilteredCubemapSize;

    prefilteredCubemapCreateInfo.height =
        prefilteredCubemapSize;

    prefilteredCubemapCreateInfo.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    prefilteredCubemapCreateInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    prefilteredCubemapCreateInfo.flags =
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    prefilteredCubemapCreateInfo.viewType =
        VK_IMAGE_VIEW_TYPE_CUBE;

    prefilteredCubemapCreateInfo.mipLevels =
        prefilteredMipLevels;

    prefilteredCubemapCreateInfo.arrayLayers =
        6;

    prefilteredCubemapCreateInfo.addressMode =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    std::cout
        << "[IBL] About to create prefiltered cubemap."
        << std::endl;

    m_PrefilteredEnvironmentCubemap =
        std::make_unique<TextureImage>(
            *m_PhysicalDevice,
            *m_LogicalDevice,
            prefilteredCubemapCreateInfo
        );

    std::cout
        << "[IBL] Prefiltered environment cubemap created."
        << std::endl;

    std::cout
        << "[IBL] Prefiltered cubemap size: "
        << prefilteredCubemapSize
        << " x "
        << prefilteredCubemapSize
        << std::endl;

    std::cout
        << "[IBL] Prefiltered mip levels: "
        << prefilteredMipLevels
        << std::endl;

    // =========================================================
    // Create BRDF integration LUT texture
    // =========================================================

    constexpr uint32_t brdfLUTSize =
        512;

    EmptyTextureImageCreateInfo
        brdfLUTCreateInfo{};

    brdfLUTCreateInfo.width =
        brdfLUTSize;

    brdfLUTCreateInfo.height =
        brdfLUTSize;

    // RG contains the two split-sum BRDF integration terms.
    //
    // TextureImage currently supports arbitrary Vulkan formats,
    // but RGBA16F is used here for broad render-target support.
    // The fragment shader will only read the .rg channels.
    brdfLUTCreateInfo.format =
        VK_FORMAT_R16G16B16A16_SFLOAT;

    brdfLUTCreateInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
        VK_IMAGE_USAGE_SAMPLED_BIT;

    brdfLUTCreateInfo.flags =
        0;

    brdfLUTCreateInfo.viewType =
        VK_IMAGE_VIEW_TYPE_2D;

    brdfLUTCreateInfo.mipLevels =
        1;

    brdfLUTCreateInfo.arrayLayers =
        1;

    brdfLUTCreateInfo.addressMode =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    std::cout
        << "[IBL] About to create BRDF LUT texture."
        << std::endl;

    m_BRDFLUT =
        std::make_unique<TextureImage>(
            *m_PhysicalDevice,
            *m_LogicalDevice,
            brdfLUTCreateInfo
        );

    std::cout
        << "[IBL] BRDF LUT texture created."
        << std::endl;

    std::cout
        << "[IBL] BRDF LUT size: "
        << brdfLUTSize
        << " x "
        << brdfLUTSize
        << std::endl;

    std::cout
    << "[IBL] Creating BRDF LUT render pass..."
    << std::endl;

    m_BRDFLUTRenderPass =
        std::make_unique<BRDFLUTRenderPass>(
            *m_LogicalDevice,
            m_BRDFLUT->GetFormat()
        );

    std::cout
        << "[IBL] Creating BRDF LUT framebuffer..."
        << std::endl;

    m_BRDFLUTFramebuffer =
        std::make_unique<BRDFLUTFramebuffer>(
            *m_LogicalDevice,
            *m_BRDFLUTRenderPass,
            *m_BRDFLUT,
            brdfLUTSize,
            brdfLUTSize
        );

    std::cout
        << "[IBL] Creating BRDF LUT pipeline..."
        << std::endl;

    m_BRDFLUTPipeline =
        std::make_unique<BRDFLUTPipeline>(
            *m_LogicalDevice,
            *m_BRDFLUTRenderPass,
            brdfLUTSize
        );

    std::cout
        << "[IBL] BRDF LUT pipeline ready."
        << std::endl;

    std::cout
        << "[IBL] Creating environment render pass..."
        << std::endl;

    m_EnvironmentRenderPass =
        std::make_unique<EnvironmentRenderPass>(
            *m_LogicalDevice,
            m_EnvironmentCubemap->GetFormat()
        );

    std::cout
        << "[IBL] Creating irradiance render pass..."
        << std::endl;

    m_IrradianceRenderPass =
        std::make_unique<IrradianceRenderPass>(
            *m_LogicalDevice,
            m_IrradianceCubemap->GetFormat()
        );

    std::cout
        << "[IBL] Irradiance render pass ready."
        << std::endl;

    std::cout
        << "[IBL] Creating irradiance framebuffers..."
        << std::endl;

    m_IrradianceFramebuffer =
        std::make_unique<IrradianceFramebuffer>(
            *m_LogicalDevice,
            *m_IrradianceRenderPass,
            *m_IrradianceCubemap,
            irradianceCubemapSize,
            irradianceCubemapSize
        );

    std::cout
        << "[IBL] Irradiance framebuffers ready."
        << std::endl;

    std::cout
        << "[IBL] Creating irradiance pipeline..."
        << std::endl;

    m_IrradiancePipeline =
        std::make_unique<IrradiancePipeline>(
            *m_LogicalDevice,
            *m_IrradianceRenderPass,
            irradianceCubemapSize
        );

    std::cout
        << "[IBL] Irradiance pipeline ready."
        << std::endl;

    std::cout
        << "[IBL] Creating irradiance descriptor set..."
        << std::endl;

    m_IrradianceDescriptorManager =
        std::make_unique<IrradianceDescriptorManager>(
            *m_LogicalDevice,
            *m_IrradiancePipeline,
            *m_EnvironmentCubemap
        );

    std::cout
        << "[IBL] Irradiance descriptor set ready."
        << std::endl;

    std::cout
        << "[IBL] Creating prefilter framebuffers..."
        << std::endl;

    m_PrefilterFramebuffer =
        std::make_unique<PrefilterFramebuffer>(
            *m_LogicalDevice,
            *m_EnvironmentRenderPass,
            *m_PrefilteredEnvironmentCubemap,
            prefilteredCubemapSize,
            prefilteredCubemapSize,
            prefilteredMipLevels
        );

    std::cout
        << "[IBL] Prefilter framebuffers ready."
        << std::endl;

    std::cout
        << "[IBL] Creating prefilter pipeline..."
        << std::endl;

    m_PrefilterPipeline =
        std::make_unique<PrefilterPipeline>(
            *m_LogicalDevice,
            *m_EnvironmentRenderPass,
            prefilteredCubemapSize
        );

    std::cout
        << "[IBL] Prefilter pipeline ready."
        << std::endl;

    std::cout
        << "[IBL] Creating prefilter descriptor set..."
        << std::endl;

    m_PrefilterDescriptorManager =
        std::make_unique<PrefilterDescriptorManager>(
            *m_LogicalDevice,
            *m_PrefilterPipeline,
            *m_EnvironmentCubemap
        );

    std::cout
        << "[IBL] Prefilter descriptor set ready."
        << std::endl;

    std::cout
        << "[IBL] Creating environment framebuffers..."
        << std::endl;

    m_EnvironmentFramebuffer =
        std::make_unique<EnvironmentFramebuffer>(
            *m_LogicalDevice,
            *m_EnvironmentRenderPass,
            *m_EnvironmentCubemap,
            environmentCubemapSize,
            environmentCubemapSize
        );

    std::cout
        << "[IBL] Creating HDR-to-cubemap pipeline..."
        << std::endl;

    m_HDRToCubemapPipeline =
        std::make_unique<HDRToCubemapPipeline>(
            *m_LogicalDevice,
            *m_EnvironmentRenderPass,
            environmentCubemapSize
        );

    std::cout
        << "[IBL] Creating HDR-to-cubemap descriptor set..."
        << std::endl;

    m_HDRToCubemapDescriptorManager =
        std::make_unique<HDRToCubemapDescriptorManager>(
            *m_LogicalDevice,
            *m_HDRToCubemapPipeline,
            *m_HDRTextureImage
        );

    GenerateEnvironmentCubemap();
    GenerateIrradianceCubemap();
    GeneratePrefilteredEnvironmentCubemap();
    GenerateBRDFLUT();

    // =========================================================
    // Load Marry texture
    // =========================================================

    TextureData textureData = TextureLoader::LoadRGBA(
        "assets/models/mary/MC003_Kozakura_Mari.png"
    );

    m_TextureImage = std::make_unique<TextureImage>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        textureData
    );

    // =========================================================
    // Combined vertex buffer
    // =========================================================

    const VkDeviceSize vertexBufferSize =
        sizeof(Vertex) *
        combinedMesh.vertices.size();

    std::cout
        << "[Context] Creating vertex buffer..."
        << std::endl;

    m_VertexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_VertexBuffer->UploadData(
        combinedMesh.vertices.data(),
        vertexBufferSize
    );

    std::cout
        << "[Context] Vertex buffer uploaded."
        << std::endl;

    // =========================================================
    // Combined index buffer
    // =========================================================

    const VkDeviceSize indexBufferSize =
        sizeof(uint32_t) *
        combinedMesh.indices.size();

    std::cout
        << "[Context] Creating index buffer..."
        << std::endl;

    m_IndexBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_IndexBuffer->UploadData(
        combinedMesh.indices.data(),
        indexBufferSize
    );

    std::cout
        << "[Context] Index buffer uploaded."
        << std::endl;

    // =========================================================
    // Initial camera/MVP uniform data
    // =========================================================

    UniformBufferObject ubo{};

    ubo.model = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(0.35f)
    );

    ubo.view = glm::lookAt(
        glm::vec3(0.0f, 0.8f, 3.5f),
        glm::vec3(0.0f, 0.8f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    const float aspect =
        m_Swapchain->GetExtent().width /
        static_cast<float>(
            m_Swapchain->GetExtent().height
            );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );

    ubo.proj[1][1] *= -1.0f;

    // =========================================================
    // Uniform buffer
    // =========================================================

    std::cout
        << "[Context] Creating uniform buffer..."
        << std::endl;

    m_UniformBuffer = std::make_unique<Buffer>(
        *m_PhysicalDevice,
        *m_LogicalDevice,
        sizeof(UniformBufferObject),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    m_UniformBuffer->UploadData(
        &ubo,
        sizeof(ubo)
    );

    std::cout
        << "[Context] Uniform buffer uploaded."
        << std::endl;

    // =========================================================
    // Descriptor set
    // =========================================================

    std::cout
        << "[Context] Creating descriptor manager..."
        << std::endl;

    m_DescriptorManager =
    std::make_unique<DescriptorManager>(
        *m_LogicalDevice,
        *m_GraphicsPipeline,
        *m_UniformBuffer,
        *m_TextureImage,
        *m_HDRTextureImage,

        // Original environment cubemap.
        *m_EnvironmentCubemap,

        *m_PrefilteredEnvironmentCubemap,
        *m_BRDFLUT,

        // Diffuse irradiance cubemap.
        *m_IrradianceCubemap,

        m_ShadowDepthImage->GetView(),
        m_ShadowSampler
    );

    std::cout
        << "[Context] Descriptor manager ready."
        << std::endl;

    // =========================================================
    // Command buffers
    // =========================================================

    std::cout
        << "[Context] Creating command manager..."
        << std::endl;

    m_CommandManager =
        std::make_unique<CommandManager>(
            *m_LogicalDevice,
            *m_PhysicalDevice,
            *m_Framebuffer,
            *m_RenderPass,
            *m_Swapchain,
            *m_GraphicsPipeline,
            *m_SkyboxPipeline,
            *m_ShadowRenderPass,
            *m_ShadowFramebuffer,
            *m_ShadowPipeline,
            *m_VertexBuffer,
            *m_IndexBuffer,
            *m_DescriptorManager,
            combinedMesh.subMeshes,
            combinedMesh.materials
        );

    std::cout
        << "[Context] Command manager ready."
        << std::endl;

    // =========================================================
    // Renderer
    // =========================================================

    std::cout
        << "[Context] Creating renderer..."
        << std::endl;

    m_Renderer = std::make_unique<Renderer>(
        *m_LogicalDevice,
        *m_Swapchain,
        *m_CommandManager
    );

    std::cout
        << "[Context] Renderer ready."
        << std::endl;
    SetupCameraControls();
    std::cout
        << "VulkanContext created successfully."
        << std::endl;
}

void VulkanContext::SetupCameraControls()
{
    GLFWwindow* glfwWindow = m_Window.GetHandle();

    glfwSetWindowUserPointer(
        glfwWindow,
        this
    );

    glfwSetMouseButtonCallback(
        glfwWindow,
        [](GLFWwindow* window, int button, int action, int mods)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            if (button == GLFW_MOUSE_BUTTON_LEFT)
            {
                if (action == GLFW_PRESS)
                {
                    context->m_IsDragging = true;

                    glfwGetCursorPos(
                        window,
                        &context->m_LastMouseX,
                        &context->m_LastMouseY
                    );
                }
                else if (action == GLFW_RELEASE)
                {
                    context->m_IsDragging = false;
                }
            }
        }
    );

    glfwSetCursorPosCallback(
        glfwWindow,
        [](GLFWwindow* window, double mouseX, double mouseY)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr ||
                !context->m_IsDragging)
            {
                return;
            }

            const double deltaX =
                mouseX - context->m_LastMouseX;

            const double deltaY =
                mouseY - context->m_LastMouseY;

            context->m_LastMouseX = mouseX;
            context->m_LastMouseY = mouseY;

            constexpr float rotationSpeed = 0.005f;

            context->m_CameraYaw +=
                static_cast<float>(deltaX) *
                rotationSpeed;

            context->m_CameraPitch -=
                static_cast<float>(deltaY) *
                rotationSpeed;

            context->m_CameraPitch =
                std::clamp(
                    context->m_CameraPitch,
                    -1.3f,
                    1.3f
                );
        }
    );

    glfwSetScrollCallback(
        glfwWindow,
        [](GLFWwindow* window, double xOffset, double yOffset)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            context->m_CameraDistance -=
                static_cast<float>(yOffset) *
                0.25f;

            context->m_CameraDistance =
                std::clamp(
                    context->m_CameraDistance,
                    1.5f,
                    10.0f
                );
        }
    );
    glfwSetKeyCallback(
        glfwWindow,
        [](GLFWwindow* window,
            int key,
            int scancode,
            int action,
            int mods)
        {
            auto* context =
                static_cast<VulkanContext*>(
                    glfwGetWindowUserPointer(window)
                    );

            if (context == nullptr)
            {
                return;
            }

            if (key == GLFW_KEY_H &&
                action == GLFW_PRESS)
            {
                context->ToggleSkybox();
            }

            if (key == GLFW_KEY_F5 &&
                action == GLFW_PRESS)
            {
                context->m_Exposure -= 0.1f;

                context->m_Exposure =
                    std::clamp(
                        context->m_Exposure,
                        0.1f,
                        5.0f
                    );

                std::cout
                    << "[HDR] Exposure: "
                    << context->m_Exposure
                    << std::endl;
            }

            if (key == GLFW_KEY_F6 &&
                action == GLFW_PRESS)
            {
                context->m_Exposure += 0.1f;

                context->m_Exposure =
                    std::clamp(
                        context->m_Exposure,
                        0.1f,
                        5.0f
                    );

                std::cout
                    << "[HDR] Exposure: "
                    << context->m_Exposure
                    << std::endl;
            }

            if (action != GLFW_PRESS &&
                action != GLFW_REPEAT)
            {
                return;
            }

            constexpr float moveStep = 0.15f;

            if (context->m_ManualPointLightControl)
            {
                if (key == GLFW_KEY_J)
                {
                    context->m_ManualPointLightPosition.x -= moveStep;
                }
                else if (key == GLFW_KEY_L)
                {
                    context->m_ManualPointLightPosition.x += moveStep;
                }
                else if (key == GLFW_KEY_I)
                {
                    context->m_ManualPointLightPosition.z -= moveStep;
                }
                else if (key == GLFW_KEY_K)
                {
                    context->m_ManualPointLightPosition.z += moveStep;
                }
                else if (key == GLFW_KEY_U)
                {
                    context->m_ManualPointLightPosition.y += moveStep;
                }
                else if (key == GLFW_KEY_O)
                {
                    context->m_ManualPointLightPosition.y -= moveStep;
                }
            }

            if (key == GLFW_KEY_1)
            {
                context->m_ShadowMode = 1;

                std::cout
                    << "[Shadow] Mode: Hard Shadow"
                    << std::endl;
            }
            else if (key == GLFW_KEY_2)
            {
                context->m_ShadowMode = 2;

                std::cout
                    << "[Shadow] Mode: PCF"
                    << std::endl;
            }
            else if (key == GLFW_KEY_3)
            {
                context->m_ShadowMode = 3;

                std::cout
                    << "[Shadow] Mode: PCSS"
                    << std::endl;
            }
            else if (key == GLFW_KEY_4)
            {
                context->m_ShadowMode = 4;

                std::cout
                    << "[Shadow] Mode: Shadow Map Debug"
                    << std::endl;
            }

            else if (key == GLFW_KEY_R)
            {
                context->m_CameraYaw = 0.0f;
                context->m_CameraPitch = 0.15f;
                context->m_CameraDistance = 3.5f;

                std::cout
                    << "[Camera] Reset"
                    << std::endl;
            }
            else if (key == GLFW_KEY_ESCAPE)
            {
                glfwSetWindowShouldClose(
                    window,
                    GLFW_TRUE
                );
            }
            else if (key == GLFW_KEY_P)
            {
                context->m_PointLightEnabled =
                    !context->m_PointLightEnabled;

                std::cout
                    << "[Point Light] "
                    << (
                        context->m_PointLightEnabled
                        ? "Enabled"
                        : "Disabled"
                        )
                    << std::endl;
            }
            else if (key == GLFW_KEY_SPACE)
            {
                context->m_PointLightAnimationEnabled =
                    !context->m_PointLightAnimationEnabled;

                std::cout
                    << "[Point Light Animation] "
                    << (
                        context->m_PointLightAnimationEnabled
                        ? "Running"
                        : "Paused"
                        )
                    << std::endl;
            }
            else if (key == GLFW_KEY_M)
            {
                context->m_ManualPointLightControl =
                    !context->m_ManualPointLightControl;

                std::cout
                    << "[Point Light] Control Mode: "
                    << (
                        context->m_ManualPointLightControl
                        ? "Manual"
                        : "Orbit"
                        )
                    << std::endl;
            }
            else if (
                key == GLFW_KEY_B &&
                action == GLFW_PRESS
                )
            {
                std::cout
                    << "[Input] B pressed."
                    << std::endl;

                if (context->m_Renderer != nullptr)
                {
                    context->m_Renderer->StartBenchmark(
                        context->m_ShadowMode
                    );
                }
            }
        }
    );
}

void VulkanContext::UpdateWindowTitle(double deltaTime)
{
    m_FrameTimeAccumulator += deltaTime;
    m_FrameCount++;

    const double currentTime = glfwGetTime();

    if (currentTime - m_LastTitleUpdateTime < 0.5)
    {
        return;
    }

    const double averageFrameTime =
        m_FrameTimeAccumulator /
        static_cast<double>(m_FrameCount);

    const double fps =
        averageFrameTime > 0.0
        ? 1.0 / averageFrameTime
        : 0.0;

    const double frameTimeMs =
        averageFrameTime * 1000.0;

    const char* shadowModeName = "Unknown";

    switch (m_ShadowMode)
    {
    case 1:
        shadowModeName = "Hard Shadow";
        break;

    case 2:
        shadowModeName = "PCF";
        break;

    case 3:
        shadowModeName = "PCSS";
        break;

    case 4:
        shadowModeName = "Shadow Map Debug";
        break;
    }

    const char* pointLightMode =
        m_ManualPointLightControl
        ? "Manual Light"
        : "Orbit Light";

    std::ostringstream title;

    title
        << "Vulkan Rendering Lab"
        << " | "
        << shadowModeName
        << " | "
        << pointLightMode
        << " | "
        << std::fixed
        << std::setprecision(1)
        << fps
        << " FPS"
        << " | "
        << std::setprecision(2)
        << frameTimeMs
        << " ms";

    glfwSetWindowTitle(
        m_Window.GetHandle(),
        title.str().c_str()
    );

    m_LastTitleUpdateTime = currentTime;
    m_FrameTimeAccumulator = 0.0;
    m_FrameCount = 0;
}

void VulkanContext::GenerateEnvironmentCubemap()
{

    constexpr uint32_t cubemapFaceCount =
        6;

    constexpr uint32_t cubemapSize =
        512;

    const std::array<glm::vec3, cubemapFaceCount>
        faceDirections =
    {
        glm::vec3( 1.0f,  0.0f,  0.0f),
        glm::vec3(-1.0f,  0.0f,  0.0f),
        glm::vec3( 0.0f,  1.0f,  0.0f),
        glm::vec3( 0.0f, -1.0f,  0.0f),
        glm::vec3( 0.0f,  0.0f,  1.0f),
        glm::vec3( 0.0f,  0.0f, -1.0f)
    };

    const std::array<glm::vec3, cubemapFaceCount>
        faceUpDirections =
    {
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f,  0.0f,  1.0f),
        glm::vec3(0.0f,  0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f)
    };

    glm::mat4 captureProjection =
        glm::perspective(
            glm::radians(90.0f),
            1.0f,
            0.1f,
            10.0f
        );

    captureProjection[1][1] *=
        -1.0f;

    const glm::vec3 capturePosition =
        glm::vec3(0.0f);
    if (!m_PhysicalDevice ||
        !m_LogicalDevice ||
        !m_EnvironmentRenderPass ||
        !m_EnvironmentFramebuffer ||
        !m_HDRToCubemapPipeline ||
        !m_HDRToCubemapDescriptorManager ||
        !m_EnvironmentCubemap)
    {
        throw std::runtime_error(
            "Cannot generate environment cubemap because an IBL resource is missing."
        );
    }

    std::cout
        << "[IBL] Testing environment cubemap render pass clear..."
        << std::endl;

    /*constexpr uint32_t cubemapFaceCount =
        6;

    constexpr uint32_t cubemapSize =
        512;*/

    QueueFamilyIndices queueFamilyIndices =
        m_PhysicalDevice->FindQueueFamilies();

    if (!queueFamilyIndices.graphicsFamily.has_value())
    {
        throw std::runtime_error(
            "Cannot generate environment cubemap without a graphics queue family."
        );
    }

    VkCommandPoolCreateInfo commandPoolInfo{};

    commandPoolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    commandPoolInfo.queueFamilyIndex =
        queueFamilyIndices.graphicsFamily.value();

    commandPoolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice->GetHandle(),
        &commandPoolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create environment cubemap command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    try
    {
        VkCommandBufferAllocateInfo allocationInfo{};

        allocationInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        allocationInfo.commandPool =
            commandPool;

        allocationInfo.level =
            VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        allocationInfo.commandBufferCount =
            1;

        if (vkAllocateCommandBuffers(
            m_LogicalDevice->GetHandle(),
            &allocationInfo,
            &commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to allocate environment cubemap command buffer."
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
                "Failed to begin environment cubemap command buffer."
            );
        }

        /*
         * RenderPass diagnostic test:
         *
         * Each framebuffer points to one cubemap face.
         * Beginning the render pass clears that face to solid red.
         * No graphics pipeline or draw command is used.
         */
for (uint32_t faceIndex = 0;
    faceIndex < cubemapFaceCount;
    ++faceIndex)
{
    const glm::mat4 captureView =
        glm::lookAt(
            capturePosition,
            capturePosition +
                faceDirections[faceIndex],
            faceUpDirections[faceIndex]
        );

    const glm::mat4 viewProjection =
        captureProjection *
        captureView;

    /*
     * Clear to black.
     *
     * If the draw call works, the fixed-blue fragment shader
     * should overwrite the black framebuffer.
     */
    VkClearValue clearValue{};

    clearValue.color.float32[0] =
        0.0f;

    clearValue.color.float32[1] =
        0.0f;

    clearValue.color.float32[2] =
        0.0f;

    clearValue.color.float32[3] =
        1.0f;

    VkRenderPassBeginInfo renderPassBegin{};

    renderPassBegin.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    renderPassBegin.renderPass =
        m_EnvironmentRenderPass->GetHandle();

    renderPassBegin.framebuffer =
        m_EnvironmentFramebuffer->GetHandle(
            faceIndex
        );

    renderPassBegin.renderArea.offset =
    {
        0,
        0
    };

    renderPassBegin.renderArea.extent =
    {
        cubemapSize,
        cubemapSize
    };

    renderPassBegin.clearValueCount =
        1;

    renderPassBegin.pClearValues =
        &clearValue;

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassBegin,
        VK_SUBPASS_CONTENTS_INLINE
    );

    /*
     * Bind the HDR-to-cubemap graphics pipeline.
     */
    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_HDRToCubemapPipeline->GetPipeline()
    );

    const VkDescriptorSet descriptorSet =
        m_HDRToCubemapDescriptorManager
            ->GetDescriptorSet();

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_HDRToCubemapPipeline->GetLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    /*
     * Send this cubemap face's view-projection matrix
     * to the vertex shader.
     */
    vkCmdPushConstants(
        commandBuffer,
        m_HDRToCubemapPipeline->GetLayout(),
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(glm::mat4),
        &viewProjection
    );

    /*
     * The cube vertices are generated with gl_VertexIndex,
     * so no vertex buffer is bound.
     */
    vkCmdDraw(
        commandBuffer,
        36,
        1,
        0,
        0
    );

    vkCmdEndRenderPass(
        commandBuffer
    );
}

        if (vkEndCommandBuffer(
            commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to finish environment cubemap command buffer."
            );
        }

        VkSubmitInfo submitInfo{};

        submitInfo.sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount =
            1;

        submitInfo.pCommandBuffers =
            &commandBuffer;

        if (vkQueueSubmit(
            m_LogicalDevice->GetGraphicsQueue(),
            1,
            &submitInfo,
            VK_NULL_HANDLE
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to submit environment cubemap commands."
            );
        }

        if (vkQueueWaitIdle(
            m_LogicalDevice->GetGraphicsQueue()
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed while waiting for environment cubemap test."
            );
        }

        std::cout
            << "[IBL] Environment cubemap render pass clear completed."
            << std::endl;
    }
    catch (...)
    {
        if (commandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(
                m_LogicalDevice->GetHandle(),
                commandPool,
                1,
                &commandBuffer
            );
        }

        vkDestroyCommandPool(
            m_LogicalDevice->GetHandle(),
            commandPool,
            nullptr
        );

        throw;
    }

    vkFreeCommandBuffers(
        m_LogicalDevice->GetHandle(),
        commandPool,
        1,
        &commandBuffer
    );

    vkDestroyCommandPool(
        m_LogicalDevice->GetHandle(),
        commandPool,
        nullptr
    );
}

void VulkanContext::GeneratePrefilteredEnvironmentCubemap()
{
    constexpr uint32_t cubemapFaceCount =
        6;

    constexpr uint32_t baseCubemapSize =
        128;

    constexpr uint32_t mipLevelCount =
        8;

    if (!m_PhysicalDevice ||
        !m_LogicalDevice ||
        !m_EnvironmentRenderPass ||
        !m_PrefilterFramebuffer ||
        !m_PrefilterPipeline ||
        !m_PrefilterDescriptorManager ||
        !m_EnvironmentCubemap ||
        !m_PrefilteredEnvironmentCubemap)
    {
        throw std::runtime_error(
            "Cannot generate prefiltered environment cubemap because an IBL resource is missing."
        );
    }

    std::cout
        << "[IBL] Generating prefiltered environment cubemap..."
        << std::endl;

    // =========================================================
    // Cubemap capture directions
    // =========================================================

    const std::array<glm::vec3, cubemapFaceCount>
        faceDirections =
    {
        glm::vec3( 1.0f,  0.0f,  0.0f),
        glm::vec3(-1.0f,  0.0f,  0.0f),
        glm::vec3( 0.0f,  1.0f,  0.0f),
        glm::vec3( 0.0f, -1.0f,  0.0f),
        glm::vec3( 0.0f,  0.0f,  1.0f),
        glm::vec3( 0.0f,  0.0f, -1.0f)
    };

    const std::array<glm::vec3, cubemapFaceCount>
        faceUpDirections =
    {
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f,  0.0f,  1.0f),
        glm::vec3(0.0f,  0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f)
    };

    glm::mat4 captureProjection =
        glm::perspective(
            glm::radians(90.0f),
            1.0f,
            0.1f,
            10.0f
        );

    captureProjection[1][1] *=
        -1.0f;

    const glm::vec3 capturePosition =
        glm::vec3(0.0f);

    // =========================================================
    // Create one-time command pool
    // =========================================================

    QueueFamilyIndices queueFamilyIndices =
        m_PhysicalDevice->FindQueueFamilies();

    if (!queueFamilyIndices.graphicsFamily.has_value())
    {
        throw std::runtime_error(
            "Cannot generate prefiltered cubemap without a graphics queue family."
        );
    }

    VkCommandPoolCreateInfo commandPoolInfo{};

    commandPoolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    commandPoolInfo.queueFamilyIndex =
        queueFamilyIndices.graphicsFamily.value();

    commandPoolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice->GetHandle(),
        &commandPoolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create prefilter command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    try
    {
        // =====================================================
        // Allocate command buffer
        // =====================================================

        VkCommandBufferAllocateInfo allocationInfo{};

        allocationInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        allocationInfo.commandPool =
            commandPool;

        allocationInfo.level =
            VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        allocationInfo.commandBufferCount =
            1;

        if (vkAllocateCommandBuffers(
            m_LogicalDevice->GetHandle(),
            &allocationInfo,
            &commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to allocate prefilter command buffer."
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
                "Failed to begin prefilter command buffer."
            );
        }

        // =====================================================
        // Bind pipeline and source cubemap descriptor
        // =====================================================

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_PrefilterPipeline->GetPipeline()
        );

        const VkDescriptorSet descriptorSet =
            m_PrefilterDescriptorManager
                ->GetDescriptorSet();

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_PrefilterPipeline->GetLayout(),
            0,
            1,
            &descriptorSet,
            0,
            nullptr
        );

        // =====================================================
        // Render every mip and every face
        // =====================================================

        for (uint32_t mipLevel = 0;
            mipLevel < mipLevelCount;
            ++mipLevel)
        {
            const uint32_t mipSize =
                std::max(
                    1u,
                    baseCubemapSize >>
                    mipLevel
                );

            const float roughness =
                static_cast<float>(mipLevel) /
                static_cast<float>(
                    mipLevelCount - 1
                );

            VkViewport viewport{};

            viewport.x =
                0.0f;

            viewport.y =
                0.0f;

            viewport.width =
                static_cast<float>(mipSize);

            viewport.height =
                static_cast<float>(mipSize);

            viewport.minDepth =
                0.0f;

            viewport.maxDepth =
                1.0f;

            VkRect2D scissor{};

            scissor.offset =
            {
                0,
                0
            };

            scissor.extent =
            {
                mipSize,
                mipSize
            };

            vkCmdSetViewport(
                commandBuffer,
                0,
                1,
                &viewport
            );

            vkCmdSetScissor(
                commandBuffer,
                0,
                1,
                &scissor
            );

            for (uint32_t faceIndex = 0;
                faceIndex < cubemapFaceCount;
                ++faceIndex)
            {
                const glm::mat4 captureView =
                    glm::lookAt(
                        capturePosition,
                        capturePosition +
                            faceDirections[faceIndex],
                        faceUpDirections[faceIndex]
                    );

                PrefilterPushConstants pushConstants{};

                pushConstants.viewProjection =
                    captureProjection *
                    captureView;

                pushConstants.parameters =
                    glm::vec4(
                        roughness,
                        0.0f,
                        0.0f,
                        0.0f
                    );

                VkClearValue clearValue{};

                clearValue.color.float32[0] =
                    0.0f;

                clearValue.color.float32[1] =
                    0.0f;

                clearValue.color.float32[2] =
                    0.0f;

                clearValue.color.float32[3] =
                    1.0f;

                VkRenderPassBeginInfo renderPassBegin{};

                renderPassBegin.sType =
                    VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

                renderPassBegin.renderPass =
                    m_EnvironmentRenderPass
                        ->GetHandle();

                renderPassBegin.framebuffer =
                    m_PrefilterFramebuffer
                        ->GetHandle(
                            faceIndex,
                            mipLevel
                        );

                renderPassBegin.renderArea.offset =
                {
                    0,
                    0
                };

                renderPassBegin.renderArea.extent =
                {
                    mipSize,
                    mipSize
                };

                renderPassBegin.clearValueCount =
                    1;

                renderPassBegin.pClearValues =
                    &clearValue;

                vkCmdBeginRenderPass(
                    commandBuffer,
                    &renderPassBegin,
                    VK_SUBPASS_CONTENTS_INLINE
                );

                vkCmdPushConstants(
                    commandBuffer,
                    m_PrefilterPipeline->GetLayout(),
                    VK_SHADER_STAGE_VERTEX_BIT |
                    VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(PrefilterPushConstants),
                    &pushConstants
                );

                vkCmdDraw(
                    commandBuffer,
                    36,
                    1,
                    0,
                    0
                );

                vkCmdEndRenderPass(
                    commandBuffer
                );
            }

            std::cout
                << "[IBL] Prefilter mip "
                << mipLevel
                << " generated. Size: "
                << mipSize
                << " x "
                << mipSize
                << ", roughness: "
                << roughness
                << std::endl;
        }

        if (vkEndCommandBuffer(
            commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to finish prefilter command buffer."
            );
        }

        // =====================================================
        // Submit and wait
        // =====================================================

        VkSubmitInfo submitInfo{};

        submitInfo.sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount =
            1;

        submitInfo.pCommandBuffers =
            &commandBuffer;

        if (vkQueueSubmit(
            m_LogicalDevice->GetGraphicsQueue(),
            1,
            &submitInfo,
            VK_NULL_HANDLE
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to submit prefilter commands."
            );
        }

        if (vkQueueWaitIdle(
            m_LogicalDevice->GetGraphicsQueue()
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed while waiting for prefilter generation."
            );
        }

        std::cout
            << "[IBL] Prefiltered environment cubemap generated successfully."
            << std::endl;
    }
    catch (...)
    {
        if (commandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(
                m_LogicalDevice->GetHandle(),
                commandPool,
                1,
                &commandBuffer
            );
        }

        vkDestroyCommandPool(
            m_LogicalDevice->GetHandle(),
            commandPool,
            nullptr
        );

        throw;
    }

    vkFreeCommandBuffers(
        m_LogicalDevice->GetHandle(),
        commandPool,
        1,
        &commandBuffer
    );

    vkDestroyCommandPool(
        m_LogicalDevice->GetHandle(),
        commandPool,
        nullptr
    );
}

void VulkanContext::GenerateIrradianceCubemap()
{
    constexpr uint32_t cubemapFaceCount =
        6;

    constexpr uint32_t cubemapSize =
        128;

    if (!m_PhysicalDevice ||
        !m_LogicalDevice ||
        !m_IrradianceRenderPass ||
        !m_IrradianceFramebuffer ||
        !m_IrradiancePipeline ||
        !m_IrradianceDescriptorManager ||
        !m_EnvironmentCubemap ||
        !m_IrradianceCubemap)
    {
        throw std::runtime_error(
            "Cannot generate irradiance cubemap because an IBL resource is missing."
        );
    }

    std::cout
        << "[IBL] Generating irradiance cubemap..."
        << std::endl;

    // =========================================================
    // Cubemap capture directions
    // =========================================================

    const std::array<glm::vec3, cubemapFaceCount>
        faceDirections =
    {
        glm::vec3( 1.0f,  0.0f,  0.0f),
        glm::vec3(-1.0f,  0.0f,  0.0f),
        glm::vec3( 0.0f,  1.0f,  0.0f),
        glm::vec3( 0.0f, -1.0f,  0.0f),
        glm::vec3( 0.0f,  0.0f,  1.0f),
        glm::vec3( 0.0f,  0.0f, -1.0f)
    };

    const std::array<glm::vec3, cubemapFaceCount>
        faceUpDirections =
    {
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f,  0.0f,  1.0f),
        glm::vec3(0.0f,  0.0f, -1.0f),
        glm::vec3(0.0f, -1.0f,  0.0f),
        glm::vec3(0.0f, -1.0f,  0.0f)
    };

    glm::mat4 captureProjection =
        glm::perspective(
            glm::radians(90.0f),
            1.0f,
            0.1f,
            10.0f
        );

    captureProjection[1][1] *= -1.0f;

    const glm::vec3 capturePosition =
        glm::vec3(0.0f);

    // =========================================================
    // Create one-time command pool
    // =========================================================

    QueueFamilyIndices queueFamilyIndices =
        m_PhysicalDevice->FindQueueFamilies();

    if (!queueFamilyIndices.graphicsFamily.has_value())
    {
        throw std::runtime_error(
            "Cannot generate irradiance cubemap without a graphics queue family."
        );
    }

    VkCommandPoolCreateInfo commandPoolInfo{};

    commandPoolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    commandPoolInfo.queueFamilyIndex =
        queueFamilyIndices.graphicsFamily.value();

    commandPoolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice->GetHandle(),
        &commandPoolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create irradiance command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    try
    {
        // =====================================================
        // Allocate command buffer
        // =====================================================

        VkCommandBufferAllocateInfo allocationInfo{};

        allocationInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        allocationInfo.commandPool =
            commandPool;

        allocationInfo.level =
            VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        allocationInfo.commandBufferCount =
            1;

        if (vkAllocateCommandBuffers(
            m_LogicalDevice->GetHandle(),
            &allocationInfo,
            &commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to allocate irradiance command buffer."
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
                "Failed to begin irradiance command buffer."
            );
        }
                // =====================================================
        // Bind irradiance pipeline and source cubemap descriptor
        // =====================================================

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_IrradiancePipeline->GetPipeline()
        );

        const VkDescriptorSet descriptorSet =
    m_IrradianceDescriptorManager
        ->GetDescriptorSet();

for (uint32_t faceIndex = 0;
     faceIndex < cubemapFaceCount;
     ++faceIndex)
{
    VkRenderPassBeginInfo renderPassBeginInfo{};

    renderPassBeginInfo.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

    renderPassBeginInfo.renderPass =
        m_IrradianceRenderPass->GetHandle();

    renderPassBeginInfo.framebuffer =
        m_IrradianceFramebuffer
            ->GetHandle(faceIndex);

    renderPassBeginInfo.renderArea.offset =
    {
        0,
        0
    };

    renderPassBeginInfo.renderArea.extent =
    {
        cubemapSize,
        cubemapSize
    };

    VkClearValue clearValue{};

    clearValue.color =
    {
        {
            1.0f,
            0.0f,
            1.0f,
            1.0f
        }
    };

    renderPassBeginInfo.clearValueCount =
        1;

    renderPassBeginInfo.pClearValues =
        &clearValue;

    vkCmdBeginRenderPass(
        commandBuffer,
        &renderPassBeginInfo,
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_IrradiancePipeline->GetPipeline()
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        m_IrradiancePipeline->GetLayout(),
        0,
        1,
        &descriptorSet,
        0,
        nullptr
    );

    // Push face capture matrix here.

    vkCmdDraw(
        commandBuffer,
        36,
        1,
        0,
        0
    );

    vkCmdEndRenderPass(
        commandBuffer
    );

            std::cout
                << "[IBL] Irradiance cubemap face "
                << faceIndex
                << " generated."
                << std::endl;
        }
                // =====================================================
        // Finish command buffer recording
        // =====================================================

        if (vkEndCommandBuffer(
            commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to finish irradiance command buffer."
            );
        }

        // =====================================================
        // Submit irradiance generation commands
        // =====================================================

        VkSubmitInfo submitInfo{};

        submitInfo.sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount =
            1;

        submitInfo.pCommandBuffers =
            &commandBuffer;

        if (vkQueueSubmit(
            m_LogicalDevice->GetGraphicsQueue(),
            1,
            &submitInfo,
            VK_NULL_HANDLE
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to submit irradiance generation commands."
            );
        }

        if (vkQueueWaitIdle(
            m_LogicalDevice->GetGraphicsQueue()
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed while waiting for irradiance cubemap generation."
            );
        }

        std::cout
            << "[IBL] Irradiance cubemap generated successfully. "
            << "Resolution: "
            << cubemapSize
            << " x "
            << cubemapSize
            << ", Faces: "
            << cubemapFaceCount
            << "."
            << std::endl;
    }
    catch (...)
    {
        if (commandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(
                m_LogicalDevice->GetHandle(),
                commandPool,
                1,
                &commandBuffer
            );

            commandBuffer =
                VK_NULL_HANDLE;
        }

        if (commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(
                m_LogicalDevice->GetHandle(),
                commandPool,
                nullptr
            );

            commandPool =
                VK_NULL_HANDLE;
        }

        std::cerr
            << "[IBL] Irradiance cubemap generation failed."
            << std::endl;

        throw;
    }

    // =========================================================
    // Release temporary command resources
    // =========================================================

    if (commandBuffer != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(
            m_LogicalDevice->GetHandle(),
            commandPool,
            1,
            &commandBuffer
        );

        commandBuffer =
            VK_NULL_HANDLE;
    }

    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(
            m_LogicalDevice->GetHandle(),
            commandPool,
            nullptr
        );

        commandPool =
            VK_NULL_HANDLE;
    }

    std::cout
        << "[IBL] Irradiance generation command resources released."
        << std::endl;
}

VulkanContext::~VulkanContext()
{
    m_Renderer.reset();
    m_CommandManager.reset();
    m_DescriptorManager.reset();

    m_ShadowPipeline.reset();
    m_SkyboxPipeline.reset();
    m_GraphicsPipeline.reset();

    m_DescriptorSetLayout.reset();

    m_TextureImage.reset();
    
    m_HDRToCubemapDescriptorManager.reset();
    m_PrefilterDescriptorManager.reset();

    m_HDRToCubemapPipeline.reset();
    m_PrefilterPipeline.reset();
    
    m_EnvironmentFramebuffer.reset();
    m_PrefilterFramebuffer.reset();
    m_EnvironmentRenderPass.reset();

    m_BRDFLUTPipeline.reset();
    m_BRDFLUTFramebuffer.reset();
    m_BRDFLUTRenderPass.reset();
    m_BRDFLUT.reset();

    m_PrefilteredEnvironmentCubemap.reset();
    m_EnvironmentCubemap.reset();
    m_HDRTextureImage.reset();

    m_IndexBuffer.reset();
    m_VertexBuffer.reset();

    m_Framebuffer.reset();
    m_DepthImage.reset();
    m_RenderPass.reset();

    m_ShadowFramebuffer.reset();
    m_ShadowRenderPass.reset();
    m_ShadowDepthImage.reset();

    m_Swapchain.reset();

    if (m_ShadowSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(
            m_LogicalDevice->GetHandle(),
            m_ShadowSampler,
            nullptr
        );

        m_ShadowSampler = VK_NULL_HANDLE;
    }

    m_LogicalDevice.reset();
    m_PhysicalDevice.reset();

    if (m_Instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }
}

void VulkanContext::CreateSurface()
{
    VkResult result = glfwCreateWindowSurface(
        m_Instance,
        m_Window.GetHandle(),
        nullptr,
        &m_Surface
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface.");
    }

    std::cout << "Window surface created successfully." << std::endl;
}

void VulkanContext::CreateInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Rendering Lab";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Mini Vulkan Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);

    if (!extensions || extensionCount == 0)
    {
        throw std::runtime_error("Failed to get required GLFW Vulkan extensions.");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance.");
    }
}

void VulkanContext::GenerateBRDFLUT()
{
    constexpr uint32_t brdfLUTSize =
        512;

    std::cout
        << "[IBL] Generating BRDF integration LUT..."
        << std::endl;

    QueueFamilyIndices queueFamilyIndices =
        m_PhysicalDevice->FindQueueFamilies();

    if (!queueFamilyIndices.graphicsFamily.has_value())
    {
        throw std::runtime_error(
            "Cannot generate BRDF LUT without a graphics queue family."
        );
    }

    // =========================================================
    // Create temporary command pool
    // =========================================================

    VkCommandPoolCreateInfo commandPoolInfo{};

    commandPoolInfo.sType =
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;

    commandPoolInfo.flags =
        VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    commandPoolInfo.queueFamilyIndex =
        queueFamilyIndices.graphicsFamily.value();

    VkCommandPool commandPool =
        VK_NULL_HANDLE;

    if (vkCreateCommandPool(
        m_LogicalDevice->GetHandle(),
        &commandPoolInfo,
        nullptr,
        &commandPool
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create BRDF LUT command pool."
        );
    }

    VkCommandBuffer commandBuffer =
        VK_NULL_HANDLE;

    try
    {
        // =====================================================
        // Allocate command buffer
        // =====================================================

        VkCommandBufferAllocateInfo
            commandBufferAllocateInfo{};

        commandBufferAllocateInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;

        commandBufferAllocateInfo.commandPool =
            commandPool;

        commandBufferAllocateInfo.level =
            VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        commandBufferAllocateInfo.commandBufferCount =
            1;

        if (vkAllocateCommandBuffers(
            m_LogicalDevice->GetHandle(),
            &commandBufferAllocateInfo,
            &commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to allocate BRDF LUT command buffer."
            );
        }

        // =====================================================
        // Begin one-time command buffer
        // =====================================================

        VkCommandBufferBeginInfo
            commandBufferBeginInfo{};

        commandBufferBeginInfo.sType =
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        commandBufferBeginInfo.flags =
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        if (vkBeginCommandBuffer(
            commandBuffer,
            &commandBufferBeginInfo
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to begin BRDF LUT command buffer."
            );
        }

        // =====================================================
        // Begin BRDF LUT render pass
        // =====================================================

        VkClearValue clearValue{};

        clearValue.color =
        {
            {
                0.0f,
                0.0f,
                0.0f,
                1.0f
            }
        };

        VkRenderPassBeginInfo
            renderPassBeginInfo{};

        renderPassBeginInfo.sType =
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;

        renderPassBeginInfo.renderPass =
            m_BRDFLUTRenderPass->GetHandle();

        renderPassBeginInfo.framebuffer =
            m_BRDFLUTFramebuffer->GetHandle();

        renderPassBeginInfo.renderArea.offset =
        {
            0,
            0
        };

        renderPassBeginInfo.renderArea.extent =
        {
            brdfLUTSize,
            brdfLUTSize
        };

        renderPassBeginInfo.clearValueCount =
            1;

        renderPassBeginInfo.pClearValues =
            &clearValue;

        vkCmdBeginRenderPass(
            commandBuffer,
            &renderPassBeginInfo,
            VK_SUBPASS_CONTENTS_INLINE
        );

        // =====================================================
        // Bind BRDF LUT pipeline
        // =====================================================

        vkCmdBindPipeline(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            m_BRDFLUTPipeline->GetPipeline()
        );

        // =====================================================
        // Draw full-screen triangle
        //
        // The vertex shader generates all positions using
        // gl_VertexIndex, so no vertex buffer is required.
        // =====================================================

        vkCmdDraw(
            commandBuffer,
            3,
            1,
            0,
            0
        );

        vkCmdEndRenderPass(
            commandBuffer
        );

        // The BRDF LUT render pass automatically transitions:
        //
        // COLOR_ATTACHMENT_OPTIMAL
        //              ↓
        // SHADER_READ_ONLY_OPTIMAL

        if (vkEndCommandBuffer(
            commandBuffer
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to finish BRDF LUT command buffer."
            );
        }

        // =====================================================
        // Submit generation commands
        // =====================================================

        VkSubmitInfo submitInfo{};

        submitInfo.sType =
            VK_STRUCTURE_TYPE_SUBMIT_INFO;

        submitInfo.commandBufferCount =
            1;

        submitInfo.pCommandBuffers =
            &commandBuffer;

        if (vkQueueSubmit(
            m_LogicalDevice->GetGraphicsQueue(),
            1,
            &submitInfo,
            VK_NULL_HANDLE
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed to submit BRDF LUT generation commands."
            );
        }

        if (vkQueueWaitIdle(
            m_LogicalDevice->GetGraphicsQueue()
        ) != VK_SUCCESS)
        {
            throw std::runtime_error(
                "Failed while waiting for BRDF LUT generation."
            );
        }

        vkFreeCommandBuffers(
            m_LogicalDevice->GetHandle(),
            commandPool,
            1,
            &commandBuffer
        );

        commandBuffer =
            VK_NULL_HANDLE;

        vkDestroyCommandPool(
            m_LogicalDevice->GetHandle(),
            commandPool,
            nullptr
        );

        commandPool =
            VK_NULL_HANDLE;
    }
    catch (...)
    {
        if (commandBuffer != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(
                m_LogicalDevice->GetHandle(),
                commandPool,
                1,
                &commandBuffer
            );
        }

        if (commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(
                m_LogicalDevice->GetHandle(),
                commandPool,
                nullptr
            );
        }

        throw;
    }

    std::cout
        << "[IBL] BRDF integration LUT generated successfully."
        << std::endl;
}

void VulkanContext::CreateShadowSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType =
        VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;

    samplerInfo.addressModeU =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.addressModeV =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;

    samplerInfo.borderColor =
        VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    // We will manually compare depths in the shader first.
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    samplerInfo.mipmapMode =
        VK_SAMPLER_MIPMAP_MODE_LINEAR;

    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(
        m_LogicalDevice->GetHandle(),
        &samplerInfo,
        nullptr,
        &m_ShadowSampler
    ) != VK_SUCCESS)
    {
        throw std::runtime_error(
            "Failed to create shadow map sampler."
        );
    }

    std::cout
        << "[Shadow] Shadow sampler created successfully."
        << std::endl;
}

void VulkanContext::UpdateUniformBuffer()
{
    UniformBufferObject ubo{};

    // =========================================================
    // Model
    // =========================================================

    ubo.model = glm::scale(
        glm::mat4(1.0f),
        glm::vec3(0.35f)
    );

    // =========================================================
    // Orbit camera
    // =========================================================

    const glm::vec3 cameraTarget(
        0.0f,
        0.8f,
        0.0f
    );

    const float cosPitch =
        std::cos(m_CameraPitch);

    glm::vec3 cameraPosition{};

    cameraPosition.x =
        cameraTarget.x +
        m_CameraDistance *
        cosPitch *
        std::sin(m_CameraYaw);

    cameraPosition.y =
        cameraTarget.y +
        m_CameraDistance *
        std::sin(m_CameraPitch);

    cameraPosition.z =
        cameraTarget.z +
        m_CameraDistance *
        cosPitch *
        std::cos(m_CameraYaw);

    ubo.view = glm::lookAt(
        cameraPosition,
        cameraTarget,
        glm::vec3(0.0f, 1.0f, 0.0f)
    );

    ubo.cameraPosition =
        glm::vec4(
            cameraPosition,
            1.0f
        );

    // =========================================================
    // Main camera projection
    // =========================================================

    const float aspect =
        m_Swapchain->GetExtent().width /
        static_cast<float>(
            m_Swapchain->GetExtent().height
            );

    ubo.proj = glm::perspective(
        glm::radians(45.0f),
        aspect,
        0.1f,
        100.0f
    );

    ubo.proj[1][1] *= -1.0f;

    // =========================================================
    // Shadow mode
    // =========================================================

    ubo.shadowSettings = glm::ivec4(
        m_ShadowMode,
        m_EnableEnvironmentLighting ? 1 : 0,

        // Store exposure as an integer multiplied by 100.
        static_cast<int>(m_Exposure * 100.0f),

        0
    );

    // =========================================================
    // Orbiting / manually controlled light
    // =========================================================

    const double currentTime =
        glfwGetTime();

    if (m_LastLightUpdateTime == 0.0)
    {
        m_LastLightUpdateTime =
            currentTime;
    }

    const float deltaTime =
        static_cast<float>(
            currentTime -
            m_LastLightUpdateTime
            );

    m_LastLightUpdateTime =
        currentTime;

    if (m_PointLightAnimationEnabled &&
        !m_ManualPointLightControl)
    {
        constexpr float lightSpeed = 0.8f;

        m_PointLightAngle +=
            deltaTime * lightSpeed;
    }

    constexpr float lightRadius = 2.8f;
    constexpr float lightHeight = 2.5f;

    glm::vec3 pointLightPosition{};

    if (m_ManualPointLightControl)
    {
        pointLightPosition =
            m_ManualPointLightPosition;
    }
    else
    {
        pointLightPosition =
            glm::vec3(
                std::cos(m_PointLightAngle) *
                lightRadius,

                lightHeight,

                std::sin(m_PointLightAngle) *
                lightRadius
            );

        m_ManualPointLightPosition =
            pointLightPosition;
    }

    ubo.pointLight.position =
        glm::vec4(
            pointLightPosition,
            1.0f
        );

    ubo.pointLight.color =
        glm::vec4(
            1.0f,
            0.82f,
            0.68f,
            m_PointLightEnabled
            ? 7.0f
            : 0.0f
        );

    // =========================================================
    // Shadow camera follows the same moving light
    // =========================================================

    const glm::vec3 shadowTarget(
        0.0f,
        0.8f,
        0.0f
    );

    const glm::vec3 fixedLightPosition(
        0.0f,
        2.2f,
        3.5f
    );

    glm::vec3 shadowLightPosition =
        m_PointLightEnabled
        ? pointLightPosition
        : fixedLightPosition;

    glm::mat4 lightView =
        glm::lookAt(
            shadowLightPosition,
            shadowTarget,
            glm::vec3(0.0f, 1.0f, 0.0f)
        );

    glm::mat4 lightProjection =
        glm::perspective(
            glm::radians(70.0f),
            1.0f,
            0.1f,
            12.0f
        );

    lightProjection[1][1] *= -1.0f;

    ubo.lightViewProj =
        lightProjection *
        lightView;

    // =========================================================
    // Upload
    // =========================================================

    m_UniformBuffer->UploadData(
        &ubo,
        sizeof(ubo)
    );
}

void VulkanContext::ToggleSkybox()
{
    m_EnableSkybox =
        !m_EnableSkybox;

    // For now, skybox visibility and environment lighting
    // are controlled together as one rendering mode.
    m_EnableEnvironmentLighting =
        m_EnableSkybox;

    if (m_CommandManager)
    {
        m_CommandManager->SetSkyboxEnabled(
            m_EnableSkybox
        );
    }

    std::cout
        << "[Lighting] Skybox: "
        << (m_EnableSkybox ? "ON" : "OFF")
        << std::endl;

    std::cout
        << "[Lighting] Environment Lighting: "
        << (
            m_EnableEnvironmentLighting
            ? "ON"
            : "OFF"
        )
        << std::endl;
}

void VulkanContext::DrawFrame()
{
    std::cout << "[Context] Before DrawFrame" << std::endl;

    m_Renderer->DrawFrame();

    std::cout << "[Context] After DrawFrame" << std::endl;
}