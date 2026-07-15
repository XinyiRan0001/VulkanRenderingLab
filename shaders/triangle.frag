#version 450

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
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D shadowMap;

layout(push_constant) uniform MaterialPushConstant
{
    int useTexture;
} material;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragLightSpacePosition;
layout(location = 5) flat in int fragShadowMode;

layout(location = 0) out vec4 outColor;


// =============================================================
// Shadow depth utilities
// =============================================================

float LinearizeShadowDepth(float depth)
{
    const float nearPlane = 0.1;
    const float farPlane = 12.0;

    return
        nearPlane * farPlane /
        (
            farPlane -
            depth * (farPlane - nearPlane)
        );
}


// =============================================================
// PCSS blocker search
// =============================================================

float FindBlocker(
    vec2 uv,
    float receiverDepth,
    float bias,
    out int blockerCount
)
{
    vec2 texelSize =
        1.0 /
        vec2(textureSize(shadowMap, 0));

    const float searchRadius = 5.0;

    float blockerDepthSum = 0.0;

    blockerCount = 0;

    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2 sampleUV =
                uv +
                vec2(x, y) *
                texelSize *
                searchRadius /
                2.0;

            float sampleDepth =
                texture(
                    shadowMap,
                    sampleUV
                ).r;

            if (sampleDepth <
                receiverDepth - bias)
            {
                blockerDepthSum +=
                    LinearizeShadowDepth(
                        sampleDepth
                    );

                blockerCount++;
            }
        }
    }

    if (blockerCount == 0)
    {
        return -1.0;
    }

    return
        blockerDepthSum /
        float(blockerCount);
}


// =============================================================
// Hard shadow
// =============================================================

float CalculateHardShadow(
    vec2 uv,
    float receiverDepth,
    float bias
)
{
    float closestDepth =
        texture(
            shadowMap,
            uv
        ).r;

    return
        receiverDepth - bias > closestDepth
        ? 1.0
        : 0.0;
}


// =============================================================
// Fixed 3x3 PCF
// =============================================================

float CalculatePCF(
    vec2 uv,
    float receiverDepth,
    float bias
)
{
    vec2 texelSize =
        1.0 /
        vec2(textureSize(shadowMap, 0));

    float shadow = 0.0;

    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float closestDepth =
                texture(
                    shadowMap,
                    uv +
                    vec2(x, y) *
                    texelSize
                ).r;

            shadow +=
                receiverDepth - bias >
                closestDepth
                ? 1.0
                : 0.0;
        }
    }

    return shadow / 9.0;
}


// =============================================================
// Shadow mode selection
// =============================================================

float CalculateShadow(
    vec4 lightSpacePosition,
    vec3 normal,
    vec3 lightDirection
)
{
    vec3 projected =
        lightSpacePosition.xyz /
        lightSpacePosition.w;

    projected.xy =
        projected.xy * 0.5 + 0.5;

    if (projected.x < 0.0 ||
        projected.x > 1.0 ||
        projected.y < 0.0 ||
        projected.y > 1.0 ||
        projected.z < 0.0 ||
        projected.z > 1.0)
    {
        return 0.0;
    }

    float receiverDepth =
        projected.z;

    float bias =
        max(
            0.0012 *
            (1.0 - dot(normal, lightDirection)),
            0.00025
        );

    // Mode 1: hard shadow
    if (fragShadowMode == 1)
    {
        return
            CalculateHardShadow(
                projected.xy,
                receiverDepth,
                bias
            );
    }

    // Mode 2: fixed PCF
    if (fragShadowMode == 2)
    {
        return
            CalculatePCF(
                projected.xy,
                receiverDepth,
                bias
            );
    }

    // Mode 3: PCSS
    int blockerCount = 0;

    float avgBlockerLinearDepth =
        FindBlocker(
            projected.xy,
            receiverDepth,
            bias,
            blockerCount
        );

    if (avgBlockerLinearDepth < 0.0)
    {
        return 0.0;
    }

    if (blockerCount == 25)
    {
        return 1.0;
    }

    float receiverLinearDepth =
        LinearizeShadowDepth(
            receiverDepth
        );

    float penumbraRatio =
        (
            receiverLinearDepth -
            avgBlockerLinearDepth
        ) /
        max(
            avgBlockerLinearDepth,
            0.0001
        );

    const float lightRadius = 18.0;

    float filterRadius =
        clamp(
            penumbraRatio * lightRadius,
            1.0,
            18.0
        );

    vec2 texelSize =
        1.0 /
        vec2(textureSize(shadowMap, 0));

    float shadow = 0.0;
    float sampleCount = 0.0;

    for (int x = -3; x <= 3; ++x)
    {
        for (int y = -3; y <= 3; ++y)
        {
            vec2 offset =
                vec2(x, y) *
                texelSize *
                filterRadius /
                3.0;

            float closestDepth =
                texture(
                    shadowMap,
                    projected.xy + offset
                ).r;

            shadow +=
                receiverDepth - bias >
                closestDepth
                ? 1.0
                : 0.0;

            sampleCount += 1.0;
        }
    }

    return
        shadow /
        sampleCount;
}


