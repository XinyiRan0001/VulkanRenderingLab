#include "AnimationRig.h"
#include <algorithm>
#include <cgltf.h>
#include <cmath>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(std::string("Animation rig: ") + message);
}
std::vector<float> Read(const cgltf_accessor *a)
{
    Require(a, "missing accessor");
    const size_t components = cgltf_num_components(a->type);
    Require(components > 0 && a->count <= std::numeric_limits<size_t>::max() / components,
            "invalid accessor size");
    std::vector<float> values(a->count * components);
    // Blender emits sparse morph deltas. Unpack handles the implicit zero base,
    // sparse overrides, normalized integer weights and accessor stride together.
    Require(cgltf_accessor_unpack_floats(a, values.data(), values.size()) == values.size(),
            "cannot decode accessor");
    for (float f : values)
        Require(std::isfinite(f), "nonfinite accessor data");
    return values;
}
glm::vec3 Vec3(const std::vector<float> &v, size_t i)
{
    return {v[i * 3], v[i * 3 + 1], v[i * 3 + 2]};
}
glm::quat Quaternion(const float *v)
{
    glm::quat q(v[3], v[0], v[1], v[2]);
    Require(glm::dot(q, q) > 1e-12f, "zero animation quaternion");
    return glm::normalize(q);
}
} // namespace

