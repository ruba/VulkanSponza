#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#define NUM_LIGHTS 3

layout (location = 0) in vec3 inPos;

layout (binding = 0) uniform UBO 
{
	mat4 depthMVP[NUM_LIGHTS];
} ubo;

layout(push_constant) uniform PushConsts {
	int lightIdx;
} pushConsts;

out gl_PerVertex 
{
    vec4 gl_Position;   
};

 
void main()
{
	gl_Position =  ubo.depthMVP[pushConsts.lightIdx] * vec4(inPos, 1.0);
}