#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 o_fragColor;

void main(void)
{
	vec4 color = vec4(1.0, 0.5, 0.25, 1.0);
	// si color est sRGB (provient d'une texture ou color-picker)
	// il faut appliquer une conversion sRGB->Linear
	color.rgb = pow(color.rgb, vec3(2.2));
/*
	// on reprend l'esprit de l'eq. d'illumination de Phong
	// mais on separe en deux parties : directe et indirecte
	float cosTheta = dot(N, L);
	vec3 diffuse = albedo * Lambert(cosTheta);

	float shininess = RemapRoughness(roughness);

	// speculaire est ici une BRDF
	vec3 speculaire = Phong(, shininess) * cosTheta;
	// le modele de CookTorrance (speculaire) est generaliste
	// de la forme D*F*G / (4*N.V*N.L)
	// D(), F() et G() sont customisables
	// Il existe plusieurs variantes de D() on parle de NDF
	// Attention NDF != BRDF
	// ex: Phong BRDF = cos@^n
	/// Phong NDF = distribution des normales comme la Phong BRDF
	vec3 speculaire = CookTorrance(...)


	// Regle PBR #1 : conservation et balance de l'energie
	// balance : somme des energies reflechies <= 1.0
	// Kd + Ks = 1.0
	vec3 F0;
	vec3 Ks = FresnelSchlick(F0, cosV);
	vec3 Kd = 1.0 - Ks;

	vec3 illum_directe = Kd * diffuse + Ks * speculaire;
*/

	// si la swapchain utilise un format _SRGB
	// le GPU va automatiquement convertir de linear->SRGB (gamma)
	// c'est a dire pow(o_fragColor, 1/2.2)
	o_fragColor = color;
}