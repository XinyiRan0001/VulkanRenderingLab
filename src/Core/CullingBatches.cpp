#include "CullingBatches.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

CullingBatchLayout BuildCullingBatches(const MeshData &mesh)
{
    CullingBatchLayout result;
    struct DrawRef
    {
        uint32_t group;
        const SubMesh *sub;
    };
    std::vector<DrawRef> refs;
    for (const auto &sub : mesh.subMeshes)
    {
        if (!sub.firstInstance)
            continue; // identity instance 0 remains on the direct path
        if (!sub.instanceCount || uint64_t(sub.firstInstance) + sub.instanceCount > mesh.instances.size() ||
            !sub.indexCount || uint64_t(sub.firstIndex) + sub.indexCount > mesh.indices.size())
            throw std::runtime_error("Invalid instanced draw input range");
        for (int i = 0; i < 4; ++i)
            if (!std::isfinite(sub.localSphere[i]))
                throw std::runtime_error("Nonfinite prototype bounds");
        if (sub.localSphere.w <= 0)
            throw std::runtime_error("Prototype sphere must have positive radius");
        uint32_t group = uint32_t(result.batches.size());
        for (uint32_t i = 0; i < result.batches.size(); ++i)
        {
            const auto &g = result.batches[i];
            if (g.instances.x == sub.firstInstance && g.instances.y == sub.instanceCount)
            {
                if (g.localSphere != sub.localSphere)
                    throw std::runtime_error("Material ranges disagree on prototype bounds");
                group = i;
                break;
            }
            if (uint64_t(sub.firstInstance) < uint64_t(g.instances.x) + g.instances.y &&
                uint64_t(g.instances.x) < uint64_t(sub.firstInstance) + sub.instanceCount)
                throw std::runtime_error("Distinct culling groups must not overlap instance ranges");
        }
        if (group == result.batches.size())
        {
            if (uint64_t(result.totalInstances) + sub.instanceCount >
                uint64_t(std::numeric_limits<int32_t>::max()))
                throw std::runtime_error("Visible output exceeds push-constant addressing");
            CullingBatchGPU g;
            g.localSphere = sub.localSphere;
            g.instances = {sub.firstInstance, sub.instanceCount, result.totalInstances, 0};
            g.commands.z = group;
            result.batches.push_back(g);
            result.names.push_back(sub.instanceGroupName.empty() ? "group" + std::to_string(group)
                                                                 : sub.instanceGroupName);
            result.totalInstances += sub.instanceCount;
            result.maxGroupInstances = std::max(result.maxGroupInstances, sub.instanceCount);
        }
        refs.push_back({group, &sub});
    }
    if (result.batches.empty())
        throw std::runtime_error("No instanced culling groups");
    result.draws.resize(refs.size());
    for (uint32_t group = 0; group < result.batches.size(); ++group)
    {
        auto &g = result.batches[group];
        g.commands.x = uint32_t(result.commands.size());
        for (uint32_t draw = 0; draw < refs.size(); ++draw)
            if (refs[draw].group == group)
            {
                const auto &sub = *refs[draw].sub;
                result.draws[draw] = {uint32_t(result.commands.size()), g.instances.z, group};
                result.commands.push_back({sub.indexCount, 0, sub.firstIndex, 0, 0});
                ++g.commands.y;
            }
    }
    if (result.commands.size() * sizeof(VkDrawIndexedIndirectCommand) > 65536)
        throw std::runtime_error("Indirect templates exceed vkCmdUpdateBuffer capacity");
    return result;
}
