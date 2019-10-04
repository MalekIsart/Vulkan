#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_eyePosition;

layout(location = 0) out vec4 outColor;

vec3 FresnelSchlick(vec3 f0, float cosTheta) {
	return f0 + (vec3(1.0) - f0) * pow(1.0 - cosTheta, 5.0);
}

void main() 
{
	// LUMIERE : vecteur VERS la lumiere en repere main droite OpenGL (+Z vers nous)
	const vec3 L = normalize(vec3(1.0, 0.0, 1.0));
	
	// MATERIAU
	const vec3 albedo = vec3(1.0, 0.0, 1.0);	// albedo = Cdiff, ici magenta
	//const float metallic = 0.0;					// surface metallique ou pas ?
	const vec3 f0 = vec3(0.04);					// reflectivite a 0 degre, ici 4% donc isolant
	const float shininess = 512.0;				// brillance speculaire (intensite & rugosite)

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
	// diffuse = Lambert BRDF * cos0
	//
	vec3 diffuse = albedo * NdotL;
	
	//
	// specular = Gotanda BRDF * cos0
	//
	// Gotanda utilise VdotH plutot que NdotV
	// car Blinn-Phong est inspire du modele "micro-facette" base sur le vecteur H
	vec3 fresnel = FresnelSchlick(f0, VdotH); 
	float normalisation = (shininess + 2.0) / ( 4.0 * ( 2.0 - exp2(-shininess/2.0) ) );
	float BlinnPhong = pow(NdotH, shininess);
	float G = 1.0 / max(NdotL, NdotV);			// NEUMANN
	vec3 specular = vec3(normalisation * BlinnPhong * G) * NdotL;
	
	// on utilise le RESULTAT de l'equation de Fresnel comme facteur de balance de l'energie
	vec3 Ks = fresnel;
	
	// 
	// couleur finale
	//
	// Kd implicite
	// vec3 Kd = vec3(1.0) - Ks;
	// Gotanda utilise la formulation suivante pour Kd :
	vec3 Kd = vec3(1.0) - FresnelSchlick(f0, NdotL);

	vec3 finalColor = Kd * diffuse + Ks * specular;

	// ne pas oublier la conversion linear->gamma si pas gere automatiquement
    // finalColor = pow(finalColor, vec3(1.0/2.2));

    outColor = vec4(finalColor, 1.0);
}