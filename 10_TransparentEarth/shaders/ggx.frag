#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec3 v_eyePosition;

layout(location = 0) out vec4 outColor;

layout(set = 2, binding = 0) uniform sampler2D u_cutoutTexture;

layout(push_constant) uniform PushConstants {
	float perceptual_roughness;			// controle la taille de la glossiness (de maniere perceptuellement lineaire)
	float reflectance;					// remplace f0 dans le cas isolant, ignore en metallic
	float metallic;
	int instanceIndex;
};

vec3 FresnelSchlick(vec3 f0, float cosTheta) {
	return f0 + (vec3(1.0) - f0) * pow(1.0 - cosTheta, 5.0);
}

// Calcule F0 a utiliser en input de FresnelSchlick
// isolant : reflectance = 50% -> f0 = 4%
vec3 CalcSpecularColor(float reflectance, vec3 albedo, float metallic)
{
	return mix(vec3(0.16 * reflectance * reflectance), albedo, metallic);
}

// Calcule la diffuse color du materiau selon qu'il soit metallique ou non
vec3 CalcDiffuseColor(vec3 albedo, float metallic)
{
	return mix(albedo, vec3(0.0), metallic);
}

//
// modele micro-facette (Cook-Torrance) avec D=GTR2/GGX et G/denom=Smith Height-Correlated
// adaptation de https://github.com/google/filament/blob/master/shaders/src/brdf.fs
// 

// roughness est ici perceptual_roughness²
float Distribution_GGX(float roughness, float NdotH) 
{
    // Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"
	// identique a Generalized Trowbridge-Reitz avec un facteur 2 (GTR-2)
	float oneMinusNdotHSquared = 1.0 - NdotH * NdotH;

    float a = NdotH * roughness;
    float k = roughness / (oneMinusNdotHSquared + a * a);
    float d = k * k;
    return d;
}

// le facteur de visibilite V combine G (attenuation Geometrique) et le denominateur de Cook-Torrance
// roughness est ici perceptual_roughness² 
// similaire a https://seblagarde.files.wordpress.com/2015/07/course_notes_moving_frostbite_to_pbr_v32.pdf page 13
float Visibility_SmithGGXCorrelated(float roughness, float NdotV, float NdotL) 
{
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"

    float a2 = roughness * roughness;
    // TODO: lambdaV can be pre-computed for all the lights, it should be moved out of this function
    float lambdaV = NdotL * sqrt((NdotV - a2 * NdotV) * NdotV + a2);
    float lambdaL = NdotV * sqrt((NdotL - a2 * NdotL) * NdotL + a2);
    float v = 0.5 / (lambdaV + lambdaL);
    // a2=0 => v = 1 / 4*NdotL*NdotV   => min=1/4, max=+inf
    // a2=1 => v = 1 / 2*(NdotL+NdotV) => min=1/4, max=+inf
    return v;
}

vec3 CookTorranceGGX(float roughness, vec3 f0, float NdotL, float NdotV, float NdotH, float VdotH)
{
	// la Normal Distribution Function (NDF)
	float D = Distribution_GGX(roughness, NdotH);
	// V = G / (4.NdotL.NdotV)
	float V = Visibility_SmithGGXCorrelated(roughness, NdotV, NdotL);
	// Equation de Fresnel
	vec3 F = FresnelSchlick(f0, VdotH);
	
	// divisez par PI plus tard si votre equation d'illumination est physiquement correcte
	return F*D*V;
}


void main() 
{	
	// MATERIAU GENERIQUE

	// Cdiff : couleur sRGB car issue d'un color picker
	vec3 Cdiff = vec3(219, 37, 110) * 1.0/255.0; 

	vec4 texColor = texture(u_cutoutTexture, v_uv);
	float alpha = texColor.r;						// canal r&g&b contient en fait l'opacite
	
	vec3 albedo = pow(Cdiff, vec3(2.2)); // gamma->linear
	
	// premultiplication de l'albedo par l'alpha
	albedo *= alpha;

	// LUMIERE : vecteur VERS la lumiere en repere main droite OpenGL (+Z vers nous)
	//const vec3 LightPosition = vec3(-500.0, 0.0, 1000.0);
	//vec3 L = normalize(LightPosition - v_position);
	vec3 L = normalize(vec3(-1.0, 0.0, 1.0));

	// rappel : le rasterizer interpole lineairement
	// il faut donc normaliser sinon la norme des vecteurs va changer de magnitude
	vec3 N = normalize(v_normal);
	vec3 V = normalize(v_eyePosition - v_position);
	vec3 H = normalize(L + V);

	// on max a 0.001 afin d'eviter les problemes a 90°
	float NdotL = max(dot(N, L), 0.001);
	float NdotH = max(dot(N, H), 0.001);
	float VdotH = max(dot(V, H), 0.001);
	float NdotV = max(dot(N, V), 0.001);

	
	//
	// remapping des inputs qui sont perceptuelles
	// on peut mettre la roughness au carre, au cube, puissance 4, ou customisee
	float roughness = perceptual_roughness * perceptual_roughness;
	
	// la reflectivite a 0° (Fresnel 0°) se calcul avec "reflectance" 
	// pour les dielectrics (isolants) et l'albedo pour les metaux
	vec3 f0 = CalcSpecularColor(reflectance, albedo, metallic);

	//
	// diffuse = Lambert BRDF * cos0
	// Pas de composante diffuse si metallique 
	//
	vec3 diffuse = CalcDiffuseColor(albedo, metallic) * NdotL;

	// Gotanda utilise la formulation suivante pour Kd :
	vec3 Kd = vec3(1.0) - FresnelSchlick(f0, NdotL);

	//
	// specular = Cook-Torrance BRDF * cos0
	//
	vec3 specular = CookTorranceGGX(roughness, f0, NdotL, NdotV, NdotH, VdotH) * NdotL;
	
	// 
	// couleur finale
	//
	// Ks figure implicitement dans CookTorrance (composante F)
	
	vec3 finalColor = Kd * diffuse + specular;
	
	// ne pas oublier la conversion linear->gamma si pas gere automatiquement
    finalColor = pow(finalColor, vec3(1.0/2.2));
	
	outColor = vec4(finalColor, alpha);
}