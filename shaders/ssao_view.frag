#version 450
layout(binding=4) uniform sampler2D rawAo;
layout(binding=6) uniform sampler2D blurredAo;
layout(std140,binding=2) uniform Preview {
    mat4 inverseProjection; mat4 inverseView; ivec4 settings; vec4 parameters;
} v;
layout(location=0) out vec4 outColor;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    float ao=v.settings.x==5?texelFetch(rawAo,p,0).r:texelFetch(blurredAo,p,0).r;
    outColor=vec4(vec3(ao),1);
}
