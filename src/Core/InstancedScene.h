#pragma once
#include "MeshData.h"

// Appends geometry once and attaches a contiguous range of instance records.
void AppendInstancedMesh(MeshData &scene, const MeshData &mesh, const std::vector<InstanceData> &instances);

// Low-detail geometry for instance-count stress testing; not the quality-demo model.
MeshData CreateStressSphere();
std::vector<InstanceData> CreateMaterialInstances(uint32_t stressCount = 0);

void AppendOcclusionWall(MeshData &scene);
