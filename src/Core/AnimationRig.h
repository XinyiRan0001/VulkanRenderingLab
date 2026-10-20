#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>

// CPU import/evaluation only. Static GLTFLoader retains its strict rendering contract.
struct RigNode
{
    std::string name;
    int parent = -1, skin = -1;
    glm::vec3 translation{0}, scale{1};
    glm::quat rotation{1, 0, 0, 0};
    glm::mat4 matrix{1};
    bool hasMatrix = false;
    std::vector<float> morphWeights;
};
struct RigSkin
{
    std::string name;
    std::vector<int> joints;
    std::vector<glm::mat4> inverseBind;
};
enum class RigPath
{
    Translation,
    Rotation,
    Scale,
    Weights
};
struct RigChannel
{
    int node = -1;
    RigPath path{};
    bool step = false;
    size_t components = 0;
    std::vector<float> times, values;
};
struct RigClip
{
    std::string name;
    float start = 0, end = 0;
    std::vector<RigChannel> channels;
};
struct RigVertex
{
    glm::vec3 position{}, normal{};
    glm::vec2 uv{};
    glm::uvec4 joints{};
    glm::vec4 weights{};
};
struct RigMorph
{
    std::vector<glm::vec3> positions, normals;
};
struct RigPrimitive
{
    int node = -1, material = -1;
    std::vector<RigVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<RigMorph> morphs;
};
struct RigPose
{
    std::vector<glm::mat4> world;
    std::vector<std::vector<glm::mat4>> palettes; // indexed by mesh node, then skin-local joint index
    std::vector<std::vector<float>> morphWeights;
};
class AnimationRig
{
  public:
    static AnimationRig Load(const std::string &path);
    RigPose Sample(size_t clip, float elapsedSeconds, bool loop = true) const;
    RigPose BindPose() const;
    std::vector<RigNode> nodes;
    std::vector<RigSkin> skins;
    std::vector<RigClip> clips;
    std::vector<RigPrimitive> primitives;
    size_t blendMaterials = 0, doubleSidedMaterials = 0, mipmappedSamplers = 0;
    size_t renormalizedWeights = 0;

  private:
    RigPose Evaluate(const std::vector<RigNode> &) const;
};
