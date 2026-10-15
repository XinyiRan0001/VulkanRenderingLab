#version 450

layout(location = 0) in vec3 inDirection;

layout(location = 0) out vec4 outColor;

// Equirectangular HDR environment texture.
layout(binding = 3) uniform sampler2D hdrEnvironment;

struct PointLight { vec4 position; vec4 color; };
layout(binding = 0) uniform UniformBufferObject {
    mat4 model; mat4 view; mat4 proj; mat4 lightViewProj;
    ivec4 shadowSettings;
    PointLight pointLight;
    vec4 cameraPosition;
    vec4 lightingSettings;
} ubo;

const float PI = 3.14159265359;

vec2 DirectionToEquirectangularUV(vec3 direction)
{
    direction =
        normalize(direction);

    float longitude =
        atan(
            direction.z,
            direction.x
        );

    float latitude =
        asin(
            clamp(
                direction.y,
                -1.0,
                1.0
            )
        );

    vec2 uv;

    uv.x =
        longitude /
        (2.0 * PI) +
        0.5;

    uv.y =
        0.5 +
        latitude / PI;

    return uv;
}

vec3 RotateEnvironment(vec3 direction)
{
    float angle = ubo.lightingSettings.x;
    float c = cos(angle), s = sin(angle);
    return vec3(c * direction.x + s * direction.z, direction.y,
                -s * direction.x + c * direction.z);
}

void main()
{
    vec3 direction =
        normalize(inDirection);

    vec2 uv =
        DirectionToEquirectangularUV(
            RotateEnvironment(direction)
        );

    vec3 hdrColor =
        texture(
            hdrEnvironment,
            uv
        ).rgb;

    float exposure = max(float(ubo.shadowSettings.z) / 100.0, 0.01);
    // Match the main pass so background and reflected radiance respond together.
    vec3 mappedColor = vec3(1.0) - exp(-hdrColor * exposure);

    // Do not apply manual gamma correction here.
    // The swapchain format is VK_FORMAT_B8G8R8A8_SRGB.
    outColor =
        vec4(
            mappedColor,
            1.0
        );
}