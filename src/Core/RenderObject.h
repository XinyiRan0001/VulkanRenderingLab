#pragma once

#include <cstdint>

#include <glm/glm.hpp>

// =========================================================
// A renderable object in the scene.
//
// A RenderObject does not own vertex or index data.
// It only references a range inside the shared mesh buffers.
//
// firstIndex / indexCount:
//     Select which part of the shared index buffer to draw.
//
// materialId:
//     Select which material should be used.
//
// modelMatrix:
//     Places the object in world space.
// =========================================================

struct RenderObject
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;

    int materialId = -1;

    glm::mat4 modelMatrix =
        glm::mat4(1.0f);
};