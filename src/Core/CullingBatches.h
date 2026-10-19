#pragma once
#include "MeshData.h"
#include <string>

// Each group owns disjoint instance, visible-output, command and statistics ranges.
// Material submeshes with the same input range share a visibility decision.
struct alignas(16) CullingBatchGPU
{
    glm::vec4 localSphere{};
    glm::uvec4 instances{}; // input first, input count, visible base, reserved
    glm::uvec4 commands{};  // command first, command count, stats index, reserved
};
static_assert(sizeof(CullingBatchGPU) == 48, "std430 batch stride mismatch");
struct CullingDrawAddress
{
    uint32_t command, visibleBase, group;
};
struct CullingBatchLayout
{
    std::vector<CullingBatchGPU> batches;
    std::vector<std::string> names;
    std::vector<VkDrawIndexedIndirectCommand> commands;
    // In original instanced-submesh traversal order, even if groups are interleaved.
    std::vector<CullingDrawAddress> draws;
    uint32_t totalInstances = 0, maxGroupInstances = 0;
};
CullingBatchLayout BuildCullingBatches(const MeshData &mesh);
