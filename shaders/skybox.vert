#version 450

layout(location = 0) out vec3 outDirection;

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

const vec3 SKYBOX_VERTICES[36] =
{
    // Back face
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),

    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),

    // Front face
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),

    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),

    // Left face
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),

    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),

    // Right face
    vec3(1.0, -1.0, -1.0),
    vec3(1.0,  1.0,  1.0),
    vec3(1.0, -1.0,  1.0),

    vec3(1.0, -1.0, -1.0),
    vec3(1.0,  1.0, -1.0),
    vec3(1.0,  1.0,  1.0),

    // Bottom face
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),

    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3(-1.0, -1.0,  1.0),

    // Top face
    vec3(-1.0, 1.0, -1.0),
    vec3(-1.0, 1.0,  1.0),
    vec3( 1.0, 1.0,  1.0),

    vec3(-1.0, 1.0, -1.0),
    vec3( 1.0, 1.0,  1.0),
    vec3( 1.0, 1.0, -1.0)
};

void main()
{
    vec3 position = SKYBOX_VERTICES[gl_VertexIndex];

    outDirection = position;

    // Remove camera translation so the sky remains infinitely far away.
    mat4 rotationOnlyView = mat4(mat3(ubo.view));

    vec4 clipPosition =
        ubo.proj *
        rotationOnlyView *
        vec4(position, 1.0);

    // Force skybox depth to the far plane.
    gl_Position = clipPosition.xyww;
}