AnimationRig AnimationRig::Load(const std::string &path)
{
    cgltf_options options{};
    cgltf_data *raw = nullptr;
    Require(cgltf_parse_file(&options, path.c_str(), &raw) == cgltf_result_success, "cannot parse file");
    std::unique_ptr<cgltf_data, decltype(&cgltf_free)> owner(raw, cgltf_free);
    Require(cgltf_load_buffers(&options, raw, path.c_str()) == cgltf_result_success, "cannot load buffers");
    Require(cgltf_validate(raw) == cgltf_result_success, "invalid glTF document");
    Require(raw->extensions_required_count == 0, "required extensions unsupported");
    AnimationRig result;
    result.nodes.resize(raw->nodes_count);
    for (size_t i = 0; i < raw->nodes_count; ++i)
    {
        const auto &source = raw->nodes[i];
        auto &n = result.nodes[i];
        n.name = source.name ? source.name : "";
        n.parent = source.parent ? int(source.parent - raw->nodes) : -1;
        n.skin = source.skin ? int(source.skin - raw->skins) : -1;
        n.hasMatrix = source.has_matrix;
        if (source.has_matrix)
            n.matrix = glm::make_mat4(source.matrix);
        if (source.has_translation)
            n.translation = glm::make_vec3(source.translation);
        if (source.has_rotation)
            n.rotation = Quaternion(source.rotation);
        if (source.has_scale)
            n.scale = glm::make_vec3(source.scale);
        if (source.mesh && source.mesh->primitives_count)
        {
            const size_t count = source.mesh->primitives[0].targets_count;
            n.morphWeights.resize(count, 0);
            if (source.weights_count)
            {
                Require(source.weights_count == count, "node morph weight count mismatch");
                std::copy(source.weights, source.weights + count, n.morphWeights.begin());
            }
            else if (source.mesh->weights_count)
            {
                Require(source.mesh->weights_count == count, "mesh morph weight count mismatch");
                std::copy(source.mesh->weights, source.mesh->weights + count, n.morphWeights.begin());
            }
        }
    }
    for (size_t i = 0; i < raw->skins_count; ++i)
    {
        const auto &source = raw->skins[i];
        RigSkin skin;
        skin.name = source.name ? source.name : "";
        Require(source.joints_count > 0, "skin has no joints");
        for (size_t j = 0; j < source.joints_count; ++j)
            skin.joints.push_back(int(source.joints[j] - raw->nodes));
        skin.inverseBind.resize(source.joints_count, glm::mat4(1));
        if (source.inverse_bind_matrices)
        {
            Require(source.inverse_bind_matrices->type == cgltf_type_mat4 &&
                        source.inverse_bind_matrices->count == source.joints_count,
                    "inverse-bind matrix shape mismatch");
            auto values = Read(source.inverse_bind_matrices);
            for (size_t j = 0; j < source.joints_count; ++j)
                skin.inverseBind[j] = glm::make_mat4(values.data() + j * 16);
        }
        result.skins.push_back(std::move(skin));
    }
    // Retain local-space vertices: baking world transforms here would apply them twice during skinning.
    for (size_t ni = 0; ni < raw->nodes_count; ++ni)
    {
        const auto &node = raw->nodes[ni];
        if (!node.mesh)
            continue;
        for (size_t pi = 0; pi < node.mesh->primitives_count; ++pi)
        {
            const auto &source = node.mesh->primitives[pi];
            Require(source.type == cgltf_primitive_type_triangles && !source.has_draco_mesh_compression,
                    "only uncompressed triangles supported");
            const cgltf_accessor *p = nullptr, *n = nullptr, *uv = nullptr, *joints = nullptr,
                                 *weights = nullptr;
            for (size_t ai = 0; ai < source.attributes_count; ++ai)
            {
                const auto &a = source.attributes[ai];
                if (a.type == cgltf_attribute_type_position)
                    p = a.data;
                if (a.type == cgltf_attribute_type_normal)
                    n = a.data;
                if (a.type == cgltf_attribute_type_texcoord && a.index == 0)
                    uv = a.data;
                if (a.type == cgltf_attribute_type_joints)
                {
                    Require(a.index == 0, "more than four influences unsupported");
                    joints = a.data;
                }
                if (a.type == cgltf_attribute_type_weights)
                {
                    Require(a.index == 0, "more than four influences unsupported");
                    weights = a.data;
                }
            }
            Require(p && p->type == cgltf_type_vec3, "POSITION missing or malformed");
            auto positions = Read(p);
            auto normals = n ? Read(n) : std::vector<float>{};
            auto uvs = uv ? Read(uv) : std::vector<float>{};
            Require(!n || (n->type == cgltf_type_vec3 && n->count == p->count), "normal shape mismatch");
            Require(!uv || (uv->type == cgltf_type_vec2 && uv->count == p->count), "UV shape mismatch");
            std::vector<float> influence;
            if (node.skin)
            {
                Require(joints && weights && !joints->is_sparse && joints->type == cgltf_type_vec4 &&
                            weights->type == cgltf_type_vec4 && joints->count == p->count &&
                            weights->count == p->count,
                        "skin influence accessor mismatch");
                influence = Read(weights);
            }
            RigPrimitive primitive;
            primitive.node = int(ni);
            primitive.material = source.material ? int(source.material - raw->materials) : -1;
            primitive.vertices.resize(p->count);
            for (size_t vi = 0; vi < p->count; ++vi)
            {
                auto &v = primitive.vertices[vi];
                v.position = Vec3(positions, vi);
                if (n)
                    v.normal = Vec3(normals, vi);
                if (uv)
                    v.uv = {uvs[vi * 2], uvs[vi * 2 + 1]};
                if (node.skin)
                {
                    cgltf_uint ids[4];
                    Require(cgltf_accessor_read_uint(joints, vi, ids, 4), "cannot read joint indices");
                    float sum = 0;
                    for (size_t k = 0; k < 4; ++k)
                    {
                        Require(ids[k] < node.skin->joints_count, "skin-local joint index out of range");
                        v.joints[k] = ids[k];
                        v.weights[k] = influence[vi * 4 + k];
                        Require(v.weights[k] >= 0, "negative skin weight");
                        sum += v.weights[k];
                    }
                    Require(sum > 1e-8f, "zero influence weight sum");
                    if (std::abs(sum - 1) > 1e-5f)
                        ++result.renormalizedWeights;
                    v.weights /= sum;
                }
            }
            const size_t count = source.indices ? source.indices->count : p->count;
            Require(count % 3 == 0, "triangle index count mismatch");
            Require(!source.indices || !source.indices->is_sparse, "sparse indices unsupported");
            for (size_t i = 0; i < count; ++i)
            {
                const size_t index = source.indices ? cgltf_accessor_read_index(source.indices, i) : i;
                Require(index < p->count, "index out of range");
                primitive.indices.push_back(uint32_t(index));
            }
            Require(source.targets_count == result.nodes[ni].morphWeights.size(),
                    "inconsistent morph target counts");
            for (size_t ti = 0; ti < source.targets_count; ++ti)
            {
                RigMorph morph;
                for (size_t ai = 0; ai < source.targets[ti].attributes_count; ++ai)
                {
                    const auto &a = source.targets[ti].attributes[ai];
                    Require(a.data->type == cgltf_type_vec3 && a.data->count == p->count,
                            "morph shape mismatch");
                    auto values = Read(a.data);
                    if (a.type == cgltf_attribute_type_position || a.type == cgltf_attribute_type_normal)
                    {
                        auto &target =
                            a.type == cgltf_attribute_type_position ? morph.positions : morph.normals;
                        for (size_t vi = 0; vi < p->count; ++vi)
                            target.push_back(Vec3(values, vi));
                    }
                    else
                        Require(false, "morph attribute unsupported");
                }
                primitive.morphs.push_back(std::move(morph));
            }
            result.primitives.push_back(std::move(primitive));
        }
    }
    for (size_t ci = 0; ci < raw->animations_count; ++ci)
    {
        const auto &source = raw->animations[ci];
        RigClip clip;
        clip.name = source.name ? source.name : "";
        clip.start = std::numeric_limits<float>::max();
        std::set<std::pair<int, int>> targets;
        for (size_t ch = 0; ch < source.channels_count; ++ch)
        {
            const auto &c = source.channels[ch];
            Require(c.target_node && c.sampler, "animation target missing");
            RigChannel channel;
            channel.node = int(c.target_node - raw->nodes);
            const auto &sampler = *c.sampler;
            Require(sampler.interpolation == cgltf_interpolation_type_linear ||
                        sampler.interpolation == cgltf_interpolation_type_step,
                    "CUBICSPLINE unsupported");
            channel.step = sampler.interpolation == cgltf_interpolation_type_step;
            if (c.target_path == cgltf_animation_path_type_translation)
            {
                channel.path = RigPath::Translation;
                channel.components = 3;
            }
            else if (c.target_path == cgltf_animation_path_type_rotation)
            {
                channel.path = RigPath::Rotation;
                channel.components = 4;
            }
            else if (c.target_path == cgltf_animation_path_type_scale)
            {
                channel.path = RigPath::Scale;
                channel.components = 3;
            }
            else
            {
                Require(c.target_path == cgltf_animation_path_type_weights, "unknown animation path");
                channel.path = RigPath::Weights;
                channel.components = result.nodes[channel.node].morphWeights.size();
            }
            Require(targets.insert({channel.node, int(channel.path)}).second, "duplicate animation target");
            Require(channel.components > 0 &&
                        (channel.path == RigPath::Weights || !result.nodes[channel.node].hasMatrix),
                    "invalid animation target transform");
            Require(sampler.input->type == cgltf_type_scalar, "key time must be scalar");
            channel.times = Read(sampler.input);
            Require(!channel.times.empty(), "empty animation channel");
            for (size_t k = 0; k < channel.times.size(); ++k)
                Require(channel.times[k] >= 0 && (k == 0 || channel.times[k] > channel.times[k - 1]),
                        "key times must increase strictly");
            channel.values = Read(sampler.output);
            Require(channel.values.size() == channel.times.size() * channel.components,
                    "key output shape mismatch");
            Require(sampler.output->type == (channel.path == RigPath::Weights    ? cgltf_type_scalar
                                             : channel.path == RigPath::Rotation ? cgltf_type_vec4
                                                                                 : cgltf_type_vec3),
                    "key output accessor type mismatch");
            if (channel.path == RigPath::Rotation)
                for (size_t k = 0; k < channel.times.size(); ++k)
                    Quaternion(channel.values.data() + k * 4);
            clip.start = std::min(clip.start, channel.times.front());
            clip.end = std::max(clip.end, channel.times.back());
            clip.channels.push_back(std::move(channel));
        }
        Require(!clip.channels.empty(), "empty animation clip");
        result.clips.push_back(std::move(clip));
    }
    for (size_t i = 0; i < raw->materials_count; ++i)
    {
        result.blendMaterials += raw->materials[i].alpha_mode == cgltf_alpha_mode_blend;
        result.doubleSidedMaterials += raw->materials[i].double_sided;
    }
    for (size_t i = 0; i < raw->samplers_count; ++i)
        result.mipmappedSamplers += raw->samplers[i].min_filter >= 9984;
    Require(!result.skins.empty() && !result.primitives.empty(), "asset needs a skinned mesh");
    result.BindPose();
    return result;
}

