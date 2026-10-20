#version 450
layout(location=0) in vec3 viewNormal;
layout(location=1) in vec3 viewPosition;
layout(location=2) in vec4 previousClip;
layout(location=3) flat in uint instanceId;
layout(std140,binding=2) uniform MotionState {
    mat4 previousViewProjection; mat4 previousGlobalModel; ivec4 settings;
} motion;
layout(location=0) out vec4 normalAndDistance;
// XY: previousUV-currentUV; Z: previous device depth; signed W: instanceId+1.
// Positive W means projection is a candidate, not proof of previous visibility.
layout(location=1) out vec4 motionVector;
void main() {
    vec2 velocity=vec2(0);float previousDepth=-1;bool valid=false;
    if(motion.settings.x!=0 && previousClip.w>1e-6) {
        vec3 ndc=previousClip.xyz/previousClip.w;
        vec2 previousUv=ndc.xy*.5+.5;
        velocity=previousUv-gl_FragCoord.xy/vec2(motion.settings.yz);
        previousDepth=ndc.z;
        valid=all(greaterThanEqual(previousUv,vec2(0))) && all(lessThan(previousUv,vec2(1))) && ndc.z>=0 && ndc.z<=1;
    }
    motionVector=vec4(velocity,previousDepth,(valid?1:-1)*float(instanceId+1));
    normalAndDistance=vec4(normalize(viewNormal)*(gl_FrontFacing?1:-1),-viewPosition.z);
}
