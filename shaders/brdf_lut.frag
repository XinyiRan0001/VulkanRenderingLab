#version 450

layout(location = 0)
in vec2 fragmentUV;

layout(location = 0)
out vec4 outColor;

const float PI =
    3.14159265359;

float RadicalInverseVdC(
    uint bits
)
{
    bits =
        (bits << 16u) |
        (bits >> 16u);

    bits =
        (
            (bits & 0x55555555u) << 1u
        ) |
        (
            (bits & 0xAAAAAAAAu) >> 1u
        );

    bits =
        (
            (bits & 0x33333333u) << 2u
        ) |
        (
            (bits & 0xCCCCCCCCu) >> 2u
        );

    bits =
        (
            (bits & 0x0F0F0F0Fu) << 4u
        ) |
        (
            (bits & 0xF0F0F0F0u) >> 4u
        );

    bits =
        (
            (bits & 0x00FF00FFu) << 8u
        ) |
        (
            (bits & 0xFF00FF00u) >> 8u
        );

    return
        float(bits) *
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

vec3 ImportanceSampleGGX(
    vec2 randomSample,
    vec3 normal,
    float roughness
)
{
    float alpha =
        roughness *
        roughness;

    float phi =
        2.0 *
        PI *
        randomSample.x;

    float cosTheta =
        sqrt(
            (
                1.0 -
                randomSample.y
            ) /
            (
                1.0 +
                (
                    alpha *
                    alpha -
                    1.0
                ) *
                randomSample.y
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

    vec3 halfVector =
        vec3(
            cos(phi) *
            sinTheta,

            sin(phi) *
            sinTheta,

            cosTheta
        );

    vec3 upVector =
        abs(normal.z) <
        0.999
        ?
        vec3(0.0, 0.0, 1.0)
        :
        vec3(1.0, 0.0, 0.0);

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

    return normalize(
        tangent *
        halfVector.x +

        bitangent *
        halfVector.y +

        normal *
        halfVector.z
    );
}

float GeometrySchlickGGX(
    float NdotV,
    float roughness
)
{
    float k =
        (
            roughness *
            roughness
        ) /
        2.0;

    float denominator =
        NdotV *
        (
            1.0 -
            k
        ) +
        k;

    return
        NdotV /
        max(
            denominator,
            0.0001
        );
}

float GeometrySmith(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    float roughness
)
{
    float NdotV =
        max(
            dot(
                normal,
                viewDirection
            ),
            0.0
        );

    float NdotL =
        max(
            dot(
                normal,
                lightDirection
            ),
            0.0
        );

    float geometryView =
        GeometrySchlickGGX(
            NdotV,
            roughness
        );

    float geometryLight =
        GeometrySchlickGGX(
            NdotL,
            roughness
        );

    return
        geometryView *
        geometryLight;
}

vec2 IntegrateBRDF(
    float NdotV,
    float roughness
)
{
    vec3 viewDirection =
        vec3(
            sqrt(
                max(
                    1.0 -
                    NdotV *
                    NdotV,
                    0.0
                )
            ),
            0.0,
            NdotV
        );

    float scale =
        0.0;

    float bias =
        0.0;

    vec3 normal =
        vec3(
            0.0,
            0.0,
            1.0
        );

    const uint sampleCount =
        1024u;

    for (
        uint sampleIndex = 0u;
        sampleIndex < sampleCount;
        ++sampleIndex
    )
    {
        vec2 randomSample =
            Hammersley(
                sampleIndex,
                sampleCount
            );

        vec3 halfVector =
            ImportanceSampleGGX(
                randomSample,
                normal,
                roughness
            );

        vec3 lightDirection =
            normalize(
                2.0 *
                dot(
                    viewDirection,
                    halfVector
                ) *
                halfVector -
                viewDirection
            );

        float NdotL =
            max(
                lightDirection.z,
                0.0
            );

        float NdotH =
            max(
                halfVector.z,
                0.0
            );

        float VdotH =
            max(
                dot(
                    viewDirection,
                    halfVector
                ),
                0.0
            );

        if (NdotL > 0.0)
        {
            float geometry =
                GeometrySmith(
                    normal,
                    viewDirection,
                    lightDirection,
                    roughness
                );

            float visibility =
                (
                    geometry *
                    VdotH
                ) /
                max(
                    NdotH *
                    NdotV,
                    0.0001
                );

            float fresnel =
                pow(
                    1.0 -
                    VdotH,
                    5.0
                );

            scale +=
                (
                    1.0 -
                    fresnel
                ) *
                visibility;

            bias +=
                fresnel *
                visibility;
        }
    }

    return
        vec2(
            scale,
            bias
        ) /
        float(sampleCount);
}

void main()
{
    float NdotV =
        clamp(
            fragmentUV.x,
            0.0,
            1.0
        );

    float roughness =
        clamp(
            fragmentUV.y,
            0.0,
            1.0
        );

    vec2 integratedBRDF =
        IntegrateBRDF(
            NdotV,
            roughness
        );

    outColor =
        vec4(
            integratedBRDF,
            0.0,
            1.0
        );
}