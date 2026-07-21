#version 450

layout(location = 0) in vec3 inDirection;

layout(location = 0) out vec4 outColor;

// Equirectangular HDR environment texture.
layout(binding = 3) uniform sampler2D hdrEnvironment;

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

vec3 ToneMapACES(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;

    return clamp(
        (color * (a * color + b)) /
        (color * (c * color + d) + e),
        0.0,
        1.0
    );
}

void main()
{
    vec3 direction =
        normalize(inDirection);

    vec2 uv =
        DirectionToEquirectangularUV(direction);

    vec3 hdrColor =
        texture(
            hdrEnvironment,
            uv
        ).rgb;

    const float exposure = 1.0;

    hdrColor *= exposure;

    vec3 mappedColor =
        ToneMapACES(hdrColor);

    mappedColor =
        pow(
            mappedColor,
            vec3(1.0 / 2.2)
        );

    outColor =
        vec4(
            mappedColor,
            1.0
        );
}