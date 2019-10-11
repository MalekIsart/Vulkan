#version 450

layout(location = 0) out vec3 v_worldPos;
//layout(location = 1) out vec2 v_uv;

#define PI 3.141592653589793

layout(set = 0, binding = 0) uniform Matrices
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout(set = 1, binding = 0) uniform Instances
{
	mat4 worldMatrix;
};


// un quad en triangle strip agencement en Z (0,1,2 CW)(1,2,3 CCW)
vec2 positions[4] = vec2[](
    vec2(-1.0, 1.0),
	vec2(1.0, 1.0),
    vec2(-1.0, -1.0),
    vec2(1.0, -1.0)
);

void main() 
{
	vec2 positionNDC = positions[gl_VertexIndex];

	// gl_Position contient les coordonnées du quad plein ecran en NDC
    gl_Position = vec4(positionNDC, 1.0, 1.0);
	
	//v_uv = positionNDC * 0.5 + 0.5;

	// on part du principe que chaque pixel de l'ecran est en fait la projection
	// des texels d'une cube map (sky box) en incluant la rotation de la camera
	// Il est des lors possible de recreer (retro-projeter) les coordonnees d'un cube
	// a partir des coordonnees NDC.
	 
	// principe de base, on effectue les calculs a l'envers:
	// NDC -> view space = inverse(projectionMatrix)
	// view space -> world space = inverse(viewMatrix)
	// combinee inverse(projectionMatrix * viewMatrix)
	// il pourrait etre tentant de faire simplement (viewMatrix * projectionMatrix)
	// cependant une matrice de projection n'est pas orthogonale donc M^-1 = M^t pas vrai ici

	// de plus on oublie un element, la 4eme dimension homogene tres importante dans la projection


	// ne pas oublier que le view matrix contient une translation qu'il faut annuler
	mat4 NDCToWorld = inverse(projectionMatrix * mat4(mat3(viewMatrix)));
	// je triche un peu pour ma camera ici, 
	// juste histoire de faire tourner la skybox
	mat3 rotationMatrix = mat3(worldMatrix);

	v_worldPos = rotationMatrix * (NDCToWorld * gl_Position).xyz; 
	return;



	vec4 positionH = inverse(projectionMatrix) * gl_Position;
	vec3 position = positionH.xyz / positionH.w;
	position = inverse(mat3(viewMatrix)) * position;
	v_worldPos = rotationMatrix * position;
}
