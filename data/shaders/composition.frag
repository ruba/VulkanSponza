#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (binding = 1) uniform sampler2D samplerposition;
layout (binding = 2) uniform sampler2D samplerNormal;
layout (binding = 3) uniform usampler2D samplerAlbedo;
layout (binding = 4) uniform sampler2D samplerSSAO;

layout (constant_id = 0) const int SSAO_ENABLED = 1;
layout (constant_id = 1) const float AMBIENT_FACTOR = 0.0;

layout (location = 0) in vec2 inUV;

layout (location = 0) out vec4 outFragcolor;

#define PI 3.14159265358979f
#define EPS 0.00000001f

struct Light {
	vec4 position;
	vec4 color;
	float radius;
	float quadraticFalloff;
	float linearFalloff;
	float _pad;
};

#define NUM_LIGHTS 17

layout (binding = 5) uniform UBO 
{
	Light lights[NUM_LIGHTS];
	vec4 viewPos;
	mat4 view;
	mat4 model;
} ubo;


float DTerm_GGX(float roughness, float NdotH)
{
	float roughness2 = roughness * roughness;
	float v = (NdotH * NdotH * (roughness2 - 1) + 1);
	return roughness2 / (PI * v * v);
}


float G1(vec3 n, vec3 v, float k)
{
	const float NdotV = clamp(dot(n, v), 0.f, 1.f);
	return NdotV / (NdotV * (1 - k) + k);
}

float GTerm(float roughness, vec3 n, vec3 v, vec3 l)
{
	const float t = roughness + 1;
	const float k = t * t / 8.f;

	return G1(n, v, k) * G1(n, l, k);
}

float FTerm(float F0, vec3 v, vec3 h)
{
	const float VdotH = clamp(dot(v, h), 0.f, 1.f);
	float p = (-5.55473 * VdotH - 6.98316) * VdotH;

	return F0 + (1 - F0) * pow(2.f, p);
}

// Schlick approximation
vec3 FTerm(vec3 specularColor, vec3 h, vec3 v)
{
	const float VdotH = clamp(dot(v, h), 0.f, 1.f);
    return (specularColor + (1.0f - specularColor) * pow(1.0f - VdotH, 5));
}

void main() 
{
	// Get G-Buffer values
	vec3 fragPos = texture(samplerposition, inUV).rgb;
	vec3 normal = texture(samplerNormal, inUV).rgb * 2.0 - 1.0;

	// unpack
	ivec2 texDim = textureSize(samplerAlbedo, 0);
	uvec4 albedo = texelFetch(samplerAlbedo, ivec2(inUV.st * texDim ), 0);

	vec4 color;
	color.rg = unpackHalf2x16(albedo.r);
	color.ba = unpackHalf2x16(albedo.g);

	float roughness = unpackHalf2x16(albedo.b).r;
	float metallic = unpackHalf2x16(albedo.a).r;

	vec3 fragcolor = vec3(0.f, 0.f, 0.f);
	
	// 0.03 - default specular value for dielectric.
	vec3 realSpecularColor = mix( vec3(0.03f), color.rgb, metallic);
	vec4 realAlbedo = clamp( color - color * metallic, vec4(0.f), vec4(1.0f) );
	
	vec3 N = normalize(normal);
	vec3 viewPos = vec3(ubo.view * ubo.model * vec4(ubo.viewPos.xyz, 1.0));
	vec3 V = normalize(viewPos - fragPos);

	if (length(fragPos) == 0.0)
	{
		roughness = 0.f;
		metallic = 0.f;
		fragcolor = color.rgb;
	}
	else
	{	
		for(int i = 0; i < NUM_LIGHTS; ++i)
		{
			vec3 lightPos = vec3(ubo.view * ubo.model * vec4(ubo.lights[i].position.xyz, 1.0));
			vec3 L = lightPos - fragPos;

			float dist = length(L);
			float atten = ubo.lights[i].radius / (pow(dist, 2.0) + 1.0);

			L = L / dist;
			
			vec3 H = normalize(L + N);

			float NdotV = clamp(dot(N, V), 0.f, 1.f);
			float NdotH = clamp(dot(N, H), 0.f, 1.f);
			float NdotL = clamp(dot(N, L), 0.f, 1.f);
			
			const float F0 = 1.1f;

			// Cook-Torrance, GGX distribution, Schlick Fresnel approximation
			float 	Dterm = DTerm_GGX(roughness, NdotH);
			float 	Gterm = GTerm(roughness, N, V, L);
			vec3	Fterm = FTerm(realSpecularColor, H, V);

			vec3 diffuse = NdotL * realAlbedo.rgb;
			vec3 specular = ( Dterm * Gterm * Fterm ) / (4.0f * NdotL * NdotV + EPS);

			fragcolor += ubo.lights[i].color.rgb * atten * (diffuse + specular);
		}    	
	}

	outFragcolor = vec4(fragcolor, 1.0f);
}