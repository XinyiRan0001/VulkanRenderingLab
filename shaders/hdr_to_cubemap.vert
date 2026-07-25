#version 450

layout(push_constant) uniform PushConstants
{
    mat4 viewProjection;
} pushConstants;

layout(location = 0) out vec3 outLocalPosition;

const vec3 cubeVertices[36] =
{
    // +X
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0,  1.0,  1.0),

    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3( 1.0,  1.0, -1.0),

    // -X
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),

    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),

    // +Y
    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),

    vec3(-1.0,  1.0, -1.0),
    vec3( 1.0,  1.0,  1.0),
    vec3(-1.0,  1.0,  1.0),

    // -Y
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),

    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),

    // +Z
    vec3( 1.0, -1.0, 1.0),
    vec3(-1.0, -1.0, 1.0),
    vec3(-1.0,  1.0, 1.0),

    vec3( 1.0, -1.0, 1.0),
    vec3(-1.0,  1.0, 1.0),
    vec3( 1.0,  1.0, 1.0),

    // -Z
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),

    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0,  1.0, -1.0)
};

void main()
{
    vec3 localPosition =
        cubeVertices[gl_VertexIndex];

    outLocalPosition =
        localPosition;

    gl_Position =
        pushConstants.viewProjection *
        vec4(localPosition, 1.0);
}