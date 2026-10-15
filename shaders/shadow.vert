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


// CPU InstanceData is 80 bytes; std430 uses the same array stride.
struct InstanceData { mat4 model; vec4 materialProperties; };
layout(std430, binding = 12) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position =
        ubo.lightViewProj *
        ubo.model *
        instances[gl_InstanceIndex].model *
        vec4(inPosition, 1.0);
}