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
    vec4 lightingSettings;
} ubo;

layout(binding = 1) uniform sampler2D texSampler;
layout(binding = 2) uniform sampler2D shadowMap;
layout(binding = 3) uniform sampler2D hdrEnvironment;
layout(binding = 4)
    uniform samplerCube environmentCubemap;
layout(binding = 5)
    uniform samplerCube
        prefilteredEnvironmentMap;
layout(binding = 6) uniform sampler2D brdfLUT;
layout(binding = 7)
    uniform samplerCube irradianceMap;

layout(push_constant) uniform MaterialPushConstant
{
    ivec4 flags;

    vec4 baseColor;

    vec4 emissive;
    vec4 textureSettings;
    vec4 properties;
} material;

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec3 fragWorldPosition;
layout(location = 4) in vec4 fragLightSpacePosition;
layout(location = 5) flat in int fragShadowMode;

layout(location = 6) in vec4 fragTangent;
layout(location = 7) flat in vec4 fragInstanceProperties;
layout(binding = 8) uniform sampler2D normalMap;
layout(binding = 9) uniform sampler2D metallicRoughnessMap;
layout(binding = 10) uniform sampler2D occlusionMap;
layout(binding = 11) uniform sampler2D emissiveMap;
layout(binding = 14) uniform sampler2D screenAo;
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


// Depth changes over the receiver's local plane in shadow UV space.
// Evaluate derivatives before the shadow-frustum early return below.
vec2 ReceiverDepthGradient(vec3 projected)
{
    vec3 dx = dFdx(projected);
    vec3 dy = dFdy(projected);
    float determinant = dx.x * dy.y - dx.y * dy.x;
    float scale = max(length(dx.xy) * length(dy.xy), 1e-20);
    if (abs(determinant) <= 1e-6 * scale) return vec2(0.0);
    return vec2(dy.y * dx.z - dx.y * dy.z,
                dx.x * dy.z - dy.x * dx.z) / determinant;
}

// PCSS compares actual stored depths, not linearly interpolated depth values.
// Use the texel center for the receiver-plane correction as well.
float ReadPCSSDepth(vec2 requestedUV, out vec2 sampleUV)
{
    ivec2 size = textureSize(shadowMap, 0);
    ivec2 pixel = ivec2(floor(requestedUV * vec2(size)));
    sampleUV = (vec2(pixel) + 0.5) / vec2(size);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size)))
        return 1.0;
    return texelFetch(shadowMap, pixel, 0).r;
}

// =============================================================
// PCSS blocker search
// =============================================================

