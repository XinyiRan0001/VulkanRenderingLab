#include "InstancedScene.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <stdexcept>

void AppendInstancedMesh(MeshData &scene, const MeshData &mesh, const std::vector<InstanceData> &instances)
{
    if (instances.empty() || mesh.vertices.empty() || mesh.indices.empty())
        throw std::runtime_error("Instanced batch requires geometry and instances");
    const auto vertexOffset = static_cast<uint32_t>(scene.vertices.size());
    const auto indexOffset = static_cast<uint32_t>(scene.indices.size());
    const auto firstInstance = static_cast<uint32_t>(scene.instances.size());
    const auto materialOffset = static_cast<int>(scene.materials.size());
    const auto imageOffset = static_cast<int>(scene.images.size());
    const auto limit = std::numeric_limits<uint32_t>::max();
    if (scene.vertices.size() + mesh.vertices.size() > limit ||
        scene.indices.size() + mesh.indices.size() > limit ||
        scene.instances.size() + instances.size() > limit)
        throw std::runtime_error("Instanced batch exceeds 32-bit draw addressing");
    for (uint32_t index : mesh.indices)
        if (index >= mesh.vertices.size())
            throw std::runtime_error("Invalid mesh index");
    for (const auto &sub : mesh.subMeshes)
        if (uint64_t(sub.firstIndex) + sub.indexCount > mesh.indices.size() || sub.materialId < -1 ||
            sub.materialId >= static_cast<int>(mesh.materials.size()))
            throw std::runtime_error("Invalid submesh range/material");

    glm::vec3 minimum(std::numeric_limits<float>::max()), maximum(std::numeric_limits<float>::lowest());
    for (const auto &vertex : mesh.vertices)
    {
        glm::vec3 p(vertex.position[0], vertex.position[1], vertex.position[2]);
        minimum = glm::min(minimum, p);
        maximum = glm::max(maximum, p);
    }
    const glm::vec3 center = (minimum + maximum) * 0.5f;
    float radius = 0.0f;
    for (const auto &vertex : mesh.vertices)
        radius = glm::max(
            radius,
            glm::length(glm::vec3(vertex.position[0], vertex.position[1], vertex.position[2]) - center));
    const glm::vec4 localSphere(center, radius);
    scene.vertices.insert(scene.vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
    for (uint32_t index : mesh.indices)
        scene.indices.push_back(vertexOffset + index);
    scene.images.insert(scene.images.end(), mesh.images.begin(), mesh.images.end());
    for (auto material : mesh.materials)
    {
        for (auto &texture : material.textures)
            if (texture >= 0)
                texture += imageOffset;
        scene.materials.push_back(material);
    }
    scene.instances.insert(scene.instances.end(), instances.begin(), instances.end());
    for (auto sub : mesh.subMeshes)
    {
        sub.firstIndex += indexOffset;
        if (sub.materialId >= 0)
            sub.materialId += materialOffset;
        sub.firstInstance = firstInstance;
        sub.localSphere = localSphere;
        sub.instanceCount = static_cast<uint32_t>(instances.size());
        scene.subMeshes.push_back(sub);
    }
}

std::vector<InstanceData> CreateMaterialInstances(uint32_t stressCount)
{
    if (stressCount > 10000)
        throw std::runtime_error("Stress count is limited to 10000");
    const uint32_t count = stressCount ? stressCount : 10;
    const uint32_t side = static_cast<uint32_t>(std::ceil(std::sqrt(float(count))));
    const float roughness[] = {0.02f, 0.20f, 0.45f, 0.70f, 1.0f};
    std::vector<InstanceData> instances;
    instances.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto row = i / 5, column = i % 5;
        glm::vec3 position = stressCount
                                 ? glm::vec3((float(i % side) - float(side - 1) * 0.5f) * 1.5f, 0.85f,
                                             (float(i / side) - float(side - 1) * 0.5f) * 1.5f)
                                 : glm::vec3(1.9f + float(column) * 1.5f, 2.55f - float(row) * 1.7f, -0.3f);
        InstanceData instance;
        // GLM uses column vectors: scaling happens first, then translation.
        instance.model = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(0.65f));
        instance.materialProperties =
            glm::vec4(stressCount ? float((i / side) % 2) : float(row), roughness[column], 1.0f, 1.0f);
        instances.push_back(instance);
    }
    return instances;
}

