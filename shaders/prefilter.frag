#version 450

layout(binding = 0) uniform samplerCube environmentMap;

layout(push_constant) uniform PrefilterPushConstants
{
    mat4 viewProjection;
    vec4 parameters;
} pushConstants;

layout(location = 0) in vec3 inLocalDirection;

layout(location = 0) out vec4 outColor;

const float PI =
    3.14159265359;

const uint SAMPLE_COUNT =
    1024u;

float RadicalInverseVdC(
    uint bits
)
{
    bits =
        (bits << 16u) |
        (bits >> 16u);

    bits =
        ((bits & 0x55555555u) << 1u) |
        ((bits & 0xAAAAAAAAu) >> 1u);

    bits =
        ((bits & 0x33333333u) << 2u) |
        ((bits & 0xCCCCCCCCu) >> 2u);

    bits =
        ((bits & 0x0F0F0F0Fu) << 4u) |
        ((bits & 0xF0F0F0F0u) >> 4u);

    bits =
        ((bits & 0x00FF00FFu) << 8u) |
        ((bits & 0xFF00FF00u) >> 8u);

    return float(bits) *
        2.3283064365386963e-10;
}

vec2 Hammersley(
    uint index,
    uint sampleCount
)
{
    return vec2(
        float(index) /
            float(sampleCount),

        RadicalInverseVdC(index)
    );
}

float DistributionGGX(
    vec3 normal,
    vec3 halfwayVector,
    float roughness
)
{
    float alpha =
        roughness * roughness;

    float alphaSquared =
        alpha * alpha;

    float normalDotHalfway =
        max(
            dot(
                normal,
                halfwayVector
            ),
            0.0
        );

    float normalDotHalfwaySquared =
        normalDotHalfway *
        normalDotHalfway;

    float denominator =
        normalDotHalfwaySquared *
        (
            alphaSquared - 1.0
        ) +
        1.0;

    denominator =
        PI *
        denominator *
        denominator;

    return alphaSquared /
        max(
            denominator,
            0.000001
        );
}

vec3 ImportanceSampleGGX(
    vec2 xi,
    vec3 normal,
    float roughness
)
{
    float alpha =
        roughness * roughness;

    float phi =
        2.0 *
        PI *
        xi.x;

    float cosTheta =
        sqrt(
            max(
                (
                    1.0 - xi.y
                ) /
                (
                    1.0 +
                    (
                        alpha * alpha -
                        1.0
                    ) *
                    xi.y
                ),
                0.0
            )
        );

    float sinTheta =
        sqrt(
            max(
                1.0 -
                cosTheta *
                cosTheta,
                0.0
            )
        );

    vec3 halfwayTangent =
        vec3(
            cos(phi) * sinTheta,
            sin(phi) * sinTheta,
            cosTheta
        );

    vec3 upVector =
        abs(normal.z) < 0.999
        ? vec3(0.0, 0.0, 1.0)
        : vec3(1.0, 0.0, 0.0);

    vec3 tangent =
        normalize(
            cross(
                upVector,
                normal
            )
        );

    vec3 bitangent =
        cross(
            normal,
            tangent
        );

    vec3 halfwayWorld =
        tangent *
            halfwayTangent.x +
        bitangent *
            halfwayTangent.y +
        normal *
            halfwayTangent.z;

    return normalize(
        halfwayWorld
    );
}

void main()
{
    vec3 normal =
        normalize(
            inLocalDirection
        );

    vec3 reflectionDirection =
        normal;

    vec3 viewDirection =
        reflectionDirection;

    float roughness =
        clamp(
            pushConstants.parameters.x,
            0.0,
            1.0
        );

    vec3 prefilteredColor =
        vec3(0.0);

    float totalWeight =
        0.0;

    ivec2 environmentSize =
        textureSize(
            environmentMap,
            0
        );

    float environmentResolution =
        float(
            environmentSize.x
        );

    float texelSolidAngle =
        4.0 *
        PI /
        (
            6.0 *
            environmentResolution *
            environmentResolution
        );

    for (
        uint sampleIndex = 0u;
        sampleIndex < SAMPLE_COUNT;
        ++sampleIndex
    )
    {
        vec2 xi =
            Hammersley(
                sampleIndex,
                SAMPLE_COUNT
            );

        vec3 halfwayVector =
            ImportanceSampleGGX(
                xi,
                normal,
                roughness
            );

        vec3 lightDirection =
            normalize(
                2.0 *
                dot(
                    viewDirection,
                    halfwayVector
                ) *
                halfwayVector -
                viewDirection
            );

        float normalDotLight =
            max(
                dot(
                    normal,
                    lightDirection
                ),
                0.0
            );

        if (normalDotLight > 0.0)
        {
            float distribution =
                DistributionGGX(
                    normal,
                    halfwayVector,
                    roughness
                );

            float normalDotHalfway =
                max(
                    dot(
                        normal,
                        halfwayVector
                    ),
                    0.0
                );

            float halfwayDotView =
                max(
                    dot(
                        halfwayVector,
                        viewDirection
                    ),
                    0.0
                );

            float probabilityDensity =
                distribution *
                normalDotHalfway /
                max(
                    4.0 *
                    halfwayDotView,
                    0.000001
                );

            probabilityDensity =
                max(
                    probabilityDensity,
                    0.000001
                );

            float sampleSolidAngle =
                1.0 /
                (
                    float(SAMPLE_COUNT) *
                    probabilityDensity
                );

            float mipLevel =
                roughness <= 0.0001
                ? 0.0
                : 0.5 *
                    log2(
                        sampleSolidAngle /
                        texelSolidAngle
                    );

            mipLevel =
                max(
                    mipLevel,
                    0.0
                );

            vec3 sampleColor =
    textureLod(
        environmentMap,
        lightDirection,
        0.0
    ).rgb;

            prefilteredColor +=
                sampleColor *
                normalDotLight;

            totalWeight +=
                normalDotLight;
        }
    }

    prefilteredColor /=
        max(
            totalWeight,
            0.000001
        );

    outColor =
        vec4(
            prefilteredColor,
            1.0
        );
}