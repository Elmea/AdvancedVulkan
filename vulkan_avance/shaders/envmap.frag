#version 450

#define PI 3.141592653589793

layout(set = 2, binding = 0) uniform sampler2D u_envmap;

layout(location = 0) in vec3 v_position;
layout(location = 1) in vec2 v_uv;

layout(location = 0) out vec4 outColor;

vec2 RadialToTexCoords(vec3 position)
{
	// position doit etre normalise au prealable => radius = 1.0
	// float r = length(position);
	
	// suppose un repere ou Y est l'axe vertical (UP) - la convention mathematique est Z UP!
	float longitude = atan(position.z, position.x);
	float latitude = acos(position.y); // acos(position.y / r);
	
	// projete la sphere dans le domaine de la texture
    // afin d'obtenir des coordonnees normalises [0;1]
    const vec2 radianFactors = vec2(1.0 / (2.0*PI), 1.0 / PI);
	return vec2(longitude, latitude) * radianFactors;
}

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESfilm(vec3 y)
{
	vec3 x = y * 0.6; // 0.6 = original ACES
	float a = 2.51;
	float b = 0.03;
	float c = 2.43;
	float d = 0.59;
	float e = 0.14;
	return min(vec3(1.0), (x*(a*x+vec3(b))) / (x*(c*x+vec3(d)) + vec3(e)));
}

vec3 Reinhardt(vec3 x)
{
	return x / (vec3(1.0) + x);
}

void main() 
{
	// important: normalisation 
	vec3 cubeDir = normalize(v_position);
	
	// important: si votre repere monde/camera est main droite il faut corriger le z
	cubeDir.z = -cubeDir.z;

	// on sample une texture2D equi-rectangulaire (lattitude-longitude)
	vec2 uv = RadialToTexCoords(cubeDir);

    outColor = texture(u_envmap, uv);

	// idealement il faudrait faire tout ceci en post-process

	// exposition
	//float exposition = 0.5;
	//outColor.rgb = vec3(1.0) - exp(outColor.rgb * -exposition);

	// Tone-mapping
	//outColor.rgb = Reinhardt(outColor.rgb);
	outColor.rgb = ACESfilm(outColor.rgb);
}