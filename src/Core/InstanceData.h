#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include <type_traits>

// Matches GLSL std430: a 64-byte mat4 followed by one 16-byte vec4.
// Keep instance 0 as identity for the character, floor and imported geometry.
struct alignas(16) InstanceData
{
    glm::mat4 model{1.0f};
    // xyz = metallic, roughness, AO; w = whether these override the mesh material.
    glm::vec4 materialProperties{0.0f};
};
static_assert(std::is_standard_layout<InstanceData>::value, "GPU data must have a stable layout");
static_assert(offsetof(InstanceData, materialProperties) == 64, "std430 member offset mismatch");
static_assert(sizeof(InstanceData) == 80, "std430 array stride mismatch");
