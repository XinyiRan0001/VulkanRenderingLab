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

void main()
{
    vec4 worldPosition =
        ubo.model *
        vec4(inPosition, 1.0);

    gl_Position =
        ubo.proj *
        ubo.view *
        worldPosition;

    fragColor = inColor;
    fragTangent = vec4(mat3(ubo.model) * inTangent.xyz, inTangent.w * sign(determinant(mat3(ubo.model))));

    fragNormal =
        mat3(transpose(inverse(ubo.model))) *
        inNormal;

    fragTexCoord = inTexCoord;
    fragWorldPosition = worldPosition.xyz;

    fragLightSpacePosition =
        ubo.lightViewProj *
        worldPosition;
        
    fragShadowMode =
    ubo.shadowSettings.x;
}