MeshData CreateStressSphere()
{
    constexpr uint32_t slices = 16, stacks = 8;
    constexpr float pi = 3.14159265359f;
    MeshData mesh;
    for (uint32_t y = 0; y <= stacks; ++y)
        for (uint32_t x = 0; x <= slices; ++x)
        {
            const float theta = pi * float(y) / stacks, phi = 2.0f * pi * float(x) / slices;
            Vertex vertex{};
            vertex.position[0] = vertex.normal[0] = std::sin(theta) * std::cos(phi);
            vertex.position[1] = vertex.normal[1] = std::cos(theta);
            vertex.position[2] = vertex.normal[2] = std::sin(theta) * std::sin(phi);
            for (float &channel : vertex.color)
                channel = 1.0f;
            vertex.texCoord[0] = float(x) / slices;
            vertex.texCoord[1] = float(y) / stacks;
            mesh.vertices.push_back(vertex);
        }
    for (uint32_t y = 0; y < stacks; ++y)
        for (uint32_t x = 0; x < slices; ++x)
        {
            const uint32_t a = y * (slices + 1) + x, b = a + slices + 1;
            // The top/bottom degenerate triangles are omitted.
            if (y != 0)
                mesh.indices.insert(mesh.indices.end(), {a, a + 1, b});
            if (y != stacks - 1)
                mesh.indices.insert(mesh.indices.end(), {a + 1, b + 1, b});
        }
    MaterialData material;
    material.diffuseColor[0] = 0.796f;
    material.diffuseColor[1] = 0.62f;
    material.diffuseColor[2] = 0.482f;
    mesh.materials.push_back(material);
    mesh.subMeshes.push_back({0, static_cast<uint32_t>(mesh.indices.size()), 0});
    return mesh;
}

void AppendOcclusionWall(MeshData &scene)
{
    const auto firstIndex = static_cast<uint32_t>(scene.indices.size());
    const auto materialId = static_cast<int>(scene.materials.size());
    const glm::vec3 center(0.0f, 3.5f, 6.0f), half(7.0f, 3.5f, .25f);
    const glm::vec3 normals[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto &normal : normals)
    {
        const glm::vec3 tangent = std::abs(normal.y) > .5f
                                      ? glm::vec3(1, 0, 0)
                                      : glm::normalize(glm::cross(glm::vec3(0, 1, 0), normal));
        const auto bitangent = glm::cross(normal, tangent);
        const uint32_t start = static_cast<uint32_t>(scene.vertices.size());
        for (const glm::vec2 uv : {glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1)})
        {
            const auto p =
                center + normal * half + (uv.x * 2 - 1) * tangent * half + (uv.y * 2 - 1) * bitangent * half;
            Vertex vertex{};
            for (int i = 0; i < 3; ++i)
            {
                vertex.position[i] = p[i];
                vertex.normal[i] = normal[i];
                vertex.color[i] = 1.0f;
            }
            scene.vertices.push_back(vertex);
        }
        scene.indices.insert(scene.indices.end(), {start, start + 1, start + 2, start, start + 2, start + 3});
    }
    MaterialData material;
    material.diffuseColor[0] = .15f;
    material.diffuseColor[1] = .30f;
    material.diffuseColor[2] = .45f;
    material.roughness = .8f;
    scene.materials.push_back(material);
    SubMesh wall{firstIndex, 36, materialId};
    wall.occluder = true;
    scene.subMeshes.push_back(wall);
}
