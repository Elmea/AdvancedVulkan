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

//
// modele micro-facette (Cook-Torrance) avec GTR2/GGX et Smith-Correlated
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

// voir https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf
// 
// remapping de Karis, k = @ / 2, @ = roughness² 
// de plus, en se basant sur Burley (Disney) il remappe
// roughness = (roughness+1.0)/2.0
// du coup, comme @ = roughness² -> (roughness+1)² / 2²
// et k = @ / (2/1)
// identite (a/b) / (c/d) -> a*d / b*c 
// ce qui donne (roughness+1)² / 8
// mais attention, ca ne marche en IBL, trop sombre
float GeometrySchlickGGX(float NdotX, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float nom   = NdotX;
    float denom = NdotX * (1.0 - k) + k;

    return nom / denom;
}

float Geometry_Smith(float roughness, float NdotV, float NdotL)
{
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
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

vec3 CookTorranceGGX_Karis(float roughness, vec3 f0, float NdotL, float NdotV, float NdotH, float VdotH)
{
	// la Normal Distribution Function (NDF)
	float D = Distribution_GGX(roughness, NdotH);
	// Smith avec G1 = Shlick-Smith corrige par Karis et Burley
	float G = Geometry_Smith(roughness, NdotV, NdotL);
	// Equation de Fresnel
	vec3 F = FresnelSchlick(f0, VdotH);
	
	// divisez par PI plus tard si votre equation d'illumination est physiquement correcte
	return F*D*G / (4.0*NdotL*NdotV);
}


void main() 
{	
	// MATERIAU GENERIQUE
	// doit etre parametre en tant que variables uniformes
	const float metallic = 0.0;					// surface metallique ou pas ?
	
	// Cdiff : couleur sRGB car issue d'un color picker
	const vec3 Cdiff = vec3(219, 37, 110) * 1.0/255.0; 
	vec3 albedo = pow(Cdiff, vec3(2.2)); // gamma->linear
	
	const float reflectance = 0.5;	// remplace f0 dans le cas isolant, ignore en metallic
	const float perceptual_roughness = 0.5;		// controle la taille de la glossiness 

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
    // finalColor = pow(finalColor, vec3(1.0/2.2));
	
	outColor = vec4(finalColor, 1.0);
}