#version 450

layout(binding = 0) uniform UniformBufferObject
{
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightViewProj;

    ivec4 shadowSettings;

    vec4 pointLightPosition;
    vec4 pointLightColor;
} ubo;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position =
        ubo.lightViewProj *
        ubo.model *
        vec4(inPosition, 1.0);
}