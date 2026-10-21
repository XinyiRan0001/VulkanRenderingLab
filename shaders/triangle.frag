#version 460
#ifdef ENABLE_RAY_QUERY
#extension GL_EXT_ray_query : require
#endif

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
#ifdef ENABLE_RAY_QUERY
layout(set=1,binding=0) uniform accelerationStructureEXT rayScene;
struct RayTriangle{vec4 a,b,c,na,nb,nc,color,properties,uv01,uv2;};
layout(set=1,binding=1,std430) readonly buffer RayTriangles{RayTriangle rayTriangles[];};
layout(set=1,binding=2) uniform RaySettings{vec4 settings;} ray;
layout(set=1,binding=7) uniform sampler2D rayAtlas;
#endif


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

#ifdef ENABLE_RAY_QUERY
vec2 RayUv(RayTriangle t,vec2 bary){return t.uv01.xy*(1-bary.x-bary.y)+t.uv01.zw*bary.x+t.uv2.xy*bary.y;}
void ProceedMirror(rayQueryEXT query){while(rayQueryProceedEXT(query)){
    if(rayQueryGetIntersectionTypeEXT(query,false)==gl_RayQueryCandidateIntersectionTriangleEXT){
        RayTriangle t=rayTriangles[rayQueryGetIntersectionPrimitiveIndexEXT(query,false)];bool accept=true;
        if((int(t.properties.w)&2)!=0){vec2 bary=rayQueryGetIntersectionBarycentricsEXT(query,false);float alpha=t.color.a;if((int(t.properties.w)&1)!=0)alpha*=textureLod(rayAtlas,RayUv(t,bary),0).a;accept=alpha>=.5;}
        if(accept)rayQueryConfirmIntersectionEXT(query);
    }
}}
// Outer mirrors filter the inner mirror's linear radiance, not its source albedo.
vec3 FilterMirrorImage(vec3 radiance,int style){
    float luminance=dot(radiance,vec3(.2126,.7152,.0722));
    if(style==0 || luminance<=.00001)return radiance;
    float tone=luminance/(1.0+luminance);
    if(style==1){
        float band=.10+.20*smoothstep(.18,.21,tone)+.32*smoothstep(.40,.43,tone);
        // Preserve the inner palette while simplifying its brightness.
        return radiance*(band/max(luminance,.025));
    }
    return mix(vec3(.035,.12,.42),vec3(1.35,.55,.10),smoothstep(.08,.55,tone))
           *(.4+.8*sqrt(luminance));
}
// Environment filtering preserves radiance detail instead of flattening it to a palette.
vec3 FilterMirrorEnvironment(vec3 radiance,int style,float strength){
    float luminance=dot(radiance,vec3(.2126,.7152,.0722));
    if(style==0 || luminance<=.00001)return radiance;
    float tone=luminance/(1.0+luminance);
    if(style==1){
        float bands=.1;
        for(int k=1;k<5;++k){
            float edge=float(k)*.2;
            bands+=.2*smoothstep(edge-.025,edge+.025,tone);
        }
        // Blend the bands with continuous luminance: cloud and horizon detail survives.
        float mapped=mix(tone,bands,.55*strength);
        float target=mapped/max(1.0-mapped,.001);
        return radiance*(target/luminance);
    }
    // A normalized tint keeps source luminance; retain some original environment hue.
    vec3 tint=mix(vec3(.30,.53,1.20),vec3(1.25,.73,.32),smoothstep(.12,.78,tone));
    tint/=dot(tint,vec3(.2126,.7152,.0722));
    return mix(radiance,luminance*tint,.72*strength);
}
vec3 ResolveMirrorStyles(vec3 radiance,int styles[3],vec3 weights[3],int count){
    // The innermost style already shaded the final surface. Walk back outwards.
    for(int i=count-1;i>=0;--i){
        radiance*=weights[i];
        if(i>0)radiance=FilterMirrorImage(radiance,styles[i-1]);
    }
    return radiance;
}
vec3 TraceMirror(vec3 p,vec3 n,vec3 viewDirection,out bool hit,out float distance){
    vec3 direction=normalize(reflect(-viewDirection,n));vec3 origin=p+n*ray.settings.z;
    // Iterative path: the primary receiver already supplied the first reflection.
    // Up to three traced segments, including at most two further mirror hits.
    vec3 normal=vec3(0),position=vec3(0);
    int styles[3];vec3 weights[3];int count=1;
    styles[0]=ray.settings.x>1.5?int(material.properties.w+.5)-1:0;
    weights[0]=vec3(1);
    RayTriangle triangle;vec2 bary=vec2(0);
    hit=false;distance=ray.settings.w;
    for(int segment=0;segment<3;++segment){
        rayQueryEXT query;
        rayQueryInitializeEXT(query,rayScene,0,255,origin,ray.settings.z,direction,ray.settings.w);
        ProceedMirror(query);
        bool found=rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionTriangleEXT;
        if(segment==0){hit=found;if(found)distance=rayQueryGetIntersectionTEXT(query,true);}
        if(!found){
            vec3 environment=ubo.shadowSettings.y!=0?
                textureLod(prefilteredEnvironmentMap,RotateEnvironment(direction),0).rgb:vec3(0);
            // Same environment filter for direct and nested paths. Share the artistic
            // strength across layers so repeated reflections do not erase image detail.
            float strength=.85/float(count);
            for(int layer=count-1;layer>=0;--layer){
                environment=FilterMirrorEnvironment(environment,styles[layer],strength);
                environment*=weights[layer];
            }
            return environment;
        }
        float segmentDistance=rayQueryGetIntersectionTEXT(query,true);
        triangle=rayTriangles[rayQueryGetIntersectionPrimitiveIndexEXT(query,true)];
        bary=rayQueryGetIntersectionBarycentricsEXT(query,true);
        normal=normalize(triangle.na.xyz*(1-bary.x-bary.y)+triangle.nb.xyz*bary.x+triangle.nc.xyz*bary.y);
        if(dot(normal,direction)>0)normal=-normal;
        position=origin+direction*segmentDistance;
        if(triangle.properties.z<.5)break;
        // Never shade an intermediate mirror as an opaque painted surface.
        // The bounded path returns no extra radiance if the budget is exhausted.
        if(segment==2)return vec3(0);
        styles[count]=ray.settings.x>1.5?int(triangle.properties.z+.5)-1:0;
        weights[count]=FresnelSchlick(max(dot(normal,-direction),0.0),
                                    mix(vec3(.04),triangle.color.rgb,triangle.properties.x));
        ++count;
        direction=normalize(reflect(direction,normal));
        origin=position+normal*ray.settings.z;
    }
    vec3 lightOffset=ubo.pointLight.position.xyz-position;
    float lightDistance=length(lightOffset);vec3 lightDirection=lightOffset/max(lightDistance,.0001);
    float visibility=1;
    if(lightDistance>4*ray.settings.z && dot(normal,lightDirection)>0){rayQueryEXT shadow;rayQueryInitializeEXT(shadow,rayScene,gl_RayFlagsTerminateOnFirstHitEXT,255,position+normal*ray.settings.z,ray.settings.z,lightDirection,lightDistance-2*ray.settings.z);ProceedMirror(shadow);if(rayQueryGetIntersectionTypeEXT(shadow,true)!=gl_RayQueryCommittedIntersectionNoneEXT)visibility=0;}
    float metallic=triangle.properties.x,roughness=triangle.properties.y;vec3 color=triangle.color.rgb;if((int(triangle.properties.w)&1)!=0)color*=textureLod(rayAtlas,RayUv(triangle,bary),0).rgb;
    // Shade the terminal surface using the innermost receiver, then unwind the path.
    int style=styles[count-1];
    if(style==1){
        float ndl=max(dot(normal,lightDirection),0.0);
        // A narrow continuous transition softens the lighting-band boundary.
        float width=.015;
        float band=.28+.35*smoothstep(.28-width,.28+width,ndl)
                        +.37*smoothstep(.68-width,.68+width,ndl);
        band=mix(.22,band,visibility);
        float luminance=dot(color,vec3(.2126,.7152,.0722));
        // Preserve the atlas hue, but give near-black fabric an ink-blue floor.
        // A multiplicative light ramp alone cannot reveal a black base color.
        float ink=1.0-smoothstep(.015,.16,luminance);
        vec3 painted=mix(color,vec3(.10,.14,.26),ink);
        vec3 tint=mix(vec3(.65,.57,.88),vec3(1.04,1.0,.90),band);
        return ResolveMirrorStyles(painted*(.38+1.05*band)*tint,styles,weights,count);
    }
    if(style==2){
        float facing=dot(normal,lightDirection)*.5+.5;
        float tone=mix(.12,facing,visibility);
        vec3 cool=vec3(.035,.12,.42),warm=vec3(1.35,.55,.10);
        float luminance=dot(color,vec3(.2126,.7152,.0722));
        return ResolveMirrorStyles(mix(cool,warm,smoothstep(.2,.85,tone))*(.4+.8*sqrt(max(luminance,0.0))),styles,weights,count);
    }
    vec3 radiance=EvaluateCookTorranceLight(color,normal,-direction,lightDirection,ubo.pointLight.color.rgb*ubo.pointLight.color.a/max(lightDistance*lightDistance,.01),metallic,roughness)*visibility;
    if(ubo.shadowSettings.y!=0){
        float ndv=max(dot(normal,-direction),.001);vec3 fresnel=FresnelSchlickRoughness(ndv,mix(vec3(.04),color,metallic),roughness);vec2 brdf=texture(brdfLUT,vec2(ndv,roughness)).rg;
        radiance+=(1-fresnel)*(1-metallic)*texture(irradianceMap,RotateEnvironment(normal)).rgb*color;
        radiance+=textureLod(prefilteredEnvironmentMap,RotateEnvironment(reflect(direction,normal)),roughness*7).rgb*(fresnel*brdf.x+brdf.y);
    }else radiance+=color*.04*(1-metallic);
    return ResolveMirrorStyles(radiance,styles,weights,count);
}
#endif
void main()
{
    // =========================================================
    // Material properties
    // =========================================================

    float alpha=1;
    if((material.flags.y&32)!=0){alpha=material.baseColor.a;if(material.flags.x!=0)alpha*=texture(texSampler,fragTexCoord).a;}
    if(alpha<=.001)discard;
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

    if((material.flags.y & 16)!=0 && !gl_FrontFacing)normal=-normal;
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
#ifdef ENABLE_RAY_QUERY
    bool useRay=material.properties.w>.5 && ray.settings.x>.5;
    vec3 rayRadiance=vec3(0);bool rayHit=false;float rayDistance=0;
    if(useRay){rayRadiance=TraceMirror(fragWorldPosition,normal,viewDirection,rayHit,rayDistance);
        if(ray.settings.y>1.5){outColor=vec4(rayHit?vec3(.05,.8,.15):vec3(.5,.05,.7),1);return;}
        if(ray.settings.y>.5){outColor=vec4(1-exp(-rayRadiance),1);return;}
    }
#endif

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
#ifdef ENABLE_RAY_QUERY
        if(useRay)specularEnvironmentLighting=rayRadiance*fresnel;
#endif

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
#ifdef ENABLE_RAY_QUERY
    if(useRay && !environmentLightingEnabled)fallbackAmbient+=rayRadiance*FresnelSchlickRoughness(max(dot(normal,viewDirection),.001),mix(vec3(.04),baseColor,metallic),roughness);
#endif

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
            alpha
        );
}
