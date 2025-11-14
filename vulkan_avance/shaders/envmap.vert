#version 450
#extension GL_ARB_separate_shader_objects : enable

//#define OPENGL_NDC

layout(location = 0) out vec3 v_position;
layout(location = 1) out vec2 v_uv;

layout(set = 1, binding = 0) uniform Matrices
{
	mat4 viewMatrix;
	mat4 projectionMatrix;
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
    vec4 position = vec4(positionNDC, 1.0, 1.0);
	v_uv = positionNDC * 0.5 + 0.5;
	v_position = vec3(mat4(inverse(mat3(viewMatrix))) * inverse(projectionMatrix) * position);
	gl_Position = position;

#ifdef OPENGL_NDC
	// cette ligne est importante si votre mesh est defini en counter clockwise
	// et que le mode frontFace est VK_FRONT_FACE_COUNTER_CLOCKWISE
    gl_Position.y = -gl_Position.y;

	// dans le cas ou la matrice de projection n'est pas [0;+1] decommentez cette ligne
	//gl_Position.z = (gl_Position.z+gl_Position.w)/2.0;
#endif
}