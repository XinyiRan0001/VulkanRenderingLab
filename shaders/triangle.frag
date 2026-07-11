#version 450

layout(binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform MaterialPushConstant
{
    int useTexture;
} material;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main()
{
    if (material.useTexture != 0)
    {
        outColor = texture(texSampler, fragTexCoord);
    }
    else
    {
        outColor = vec4(fragColor, 1.0);
    }
}