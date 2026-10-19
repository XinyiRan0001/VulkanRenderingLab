#version 450

struct PointLight
{
    vec4 position;
    vec4 color;
};

layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;

    ivec4 shadowSettings;

    PointLight pointLight;

    vec4 cameraPosition;
    vec4 lightingSettings;
} ubo;


// CPU InstanceData is 80 bytes; std430 uses the same array stride.
struct InstanceData { mat4 model; vec4 materialProperties; };
layout(std430, binding = 12) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(std430, binding = 13) readonly buffer VisibleInstances { uint visibleIds[]; };
layout(push_constant) uniform MaterialPushConstant {
    ivec4 flags; vec4 baseColor; vec4 emissive; vec4 textureSettings; vec4 properties;
} material;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec4 inTangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out vec3 fragWorldPosition;
layout(location = 4) out vec4 fragLightSpacePosition;
layout(location = 5) flat out int fragShadowMode;

layout(location = 6) out vec4 fragTangent;
layout(location = 7) flat out vec4 fragInstanceProperties;

void main()
{
    // gl_InstanceIndex already includes firstInstance from vkCmdDrawIndexed.
    uint instanceId = material.flags.z != 0 ? visibleIds[uint(material.flags.w)+gl_InstanceIndex] : uint(gl_InstanceIndex);
    InstanceData instance = instances[instanceId];
    mat4 objectToWorld = ubo.model * instance.model;
    fragInstanceProperties = instance.materialProperties;
    vec4 worldPosition =
        objectToWorld *
        vec4(inPosition, 1.0);

    gl_Position =
        ubo.proj *
        ubo.view *
        worldPosition;

    fragColor = inColor;
    fragTangent = vec4(mat3(objectToWorld) * inTangent.xyz, inTangent.w * sign(determinant(mat3(objectToWorld))));

    fragNormal =
        mat3(transpose(inverse(objectToWorld))) *
        inNormal;

    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;

    fragLightSpacePosition =
        ubo.lightViewProj *
        worldPosition;
        
    fragShadowMode =
    ubo.shadowSettings.x;
}