// =============================================================
// Main
// =============================================================

void main()
{
    // ---------------------------------------------------------
    // Shadow map debug view
    // ---------------------------------------------------------

    if (fragShadowMode == 4)
    {
        vec3 projected =
            fragLightSpacePosition.xyz /
            fragLightSpacePosition.w;

        projected.xy =
            projected.xy * 0.5 + 0.5;

        if (projected.x < 0.0 ||
            projected.x > 1.0 ||
            projected.y < 0.0 ||
            projected.y > 1.0)
        {
            outColor =
                vec4(
                    0.0,
                    0.0,
                    0.0,
                    1.0
                );

            return;
        }

        float depth =
            texture(
                shadowMap,
                projected.xy
            ).r;

        outColor =
            vec4(
                depth,
                depth,
                depth,
                1.0
            );

        return;
    }

    // ---------------------------------------------------------
    // Base material colour
    // ---------------------------------------------------------

    vec3 baseColor;

    if (material.useTexture != 0)
    {
        baseColor =
            texture(
                texSampler,
                fragTexCoord
            ).rgb;
    }
    else
    {
        baseColor =
            fragColor;
    }

    vec3 normal =
        normalize(
            fragNormal
        );

    // ---------------------------------------------------------
    // Ambient light
    // ---------------------------------------------------------

    const float ambientStrength = 0.22;

    vec3 ambientLighting =
        baseColor *
        ambientStrength;

    // ---------------------------------------------------------
    // Fixed front white light
    // ---------------------------------------------------------

    const vec3 fixedLightPosition =
        vec3(
            0.0,
            2.2,
            3.5
        );

    vec3 toFixedLight =
        fixedLightPosition -
        fragWorldPosition;

    float fixedLightDistance =
        length(
            toFixedLight
        );

    vec3 fixedLightDirection =
        normalize(
            toFixedLight
        );

    float fixedDiffuse =
        max(
            dot(
                normal,
                fixedLightDirection
            ),
            0.0
        );

    float fixedAttenuation =
        1.0 /
        (
            1.0 +
            0.12 * fixedLightDistance +
            0.04 *
            fixedLightDistance *
            fixedLightDistance
        );

    const float fixedLightIntensity = 1.4;

    vec3 fixedLighting =
        baseColor *
        vec3(1.0) *
        fixedDiffuse *
        fixedAttenuation *
        fixedLightIntensity;

    // ---------------------------------------------------------
    // Orbiting point light
    // ---------------------------------------------------------

    vec3 pointLightPosition =
        ubo.pointLight.position.xyz;

    vec3 toPointLight =
        pointLightPosition -
        fragWorldPosition;

    float pointLightDistance =
        length(
            toPointLight
        );

    vec3 pointLightDirection =
        normalize(
            toPointLight
        );

    float pointDiffuse =
        max(
            dot(
                normal,
                pointLightDirection
            ),
            0.0
        );

    float pointAttenuation =
        1.0 /
        (
            1.0 +
            0.35 * pointLightDistance +
            0.20 *
            pointLightDistance *
            pointLightDistance
        );

    bool pointLightEnabled =
        ubo.pointLight.color.a >
        0.001;

    vec3 pointLighting =
        baseColor *
        ubo.pointLight.color.rgb *
        ubo.pointLight.color.a *
        pointDiffuse *
        pointAttenuation;

    // ---------------------------------------------------------
    // The current shadow map belongs to one light:
    //
    // Point light enabled:
    //     shadow map follows the orbiting light.
    //
    // Point light disabled:
    //     shadow map uses the fixed front light.
    // ---------------------------------------------------------

    vec3 shadowLightDirection =
        pointLightEnabled
        ? pointLightDirection
        : fixedLightDirection;

    float shadow =
        CalculateShadow(
            fragLightSpacePosition,
            normal,
            shadowLightDirection
        );

    if (pointLightEnabled)
    {
        pointLighting *=
            (1.0 - shadow);
    }
    else
    {
        fixedLighting *=
            (1.0 - shadow);
    }

    // ---------------------------------------------------------
    // Final output
    // ---------------------------------------------------------

    vec3 finalLighting =
        ambientLighting +
        fixedLighting +
        pointLighting;

    outColor =
        vec4(
            finalLighting,
            1.0
        );
}