#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_eyePosition;

layout(location = 0) out vec4 outColor;

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

// Calcul un facteur de "glossiness/shininess" a partir de "roughness"
// roughness = 1.0 -> glossiness = 0.0
// roughness ~= 0.0 -> glossiness = tres tres elevee
float CalcGlossiness(float roughness)
{
	roughness = max(roughness, 0.001);
	return ( 2.0 / (roughness * roughness) ) - 2.0;
}

void main() 
{
	// LUMIERE : vecteur VERS la lumiere en repere main droite OpenGL (+Z vers nous)
	const vec3 L = normalize(vec3(1.0, 0.0, 1.0));
	
	// MATERIAU GENERIQUE
	// doit etre parametre en tant que variables uniformes
	const float metallic = 0.0;					// surface metallique ou pas ?
	const vec3 albedo = vec3(1.0, 0.0, 1.0);	// albedo = Cdiff si isolant, Cspec si metallic
	const float reflectance = 0.5;	// remplace f0 dans le cas isolant, ignore en metallic
	const float perceptual_roughness = 0.5;		// controle la taille de la glossiness 
	
	//
	// remapping des inputs qui sont perceptuelles
	//
	// le facteur de glossiness de Blinn-Phong est calcule avec la roughness
	float roughness = perceptual_roughness * perceptual_roughness;
	float shininess = CalcGlossiness(roughness);
	// la reflectivite a 0° (Fresnel 0°) se calcul avec "reflectance" pour les dielectrics (isolants)
	// et l'albedo pour les metaux
	vec3 f0 = CalcSpecularColor(reflectance, albedo, metallic);

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
	// Pas de composante diffuse si metallique 
	vec3 diffuse = CalcDiffuseColor(albedo, metallic) * NdotL;
	
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