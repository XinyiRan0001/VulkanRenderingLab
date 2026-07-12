#version 450

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

float FindBlocker(
    vec2 uv,
    float receiverDepth,
    float bias
)
{
    vec2 texelSize =
        1.0 /
        vec2(textureSize(shadowMap, 0));

    // Temporary fixed search radius.
    float searchRadius = 6.0;

    float blockerDepthSum = 0.0;
    int blockerCount = 0;

    for (int x = -3; x <= 3; ++x)
    {
        for (int y = -3; y <= 3; ++y)
        {
            vec2 offset =
                vec2(x, y) *
                texelSize *
                searchRadius;

            float sampleDepth =
                texture(
                    shadowMap,
                    uv + offset
                ).r;

            // A blocker is closer to the light
            // than the receiver.
            if (sampleDepth <
                receiverDepth - bias)
            {
                blockerDepthSum +=
                    sampleDepth;

                blockerCount++;
            }
        }
    }

    if (blockerCount == 0)
    {
        return -1.0;
    }

    return blockerDepthSum /
        float(blockerCount);
}

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

    return receiverDepth - bias > closestDepth
        ? 1.0
        : 0.0;
}

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

    float bias = max(
        0.0015 *
        (1.0 - dot(normal, lightDirection)),
        0.0003
    );

    // Mode 1: Hard Shadow
    if (fragShadowMode == 1)
    {
        return CalculateHardShadow(
            projected.xy,
            receiverDepth,
            bias
        );
    }

    // Mode 2: PCF
    if (fragShadowMode == 2)
    {
        return CalculatePCF(
            projected.xy,
            receiverDepth,
            bias
        );
    }

    // Mode 3: PCSS
    float avgBlockerDepth =
        FindBlocker(
            projected.xy,
            receiverDepth,
            bias
        );

    if (avgBlockerDepth < 0.0)
    {
        return 0.0;
    }

    float penumbra =
        (receiverDepth - avgBlockerDepth) /
        max(avgBlockerDepth, 0.0001);

    const float lightSize = 35.0;

    float filterRadius =
        clamp(
            penumbra * lightSize,
            1.0,
            12.0
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

    return shadow / sampleCount;
}

void main()
{
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
    vec3 baseColor;

    if (material.useTexture != 0)
    {
        baseColor =
            texture(texSampler, fragTexCoord).rgb;
    }
    else
    {
        baseColor = fragColor;
    }

    vec3 normal = normalize(fragNormal);

    vec3 lightPosition =
        vec3(-3.0, 5.0, 4.0);

    vec3 lightDirection =
        normalize(
            lightPosition - fragWorldPosition
        );

    float diffuse =
        max(dot(normal, lightDirection), 0.0);

    float shadow =
        CalculateShadow(
            fragLightSpacePosition,
            normal,
            lightDirection
        );

    const float ambientStrength = 0.30;
    const float diffuseStrength = 0.70;

    vec3 ambient =
        baseColor * ambientStrength;

    vec3 diffuseLighting =
        baseColor *
        diffuseStrength *
        diffuse *
        (1.0 - shadow);

    outColor = vec4(
        ambient + diffuseLighting,
        1.0
    );
}