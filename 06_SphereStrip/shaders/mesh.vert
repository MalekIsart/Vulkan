#version 450
#extension GL_ARB_separate_shader_objects : enable

//#define OPENGL_NDC

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;

layout(location = 0) out vec4 fragColor;

layout(binding = 0) uniform Matrices
{
	mat4 worldMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout(push_constant) uniform PushConstants {
	float time;
};

void main() {
    vec4 positionWS = worldMatrix * vec4(a_position, 1.0);
	// todo: UBO
	mat3 normalMatrix = transpose(inverse(mat3(worldMatrix))); 
	vec3 normalWS = normalMatrix * a_normal;
	
	gl_Position = projectionMatrix * viewMatrix * positionWS;

#ifdef OPENGL_NDC
    gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z+gl_Position.w)/2.0;
#endif
	fragColor = vec4(a_normal * 0.5 + 0.5, 1.0);
}