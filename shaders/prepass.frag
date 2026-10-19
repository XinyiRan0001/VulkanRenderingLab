#version 450
layout(location=0) in vec3 viewNormal;
layout(location=1) in vec3 viewPosition;
layout(location=0) out vec4 normalAndDistance;
void main() {
    normalAndDistance=vec4(normalize(viewNormal),-viewPosition.z);
}
