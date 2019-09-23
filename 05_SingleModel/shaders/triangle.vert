#version 450
#extension GL_ARB_separate_shader_objects : enable

//#define OPENGL_NDC

layout(location = 0) out vec3 fragColor;

layout(binding = 0) uniform Matrices
{
	mat4 worldMatrix;
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

#ifdef OPENGL_NDC
vec2 positions[3] = vec2[](
    vec2(0.0, 0.5),
    vec2(-0.5, -0.5),
    vec2(0.5, -0.5)
);
#else
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);
#endif

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),
    vec3(0.0, 1.0, 0.0),
    vec3(0.0, 0.0, 1.0)
);

layout(push_constant) uniform PushConstants {
	float time;
};

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	float scale = 1.0;//cos(time);
	mat3 scaleMatrix = mat3(
		vec3(scale,0.0,0.0),
		vec3(0.0,scale,0.0),
		vec3(0.0,0.0,scale)
	);
	mat3 rotationMatrix = mat3(
		vec3(cos(time),0.0,-sin(time)),
		vec3(0.0,1.0,0.0),
		vec3(sin(time),0.0,cos(time))
	);

	gl_Position = projectionMatrix * viewMatrix * worldMatrix * vec4(/*rotationMatrix * scaleMatrix **/ gl_Position.xyz, 1.0);

#ifdef OPENGL_NDC
    gl_Position.y = -gl_Position.y;
	gl_Position.z = (gl_Position.z+gl_Position.w)/2.0;
#endif
	fragColor = colors[gl_VertexIndex];
}