RigPose AnimationRig::Evaluate(const std::vector<RigNode> &pose) const
{
    RigPose result;
    result.world.resize(pose.size());
    result.palettes.resize(pose.size());
    result.morphWeights.resize(pose.size());
    std::vector<int> state(pose.size());
    std::function<void(size_t)> visit = [&](size_t i) {
        Require(i < pose.size() && state[i] != 1, "node hierarchy cycle or invalid parent");
        if (state[i] == 2)
            return;
        state[i] = 1;
        const auto &n = pose[i];
        const auto local = n.hasMatrix ? n.matrix
                                       : glm::translate(glm::mat4(1), n.translation) *
                                             glm::mat4_cast(n.rotation) * glm::scale(glm::mat4(1), n.scale);
        if (n.parent >= 0)
        {
            visit(size_t(n.parent));
            result.world[i] = result.world[n.parent] * local;
        }
        else
            result.world[i] = local;
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                Require(std::isfinite(result.world[i][c][r]), "nonfinite pose matrix");
        result.morphWeights[i] = n.morphWeights;
        for (float value : n.morphWeights)
            Require(std::isfinite(value), "nonfinite morph weight");
        state[i] = 2;
    };
    for (size_t i = 0; i < pose.size(); ++i)
        visit(i);
    for (size_t i = 0; i < pose.size(); ++i)
        if (pose[i].skin >= 0)
        {
            const auto &skin = skins.at(size_t(pose[i].skin));
            Require(std::abs(glm::determinant(result.world[i])) > 1e-12f, "singular animated mesh transform");
            const auto inverseMesh = glm::inverse(result.world[i]);
            for (size_t j = 0; j < skin.joints.size(); ++j)
            {
                const auto matrix =
                    inverseMesh * result.world.at(size_t(skin.joints[j])) * skin.inverseBind.at(j);
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        Require(std::isfinite(matrix[c][r]), "nonfinite skin palette");
                result.palettes[i].push_back(matrix);
            }
        }
    return result;
}
RigPose AnimationRig::BindPose() const
{
    return Evaluate(nodes);
}
RigPose AnimationRig::Sample(size_t clipIndex, float seconds, bool loop) const
{
    Require(std::isfinite(seconds) && seconds >= 0, "elapsed time must be finite and nonnegative");
    const auto &clip = clips.at(clipIndex);
    const float duration = clip.end - clip.start;
    const float time =
        clip.start + (duration > 0 ? (loop ? std::fmod(seconds, duration) : std::min(seconds, duration)) : 0);
    auto pose = nodes;
    for (const auto &channel : clip.channels)
    {
        const auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
        const size_t a = upper == channel.times.begin() ? 0 : size_t(upper - channel.times.begin() - 1),
                     b = std::min(a + 1, channel.times.size() - 1);
        const float t =
            channel.step || a == b
                ? 0
                : glm::clamp((time - channel.times[a]) / (channel.times[b] - channel.times[a]), 0.f, 1.f);
        const float *va = channel.values.data() + a * channel.components,
                    *vb = channel.values.data() + b * channel.components;
        auto &n = pose[channel.node];
        if (channel.path == RigPath::Rotation)
        {
            auto qa = Quaternion(va), qb = Quaternion(vb);
            if (glm::dot(qa, qb) < 0)
                qb = -qb;
            n.rotation = glm::normalize(glm::slerp(qa, qb, t));
        }
        else if (channel.path == RigPath::Weights)
        {
            for (size_t k = 0; k < channel.components; ++k)
                n.morphWeights[k] = glm::mix(va[k], vb[k], t);
        }
        else
        {
            const auto value = glm::mix(glm::make_vec3(va), glm::make_vec3(vb), t);
            if (channel.path == RigPath::Translation)
                n.translation = value;
            else
                n.scale = value;
        }
    }
    return Evaluate(pose);
}
