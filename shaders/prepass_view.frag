#version 450
layout(binding=0) uniform sampler2D normalTexture;
layout(binding=1) uniform sampler2D depthTexture;
layout(binding=11) uniform sampler2D motionTexture;
layout(std140,binding=2) uniform Preview {
    mat4 inverseProjection; mat4 inverseView; ivec4 settings; vec4 parameters;
} v;
layout(location=0) out vec4 outColor;
void main() {
    if((v.settings.x==4 || v.settings.x==7)) discard;
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if(v.settings.x==8) {
        vec4 motion=texelFetch(motionTexture,pixel,0);
        if(motion.w==0) {outColor=vec4(0,0,0,1);return;}
        if(motion.w<0) {outColor=vec4(.6,0,.6,1);return;}
        vec2 pixels=motion.xy*vec2(v.settings.yz);
        outColor=vec4(clamp(vec2(.5)+pixels/16,0,1),.5,1);return;
    }
    vec4 normalDistance=texelFetch(normalTexture,pixel,0);
    float depth=texelFetch(depthTexture,pixel,0).r;
    if(normalDistance.a<=0.0 || depth>=1.0) { outColor=vec4(.025,.03,.06,1);return; }
    vec3 color;
    if(v.settings.x==2) {
        vec2 uv=gl_FragCoord.xy/vec2(v.settings.yz);
        vec4 position=v.inverseProjection*vec4(uv*2-1,depth,1);
        color=vec3(clamp((-position.z/position.w)/v.parameters.x,0,1));
    } else {
        vec3 n=normalize(normalDistance.xyz);
        if(v.settings.x==3) n=normalize(mat3(v.inverseView)*n);
        color=n*.5+.5;
    }
    outColor=vec4(color,1);
}