float FindBlocker(
    vec2 uv,
    float receiverDepth,
    float bias,
    vec2 depthGradient,
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

            vec2 texelUV;
            float sampleDepth = ReadPCSSDepth(sampleUV, texelUV);
            float depthOffset = dot(depthGradient, texelUV - uv);
            float sampleReceiverDepth = receiverDepth + depthOffset;

            if (sampleDepth < sampleReceiverDepth - bias)
            {
                // Bring the blocker onto the center ray before estimating
                // penumbra width; otherwise the receiver slope inflates it.
                blockerDepthSum += LinearizeShadowDepth(
                    clamp(sampleDepth - depthOffset, 0.0, 1.0));

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

    vec2 depthGradient = ReceiverDepthGradient(projected);

    if (lightSpacePosition.w <= 0.0 || projected.x < 0.0 ||
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

    if (fragShadowMode == 0) return 0.0;

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
            depthGradient,
            blockerCount
        );

    if (avgBlockerLinearDepth < 0.0)
    {
        return 0.0;
    }

    // Even when all 25 search taps are blockers, the wider filter footprint
    // can contain lit samples. Continue filtering to avoid a false hard edge.

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

            vec2 texelUV;
            float closestDepth = ReadPCSSDepth(projected.xy + offset, texelUV);
            float sampleReceiverDepth = receiverDepth +
                dot(depthGradient, texelUV - projected.xy);

            shadow += sampleReceiverDepth - bias > closestDepth ? 1.0 : 0.0;

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

vec3 FresnelSchlickRoughness(
    float cosineTheta,
    vec3 reflectanceAtNormalIncidence,
    float roughness
)
{
    return
        reflectanceAtNormalIncidence +
        (
            max(
                vec3(1.0 - roughness),
                reflectanceAtNormalIncidence
            ) -
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

vec2 DirectionToEquirectangularUV(vec3 direction)
{
    direction = normalize(direction);

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
        longitude / (2.0 * PI) +
        0.5;

    uv.y =
        0.5 +
        latitude / PI;

    return uv;
}

// =============================================================
// Main
// =============================================================
vec3 RotateEnvironment(vec3 direction)
{
    float angle = ubo.lightingSettings.x;
    float c = cos(angle), s = sin(angle);
    return vec3(c * direction.x + s * direction.z, direction.y,
                -s * direction.x + c * direction.z);
}

void main()
{
    // =========================================================
    // Material properties
    // =========================================================

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

    baseColor *= material.flags.x != 0 ? material.baseColor.rgb : vec3(1.0);

    vec3 normal =
        normalize(
            fragNormal
        );

    if ((material.flags.y & 1) != 0) {
        vec3 tangent = normalize(fragTangent.xyz - normal * dot(normal, fragTangent.xyz));
        vec3 bitangent = cross(normal, tangent) * fragTangent.w;
        vec3 mapped = texture(normalMap, fragTexCoord).xyz * 2.0 - 1.0;
        mapped.xy *= material.textureSettings.x;
        normal = normalize(mat3(tangent, bitangent, normal) * normalize(mapped));
    }

    vec3 viewDirection =
        normalize(
            ubo.cameraPosition.xyz -
            fragWorldPosition
        );

    float metallic =
        clamp(
            material.properties.x,
            0.0,
            1.0
        );

    float roughness =
        clamp(
            material.properties.y,
            0.02,
            1.0
        );

    float ambientOcclusion =
        clamp(
            material.properties.z,
            0.0,
            1.0
        );

    // These vary per instance, while texture bindings and base color remain
    // shared by the material batch. Identity instances keep their material values.
    if (fragInstanceProperties.w > 0.5) {
        metallic = clamp(fragInstanceProperties.x, 0.0, 1.0);
        roughness = clamp(fragInstanceProperties.y, 0.02, 1.0);
        ambientOcclusion = clamp(fragInstanceProperties.z, 0.0, 1.0);
    }

    if ((material.flags.y & 2) != 0) {
        vec4 mr = texture(metallicRoughnessMap, fragTexCoord);
        metallic *= mr.b;
        roughness = clamp(roughness * mr.g, 0.02, 1.0);
    }
    if ((material.flags.y & 4) != 0)
        ambientOcclusion *= mix(1.0, texture(occlusionMap, fragTexCoord).r, material.textureSettings.y);
    vec3 emissive = material.emissive.rgb;
    if ((material.flags.y & 8) != 0) emissive *= texture(emissiveMap, fragTexCoord).rgb;

    // Material inspection deliberately bypasses light, shadows and tone mapping.
    int view = ubo.shadowSettings.w;
    if (view != 0) {
        vec3 value = view == 1 ? baseColor : view == 2 ? normal * 0.5 + 0.5 :
            view == 3 ? vec3(roughness) : view == 4 ? vec3(metallic) : vec3(ambientOcclusion);
        outColor = vec4(value, 1.0);
        return;
    }

    // =========================================================
    // Direct point light
    // =========================================================

    vec3 lightOffset =
        ubo.pointLight.position.xyz -
        fragWorldPosition;

    float lightDistance =
        length(
            lightOffset
        );

    vec3 lightDirection =
        lightDistance > 0.0001
        ? lightOffset / lightDistance
        : vec3(0.0, 1.0, 0.0);

    float attenuation =
        1.0 /
        max(
            lightDistance *
            lightDistance,
            0.01
        );

    vec3 lightRadiance =
        ubo.pointLight.color.rgb *
        ubo.pointLight.color.a *
        attenuation;

    vec3 directLighting =
        EvaluateCookTorranceLight(
            baseColor,
            normal,
            viewDirection,
            lightDirection,
            lightRadiance,
            metallic,
            roughness
        );

    // =========================================================
    // Shadow
    // =========================================================

    float shadow =
        CalculateShadow(
            fragLightSpacePosition,
            normal,
            lightDirection
        );

    directLighting *=
        1.0 -
        shadow;

    // =========================================================
    // Image-based lighting
    // =========================================================

    bool environmentLightingEnabled =
        ubo.shadowSettings.y != 0;

    vec3 environmentLighting =
        vec3(0.0);

    if (environmentLightingEnabled)
    {
        vec3 F0 =
            mix(
                vec3(0.04),
                baseColor,
                metallic
            );

        float NdotV =
            max(
                dot(
                    normal,
                    viewDirection
                ),
                0.001
            );

        vec3 fresnel =
            FresnelSchlickRoughness(
                NdotV,
                F0,
                roughness
            );

        vec3 diffuseWeight =
            (
                vec3(1.0) -
                fresnel
            ) *
            (
                1.0 -
                metallic
            );

        // -----------------------------------------------------
        // Diffuse IBL
        // -----------------------------------------------------

        vec3 irradiance =
            texture(
                irradianceMap,
                RotateEnvironment(normal)
            ).rgb;

        vec3 diffuseEnvironmentLighting =
            diffuseWeight *
            irradiance *
            baseColor;

        // -----------------------------------------------------
        // Specular IBL
        // -----------------------------------------------------

        vec3 reflectionDirection =
            normalize(
                reflect(
                    -viewDirection,
                    normal
                )
            );

        const float maxMipLevel =
            7.0;

        vec3 reflectedEnvironment =
            textureLod(
                prefilteredEnvironmentMap,
                RotateEnvironment(reflectionDirection),
                roughness *
                maxMipLevel
            ).rgb;

        vec2 environmentBRDF =
            texture(
                brdfLUT,
                vec2(
                    NdotV,
                    roughness
                )
            ).rg;

        vec3 specularEnvironmentLighting =
            reflectedEnvironment *
            (
                fresnel *
                environmentBRDF.x +
                environmentBRDF.y
            );

        // Screen-space AO affects indirect diffuse only. Existing material AO is unchanged.
        if(ubo.lightingSettings.z>.5) diffuseEnvironmentLighting*=texelFetch(screenAo,ivec2(gl_FragCoord.xy),0).r;
        int component = int(ubo.lightingSettings.y);
        environmentLighting = (component == 2 ? diffuseEnvironmentLighting :
            component == 3 ? specularEnvironmentLighting :
            diffuseEnvironmentLighting + specularEnvironmentLighting) * ambientOcclusion;
    }

    // =========================================================
    // Fallback ambient
    // =========================================================

    vec3 fallbackAmbient =
        baseColor *
        0.04 *
        ambientOcclusion *
        (
            1.0 -
            metallic
        );

    if(ubo.lightingSettings.z>.5) fallbackAmbient*=texelFetch(screenAo,ivec2(gl_FragCoord.xy),0).r;

    // =========================================================
    // Final lighting composition
    // =========================================================

    int lightingView = int(ubo.lightingSettings.y);
    vec3 hdrColor = vec3(0.0);
    if (lightingView == 0) {
        hdrColor = directLighting + (environmentLightingEnabled ? environmentLighting : fallbackAmbient) + emissive;
    } else if (lightingView == 1) {
        hdrColor = directLighting;
    } else {
        hdrColor = environmentLighting;
    }

    // Exposure is stored as exposure * 100 in shadowSettings.z.
    float exposure =
        max(
            float(
                ubo.shadowSettings.z
            ) /
            100.0,
            0.01
        );

    // Exponential tone mapping.
    // The swapchain uses an SRGB format, so no manual gamma
    // correction is applied here.
    vec3 mappedColor =
        vec3(1.0) -
        exp(
            -hdrColor *
            exposure
        );

    outColor =
        vec4(
            mappedColor,
            1.0
        );
}