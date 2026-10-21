#include "Core/AnimationRig.h"
#include "Core/AoMotionScene.h"
#include "Core/VulkanContext.h"
#include "Core/Window.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <sstream>
#ifdef _WIN32
#define NOMINMAX
#include "../third_party/renderdoc/renderdoc_app.h"
#include <windows.h>
#endif

#include <GLFW/glfw3.h>

int main(int argc, char **argv)
{
    try
    {
        if (argc >= 2 && std::string(argv[1]) == "--inspect-animation")
        {
            if (argc != 3 && !(argc == 5 && std::string(argv[3]) == "--dump-rig"))
                throw std::runtime_error("Usage: --inspect-animation path.glb [--dump-rig output.csv]");
            const auto rig = AnimationRig::Load(argv[2]);
            std::ofstream dump;
            if (argc == 5)
            {
                dump.open(argv[4]);
                if (!dump)
                    throw std::runtime_error("Cannot open rig CSV");
                dump << "clip,step,node,parent,x,y,z,parent_x,parent_y,parent_z\n" << std::setprecision(9);
            }
            const auto dumpPose = [&](size_t clip, int step, const RigPose &pose) {
                if (!dump.is_open())
                    return;
                for (const auto &skin : rig.skins)
                    for (int node : skin.joints)
                    {
                        const int parent = rig.nodes[node].parent;
                        const glm::vec3 p(pose.world[node][3]);
                        const glm::vec3 q = parent >= 0 ? glm::vec3(pose.world[parent][3]) : p;
                        dump << clip << ',' << step << ',' << node << ',' << parent << ',' << p.x << ','
                             << p.y << ',' << p.z << ',' << q.x << ',' << q.y << ',' << q.z << '\n';
                    }
            };
            size_t joints = 0, vertices = 0, morphs = 0;
            for (const auto &s : rig.skins)
                joints += s.joints.size();
            for (const auto &p : rig.primitives)
            {
                vertices += p.vertices.size();
                morphs += p.morphs.size();
            }
            std::cout << "[Animation Rig] nodes=" << rig.nodes.size() << " skins=" << rig.skins.size()
                      << " joints=" << joints << " primitives=" << rig.primitives.size()
                      << " vertices=" << vertices << " morph targets across primitives=" << morphs
                      << " renormalized weights=" << rig.renormalizedWeights << '\n';
            for (size_t i = 0; i < rig.clips.size(); ++i)
            {
                const auto &clip = rig.clips[i];
                const auto base = rig.Sample(i, 0, false);
                dumpPose(i, 0, base);
                float change = 0, morphChange = 0;
                for (int step = 1; step <= 4; ++step)
                {
                    const auto pose = rig.Sample(i, (clip.end - clip.start) * step / 4, false);
                    dumpPose(i, step, pose);
                    for (size_t n = 0; n < pose.palettes.size(); ++n)
                        for (size_t j = 0; j < pose.palettes[n].size(); ++j)
                            for (int c = 0; c < 4; ++c)
                                for (int r = 0; r < 4; ++r)
                                    change = std::max(change, std::abs(pose.palettes[n][j][c][r] -
                                                                       base.palettes[n][j][c][r]));
                    for (size_t n = 0; n < pose.morphWeights.size(); ++n)
                        for (size_t w = 0; w < pose.morphWeights[n].size(); ++w)
                            morphChange = std::max(
                                morphChange, std::abs(pose.morphWeights[n][w] - base.morphWeights[n][w]));
                }
                std::cout << "[Clip " << i << "] " << clip.name << " start=" << clip.start
                          << " duration=" << clip.end - clip.start << " channels=" << clip.channels.size()
                          << " palette delta=" << change << " morph delta=" << morphChange << '\n';
            }
            std::cout << "[Rendering requirements] blend materials=" << rig.blendMaterials
                      << " double sided=" << rig.doubleSidedMaterials
                      << " mipmapped samplers=" << rig.mipmappedSamplers
                      << "\nCPU pose evaluation only; GPU skinning, morph rendering and material integration "
                         "are not connected.\n";
            if (dump.is_open())
            {
                dump.flush();
                if (!dump)
                    throw std::runtime_error("Cannot write rig CSV");
            }
            return 0;
        }
        bool renderdocCapture = false;
        bool aoCycleTest = false;
        bool aoResolutionCycle = false;
        bool planeAwareBlur = true;
        bool halfAo = false;
        bool temporalAo = false;
        bool temporalCycle = false;
        bool aoMotionScene = false, aoBoxStep = false;
        int aoHistoryView = 0;
        bool adaptiveAo = false;
        bool varyingSampling = false;
        bool captureEveryFrame = false;
        int sweepStart = 0, sweepStop = -1;
        std::string aoTimingPath;
        int aoWarmup = 60, aoMeasure = 240;
        int prepassMode = 0;
        bool verifyPrepass = false;
        bool motionInstanceTest = false;
        int motionResetAt = -1;
        float depthRange = 10.f;
        float aoRadius = .5f, aoBias = .025f;
        int aoSamples = 32;
        int width = 1280, height = 720, resizeAfter = 0, resizeWidth = 1024, resizeHeight = 768;
        std::string scenePath;
        std::string animatedPath, animationName = "Idle", skinningMode = "dual-quaternion";
        std::string skinningNormals = "surface";
        bool animationControlsTest=false, animationPanel=true;
        bool rayScene=false,verifyRay=false,rayEnabled=true,rayCycle=false;
        int rayView=0;
        bool mirrorStyles=true;
        float animationFrozen = -1;
        std::string benchmarkPath;
        bool benchmarkCaptures = false;
        int benchmarkWarmup = 30, benchmarkSamples = 120, benchmarkRepeats = 3;
        int frameLimit = 0;
        uint32_t stressCount = 0;
        std::string capturePath;
        int captureFrame = 1;
        int shadowMode = 3;
        float yaw = 0.0f, pitch = 8.594367f;
        bool paused = false;
        bool yawSpecified = false;
        bool cullingObserver = false;
        bool hiZInspector = false;
        int inspectInstance = -1, inspectMip = 0, inspectGroup = 0;
        bool mixedScene = false;
        bool culling = true, verifyCulling = false;
        bool occlusionScene = false, occlusion = true, pitchSpecified = false;
        float cameraDistance = 0.0f, targetOffsetX = 0.0f, cameraSweep = 0.0f;
        int materialView = 0, lightingView = 0;
        float environmentDegrees = 0.0f;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--ao-resolution-cycle-test")
                aoResolutionCycle = true;
            else if (option == "--ao-motion-scene")
            {
                aoMotionScene = true;
                prepassMode = 6;
                temporalAo = true;
            }
            else if (option == "--ao-history-view" && i + 1 < argc)
            {
                const std::string v = argv[++i];
                if (v != "ao" && v != "status" && v != "weight")
                    throw std::runtime_error("--ao-history-view must be ao, status or weight");
                aoHistoryView = v == "status" ? 1 : v == "weight" ? 2 : 0;
                prepassMode = 6;
                temporalAo = true;
            }
            else if (option == "--ao-box-step")
                aoBoxStep = true;
            else if (option == "--ao-response" && i + 1 < argc)
            {
                const std::string value = argv[++i];
                if (value != "fixed" && value != "adaptive")
                    throw std::runtime_error("--ao-response must be fixed or adaptive");
                adaptiveAo = value == "adaptive";
            }
            else if (option == "--ao-sampling" && i + 1 < argc)
            {
                const std::string value = argv[++i];
                if (value != "fixed" && value != "varying")
                    throw std::runtime_error("--ao-sampling must be fixed or varying");
                varyingSampling = value == "varying";
            }
            else if (option == "--ao-temporal-cycle-test")
                temporalCycle = true;
            else if (option == "--ao-temporal" && i + 1 < argc)
            {
                const std::string value = argv[++i];
                if (value != "on" && value != "off")
                    throw std::runtime_error("--ao-temporal must be on or off");
                temporalAo = value == "on";
            }
            else if (option == "--ao-cycle-test")
                aoCycleTest = true;
            else if (option == "--renderdoc-capture")
                renderdocCapture = true;
            else if (option == "--motion-view")
                prepassMode = 8;
            else if (option == "--verify-motion")
            {
                verifyPrepass = true;
                if (prepassMode == 0)
                    prepassMode = 8;
            }
            else if (option == "--motion-instance-test")
                motionInstanceTest = true;
            else if (option == "--motion-reset-at" && i + 1 < argc)
                motionResetAt = std::stoi(argv[++i]);
            else if (option == "--ssao")
                prepassMode = 7;
            else if (option == "--ssao-blurred")
                prepassMode = 6;
            else if (option == "--no-ssao")
                prepassMode = 0;
            else if (option == "--ssao-view")
                prepassMode = 5;
            else if (option == "--ao-timing" && i + 1 < argc)
                aoTimingPath = argv[++i];
            else if (option == "--ao-warmup" && i + 1 < argc)
                aoWarmup = std::stoi(argv[++i]);
            else if (option == "--ao-measure" && i + 1 < argc)
                aoMeasure = std::stoi(argv[++i]);
            else if (option == "--ao-resolution" && i + 1 < argc)
            {
                const std::string scale = argv[++i];
                if (scale != "full" && scale != "half")
                    throw std::runtime_error("--ao-resolution must be full or half");
                halfAo = scale == "half";
            }
            else if (option == "--ao-blur" && i + 1 < argc)
            {
                const std::string mode = argv[++i];
                if (mode != "plane" && mode != "depth")
                    throw std::runtime_error("--ao-blur must be plane or depth");
                planeAwareBlur = mode == "plane";
            }
            else if (option == "--ao-radius" && i + 1 < argc)
                aoRadius = std::stof(argv[++i]);
            else if (option == "--ao-bias" && i + 1 < argc)
                aoBias = std::stof(argv[++i]);
            else if (option == "--ao-samples" && i + 1 < argc)
                aoSamples = std::stoi(argv[++i]);
            else if (option == "--prepass")
                prepassMode = 4;
            else if (option == "--verify-prepass")
            {
                verifyPrepass = true;
                if (prepassMode == 0)
                    prepassMode = 4;
            }
            else if (option == "--prepass-view" && i + 1 < argc)
            {
                const std::string view = argv[++i];
                if (view == "normal")
                    prepassMode = 1;
                else if (view == "depth")
                    prepassMode = 2;
                else if (view == "world-normal")
                    prepassMode = 3;
                else
                    throw std::runtime_error("--prepass-view must be normal, depth or world-normal");
            }
            else if (option == "--depth-range" && i + 1 < argc)
                depthRange = std::stof(argv[++i]);
            else if (option == "--width" && i + 1 < argc)
                width = std::stoi(argv[++i]);
            else if (option == "--height" && i + 1 < argc)
                height = std::stoi(argv[++i]);
            else if (option == "--resize-after" && i + 1 < argc)
                resizeAfter = std::stoi(argv[++i]);
            else if (option == "--resize-width" && i + 1 < argc)
                resizeWidth = std::stoi(argv[++i]);
            else if (option == "--resize-height" && i + 1 < argc)
                resizeHeight = std::stoi(argv[++i]);
            else if (option == "--benchmark-culling" && i + 1 < argc)
                benchmarkPath = argv[++i];
            else if (option == "--benchmark-captures")
                benchmarkCaptures = true;
            else if (option == "--benchmark-warmup" && i + 1 < argc)
                benchmarkWarmup = std::stoi(argv[++i]);
            else if (option == "--benchmark-samples" && i + 1 < argc)
                benchmarkSamples = std::stoi(argv[++i]);
            else if (option == "--benchmark-repeats" && i + 1 < argc)
                benchmarkRepeats = std::stoi(argv[++i]);
            else if (option == "--animated-scene" && i + 1 < argc)
            {
                animatedPath = argv[++i];
                prepassMode = 7;
            }
            else if (option == "--animation" && i + 1 < argc)
                animationName = argv[++i];
            else if (option == "--ray-query-scene") {rayScene=true;prepassMode=7;}
            else if (option == "--verify-ray-query") {verifyRay=true;verifyPrepass=true;}
            else if (option == "--ray-query-cycle-test") rayCycle=true;
            else if (option == "--mirror-styles" && i+1<argc){std::string value=argv[++i];if(value!="on" && value!="off")throw std::runtime_error("Mirror styles must be on|off");mirrorStyles=value=="on";}
            else if (option == "--ray-view" && i+1<argc) rayView=std::stoi(argv[++i]);
            else if (option == "--ray-reflections" && i+1<argc) {std::string value=argv[++i];if(value!="on"&&value!="off")throw std::runtime_error("--ray-reflections expects on|off");rayEnabled=value=="on";}
            else if (option == "--animation-controls-test")
                animationControlsTest=true;
            else if (option == "--no-animation-panel")
                animationPanel=false;
            else if (option == "--skinning" && i + 1 < argc)
            {
                skinningMode = argv[++i];
                if (skinningMode != "linear" && skinningMode != "dual-quaternion")
                    throw std::runtime_error("--skinning expects linear or dual-quaternion");
            }
            else if (option == "--skinning-normals" && i + 1 < argc)
            {
                skinningNormals = argv[++i];
                if (skinningNormals != "surface" && skinningNormals != "transformed")
                    throw std::runtime_error("--skinning-normals expects surface or transformed");
            }
            else if (option == "--animation-time" && i + 1 < argc)
            {
                animationFrozen = std::stof(argv[++i]);
                if (!std::isfinite(animationFrozen) || animationFrozen < 0)
                    throw std::runtime_error("Invalid animation time");
            }
            else if (option == "--verify-skinning")
                verifyPrepass = true;
            else if (option == "--scene" && i + 1 < argc)
                scenePath = argv[++i];
            else if (option == "--stress-count" && i + 1 < argc)
            {
                const int count = std::stoi(argv[++i]);
                if (count < 1 || count > 10000)
                    throw std::runtime_error("--stress-count must be 1..10000");
                stressCount = static_cast<uint32_t>(count);
            }
            else if (option == "--capture-every-frame")
                captureEveryFrame = true;
            else if (option == "--capture-frame" && i + 1 < argc)
                captureFrame = std::stoi(argv[++i]);
            else if (option == "--capture" && i + 1 < argc)
                capturePath = argv[++i];
            else if (option == "--shadow-mode" && i + 1 < argc)
                shadowMode = std::stoi(argv[++i]);
            else if (option == "--yaw" && i + 1 < argc)
            {
                yaw = std::stof(argv[++i]);
                yawSpecified = true;
            }
            else if (option == "--pitch" && i + 1 < argc)
            {
                pitch = std::stof(argv[++i]);
                pitchSpecified = true;
            }
            else if (option == "--camera-sweep-start-at" && i + 1 < argc)
                sweepStart = std::stoi(argv[++i]);
            else if (option == "--camera-sweep-stop-at" && i + 1 < argc)
                sweepStop = std::stoi(argv[++i]);
            else if (option == "--camera-sweep" && i + 1 < argc)
                cameraSweep = std::stof(argv[++i]);
            else if (option == "--camera-distance" && i + 1 < argc)
                cameraDistance = std::stof(argv[++i]);
            else if (option == "--target-offset-x" && i + 1 < argc)
                targetOffsetX = std::stof(argv[++i]);
            else if (option == "--mixed-scene")
            {
                mixedScene = true;
                occlusionScene = true;
            }
            else if (option == "--inspect-group" && i + 1 < argc)
            {
                inspectGroup = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--hiz-inspector")
                hiZInspector = true;
            else if (option == "--inspect-instance" && i + 1 < argc)
            {
                inspectInstance = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--hiz-mip" && i + 1 < argc)
            {
                inspectMip = std::stoi(argv[++i]);
                hiZInspector = true;
            }
            else if (option == "--occlusion-scene")
                occlusionScene = true;
            else if (option == "--culling-observer")
                cullingObserver = true;
            else if (option == "--no-occlusion")
                occlusion = false;
            else if (option == "--no-culling")
                culling = false;
            else if (option == "--verify-culling")
                verifyCulling = true;
            else if (option == "--paused")
                paused = true;
            else if (option == "--material-view" && i + 1 < argc)
                materialView = std::stoi(argv[++i]);
            else if (option == "--lighting-view" && i + 1 < argc)
                lightingView = std::stoi(argv[++i]);
            else if (option == "--environment-degrees" && i + 1 < argc)
                environmentDegrees = std::stof(argv[++i]);
            else if (option == "--frames" && i + 1 < argc)
            {
                frameLimit = std::stoi(argv[++i]);
                if (frameLimit <= 0)
                    throw std::runtime_error("--frames must be positive");
            }
            else
                throw std::runtime_error(
                    "Usage: VulkanRenderingLab [--ray-query-scene] [--verify-ray-query] "
                    "[--ray-reflections on|off] [--mirror-styles on|off] [--ray-view 0..2] [--ray-query-cycle-test] "
                    "[--inspect-animation path.glb] [--animated-scene path.glb] "
                    "[--animation name] [--animation-time seconds] [--skinning linear|dual-quaternion] "
                    "[--skinning-normals surface|transformed] "
                    "[--verify-skinning] [--motion-view] "
                    "[--verify-motion] [--ssao] [--ssao-blurred] "
                    "[--ssao-view] [--no-ssao] "
                    "[--ao-response fixed|adaptive] [--ao-history-view ao|status|weight] [--ao-motion-scene] "
                    "[--ao-box-step] [--ao-sampling "
                    "fixed|varying] [--ao-temporal on|off] [--ao-temporal-cycle-test] "
                    "[--ao-timing output.csv] [--ao-warmup "
                    "count] [--ao-measure count] [--ao-resolution "
                    "full|half] [--ao-blur plane|depth] [--ao-radius value] [--ao-bias "
                    "value] "
                    "[--ao-samples 1..64] [--prepass] [--prepass-view normal|depth|world-normal] "
                    "[--verify-prepass] [--depth-range value] [--width pixels] [--height pixels] [--scene "
                    "path.gltf|path.glb] [--frames count] [--stress-count "
                    "1..10000] [--benchmark-culling output.csv] [--benchmark-warmup 30] [--benchmark-samples "
                    "120] [--benchmark-repeats 3] [--benchmark-captures] [--mixed-scene] [--occlusion-scene] "
                    "[--inspect-group index] [--hiz-inspector] [--hiz-mip -1..11] [--inspect-instance "
                    "offset] [--culling-observer] [--no-occlusion] [--no-culling] [--verify-culling] "
                    "[--capture-every-frame] [--camera-sweep-start-at frame] [--camera-sweep-stop-at frame] "
                    "[--camera-distance value] [--target-offset-x value] [--material-view 0..5] "
                    "[--lighting-view 0..3] [--environment-degrees angle]");
        }
        if (!aoTimingPath.empty())
        {
            if (aoWarmup < 0 || aoWarmup > 10000 || aoMeasure < 2 || aoMeasure > 10000)
                throw std::runtime_error("Invalid AO timing counts");
            if (verifyPrepass || verifyCulling || aoCycleTest || aoResolutionCycle || resizeAfter ||
                cameraSweep != 0 || !capturePath.empty() || !benchmarkPath.empty() || cullingObserver ||
                hiZInspector || renderdocCapture)
                throw std::runtime_error("AO timing cannot combine verification, capture, resize, sweep, "
                                         "observer or other benchmarks");
            if (prepassMode == 0)
                prepassMode = 7;
            if (prepassMode != 7 || materialView || lightingView)
                throw std::runtime_error("AO timing requires final composition");
            if (!frameLimit)
                frameLimit = aoWarmup + aoMeasure + 1;
            if (frameLimit < aoWarmup + aoMeasure + 1)
                throw std::runtime_error("--frames too small for AO timing capture");
            paused = true;
        }
        if (captureEveryFrame && (capturePath.empty() || !frameLimit || captureFrame != 1 || aoCycleTest ||
                                  aoResolutionCycle || temporalCycle || resizeAfter || !aoTimingPath.empty()))
            throw std::runtime_error("--capture-every-frame requires --capture, bounded --frames and no "
                                     "cycle/resize/timing/capture-frame options");
        if (sweepStart < 0 || sweepStop < -1 || (sweepStop >= 0 && sweepStop < sweepStart) ||
            ((sweepStart != 0 || sweepStop != -1) && (!frameLimit || !aoTimingPath.empty())))
            throw std::runtime_error("Invalid camera sweep interval (zero-based bounded frame indices)");
        if (captureFrame < 1 || (frameLimit && captureFrame > frameLimit) ||
            (captureFrame != 1 && capturePath.empty()))
            throw std::runtime_error("Invalid --capture-frame (1-based, requires --capture)");
        if ((motionInstanceTest || motionResetAt >= 0) &&
            (!frameLimit || motionResetAt >= frameLimit || !aoTimingPath.empty()))
            throw std::runtime_error("Motion diagnostics require bounded --frames and no AO timing");
        if (width < 64 || height < 64 || width > 4096 || height > 4096 || resizeWidth < 64 ||
            resizeHeight < 64 || resizeWidth > 4096 || resizeHeight > 4096 || resizeAfter < 0)
            throw std::runtime_error("Invalid window/resize dimensions (64..4096)");
        if (resizeAfter && (!frameLimit || resizeAfter >= frameLimit))
            throw std::runtime_error("--resize-after requires a larger --frames limit");
        if (!benchmarkPath.empty())
        {
            if (prepassMode || resizeAfter || !scenePath.empty() || cullingObserver || hiZInspector ||
                verifyCulling || frameLimit || cameraSweep != 0 || !capturePath.empty())
                throw std::runtime_error("Culling benchmark cannot combine scene, observer, verification, "
                                         "frames, sweep or capture");
            if (benchmarkWarmup < 1 || benchmarkSamples < 2 || benchmarkRepeats < 1 ||
                benchmarkRepeats > 20 || benchmarkWarmup > 10000 || benchmarkSamples > 10000)
                throw std::runtime_error(
                    "Invalid benchmark warmup (1..10000), samples (2..10000), repeats (1..20)");
            occlusionScene = true;
            paused = true;
        }
        if (aoResolutionCycle && (frameLimit < 6 || !benchmarkPath.empty()))
            throw std::runtime_error("AO resolution cycle requires --frames >=6 and no benchmark");
        if (aoCycleTest && (!frameLimit || frameLimit < 8 || !benchmarkPath.empty()))
            throw std::runtime_error("--ao-cycle-test requires --frames >=8 and no benchmark");
        if (cameraSweep != 0.0f && !frameLimit)
            throw std::runtime_error("--camera-sweep requires a bounded --frames run");
        if (occlusionScene && !stressCount)
            stressCount = 1600;
        if (occlusionScene && !pitchSpecified)
            pitch = 25.0f;
#ifdef _WIN32
        RENDERDOC_API_1_0_0 *renderdoc = nullptr;
        if (renderdocCapture)
        {
            auto module = GetModuleHandleA("renderdoc.dll");
            auto get = module
                           ? reinterpret_cast<pRENDERDOC_GetAPI>(GetProcAddress(module, "RENDERDOC_GetAPI"))
                           : nullptr;
            if (!get || !get(eRENDERDOC_API_Version_1_0_0, reinterpret_cast<void **>(&renderdoc)))
                throw std::runtime_error("Launch under RenderDoc to use --renderdoc-capture");
        }
#endif
        if (temporalCycle && (frameLimit < 6 || !aoTimingPath.empty()))
            throw std::runtime_error("Temporal cycle requires --frames >= 6 and no AO timing");
        if (aoMotionScene &&
            (!scenePath.empty() || stressCount || motionInstanceTest || !aoTimingPath.empty()))
            throw std::runtime_error(
                "AO motion scene is standalone and cannot use other scenes/instance tests/timing");
        if (aoBoxStep && (!aoMotionScene || !frameLimit))
            throw std::runtime_error("Box step requires bounded AO motion scene");
        if (!animatedPath.empty() && (!scenePath.empty() || stressCount || aoMotionScene ||
                                      motionInstanceTest || aoBoxStep || !aoTimingPath.empty()))
            throw std::runtime_error(
                "Animated scene is standalone; no stress/other scene/instance tests/timing");
        if(rayScene && (!scenePath.empty() || stressCount || aoMotionScene || motionInstanceTest || !aoTimingPath.empty() || !benchmarkPath.empty()))throw std::runtime_error("Ray Query scene cannot combine other scenes or timing harnesses");
        if((verifyRay || rayCycle || rayView) && !rayScene)throw std::runtime_error("Ray diagnostics require --ray-query-scene");
        if(rayView<0 || rayView>2 || (rayCycle && frameLimit<4))throw std::runtime_error("Ray view must be 0..2; cycle test needs --frames >=4");
        if(rayScene && !pitchSpecified)pitch=14.f;
        if(rayScene && !animatedPath.empty() && !yawSpecified)yaw=20.f;
        if(animationControlsTest && (animatedPath.empty() || frameLimit<13))
            throw std::runtime_error("--animation-controls-test requires --animated-scene and --frames >=13");
        if (!animatedPath.empty() && !rayScene && !yawSpecified)
            yaw = 90.f;
        Window window(width, height, "Vulkan Rendering Lab");
        auto createContext = [&]() {
            auto result = std::make_unique<VulkanContext>(window, scenePath, stressCount, verifyCulling,
                                                          occlusionScene, mixedScene, verifyPrepass, halfAo,
                                                          aoMotionScene, animatedPath,rayScene,verifyRay);
            if(rayScene){result->SetRayReflections(rayEnabled,rayView);result->SetMirrorStyles(mirrorStyles);}
            if (!animatedPath.empty())
            {
                result->SetAnimationClip(animationName);
                result->SetDualQuaternionSkinning(skinningMode == "dual-quaternion");
                result->SetSurfaceSkinningNormals(skinningNormals == "surface");
                if(animationFrozen>=0)result->SetAnimationTime(animationFrozen);
                result->Playback()->visible=animationPanel;
            }
            result->SetOcclusion(occlusion);
            result->SetCulling(culling, verifyCulling);
            result->SetInspection(materialView, lightingView, environmentDegrees);
            result->ConfigureShadowDiagnostic(shadowMode, yaw, pitch, paused);
            result->ConfigureCullingCamera(cameraDistance, targetOffsetX);
            result->ConfigureAo(aoRadius, aoBias, aoSamples, planeAwareBlur);
            result->ConfigurePrepass(prepassMode, depthRange);
            result->SetTemporalAo(temporalAo);
            result->SetVaryingAoSampling(varyingSampling);
            result->SetAoHistoryView(aoHistoryView);
            result->SetAdaptiveAo(adaptiveAo);
            result->ConfigureHiZInspector(hiZInspector, inspectInstance, inspectMip, inspectGroup);
            if (cullingObserver)
                result->StartCullingObserver();
            return result;
        };
        auto context = createContext();
        if (!aoTimingPath.empty())
        {
            context->StartAoTiming(aoTimingPath, aoWarmup, aoMeasure);
            std::ofstream args(aoTimingPath + ".args.txt");
            if (!args)
                throw std::runtime_error("Cannot create AO timing arguments file");
            for (int i = 0; i < argc; ++i)
                args << std::quoted(argv[i]) << ' ';
            args << '\n';
        }
        const bool initialHalfAo = halfAo;
        if (!benchmarkPath.empty())
        {
            context->RunCullingBenchmark(benchmarkPath, benchmarkWarmup, benchmarkSamples, benchmarkRepeats,
                                         benchmarkCaptures);
            return EXIT_SUCCESS;
        }
        if (!capturePath.empty() && captureFrame == 1 && !captureEveryFrame)
            context->CaptureNextFrame(capturePath);
        int frames = 0;
        const double aoMotionStart = glfwGetTime();

        double lastFrameTime = glfwGetTime();

        while (!window.ShouldClose())
        {
            const double currentFrameTime = glfwGetTime();

            const double deltaTime = currentFrameTime - lastFrameTime;

            lastFrameTime = currentFrameTime;

            window.PollEvents();

            if (aoResolutionCycle)
                context->SetHalfAo(frames >= 2 && frames < 4 ? !initialHalfAo : initialHalfAo);
            int framebufferWidth = 0, framebufferHeight = 0;
            glfwGetFramebufferSize(window.GetHandle(), &framebufferWidth, &framebufferHeight);
            if (framebufferWidth == 0 || framebufferHeight == 0)
            {
                if (context->AoTimingActive())
                    throw std::runtime_error("Window minimized during AO timing; rerun at fixed size");
                glfwWaitEventsTimeout(.05);
                continue;
            }
            const auto extent = context->Extent();
            if (uint32_t(framebufferWidth) != extent.width || uint32_t(framebufferHeight) != extent.height ||
                context->NeedsRebuild())
            {
                // Wait for a short quiet interval to avoid rebuilding on every mouse movement.
                double stable = glfwGetTime();
                int previousWidth = framebufferWidth, previousHeight = framebufferHeight;
                while (!window.ShouldClose() && glfwGetTime() - stable < .15)
                {
                    glfwWaitEventsTimeout(.025);
                    glfwGetFramebufferSize(window.GetHandle(), &framebufferWidth, &framebufferHeight);
                    if (framebufferWidth != previousWidth || framebufferHeight != previousHeight)
                    {
                        stable = glfwGetTime();
                        previousWidth = framebufferWidth;
                        previousHeight = framebufferHeight;
                    }
                }
                if (window.ShouldClose())
                    break;
                if (framebufferWidth == 0 || framebufferHeight == 0)
                    continue;
                if (context->AoTimingActive())
                    throw std::runtime_error("Window changed during AO timing; rerun at fixed size");
                halfAo = context->HalfAo();
                auto restore = context->SaveRuntimeState();
                context->FinishDiagnostics();
                context.reset();
                context = createContext();
                restore(*context);
                if (aoResolutionCycle && !capturePath.empty())
                    context->CaptureNextFrame(capturePath + (halfAo ? ".half.png" : ".full.png"));
                if (resizeAfter && !capturePath.empty())
                    context->CaptureNextFrame(capturePath + ".resized.png");
                std::cout << "[Resize] rebuilt complete renderer at " << context->Extent().width << 'x'
                          << context->Extent().height << std::endl;
                lastFrameTime = glfwGetTime();
                continue;
            }
            if (aoCycleTest)
            {
                const int modes[] = {0, 7, 0, 5, 6, 7, 0, 7};
                const int mode = modes[frames % 8];
                context->ConfigurePrepass(mode, depthRange);
                if (!capturePath.empty())
                    context->CaptureNextFrame(capturePath + ".frame" + std::to_string(frames) + ".mode" +
                                              std::to_string(mode) + ".png");
            }
            if (temporalCycle)
                context->SetTemporalAo(frames < 2 || frames >= 4);
            if (cameraSweep != 0.0f)
                context->ConfigureShadowDiagnostic(
                    shadowMode,
                    yaw + float(std::max(0,
                                         std::min(frames, sweepStop < 0 ? frames : sweepStop) - sweepStart)) *
                              cameraSweep,
                    pitch, paused);
#ifdef _WIN32
            if (renderdocCapture && frames == 0)
                renderdoc->TriggerCapture();
#endif
            if (captureEveryFrame)
                context->CaptureNextFrame(capturePath + ".frame" + std::to_string(frames) + ".png");
            if (!capturePath.empty() && captureFrame > 1 && frames + 1 == captureFrame)
                context->CaptureNextFrame(capturePath);
            if (motionResetAt >= 0 && frames == motionResetAt)
                context->ResetMotionHistory();
            if (motionInstanceTest)
                context->SetMotionTestOffset(float(frames) * .08f);
            if (aoMotionScene)
                context->SetAoBoxOffset(aoBoxStep
                                            ? (frames < 24 ? -.9f : .9f)
                                            : AoBoxOffset(frameLimit ? float(frames) / 60.f
                                                                     : float(glfwGetTime() - aoMotionStart)));
            if(rayCycle){const bool enabled=frames%3!=1;const int view=frames%4==3?2:0;context->SetRayReflections(enabled,view);std::cout<<"[Ray Query Cycle] frame="<<frames<<" enabled="<<enabled<<" view="<<view<<'\n';}
            if(animationControlsTest){
                auto& c=*context->Playback();
                if(frames==1 && c.playing)c.Activate(AnimationControls::Play);
                if(frames==2){c.Press(float(12+300*.52/c.Duration()),146);c.scrubbing=false;}
                if(frames==3)c.Activate(AnimationControls::Faster);
                if(frames==4){c.Activate(AnimationControls::Linear);c.Activate(AnimationControls::Authored);}
                if(frames==5)c.Activate(AnimationControls::Next);
                if(frames==6)c.Activate(AnimationControls::Play);
                if(frames==8)c.Seek(c.Duration());
                if(frames==9)c.Activate(AnimationControls::Restart);
                if(frames==10){c.Activate(AnimationControls::Dual);c.Activate(AnimationControls::Surface);}
                if(frames==11)c.visible=false;
                if(frames==12)c.visible=true;
            }
            context->AdvanceAnimation(frameLimit?(frames?1./60.:0.):deltaTime);
            if(animationControlsTest){const auto& c=*context->Playback();std::ostringstream state;state<<std::setprecision(10)<<"[Animation Controls] frame="<<frames<<" clip="<<c.clip<<" time="<<c.time<<" speed="<<c.speed<<" playing="<<c.playing<<" dual="<<c.dual<<" surface="<<c.surface<<" panel="<<c.visible<<'\n';std::cout<<state.str();}
            context->UpdateUniformBuffer();
            context->DrawFrame();
            context->UpdateWindowTitle(deltaTime);
            ++frames;
            if (resizeAfter && frames == resizeAfter)
                glfwSetWindowSize(window.GetHandle(), resizeWidth, resizeHeight);
            if (frameLimit && frames >= frameLimit)
                break;
        }
        context->FinishDiagnostics();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Application error: " << e.what() << std::endl;

        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
