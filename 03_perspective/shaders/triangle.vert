#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec3 fragColor;

#ifdef OPENGL_HANDEDNESS
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

	mat3 scaleMatrix = mat3(
		vec3(cos(time),0.0,0.0),
		vec3(0.0,cos(time),0.0),
		vec3(0.0,0.0,1.0)
	);
	mat3 rotationMatrix = mat3(
		vec3(cos(time),sin(time),0.0),
		vec3(-sin(time),cos(time),0.0),
		vec3(0.0,0.0,1.0)
	);

	gl_Position = vec4(rotationMatrix * scaleMatrix * gl_Position.xyz, 1.0);

#ifdef OPENGL_HANDEDNESS
    gl_Position.y = -gl_Position.y;
#endif
	fragColor = colors[gl_VertexIndex];
}