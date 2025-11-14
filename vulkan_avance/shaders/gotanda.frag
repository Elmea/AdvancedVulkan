#version 450
#extension GL_ARB_separate_shader_objects : enable

#define PI 3.14159265

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 3) in vec4 v_tangent;
layout(location = 4) in vec3 v_eyePosition;

layout(set = 2, binding = 0) uniform sampler2D u_envmap;
layout(set = 2, binding = 1) uniform sampler2D u_diffuseMap;
layout(set = 2, binding = 2) uniform sampler2D u_normalMap;
layout(set = 2, binding = 3) uniform sampler2D u_pbrMap;
layout(set = 2, binding = 4) uniform sampler2D u_occlusionMap;
layout(set = 2, binding = 5) uniform sampler2D u_emissiveMap;

layout(location = 0) out vec4 outColor;

vec3 FresnelSchlick(vec3 f0, float cosTheta) {
	return f0 + (vec3(1.0) - f0) * pow(1.0 - cosTheta, 5.0);
}

vec3 Fresnel(vec3 f0, float cosTheta, float roughness)
{
	float schlick = pow(1.0 - cosTheta, 5.0);
	return f0 + ((max(vec3(1.0 - roughness), f0) - f0) * schlick);
}

void main() 
{
	// LUMIERE : vecteur VERS la lumiere en repere main droite OpenGL (+Z vers nous)
	const vec3 L = normalize(vec3(0.0, 0.0, 1.0));
	
	// MATERIAU
	const vec3 albedo = texture(u_diffuseMap, v_uv).rgb; //vec3(1.0, 0.0, 1.0);	// albedo = Cdiff, ici magenta
	const vec4 pbr = texture(u_pbrMap, v_uv);

	const float metallic = pbr.b;					// surface metallique ou pas ?
	const float perceptual_roughness = pbr.g;

	const vec3 f0 = mix(vec3(0.04), albedo, metallic);	// reflectivite a 0 degre, ici 4% si isolant

	const float roughness = perceptual_roughness * perceptual_roughness;

	const float shininess = (2.0 / max(roughness*roughness, 0.0000001)) - 2.0;//512.0;				// brillance speculaire (intensite & rugosite)

	// rappel : le rasterizer interpole lineairement
	// il faut donc normaliser sinon la norme des vecteurs va changer de magnitude
	vec3 N = normalize(v_normal);
	vec3 T = normalize(v_tangent.xyz);
	vec3 B = cross(N, T) * v_tangent.w;
	mat3 TBN = mat3(T, B, N);
	vec3 normalTS = texture(u_normalMap, v_uv).rgb * 2.0 - 1.0;
	N = normalize(TBN * normalTS);

	vec3 V = normalize(v_eyePosition - v_position);
	vec3 H = normalize(L + V);

	// on max a 0.001 afin d'eviter les problemes a 90�
	float NdotL = max(dot(N, L), 0.001);
	float NdotH = max(dot(N, H), 0.001);
	float VdotH = max(dot(V, H), 0.001);
	float NdotV = dot(N, V);//, 0.001);

	//
	// diffuse = Lambert BRDF
	//
	vec3 diffuse = albedo * (1.0 - metallic);
	
	//
	// specular = Gotanda BRDF
	//
	// Gotanda utilise VdotH plutot que NdotV
	// car Blinn-Phong est inspire du modele "micro-facette" base sur le vecteur H
	vec3 fresnel = Fresnel(f0, VdotH, 0.0); 

	float normalisation = (shininess + 2.0) / ( 4.0 * ( 2.0 - exp2(-shininess/2.0) ) );
	float BlinnPhong = pow(NdotH, shininess);
	float G = 1.0 / max(NdotL, max(NdotV, 0.001));			// NEUMANN
	vec3 specular = vec3(normalisation * BlinnPhong * G);
	
	// on utilise le RESULTAT de l'equation de Fresnel speculaire comme facteur de balance de l'energie
	vec3 Ks = fresnel;
	
	// Kd implicite
	// vec3 Kd = vec3(1.0) - Ks;
	// cependant pas de difference flagrante avec
	//vec3 Kd = vec3(1.0) - f0;
	// Gotanda utilise la formulation suivante pour Kd :
	vec3 Kd = vec3(1.0) - Fresnel(f0, NdotL, 0.0);

	vec3 directColor = (Kd * diffuse + Ks * specular) * NdotL;

	// 
	// indirect
	//

	vec3 indirectColor = vec3(0.0);

	//
	// final
	//

	float AO = texture(u_occlusionMap, v_uv).r;

	vec3 emissiveColor = texture(u_emissiveMap, v_uv).rgb;

	vec3 finalColor = emissiveColor + AO * (directColor + indirectColor);

    outColor = vec4(finalColor, 1.0);
}