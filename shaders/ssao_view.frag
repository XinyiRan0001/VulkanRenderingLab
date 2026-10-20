#version 450
layout(binding=4) uniform sampler2D rawAo;
layout(binding=10) uniform sampler2D blurredAo;
layout(binding=12) uniform sampler2D temporalAo;
layout(binding=13) uniform sampler2D historyMetadata;
layout(std140,binding=2) uniform Preview {
    mat4 inverseProjection; mat4 inverseView; ivec4 settings; vec4 parameters;
    mat4 projection; vec4 kernel[64]; ivec4 aoSettings; ivec4 sampling;
} v;
layout(location=0) out vec4 outColor;
void main() {
    ivec2 p=ivec2(gl_FragCoord.xy);
    if(v.settings.x==6 && v.sampling.z!=0) {
        vec4 metadata=texelFetch(historyMetadata,p,0);
        if(v.sampling.z==2) {
            outColor=vec4(metadata.w==0?vec3(0):mix(vec3(.05,.1,.5),vec3(1,.9,.05),metadata.z/.85),1);
        } else {
            const vec3 colors[6]=vec3[](vec3(0),vec3(.4),vec3(1,0,1),vec3(1,.1,.05),vec3(.1,.85,.2),vec3(0,.7,1));
            outColor=vec4(colors[clamp(int(metadata.w),0,5)],1);
        }
        return;
    }
    float ao=v.settings.x==5?texelFetch(rawAo,v.aoSettings.z!=0?p/2:p,0).r:(v.aoSettings.w!=0?texelFetch(temporalAo,p,0).r:texelFetch(blurredAo,p,0).r);
    outColor=vec4(vec3(ao),1);
}
