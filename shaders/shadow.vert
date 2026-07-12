#version 450

layout(push_constant) uniform ShadowPushConstant
{
    mat4 lightViewProj;
    mat4 model;
} shadowData;

layout(location = 0) in vec3 inPosition;

void main()
{
    gl_Position =
        shadowData.lightViewProj *
        shadowData.model *
        vec4(inPosition, 1.0);
}