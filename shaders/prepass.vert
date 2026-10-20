#version 450
struct PointLight { vec4 position; vec4 color; };
layout(binding=0) uniform Camera {
    mat4 model; mat4 view; mat4 proj; mat4 lightViewProj;
    ivec4 shadowSettings; PointLight pointLight; vec4 cameraPosition; vec4 lightingSettings;
} ubo;
struct InstanceData { mat4 model; vec4 materialProperties; };
layout(std430,binding=1) readonly buffer Instances { InstanceData instances[]; };
layout(std140,binding=2) uniform MotionState {
    mat4 previousViewProjection; mat4 previousGlobalModel; ivec4 settings;
} motion;
layout(std430,binding=3) readonly buffer PreviousInstances { InstanceData previousInstances[]; };
layout(std430,binding=4) readonly buffer PreviousVertices {float previousVertexData[];};
layout(location=0) in vec3 position;
layout(location=2) in vec3 normal;
layout(location=0) out vec3 viewNormal;
layout(location=1) out vec3 viewPosition;
layout(location=2) out vec4 previousClip;
layout(location=3) flat out uint instanceId;
void main() {
    // Identical position operation order to the forward vertex shader.
    mat4 objectToWorld=ubo.model*instances[gl_InstanceIndex].model;
    vec4 worldPosition=objectToWorld*vec4(position,1);
    gl_Position=ubo.proj*ubo.view*worldPosition;
    vec3 oldPosition=position;
    if(motion.settings.w!=0){uint base=uint(motion.settings.w)+uint(gl_VertexIndex)*4;oldPosition=vec3(previousVertexData[base],previousVertexData[base+1],previousVertexData[base+2]);}
    previousClip=motion.previousViewProjection*motion.previousGlobalModel*previousInstances[gl_InstanceIndex].model*vec4(oldPosition,1);
    instanceId=gl_InstanceIndex;
    viewPosition=(ubo.view*worldPosition).xyz;
    viewNormal=mat3(ubo.view)*mat3(transpose(inverse(objectToWorld)))*normal;
}
