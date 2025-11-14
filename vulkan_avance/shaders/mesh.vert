#version 450
#extension GL_ARB_separate_shader_objects : enable

//#define OPENGL_NDC

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
layout(location = 3) in vec4 a_tangent;

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec4 v_tangent;
layout(location = 4) out vec3 v_eyePosition;

layout(set = 1, binding = 0) uniform Matrices
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
};

layout(set = 0, binding = 0) uniform Instances
{
	mat4 worldMatrix;
};

void main() 
{
    vec4 positionWS = worldMatrix * vec4(a_position, 1.0);

	mat3 normalMatrix = mat3(worldMatrix);//transpose(inverse(mat3(worldMatrix))); 
	vec3 normalWS = normalMatrix * a_normal;
	vec3 tangentWS = normalMatrix * a_tangent.xyz;
	
	v_uv = a_uv;
	v_position = vec3(positionWS);
	v_tangent = vec4(tangentWS, a_tangent.w);
	v_normal = normalize(normalWS);

	v_eyePosition = -vec3(transpose(viewMatrix) * viewMatrix[3]); 

	gl_Position = projectionMatrix * viewMatrix * positionWS;

#ifdef OPENGL_NDC
	// cette ligne est importante si votre mesh est defini en counter clockwise
	// et que le mode frontFace est VK_FRONT_FACE_COUNTER_CLOCKWISE
    gl_Position.y = -gl_Position.y;

	// dans le cas ou la matrice de projection n'est pas [0;+1] decommentez cette ligne
	//gl_Position.z = (gl_Position.z+gl_Position.w)/2.0;
#endif
}