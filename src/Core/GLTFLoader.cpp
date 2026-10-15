#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <stb_image.h>
#include "GLTFLoader.h"
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <climits>

namespace {
void Require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string("glTF: ") + message);
}
glm::vec3 SafeNormal(glm::vec3 v, glm::vec3 fallback) {
    return glm::dot(v,v) > 1e-12f ? glm::normalize(v) : fallback;
}
}

MeshData GLTFLoader::Load(const std::string& path) {
    cgltf_options options{};
    cgltf_data* raw = nullptr;
    Require(cgltf_parse_file(&options, path.c_str(), &raw) == cgltf_result_success, "cannot parse file");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> data(raw, cgltf_free);
    Require(cgltf_load_buffers(&options, raw, path.c_str()) == cgltf_result_success, "cannot load buffers");
    Require(cgltf_validate(raw) == cgltf_result_success, "invalid document");
    Require(raw->extensions_required_count == 0, "required extensions are not supported");
    Require(raw->animations_count == 0, "animations are not supported");
    MeshData result;
    auto image = [&](const cgltf_texture_view& view) -> int {
        if (!view.texture) return -1;
        Require(view.texcoord == 0 && !view.has_transform, "only untransformed TEXCOORD_0 is supported");
        auto* source = view.texture->image;
        Require(source != nullptr, "texture has no image");
        // Preserve core glTF sampler defaults; reject unsupported custom behavior explicitly.
        if (view.texture->sampler) {
            const auto& s = *view.texture->sampler;
            Require(s.wrap_s == 10497 && s.wrap_t == 10497 &&
                (s.mag_filter == 0 || s.mag_filter == 9729) &&
                (s.min_filter == 0 || s.min_filter == 9729), "custom/mipmapped samplers are not supported yet");
        }
        TextureData pixels;
        if (source->uri && std::strncmp(source->uri, "data:", 5) != 0) {
            std::string uri = source->uri;
            uri.resize(cgltf_decode_uri(uri.data()));
            pixels = TextureLoader::LoadRGBA((std::filesystem::path(path).parent_path() / uri).string());
        } else {
            Require(source->buffer_view != nullptr, "image data URIs are not supported; use external images or GLB buffer views");
            const auto* bytes = cgltf_buffer_view_data(source->buffer_view);
            Require(bytes && source->buffer_view->size <= INT_MAX, "invalid embedded image");
            int channels = 0;
            stbi_uc* decoded = stbi_load_from_memory(bytes, static_cast<int>(source->buffer_view->size), &pixels.width, &pixels.height, &channels, 4);
            Require(decoded != nullptr, "cannot decode embedded image");
            std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> owned(decoded, stbi_image_free);
            pixels.channels = 4;
            pixels.pixels.assign(decoded, decoded + size_t(pixels.width) * pixels.height * 4);
        }
        result.images.push_back(std::move(pixels));
        return static_cast<int>(result.images.size() - 1);
    };
    for (size_t i = 0; i < raw->materials_count; ++i) {
        const auto& source = raw->materials[i];
        Require(source.alpha_mode == cgltf_alpha_mode_opaque && !source.double_sided && !source.unlit, "only opaque, single-sided metallic-roughness materials are supported");
        MaterialData m;
        const auto& pbr = source.pbr_metallic_roughness;
        std::copy(pbr.base_color_factor, pbr.base_color_factor + 3, m.diffuseColor);
        m.metallic = pbr.metallic_factor;
        m.roughness = pbr.roughness_factor;
        m.textures = {{image(pbr.base_color_texture), image(source.normal_texture), image(pbr.metallic_roughness_texture), image(source.occlusion_texture), image(source.emissive_texture)}};
        m.hasDiffuseTexture = m.textures[0] >= 0;
        m.normalScale = source.normal_texture.scale;
        m.occlusionStrength = source.occlusion_texture.scale;
        m.emissive = glm::make_vec3(source.emissive_factor);
        result.materials.push_back(m);
    }
    MaterialData fallback;
    fallback.metallic = 1.0f;
    fallback.roughness = 1.0f;
    const int defaultMaterial = static_cast<int>(result.materials.size());
    result.materials.push_back(fallback);
    std::function<void(const cgltf_node*)> visit = [&](const cgltf_node* node) {
        Require(!node->skin, "skinning is not supported");
        glm::mat4 world;
        cgltf_node_transform_world(node, glm::value_ptr(world));
        const float determinant = glm::determinant(glm::mat3(world));
        Require(std::abs(determinant) > 1e-8f, "singular node transform");
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(world)));
        if (node->mesh) for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const auto& primitive = node->mesh->primitives[pi];
            Require(primitive.type == cgltf_primitive_type_triangles && !primitive.has_draco_mesh_compression && primitive.targets_count == 0, "only uncompressed triangles without morph targets are supported");
            const cgltf_accessor *positions = nullptr, *normals = nullptr, *uvs = nullptr, *tangents = nullptr;
            for (size_t a = 0; a < primitive.attributes_count; ++a) {
                const auto& attr = primitive.attributes[a];
                Require(!attr.data->is_sparse, "sparse vertex accessors are not supported");
                if (attr.type == cgltf_attribute_type_position) positions = attr.data;
                if (attr.type == cgltf_attribute_type_normal) normals = attr.data;
                if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) uvs = attr.data;
                if (attr.type == cgltf_attribute_type_tangent) tangents = attr.data;
                Require(attr.type != cgltf_attribute_type_color, "vertex colors are not supported yet");
            }
            Require(positions && positions->type == cgltf_type_vec3, "POSITION is required");
            const int materialId = primitive.material ? static_cast<int>(primitive.material - raw->materials) : defaultMaterial;
            const auto& material = result.materials[materialId];
            bool textured = false;
            for (int texture : material.textures) textured |= texture >= 0;
            Require(!textured || uvs, "textured primitive requires TEXCOORD_0");
            Require(material.textures[1] < 0 || tangents, "normal-mapped assets must provide exported tangents");
            if (primitive.indices) Require(!primitive.indices->is_sparse, "sparse indices are not supported");
            const size_t count = primitive.indices ? primitive.indices->count : positions->count;
            Require(count % 3 == 0, "invalid triangle index count");
            SubMesh sub{static_cast<uint32_t>(result.indices.size()), static_cast<uint32_t>(count), materialId};
            // Expand triangle corners so missing normals can use geometric face normals.
            for (size_t corner = 0; corner < count; ++corner) {
                const size_t index = primitive.indices ? cgltf_accessor_read_index(primitive.indices, corner) : corner;
                Require(index < positions->count, "index exceeds vertex count");
                Vertex v{};
                float values[4]{};
                Require(cgltf_accessor_read_float(positions, index, values, 3), "invalid position accessor");
                glm::vec3 p = glm::vec3(world * glm::vec4(glm::make_vec3(values), 1));
                std::memcpy(v.position, glm::value_ptr(p), sizeof(v.position));
                std::fill(v.color, v.color + 3, 1.0f);
                if (normals) {
                    Require(cgltf_accessor_read_float(normals, index, values, 3), "invalid normal accessor");
                    glm::vec3 n = SafeNormal(normalMatrix * glm::make_vec3(values), {0,1,0});
                    std::memcpy(v.normal, glm::value_ptr(n), sizeof(v.normal));
                }
                if (uvs) Require(cgltf_accessor_read_float(uvs, index, v.texCoord, 2), "invalid UV accessor");
                if (tangents) {
                    Require(cgltf_accessor_read_float(tangents, index, values, 4), "invalid tangent accessor");
                    glm::vec3 t = SafeNormal(glm::mat3(world) * glm::make_vec3(values), {1,0,0});
                    std::memcpy(v.tangent, glm::value_ptr(t), sizeof(float)*3);
                    v.tangent[3] = values[3] * (determinant < 0 ? -1.0f : 1.0f);
                }
                result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
                result.vertices.push_back(v);
                if (corner % 3 == 2) {
                    const size_t end = result.indices.size();
                    if (determinant < 0) std::swap(result.indices[end-1], result.indices[end-2]);
                    if (!normals) {
                        auto& a = result.vertices[result.indices[end-3]];
                        auto& b = result.vertices[result.indices[end-2]];
                        auto& c = result.vertices[result.indices[end-1]];
                        glm::vec3 n = SafeNormal(glm::cross(glm::make_vec3(b.position)-glm::make_vec3(a.position), glm::make_vec3(c.position)-glm::make_vec3(a.position)), {0,1,0});
                        for (Vertex* vtx : {&a,&b,&c}) std::memcpy(vtx->normal, glm::value_ptr(n), sizeof(vtx->normal));
                    }
                }
            }
            result.subMeshes.push_back(sub);
        }
        for (size_t i = 0; i < node->children_count; ++i) visit(node->children[i]);
    };
    const auto* scene = raw->scene ? raw->scene : (raw->scenes_count ? raw->scenes : nullptr);
    Require(scene != nullptr, "document has no scene");
    for (size_t i = 0; i < scene->nodes_count; ++i) visit(scene->nodes[i]);
    Require(!result.vertices.empty(), "scene contains no triangles");
    return result;
}
