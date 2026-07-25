#version 450

layout(push_constant) uniform PrefilterPushConstants
{
    mat4 viewProjection;
    vec4 parameters;
} pushConstants;

layout(location = 0) out vec3 outLocalDirection;

const vec3 cubeVertices[36] =
vec3[](
    // +X
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),

    // -X
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),

    // +Y
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),

    // -Y
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),

    // +Z
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0, -1.0,  1.0),

    // -Z
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0, -1.0, -1.0)
);

void main()
{
    vec3 position =
        cubeVertices[gl_VertexIndex];

    outLocalDirection =
        position;

    gl_Position =
        pushConstants.viewProjection *
        vec4(position, 1.0);
}