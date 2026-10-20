#include "Core/AnimationRig.h"
#include "Core/MeshWinding.h"
#include "Core/SkinningMath.h"
#include "Core/SurfaceNormals.h"
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
void Check(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
bool Near(float a, float b)
{
    return std::abs(a - b) < 1e-4f;
}
int main(int argc, char **argv)
{
    try
    {
        RigPrimitive corner;
        corner.vertices.resize(6);
        corner.vertices[0].position = {0, 0, 0};
        corner.vertices[1].position = {1, 0, 0};
        corner.vertices[2].position = {0, 1, 0};
        corner.vertices[3].position = {0, 0, 0}; // UV split, same smooth surface
        corner.vertices[4].position = {0, 1, 0};
        corner.vertices[5].position = {0, 0, 1};
        for (auto &v : corner.vertices)
            v.normal = {1, 0, 1};
        corner.indices = {0, 1, 2, 3, 4, 5};
        auto adjacency = BuildSurfaceAdjacency(corner, 0, false);
        Check(adjacency[0].size() == 2 && adjacency[3].size() == 2, "UV split normals not welded");
        std::vector<glm::vec3> points;
        for (const auto &v : corner.vertices)
            points.push_back(v.position);
        Check(glm::length(RebuildSurfaceNormal(points, adjacency[0], {0, 1, 0}) -
                          glm::normalize(glm::vec3(1, 0, 1))) < 1e-5f,
              "surface normal reference wrong");
        corner.vertices[3].normal = {1, 0, 0};
        Check(BuildSurfaceAdjacency(corner, 0, false)[0].size() == 1, "hard edge welded");
        corner.vertices[3].normal = corner.vertices[0].normal;
        corner.vertices[3].weights = {1, 0, 0, 0};
        Check(BuildSurfaceAdjacency(corner, 0, false)[0].size() == 1, "different skin weights welded");
        corner.vertices[3].weights = corner.vertices[0].weights;
        RigMorph shape;
        shape.positions.resize(6);
        shape.positions[3] = {0, .1f, 0};
        corner.morphs.push_back(shape);
        Check(BuildSurfaceAdjacency(corner, 0, false)[0].size() == 1, "different morph deltas welded");
        Check(RebuildSurfaceNormal(points, {}, {0, 1, 0}) == glm::vec3(0, 1, 0),
              "degenerate surface lost fallback normal");
        using namespace SkinningMath;
        const glm::mat4 identity(1);
        auto rigidTranslation = glm::translate(identity, glm::vec3(2, 3, 4));
        auto rigid = FromRigid(rigidTranslation);
        Check(glm::length(Transform(rigid, {1, 0, 0}) - glm::vec3(3, 3, 4)) < 1e-5f,
              "dual quaternion translation failed");
        auto screwMatrix = glm::rotate(rigidTranslation, glm::radians(65.f), glm::vec3(0, 1, 0));
        Check(glm::length(Transform(FromRigid(screwMatrix), {1, 2, 3}) -
                          glm::vec3(screwMatrix * glm::vec4(1, 2, 3, 1))) < 1e-5f,
              "combined rotation and translation failed");
        auto a = FromRigid(glm::rotate(identity, glm::radians(80.f), glm::vec3(0, 0, 1)));
        auto b = FromRigid(glm::rotate(identity, glm::radians(-80.f), glm::vec3(0, 0, 1)));
        DualQuaternion blended;
        Check(Blend({a, b, {}, {}}, {.5f, .5f, 0, 0}, blended), "rigid blend failed");
        Check(glm::length(Transform(blended, {1, 0, 0}) - glm::vec3(1, 0, 0)) < 1e-5f,
              "dual blend collapsed radius");
        b.real = -b.real;
        b.dual = -b.dual;
        Check(Blend({a, b, {}, {}}, {.5f, .5f, 0, 0}, blended) &&
                  glm::length(Transform(blended, {1, 0, 0}) - glm::vec3(1, 0, 0)) < 1e-5f,
              "quaternion sign changed deformation");
        auto scaled = FromRigid(glm::scale(identity, glm::vec3(2, 1, 1)));
        Check(!Blend({rigid, scaled, {}, {}}, {.5f, .5f, 0, 0}, blended), "scale failed to fall back");
        Check(Blend({scaled, rigid, {}, {}}, {0, 1, 0, 0}, blended), "zero-weight bone affected fallback");
        auto shearMatrix = identity;
        shearMatrix[1][0] = .2f;
        Check(glm::length(FromRigid(shearMatrix).real) == 0, "shear accepted as rigid rotation");
        Check(glm::dot(FromRigid(glm::scale(identity, glm::vec3(-1, 1, 1))).real,
                       FromRigid(glm::scale(identity, glm::vec3(-1, 1, 1))).real) == 0,
              "reflection accepted as rigid rotation");
        std::vector<RigVertex> sheet(4);
        sheet[0].position = {0, 0, 0};
        sheet[1].position = {1, 0, 0};
        sheet[2].position = {0, 1, 0};
        sheet[3].position = {1, 1, 0};
        for (auto &v : sheet)
            v.normal = {0, 0, 1};
        Check(!HasUniformlyReversedNormals(sheet, {0, 1, 2, 1, 3, 2}), "valid winding changed");
        Check(HasUniformlyReversedNormals(sheet, {0, 2, 1, 1, 2, 3}), "reversed sheet missed");
        Check(!HasUniformlyReversedNormals(sheet, {0, 2, 1, 1, 3, 2}), "mixed winding repaired");
        Check(!HasUniformlyReversedNormals(sheet, {0, 0, 0}), "degenerate sheet repaired");
        for (auto &v : sheet)
            v.normal = {0, 0, 0};
        Check(!HasUniformlyReversedNormals(sheet, {0, 2, 1}), "missing normals repaired");
        AnimationRig rig;
        rig.nodes.resize(3);
        rig.nodes[0].translation = {5, 0, 0};
        rig.nodes[1].parent = 0;
        rig.nodes[1].translation = {0, 1, 0};
        rig.nodes[2].parent = 0;
        rig.nodes[2].translation = {2, 0, 0};
        rig.nodes[2].skin = 0;
        rig.nodes[2].morphWeights = {0};
        RigSkin skin;
        skin.joints = {1};
        skin.inverseBind = {glm::translate(glm::mat4(1), glm::vec3(2, -1, 0))};
        rig.skins.push_back(skin);
        auto bind = rig.BindPose();
        Check(Near(bind.world[2][3].x, 7), "parent hierarchy lost");
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                Check(Near(bind.palettes[2][0][c][r], c == r ? 1.f : 0.f),
                      "inverse mesh / inverse bind convention incorrect");
        RigClip clip;
        clip.start = 1;
        clip.end = 3;
        RigChannel translation;
        translation.node = 1;
        translation.path = RigPath::Translation;
        translation.components = 3;
        translation.times = {1, 3};
        translation.values = {0, 1, 0, 0, 3, 0};
        clip.channels.push_back(translation);
        RigChannel scale;
        scale.node = 0;
        scale.path = RigPath::Scale;
        scale.components = 3;
        scale.step = true;
        scale.times = {1, 3};
        scale.values = {1, 1, 1, 2, 2, 2};
        clip.channels.push_back(scale);
        RigChannel weights;
        weights.node = 2;
        weights.path = RigPath::Weights;
        weights.components = 1;
        weights.times = {1, 3};
        weights.values = {0, 1};
        clip.channels.push_back(weights);
        rig.clips.push_back(clip);
        const auto middle = rig.Sample(0, 1, false);
        Check(Near(middle.world[1][3].y, 2) && Near(middle.palettes[2][0][3].y, 1),
              "linear joint interpolation failed");
        Check(Near(middle.world[0][0][0], 1), "STEP interpolated between keys");
        Check(Near(middle.morphWeights[2][0], .5f), "morph weight interpolation failed");
        Check(Near(rig.Sample(0, 2, false).world[0][0][0], 2), "nonloop end was not clamped");
        Check(Near(rig.Sample(0, 2, true).world[0][0][0], 1), "loop did not return to first source key");
        RigChannel rotation;
        rotation.node = 1;
        rotation.path = RigPath::Rotation;
        rotation.components = 4;
        rotation.times = {1, 3};
        rotation.values = {0, 0, 0, 1, 0, 0, 1, 0};
        rig.clips[0].channels.push_back(rotation);
        const auto direction = rig.Sample(0, 1, false).world[1] * glm::vec4(1, 0, 0, 0);
        Check(Near(direction.x, 0) && Near(direction.y, 1),
              "quaternion interpolation or xyzw conversion failed");
        rig.clips[0].channels.back().values = {0, 0, 0, 1, 0, 0, 0, -1};
        Check(Near(rig.Sample(0, 1, false).world[1][0][0], 1),
              "equivalent opposite quaternion signs took long arc");
        bool rejected = false;
        try
        {
            rig.Sample(0, -1);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        Check(rejected, "negative elapsed time accepted");
        rig.nodes[0].parent = 1;
        rejected = false;
        try
        {
            rig.BindPose();
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        Check(rejected, "hierarchy cycle accepted");
        if (argc == 2)
        {
            const auto asset = AnimationRig::Load(argv[1]);
            Check(!asset.clips.empty(), "real asset contains no clips");
            if (asset.clips[0].name == "joint-and-morph")
            {
                const auto pose = asset.Sample(0, .5f, false);
                Check(Near(pose.palettes[1][0][3].y, .5f),
                      "imported palette did not deform in mesh-local space");
                Check(Near(pose.morphWeights[1][0], .5f), "imported morph animation lost");
                const auto &p = asset.primitives.at(0);
                Check(Near(p.vertices[0].position.x, 0), "importer baked node transform into skin vertices");
                Check(Near(p.morphs[0].positions[0].z, 0) && Near(p.morphs[0].positions[1].z, .1f) &&
                          Near(p.morphs[0].positions[2].z, 0),
                      "sparse morph implicit zeros/override incorrect");
            }
            float maximumChange = 0;
            for (size_t c = 0; c < asset.clips.size(); ++c)
            {
                const auto first = asset.Sample(c, 0, false);
                for (int step = 1; step <= 4; ++step)
                {
                    const auto pose =
                        asset.Sample(c, (asset.clips[c].end - asset.clips[c].start) * step / 4, false);
                    for (size_t n = 0; n < pose.palettes.size(); ++n)
                        for (size_t j = 0; j < pose.palettes[n].size(); ++j)
                            for (int col = 0; col < 4; ++col)
                                for (int row = 0; row < 4; ++row)
                                {
                                    const float f = pose.palettes[n][j][col][row];
                                    Check(std::isfinite(f), "nonfinite real-asset palette");
                                    maximumChange =
                                        std::max(maximumChange, std::abs(f - first.palettes[n][j][col][row]));
                                }
                }
            }
            Check(maximumChange > .001f, "real asset poses never changed");
            for (const auto &p : asset.primitives)
                for (const auto &v : p.vertices)
                    if (asset.nodes[p.node].skin >= 0)
                        Check(Near(v.weights.x + v.weights.y + v.weights.z + v.weights.w, 1),
                              "real asset weights not normalized");
            std::cout << "Real-asset all-clip palette/weight checks passed\n";
        }
        std::cout << "Hierarchy, mesh-local palette, TRS/STEP/SLERP, morph weights, loop and rejection "
                     "checks passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
