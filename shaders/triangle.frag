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

    vec4 cameraPosition;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D shadowMap;

layout(push_constant) uniform MaterialPushConstant
{
    ivec4 flags;

    vec4 baseColor;

    vec4 properties;
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
// Cook-Torrance GGX BRDF
// =============================================================

const float PI = 3.14159265359;


// GGX / Trowbridge-Reitz normal distribution.
//
// Controls the concentration of microfacet normals around the
// surface normal. Lower roughness produces a sharper highlight.
float DistributionGGX(
    vec3 normal,
    vec3 halfwayDirection,
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
                halfwayDirection
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

    return
        alphaSquared /
        max(
            denominator,
            0.000001
        );
}


// Schlick approximation of the GGX geometry term.
float GeometrySchlickGGX(
    float normalDotDirection,
    float roughness
)
{
    float roughnessPlusOne =
        roughness + 1.0;

    float k =
        (
            roughnessPlusOne *
            roughnessPlusOne
        ) /
        8.0;

    float denominator =
        normalDotDirection *
        (
            1.0 - k
        ) +
        k;

    return
        normalDotDirection /
        max(
            denominator,
            0.000001
        );
}


// Smith masking-shadowing term for both the view and light
// directions.
float GeometrySmith(
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    float roughness
)
{
    float normalDotView =
        max(
            dot(
                normal,
                viewDirection
            ),
            0.0
        );

    float normalDotLight =
        max(
            dot(
                normal,
                lightDirection
            ),
            0.0
        );

    float geometryView =
        GeometrySchlickGGX(
            normalDotView,
            roughness
        );

    float geometryLight =
        GeometrySchlickGGX(
            normalDotLight,
            roughness
        );

    return
        geometryView *
        geometryLight;
}


// Schlick approximation of the Fresnel term.
vec3 FresnelSchlick(
    float cosineTheta,
    vec3 reflectanceAtNormalIncidence
)
{
    return
        reflectanceAtNormalIncidence +
        (
            vec3(1.0) -
            reflectanceAtNormalIncidence
        ) *
        pow(
            clamp(
                1.0 - cosineTheta,
                0.0,
                1.0
            ),
            5.0
        );
}


// Evaluates the direct Cook-Torrance BRDF for one light.
vec3 EvaluateCookTorranceLight(
    vec3 baseColor,
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance,
    float metallic,
    float roughness
)
{
    float normalDotLight =
        max(
            dot(
                normal,
                lightDirection
            ),
            0.0
        );

    float normalDotView =
        max(
            dot(
                normal,
                viewDirection
            ),
            0.0
        );

    if (normalDotLight <= 0.0 ||
        normalDotView <= 0.0)
    {
        return vec3(0.0);
    }

    vec3 halfwayDirection =
        normalize(
            viewDirection +
            lightDirection
        );

    vec3 dielectricF0 =
        vec3(0.04);

    vec3 reflectanceAtNormalIncidence =
        mix(
            dielectricF0,
            baseColor,
            metallic
        );

    float distribution =
        DistributionGGX(
            normal,
            halfwayDirection,
            roughness
        );

    float geometry =
        GeometrySmith(
            normal,
            viewDirection,
            lightDirection,
            roughness
        );

    vec3 fresnel =
        FresnelSchlick(
            max(
                dot(
                    halfwayDirection,
                    viewDirection
                ),
                0.0
            ),
            reflectanceAtNormalIncidence
        );

    vec3 numerator =
        distribution *
        geometry *
        fresnel;

    float denominator =
        4.0 *
        normalDotView *
        normalDotLight;

    vec3 specular =
        numerator /
        max(
            denominator,
            0.0001
        );

    vec3 specularWeight =
        fresnel;

    vec3 diffuseWeight =
        (
            vec3(1.0) -
            specularWeight
        ) *
        (
            1.0 - metallic
        );

    vec3 diffuse =
        diffuseWeight *
        baseColor /
        PI;

    return
        (
            diffuse +
            specular
        ) *
        radiance *
        normalDotLight;
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

    if (material.flags.x != 0)
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
            material.baseColor.rgb;
    }

    vec3 normal =
        normalize(
            fragNormal
        );

    vec3 viewDirection =
        normalize(
            ubo.cameraPosition.xyz -
            fragWorldPosition
        );

    // ---------------------------------------------------------
    // Temporary global material parameters
    //
    // The character is currently treated as a rough dielectric.
    // A per-material roughness system will be added later.
    // ---------------------------------------------------------

    float metallic =
        material.properties.x;

    float roughness =
        material.properties.y;

    float ao =
        material.properties.z;

    // ---------------------------------------------------------
    // Ambient approximation
    //
    // This is not image-based lighting yet. It only prevents
    // unlit surfaces from becoming completely black.
    // ---------------------------------------------------------

    const float ambientStrength = 0.16;

    vec3 ambientLighting =
        baseColor *
        ambientStrength *
        ao;

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

    vec3 fixedRadiance =
        vec3(1.0) *
        fixedLightIntensity *
        fixedAttenuation;

    vec3 fixedLighting =
        EvaluateCookTorranceLight(
            baseColor,
            normal,
            viewDirection,
            fixedLightDirection,
            fixedRadiance,
            metallic,
            roughness
        );

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

    vec3 pointRadiance =
        ubo.pointLight.color.rgb *
        ubo.pointLight.color.a *
        pointAttenuation;

    vec3 pointLighting =
        EvaluateCookTorranceLight(
            baseColor,
            normal,
            viewDirection,
            pointLightDirection,
            pointRadiance,
            metallic,
            roughness
        );

    // ---------------------------------------------------------
    // Shadow selection
    //
    // The shadow map follows the point light when it is enabled.
    // Otherwise it represents the fixed front light.
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
            (
                1.0 -
                shadow
            );
    }
    else
    {
        fixedLighting *=
            (
                1.0 -
                shadow
            );
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