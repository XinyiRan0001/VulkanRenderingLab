#version 450

layout(location = 0) in vec3 localPosition;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube environmentMap;

const float PI = 3.14159265359;

void main()
{
    vec3 N =
        normalize(localPosition);

    // Build a stable tangent basis.
    // Using a fixed world-up vector alone can become degenerate
    // when N is parallel to that vector.
    vec3 helperAxis =
        abs(N.y) < 0.999
        ? vec3(0.0, 1.0, 0.0)
        : vec3(1.0, 0.0, 0.0);

    vec3 right =
        normalize(
            cross(
                helperAxis,
                N
            )
        );

    vec3 up =
        normalize(
            cross(
                N,
                right
            )
        );

    vec3 irradiance =
        vec3(0.0);

    float sampleCount =
        0.0;

    const float sampleDelta =
        0.025;

    for (
        float phi = 0.0;
        phi < 2.0 * PI;
        phi += sampleDelta
    )
    {
        for (
            float theta = 0.0;
            theta < 0.5 * PI;
            theta += sampleDelta
        )
        {
            vec3 tangentSample =
                vec3(
                    sin(theta) * cos(phi),
                    sin(theta) * sin(phi),
                    cos(theta)
                );

            vec3 sampleDirection =
                normalize(
                    tangentSample.x * right +
                    tangentSample.y * up +
                    tangentSample.z * N
                );

            irradiance +=
                texture(
                    environmentMap,
                    sampleDirection
                ).rgb *
                cos(theta) *
                sin(theta);

            sampleCount +=
                1.0;
        }
    }

    irradiance =
        PI *
        irradiance /
        max(
            sampleCount,
            1.0
        );

    outColor =
        vec4(
            irradiance,
            1.0
        );
}