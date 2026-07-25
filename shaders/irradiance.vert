#version 450

layout(push_constant) uniform IrradiancePushConstants
{
    mat4 viewProjection;
} pushConstants;

layout(location = 0)
out vec3 localPosition;

// A cube composed of 12 triangles / 36 vertices.
//
// The geometry is generated from gl_VertexIndex so the Vulkan
// pipeline does not require a vertex buffer or vertex input state.
const vec3 cubeVertices[36] =
{
    // Back face
    vec3(-1.0, -1.0, -1.0),
    vec3( 1.0,  1.0, -1.0),
    vec3( 1.0, -1.0, -1.0),

    vec3( 1.0,  1.0, -1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),

    // Left face
    vec3(-1.0, -1.0,  1.0),
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0,  1.0, -1.0),

    vec3(-1.0,  1.0, -1.0),
    vec3(-1.0,  1.0,  1.0),
    vec3(-1.0, -1.0,  1.0),

    // Right face
    vec3(1.0, -1.0, -1.0),
    vec3(1.0, -1.0,  1.0),
    vec3(1.0,  1.0,  1.0),

    vec3(1.0,  1.0,  1.0),
    vec3(1.0,  1.0, -1.0),
    vec3(1.0, -1.0, -1.0),

    // Front face
    vec3(-1.0, -1.0, 1.0),
    vec3( 1.0, -1.0, 1.0),
    vec3( 1.0,  1.0, 1.0),

    vec3( 1.0,  1.0, 1.0),
    vec3(-1.0,  1.0, 1.0),
    vec3(-1.0, -1.0, 1.0),

    // Top face
    vec3(-1.0, 1.0, -1.0),
    vec3( 1.0, 1.0,  1.0),
    vec3(-1.0, 1.0,  1.0),

    vec3( 1.0, 1.0,  1.0),
    vec3(-1.0, 1.0, -1.0),
    vec3( 1.0, 1.0, -1.0),

    // Bottom face
    vec3(-1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0, -1.0),

    vec3( 1.0, -1.0, -1.0),
    vec3(-1.0, -1.0,  1.0),
    vec3( 1.0, -1.0,  1.0)
};

void main()
{
    vec3 position =
        cubeVertices[
            gl_VertexIndex
        ];

    localPosition =
        position;

    gl_Position =
        pushConstants.viewProjection *
        vec4(
            position,
            1.0
        );
}