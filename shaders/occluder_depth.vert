#version 450
layout(location=0) in vec3 position;
layout(binding=0) uniform Camera { mat4 clipFromScene; } camera;
void main() { gl_Position=camera.clipFromScene*vec4(position,1); }
