/*
* Vulkan Example - Playground for rendering Crytek's Sponza model (deferred renderer)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <random>
#include <unordered_map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#if defined(__ANDROID__)
#include <android/asset_manager.h>
#endif

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

//#define PER_MESH_BUFFERS

// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_UV,
	vkMeshLoader::VERTEX_LAYOUT_COLOR,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_TANGENT
};

struct Vertex
{
	glm::vec3 pos;
	glm::vec2 uv;
	glm::vec3 color;
	glm::vec3 normal;
	glm::vec3 tangent;
};

template <typename T> 
class VulkanResourceList
{
public:
	VkDevice &device;
	std::unordered_map<std::string, T> resources;
	VulkanResourceList(VkDevice &dev) : device(dev) {};
	const T get(std::string name)
	{
		return resources[name];
	}
	T *getPtr(std::string name)
	{
		return &resources[name];
	}
	bool present(std::string name)
	{
		return resources.find(name) != resources.end();
	}
};

class PipelineLayoutList : public VulkanResourceList<VkPipelineLayout>
{
public:
	PipelineLayoutList(VkDevice &dev) : VulkanResourceList(dev) {};

	~PipelineLayoutList()
	{
		for (auto& pipelineLayout : resources)
		{
			vkDestroyPipelineLayout(device, pipelineLayout.second, nullptr);
		}
	}

	VkPipelineLayout add(std::string name, VkPipelineLayoutCreateInfo &createInfo)
	{
		VkPipelineLayout pipelineLayout;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &createInfo, nullptr, &pipelineLayout));
		resources[name] = pipelineLayout;
		return pipelineLayout;
	}
};

class PipelineList : public VulkanResourceList<VkPipeline>
{
public:
	PipelineList(VkDevice &dev) : VulkanResourceList(dev) {};

	~PipelineList()
	{
		for (auto& pipeline : resources)
		{
			vkDestroyPipeline(device, pipeline.second, nullptr);
		}
	}

	VkPipeline addGraphicsPipeline(std::string name, VkGraphicsPipelineCreateInfo &pipelineCreateInfo, VkPipelineCache &pipelineCache)
	{
		VkPipeline pipeline;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline));
		resources[name] = pipeline;
		return pipeline;
	}
};

class TextureList : public VulkanResourceList<vkTools::VulkanTexture>
{
private:	
	vkTools::VulkanTextureLoader *textureLoader;
public:
	TextureList(VkDevice &dev, vkTools::VulkanTextureLoader *textureloader) : 
		VulkanResourceList(dev), 
		textureLoader(textureloader) { };

	~TextureList()
	{
		for (auto& texture : resources)
		{
			textureLoader->destroyTexture(texture.second);
		}
	}

	vkTools::VulkanTexture addTexture2D(std::string name, std::string filename, VkFormat format)
	{
		vkTools::VulkanTexture texture;
		textureLoader->loadTexture(filename, format, &texture);
		resources[name] = texture;
		return texture;
	}

	vkTools::VulkanTexture addTextureArray(std::string name, std::string filename, VkFormat format)
	{
		vkTools::VulkanTexture texture;
		textureLoader->loadTextureArray(filename, format, &texture);
		resources[name] = texture;
		return texture;
	}

	vkTools::VulkanTexture addCubemap(std::string name, std::string filename, VkFormat format)
	{
		vkTools::VulkanTexture texture;
		textureLoader->loadCubemap(filename, format, &texture);
		resources[name] = texture;
		return texture;
	}
};

class DescriptorSetLayoutList : public VulkanResourceList<VkDescriptorSetLayout>
{
public:
	DescriptorSetLayoutList(VkDevice &dev) : VulkanResourceList(dev) {};

	~DescriptorSetLayoutList()
	{
		for (auto& descriptorSetLayout : resources)
		{
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout.second, nullptr);
		}
	}

	VkDescriptorSetLayout add(std::string name, VkDescriptorSetLayoutCreateInfo createInfo)
	{
		VkDescriptorSetLayout descriptorSetLayout;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &createInfo, nullptr, &descriptorSetLayout));
		resources[name] = descriptorSetLayout;
		return descriptorSetLayout;
	}
};

class DescriptorSetList : public VulkanResourceList<VkDescriptorSet>
{
private:
	VkDescriptorPool descriptorPool;
public:
	DescriptorSetList(VkDevice &dev, VkDescriptorPool pool) : VulkanResourceList(dev), descriptorPool(pool) {};

	~DescriptorSetList()
	{
		for (auto& descriptorSet : resources)
		{
			vkFreeDescriptorSets(device, descriptorPool, 1, &descriptorSet.second);
		}
	}

	VkDescriptorSet add(std::string name, VkDescriptorSetAllocateInfo allocInfo)
	{
		VkDescriptorSet descriptorSet;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));
		resources[name] = descriptorSet;
		return descriptorSet;
	}
};

struct Resources
{
	PipelineLayoutList* pipelineLayouts;
	PipelineList *pipelines;
	DescriptorSetLayoutList *descriptorSetLayouts;
	DescriptorSetList * descriptorSets;
	TextureList *textures;
} resources;

struct SceneMaterial 
{
	std::string name;
	vkTools::VulkanTexture diffuse;
	vkTools::VulkanTexture roughness;
	vkTools::VulkanTexture metallic;
	vkTools::VulkanTexture bump;
	bool hasAlpha = false;
	bool hasBump = false;
	bool hasRoughness = false;
	bool hasMetaliness = false;
	VkPipeline pipeline;
};

struct SceneMesh
{
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;

	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;

	uint32_t indexCount;
	uint32_t indexBase;

	// Better move to material and share among meshes with same material
	VkDescriptorSet descriptorSet;

	SceneMaterial *material;
};

VkPhysicalDeviceMemoryProperties deviceMemProps;

uint32_t getMemTypeIndex( uint32_t typeBits, VkFlags properties)
{
	for (uint32_t i = 0; i < 32; i++)
	{
		if ((typeBits & 1) == 1)
		{
			if ((deviceMemProps.memoryTypes[i].propertyFlags & properties) == properties)
			{
				return i;
			}
		}
		typeBits >>= 1;
	}

	// todo: throw if no appropriate mem type was found
	return 0;
}

class Scene
{
private:
	VkDevice device;
	VkQueue queue;
	
	// todo: rename
	vk::Buffer *defaultUBO;
	
	VkDescriptorPool descriptorPool;

	vkTools::VulkanTextureLoader *textureLoader;

	const aiScene* aScene;

	void loadMaterials()
	{
		// Add dummy textures for objects without texture
		resources.textures->addTexture2D("dummy.diffuse", assetPath + "sponza/dummy.dds", VK_FORMAT_BC2_UNORM_BLOCK);
		resources.textures->addTexture2D("dummy.specular", assetPath + "sponza/dummy_specular.dds", VK_FORMAT_BC2_UNORM_BLOCK);
		resources.textures->addTexture2D("dummy.bump", assetPath + "sponza/dummy_ddn.dds", VK_FORMAT_BC2_UNORM_BLOCK);
		resources.textures->addTexture2D("dialectric.metallic", assetPath + "SponzaPBR/textures_pbr/Dielectric_metallic_TGA_BC2_1.DDS", VK_FORMAT_BC2_UNORM_BLOCK);

		materials.resize(aScene->mNumMaterials);
		
		for (uint32_t i = 0; i < materials.size(); i++)
		{
			materials[i] = {};

			aiString name;
			aScene->mMaterials[i]->Get(AI_MATKEY_NAME, name);
			aiColor3D ambient;
			aScene->mMaterials[i]->Get(AI_MATKEY_COLOR_AMBIENT, ambient);
			materials[i].name = name.C_Str();
			std::cout << "Material \"" << materials[i].name << "\"" << std::endl;

			// Textures
			aiString texturefile;
			std::string diffuseMapFile;
			// Diffuse
			aScene->mMaterials[i]->GetTexture(aiTextureType_DIFFUSE, 0, &texturefile);
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_DIFFUSE) > 0)
			{
				std::cout << "  Diffuse: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());
				diffuseMapFile = fileName;
				std::replace(fileName.begin(), fileName.end(), '\\', '/');
				if (!resources.textures->present(fileName)) {
                    materials[i].diffuse = resources.textures->addTexture2D(fileName, assetPath + fileName, VK_FORMAT_BC2_UNORM_BLOCK);
				} else {
                    materials[i].diffuse = resources.textures->get(fileName);
				}
			}
			else
			{
				std::cout << "  Material has no diffuse, using dummy texture!" << std::endl;
				materials[i].diffuse = resources.textures->get("dummy.diffuse");
			}

			materials[i].roughness = resources.textures->get("dummy.specular");
			materials[i].metallic = resources.textures->get("dialectric.metallic");

			// Bump (map_bump is mapped to height by assimp)
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_HEIGHT) > 0)
			{
				aScene->mMaterials[i]->GetTexture(aiTextureType_HEIGHT, 0, &texturefile);
				std::cout << "  Bump: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());		
                std::replace(fileName.begin(), fileName.end(), '\\', '/');
				materials[i].hasBump = true;
				if (!resources.textures->present(fileName)) {
					materials[i].bump = resources.textures->addTexture2D(fileName, assetPath + fileName, VK_FORMAT_BC2_UNORM_BLOCK);
				}
				else {
					materials[i].bump = resources.textures->get(fileName);
				}
			}
			else
			{
				std::cout << "  Material has no bump, using dummy texture!" << std::endl;
				materials[i].bump = resources.textures->get("dummy.bump");
			}

			// Reserved channels
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_AMBIENT) > 0)
            {
				aScene->mMaterials[i]->GetTexture(aiTextureType_AMBIENT, 0, &texturefile);
				std::cout << "  Roughness: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());
				std::replace(fileName.begin(), fileName.end(), '\\', '/');
				materials[i].hasRoughness = true;

				if (!resources.textures->present(fileName)) {
					materials[i].roughness = resources.textures->addTexture2D(fileName, assetPath + fileName, VK_FORMAT_BC2_UNORM_BLOCK);
				}
				else {
					materials[i].roughness = resources.textures->get(fileName);
				}
            }

            if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_SPECULAR) > 0)
            {
				aScene->mMaterials[i]->GetTexture(aiTextureType_SPECULAR, 0, &texturefile);
				std::cout << "Metaliness: \"" << texturefile.C_Str() << "\"" << std::endl;
				std::string fileName = std::string(texturefile.C_Str());
				std::replace(fileName.begin(), fileName.end(), '\\', '/');
				materials[i].hasMetaliness = true;

				if (!resources.textures->present(fileName)) {
					materials[i].metallic = resources.textures->addTexture2D(fileName, assetPath + fileName, VK_FORMAT_BC2_UNORM_BLOCK);
				}
				else {
					materials[i].metallic = resources.textures->get(fileName);
				}
            }

			// Mask
			if (aScene->mMaterials[i]->GetTextureCount(aiTextureType_OPACITY) > 0)
			{
				std::cout << "  Material has opacity, enabling alpha test" << std::endl;
				materials[i].hasAlpha = true;
			}

			materials[i].pipeline = resources.pipelines->get("scene.solid");
		}

	}

	void loadMeshes(VkCommandBuffer copyCmd)		
	{
		std::vector<Vertex> gVertices;
		std::vector<uint32_t> gIndices;
		uint32_t gIndexBase = 0;

		meshes.resize(aScene->mNumMeshes);
		for (uint32_t i = 0; i < meshes.size(); i++)
		{
			aiMesh *aMesh = aScene->mMeshes[i];

			std::cout << "Mesh \"" << aMesh->mName.C_Str() << "\"" << std::endl;
			std::cout << "	Material: \"" << materials[aMesh->mMaterialIndex].name << "\"" << std::endl;
			std::cout << "	Faces: " << aMesh->mNumFaces << std::endl;
			
			meshes[i].material = &materials[aMesh->mMaterialIndex];
			meshes[i].indexBase = gIndexBase;

			// Vertices
			std::vector<Vertex> vertices;			
			vertices.resize(aMesh->mNumVertices);

			bool hasUV = aMesh->HasTextureCoords(0);
			bool hasTangent = aMesh->HasTangentsAndBitangents();

			uint32_t vertexBase = gVertices.size();

			for (uint32_t i = 0; i < aMesh->mNumVertices; i++)
			{
				vertices[i].pos = glm::make_vec3(&aMesh->mVertices[i].x);// *0.5f;
				vertices[i].pos.y = -vertices[i].pos.y;
				vertices[i].uv = (hasUV) ? glm::make_vec2(&aMesh->mTextureCoords[0][i].x) : glm::vec3(0.0f);
				vertices[i].normal = glm::make_vec3(&aMesh->mNormals[i].x);
				vertices[i].normal.y = -vertices[i].normal.y;
				vertices[i].color = glm::vec3(1.0f); // todo : take from material
				vertices[i].tangent = (hasTangent) ? glm::make_vec3(&aMesh->mTangents[i].x) : glm::vec3(0.0f, 1.0f, 0.0f);
				gVertices.push_back(vertices[i]);
			}

			// Indices
			std::vector<uint32_t> indices;
			meshes[i].indexCount = aMesh->mNumFaces * 3;
			indices.resize(aMesh->mNumFaces * 3);
			for (uint32_t i = 0; i < aMesh->mNumFaces; i++)
			{
				// Assume mesh is triangulated
				indices[i * 3] = aMesh->mFaces[i].mIndices[0];
				indices[i * 3 + 1] = aMesh->mFaces[i].mIndices[1];
				indices[i * 3 + 2] = aMesh->mFaces[i].mIndices[2];
				gIndices.push_back(indices[i*3] + vertexBase);
				gIndices.push_back(indices[i*3+1] + vertexBase);
				gIndices.push_back(indices[i*3+2] + vertexBase);
				gIndexBase += 3;
			}

			// Create buffers
			// todo : staging
			// todo : only one memory allocation

			uint32_t vertexDataSize = vertices.size() * sizeof(Vertex);
			uint32_t indexDataSize = indices.size() * sizeof(uint32_t);

			VkMemoryAllocateInfo memAlloc = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;

			VkResult err;
			void *data;

			struct
			{
				struct {
					VkDeviceMemory memory;
					VkBuffer buffer;
				} vBuffer;
				struct {
					VkDeviceMemory memory;
					VkBuffer buffer;
				} iBuffer;
			} staging;

			// Generate vertex buffer
			VkBufferCreateInfo vBufferInfo;

			// Staging buffer
			vBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
			VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &staging.vBuffer.buffer));
			vkGetBufferMemoryRequirements(device, staging.vBuffer.buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.vBuffer.memory));
			VK_CHECK_RESULT(vkMapMemory(device, staging.vBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
			memcpy(data, vertices.data(), vertexDataSize);
			vkUnmapMemory(device, staging.vBuffer.memory);
			VK_CHECK_RESULT(vkBindBufferMemory(device, staging.vBuffer.buffer, staging.vBuffer.memory, 0));

			// Target
			vBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexDataSize);
			VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &meshes[i].vertexBuffer));
			vkGetBufferMemoryRequirements(device, meshes[i].vertexBuffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &meshes[i].vertexMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(device, meshes[i].vertexBuffer, meshes[i].vertexMemory, 0));

			// Generate index buffer
			VkBufferCreateInfo iBufferInfo;

			// Staging buffer
			iBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexDataSize);
			VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &staging.iBuffer.buffer));
			vkGetBufferMemoryRequirements(device, staging.iBuffer.buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.iBuffer.memory));
			VK_CHECK_RESULT(vkMapMemory(device, staging.iBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
			memcpy(data, indices.data(), indexDataSize);
			vkUnmapMemory(device, staging.iBuffer.memory);
			VK_CHECK_RESULT(vkBindBufferMemory(device, staging.iBuffer.buffer, staging.iBuffer.memory, 0));

			// Target
			iBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexDataSize);
			VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &meshes[i].indexBuffer));
			vkGetBufferMemoryRequirements(device, meshes[i].indexBuffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &meshes[i].indexMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(device, meshes[i].indexBuffer, meshes[i].indexMemory, 0));

			// Copy
			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

			VkBufferCopy copyRegion = {};

			copyRegion.size = vertexDataSize;
			vkCmdCopyBuffer(
				copyCmd,
				staging.vBuffer.buffer,
				meshes[i].vertexBuffer,
				1,
				&copyRegion);

			copyRegion.size = indexDataSize;
			vkCmdCopyBuffer(
				copyCmd,
				staging.iBuffer.buffer,
				meshes[i].indexBuffer,
				1,
				&copyRegion);

			VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));
			
			VkSubmitInfo submitInfo = {};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &copyCmd;

			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
			VK_CHECK_RESULT(vkQueueWaitIdle(queue));

			vkDestroyBuffer(device, staging.vBuffer.buffer, nullptr);
			vkFreeMemory(device, staging.vBuffer.memory, nullptr);
			vkDestroyBuffer(device, staging.iBuffer.buffer, nullptr);
			vkFreeMemory(device, staging.iBuffer.memory, nullptr);
		}

		/* test: global buffers */

		size_t vertexDataSize = gVertices.size() * sizeof(Vertex);
		size_t indexDataSize = gIndices.size() * sizeof(uint32_t);

		VkMemoryAllocateInfo memAlloc = vkTools::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkResult err;
		void *data;

		struct
		{
			struct {
				VkDeviceMemory memory;
				VkBuffer buffer;
			} vBuffer;
			struct {
				VkDeviceMemory memory;
				VkBuffer buffer;
			} iBuffer;
		} staging;

		// Generate vertex buffer
		VkBufferCreateInfo vBufferInfo;

		// Staging buffer
		vBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, vertexDataSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &staging.vBuffer.buffer));
		vkGetBufferMemoryRequirements(device, staging.vBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.vBuffer.memory));
		VK_CHECK_RESULT(vkMapMemory(device, staging.vBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
		memcpy(data, gVertices.data(), vertexDataSize);
		vkUnmapMemory(device, staging.vBuffer.memory);
		VK_CHECK_RESULT(vkBindBufferMemory(device, staging.vBuffer.buffer, staging.vBuffer.memory, 0));

		// Target
		vBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, vertexDataSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &vBufferInfo, nullptr, &vertexBuffer.buffer));
		vkGetBufferMemoryRequirements(device, vertexBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &vertexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, vertexBuffer.buffer, vertexBuffer.memory, 0));

		// Generate index buffer
		VkBufferCreateInfo iBufferInfo;

		// Staging buffer
		iBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, indexDataSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &staging.iBuffer.buffer));
		vkGetBufferMemoryRequirements(device, staging.iBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &staging.iBuffer.memory));
		VK_CHECK_RESULT(vkMapMemory(device, staging.iBuffer.memory, 0, VK_WHOLE_SIZE, 0, &data));
		memcpy(data, gIndices.data(), indexDataSize);
		vkUnmapMemory(device, staging.iBuffer.memory);
		VK_CHECK_RESULT(vkBindBufferMemory(device, staging.iBuffer.buffer, staging.iBuffer.memory, 0));

		// Target
		iBufferInfo = vkTools::initializers::bufferCreateInfo(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, indexDataSize);
		VK_CHECK_RESULT(vkCreateBuffer(device, &iBufferInfo, nullptr, &indexBuffer.buffer));
		vkGetBufferMemoryRequirements(device, indexBuffer.buffer, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &indexBuffer.memory));
		VK_CHECK_RESULT(vkBindBufferMemory(device, indexBuffer.buffer, indexBuffer.memory, 0));

		// Copy
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(copyCmd, &cmdBufInfo));

		VkBufferCopy copyRegion = {};

		copyRegion.size = vertexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			staging.vBuffer.buffer,
			vertexBuffer.buffer,
			1,
			&copyRegion);

		copyRegion.size = indexDataSize;
		vkCmdCopyBuffer(
			copyCmd,
			staging.iBuffer.buffer,
			indexBuffer.buffer,
			1,
			&copyRegion);

		VK_CHECK_RESULT(vkEndCommandBuffer(copyCmd));

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &copyCmd;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));

		vkDestroyBuffer(device, staging.vBuffer.buffer, nullptr);
		vkFreeMemory(device, staging.vBuffer.memory, nullptr);
		vkDestroyBuffer(device, staging.iBuffer.buffer, nullptr);
		vkFreeMemory(device, staging.iBuffer.memory, nullptr);

		// Generate descriptor sets for all meshes
		// todo : think about a nicer solution, better suited per material?

		// Decriptor pool
		std::vector<VkDescriptorPoolSize> poolSizes;
		poolSizes.push_back(vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, meshes.size()));
		poolSizes.push_back(vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, meshes.size() * 4));

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				meshes.size());

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Shared descriptor set layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		// Binding 0: UBO
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			VK_SHADER_STAGE_VERTEX_BIT,
			0));
		// Binding 1: Diffuse map
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			1));
		// Binding 2: Roughness map
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			2));
		// Binding 3: Bump map
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			3));
		// Binding 4: Metallic map
		setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(
			VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			4));

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				setLayoutBindings.size());

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Descriptor sets
		for (uint32_t i = 0; i < meshes.size(); i++)
		{
			// Descriptor set
			VkDescriptorSetAllocateInfo allocInfo =
				vkTools::initializers::descriptorSetAllocateInfo(
					descriptorPool,
					&descriptorSetLayout,
					1);

			// Background
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &meshes[i].descriptorSet));

			std::vector<VkWriteDescriptorSet> writeDescriptorSets;

			// Binding 0 : Vertex shader uniform buffer
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(
				meshes[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&defaultUBO->descriptor));
			// Image bindings
			// Binding 0: Color map
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(
				meshes[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				1,
				&meshes[i].material->diffuse.descriptor));
			// Binding 1: Roughness
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(
				meshes[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&meshes[i].material->roughness.descriptor));
			// Binding 2: Normal
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(
				meshes[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				3,
				&meshes[i].material->bump.descriptor));
			// Binding 3: Metallic
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(
				meshes[i].descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				4,
				&meshes[i].material->metallic.descriptor));

			vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
		}
	}

public:
#if defined(__ANDROID__)
	AAssetManager* assetManager = nullptr;
#endif

	std::string assetPath = "";

	std::vector<SceneMaterial> materials;
	std::vector<SceneMesh> meshes;

	vk::Buffer vertexBuffer;
	vk::Buffer indexBuffer;

	// Same for all meshes in the scene
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	Scene(VkDevice device, VkQueue queue, vkTools::VulkanTextureLoader *textureloader, vk::Buffer *defaultUBO)
	{
		this->device = device;
		this->queue = queue;
		this->textureLoader = textureloader;
		this->defaultUBO = defaultUBO;
	}

	~Scene()
	{
		for (auto mesh : meshes)
		{
			vkDestroyBuffer(device, mesh.vertexBuffer, nullptr);
			vkFreeMemory(device, mesh.vertexMemory, nullptr);
			vkDestroyBuffer(device, mesh.indexBuffer, nullptr);
			vkFreeMemory(device, mesh.indexMemory, nullptr);
		}
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}

	void load(std::string filename, VkCommandBuffer copyCmd)
	{
		Assimp::Importer Importer;

        int flags = aiProcess_FlipWindingOrder | aiProcess_Triangulate | aiProcess_PreTransformVertices | aiProcess_CalcTangentSpace | aiProcess_GenSmoothNormals;

#if defined(__ANDROID__)
		AAsset* asset = AAssetManager_open(assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		assert(asset);
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		void *meshData = malloc(size);
		AAsset_read(asset, meshData, size);
		AAsset_close(asset);
		aScene = Importer.ReadFileFromMemory(meshData, size, flags);
		free(meshData);
#else
		aScene = Importer.ReadFile(filename.c_str(), flags);
#endif
		if (aScene)
		{
			loadMaterials();
			loadMeshes(copyCmd);
		}
		else
		{
			printf("Error parsing '%s': '%s'\n", filename.c_str(), Importer.GetErrorString());
#if defined(__ANDROID__)
			LOGE("Error parsing '%s': '%s'", filename.c_str(), Importer.GetErrorString());
#endif
		}
	}
};

#define NUM_LIGHTS 3

class VulkanExample : public VulkanExampleBase
{
public:
	Scene *scene;

	bool debugDisplay = false;
	bool attachLight = false;

	// Vendor specific
	bool enableNVDedicatedAllocation = false;
	bool enableAMDRasterizationOrder = false;

	struct {
		vkMeshLoader::MeshBuffer quad;
		vkMeshLoader::MeshBuffer skysphere;
	} meshes;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec2 viewportDim;
	} uboVS, uboSceneMatrices;

	struct {
		glm::mat4 depthMVP[NUM_LIGHTS];
	} uboShadowmapVS;

	struct Light {
		glm::vec4 position;
		glm::vec4 dir;
		glm::vec4 color;
		glm::vec4 lightParams; // x - light type, y - radius for point lights, cone sector for spot lights
		glm::mat4 lightSpace;
	};

	struct {
		Light lights[NUM_LIGHTS];
		glm::vec4 viewPos;
		glm::mat4 view;
		glm::mat4 model;
	} uboFragmentLights;

	struct {
		vk::Buffer shadowmap;
		vk::Buffer fullScreen;
		vk::Buffer sceneMatrices;
		vk::Buffer sceneLights;
	} uniformBuffers;

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;
		void destroy(VkDevice device)
		{
			vkDestroyImage(device, image, nullptr);
			vkDestroyImageView(device, view, nullptr);
			vkFreeMemory(device, mem, nullptr);
		}
	};
	struct FrameBuffer {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
		void setSize(int32_t w, int32_t h)
		{
			this->width = w;
			this->height = h;
		}
		void destroy(VkDevice device)
		{
			vkDestroyFramebuffer(device, frameBuffer, nullptr);
			vkDestroyRenderPass(device, renderPass, nullptr);
		}
	};
	
	struct ShadowmapPass {
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
		VkSampler depthSampler;
		VkDescriptorImageInfo descriptor;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		// Semaphore used to synchronize between offscreen and final scene render pass
		VkSemaphore semaphore = VK_NULL_HANDLE;
	} shadowmapPass[NUM_LIGHTS];

	struct {
		struct Offscreen : public FrameBuffer {
			std::array<FrameBufferAttachment, 3> attachments;
		} offscreen;
	} frameBuffers;
	
	// One sampler for the frame buffer color attachments
	VkSampler colorSampler;

	VkCommandBuffer deferredCmdBuffer = VK_NULL_HANDLE;

	// Semaphore used to synchronize between offscreen and final scene rendering
	VkSemaphore deferredSemaphore = VK_NULL_HANDLE;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
#if !defined(__ANDROID__)
		width = 1920;
		height = 1080;
#endif
		enableTextOverlay = true;
		title = "Vulkan Sponza - (c) 2016 by Sascha Willems";

		camera.type = Camera::CameraType::firstperson;
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 512.0f);
		camera.setRotation(glm::vec3(6.0f, -90.0f, 0.0f));
		camera.setTranslation(glm::vec3(-125.0f, 6.25f, 0.0f));
		camera.movementSpeed = 20.0f * 2.0f;

		timerSpeed = 0.075f;
		rotationSpeed = 0.15f;
#if defined(_WIN32)
		setupConsole("VulkanExample");
#endif
		srand(time(NULL));

		enableNVDedicatedAllocation = vulkanDevice->extensionSupported(VK_NV_DEDICATED_ALLOCATION_EXTENSION_NAME);
		enableAMDRasterizationOrder = vulkanDevice->extensionSupported(VK_AMD_RASTERIZATION_ORDER_EXTENSION_NAME);
	}

	~VulkanExample()
	{
		delete resources.pipelineLayouts;
		delete resources.pipelines;
		delete resources.descriptorSetLayouts;
		delete resources.descriptorSets;
		delete resources.textures;

		vkDestroySampler(device, colorSampler, nullptr);

		// Frame buffer attachments
		for (auto attachment : frameBuffers.offscreen.attachments)
		{
			vkDestroyImageView(device, attachment.view, nullptr);
			vkDestroyImage(device, attachment.image, nullptr);
			vkFreeMemory(device, attachment.mem, nullptr);
		}

		// Depth attachment
		vkDestroyImageView(device, frameBuffers.offscreen.depth.view, nullptr);
		vkDestroyImage(device, frameBuffers.offscreen.depth.image, nullptr);
		vkFreeMemory(device, frameBuffers.offscreen.depth.mem, nullptr);

		vkDestroyFramebuffer(device, frameBuffers.offscreen.frameBuffer, nullptr);

		// Meshes
		vkMeshLoader::freeMeshBufferResources(device, &meshes.quad);
		vkMeshLoader::freeMeshBufferResources(device, &meshes.skysphere);

		// Uniform buffers
		uniformBuffers.fullScreen.destroy();
		uniformBuffers.sceneMatrices.destroy();
		uniformBuffers.sceneLights.destroy();

		vkFreeCommandBuffers(device, cmdPool, 1, &deferredCmdBuffer);

		vkDestroyRenderPass(device, frameBuffers.offscreen.renderPass, nullptr);

		vkDestroySemaphore(device, deferredSemaphore, nullptr);

		delete(scene);
	}

	// Depth bias (and slope) are used to avoid shadowing artefacts
	// Constant depth bias factor (always applied)
	float depthBiasConstant = 1.25f;
	// Slope depth bias factor, applied depending on polygon's slope
	float depthBiasSlope = 1.75f;
	
	#define SHADOWMAP_DIM 2048

	// Set up a separate render pass for the offscreen frame buffer
	// This is necessary as the offscreen frame buffer attachments use formats different to those from the example render pass
	void prepareShadowmapRenderpass()
	{
		VkAttachmentDescription attachmentDescription{};
		attachmentDescription.format = VK_FORMAT_D16_UNORM;
		attachmentDescription.samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescription.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;							// Clear depth at beginning of the render pass
		attachmentDescription.storeOp = VK_ATTACHMENT_STORE_OP_STORE;						// We will read from depth, so it's important to store the depth attachment results
		attachmentDescription.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescription.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescription.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;					// We don't care about initial layout of the attachment
		attachmentDescription.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;// Attachment will be transitioned to shader read at render pass end

		VkAttachmentReference depthReference = {};
		depthReference.attachment = 0;
		depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;			// Attachment will be used as depth/stencil during render pass

		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 0;													// No color attachments
		subpass.pDepthStencilAttachment = &depthReference;									// Reference to our depth attachment
																							// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo = vkTools::initializers::renderPassCreateInfo();
		renderPassCreateInfo.attachmentCount = 1;
		renderPassCreateInfo.pAttachments = &attachmentDescription;
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpass;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();

		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &shadowmapPass[i].renderPass));
		}
	}
	// Setup the offscreen framebuffer for rendering the scene from light's point-of-view to
	// The depth attachment of this framebuffer will then be used to sample from in the fragment shader of the shadowing pass
	void prepareShadowmapFramebuffer()
	{
		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			shadowmapPass[i].width = SHADOWMAP_DIM;
			shadowmapPass[i].height = SHADOWMAP_DIM;

			// For shadow mapping we only need a depth attachment
			VkImageCreateInfo image = vkTools::initializers::imageCreateInfo();
			image.imageType = VK_IMAGE_TYPE_2D;
			image.extent.width = shadowmapPass[i].width;
			image.extent.height = shadowmapPass[i].height;
			image.extent.depth = 1;
			image.mipLevels = 1;
			image.arrayLayers = 1;
			image.samples = VK_SAMPLE_COUNT_1_BIT;
			image.tiling = VK_IMAGE_TILING_OPTIMAL;
			image.format = VK_FORMAT_D16_UNORM;																// Depth stencil attachment
			image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;		// We will sample directly from the depth attachment for the shadow mapping
			VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &shadowmapPass[i].depth.image));

			VkMemoryAllocateInfo memAlloc = vkTools::initializers::memoryAllocateInfo();
			VkMemoryRequirements memReqs;
			vkGetImageMemoryRequirements(device, shadowmapPass[i].depth.image, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &shadowmapPass[i].depth.mem));
			VK_CHECK_RESULT(vkBindImageMemory(device, shadowmapPass[i].depth.image, shadowmapPass[i].depth.mem, 0));

			VkImageViewCreateInfo depthStencilView = vkTools::initializers::imageViewCreateInfo();
			depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			depthStencilView.format = VK_FORMAT_D16_UNORM;
			depthStencilView.subresourceRange = {};
			depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			depthStencilView.subresourceRange.baseMipLevel = 0;
			depthStencilView.subresourceRange.levelCount = 1;
			depthStencilView.subresourceRange.baseArrayLayer = 0;
			depthStencilView.subresourceRange.layerCount = 1;
			depthStencilView.image = shadowmapPass[i].depth.image;
			VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &shadowmapPass[i].depth.view));

			// Create sampler to sample from to depth attachment 
			// Used to sample in the fragment shader for shadowed rendering
			VkSamplerCreateInfo sampler = vkTools::initializers::samplerCreateInfo();
			sampler.magFilter = VK_FILTER_LINEAR;
			sampler.minFilter = VK_FILTER_LINEAR;
			sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			sampler.addressModeV = sampler.addressModeU;
			sampler.addressModeW = sampler.addressModeU;
			sampler.mipLodBias = 0.0f;
			sampler.maxAnisotropy = 1.0f;
			sampler.minLod = 0.0f;
			sampler.maxLod = 1.0f;
			sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &shadowmapPass[i].depthSampler));

			prepareShadowmapRenderpass();

			// Create frame buffer
			VkFramebufferCreateInfo fbufCreateInfo = vkTools::initializers::framebufferCreateInfo();
			fbufCreateInfo.renderPass = shadowmapPass[i].renderPass;
			fbufCreateInfo.attachmentCount = 1;
			fbufCreateInfo.pAttachments = &shadowmapPass[i].depth.view;
			fbufCreateInfo.width = shadowmapPass[i].width;
			fbufCreateInfo.height = shadowmapPass[i].height;
			fbufCreateInfo.layers = 1;

			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &shadowmapPass[i].frameBuffer));
		}
	}

	void buildShadowmapCommandBuffer()
	{
		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			if (shadowmapPass[i].commandBuffer == VK_NULL_HANDLE)
			{
				shadowmapPass[i].commandBuffer = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
			}
			if (shadowmapPass[i].semaphore == VK_NULL_HANDLE)
			{
				// Create a semaphore used to synchronize offscreen rendering and usage
				VkSemaphoreCreateInfo semaphoreCreateInfo = vkTools::initializers::semaphoreCreateInfo();
				VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &shadowmapPass[i].semaphore));
			}

			VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

			VkClearValue clearValues[1];
			clearValues[0].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = shadowmapPass[i].renderPass;
			renderPassBeginInfo.framebuffer = shadowmapPass[i].frameBuffer;
			renderPassBeginInfo.renderArea.offset.x = 0;
			renderPassBeginInfo.renderArea.offset.y = 0;
			renderPassBeginInfo.renderArea.extent.width = shadowmapPass[i].width;
			renderPassBeginInfo.renderArea.extent.height = shadowmapPass[i].height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			VK_CHECK_RESULT(vkBeginCommandBuffer(shadowmapPass[i].commandBuffer, &cmdBufInfo));

			VkViewport viewport = vkTools::initializers::viewport((float)shadowmapPass[i].width, (float)shadowmapPass[i].height, 0.0f, 1.0f);
			vkCmdSetViewport(shadowmapPass[i].commandBuffer, 0, 1, &viewport);

			VkRect2D scissor = vkTools::initializers::rect2D(shadowmapPass[i].width, shadowmapPass[i].height, 0, 0);
			vkCmdSetScissor(shadowmapPass[i].commandBuffer, 0, 1, &scissor);

			// Set depth bias (aka "Polygon offset")
			// Required to avoid shadow mapping artefacts
			vkCmdSetDepthBias(
				shadowmapPass[i].commandBuffer,
				depthBiasConstant,
				0.0f,
				depthBiasSlope);

			vkCmdBeginRenderPass(shadowmapPass[i].commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkPipelineLayout pipelineLayout = resources.pipelineLayouts->get("shadowmap");
			vkCmdBindPipeline(shadowmapPass[i].commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("shadowmap"));
			vkCmdBindDescriptorSets(shadowmapPass[i].commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, resources.descriptorSets->getPtr("shadowmap"), 0, NULL);
			
			vkCmdPushConstants( shadowmapPass[i].commandBuffer,	pipelineLayout,	VK_SHADER_STAGE_VERTEX_BIT,	0,	sizeof(int), &i);

			VkDeviceSize offsets[1] = { 0 };

			// Render from global buffer using index offsets
			vkCmdBindVertexBuffers(shadowmapPass[i].commandBuffer, VERTEX_BUFFER_BIND_ID, 1, &scene->vertexBuffer.buffer, offsets);
			vkCmdBindIndexBuffer(shadowmapPass[i].commandBuffer, scene->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

			for (auto mesh : scene->meshes)
			{
				if (mesh.material->hasAlpha)
				{
					continue;
				}
				//vkCmdBindDescriptorSets(shadowmapPass.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 0, NULL);
				vkCmdDrawIndexed(shadowmapPass[i].commandBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
			}

			vkCmdEndRenderPass(shadowmapPass[i].commandBuffer);

			VK_CHECK_RESULT(vkEndCommandBuffer(shadowmapPass[i].commandBuffer));
		}
	}

	void loadAssets()
	{
		resources.textures->addTexture2D("skysphere", getAssetPath() + "textures/skysphere_night.ktx", VK_FORMAT_R8G8B8A8_UNORM);
		loadMesh(getAssetPath() + "skysphere.dae", &meshes.skysphere, vertexLayout, 1.0f);
	}

	// Create a frame buffer attachment
	void createAttachment(
		VkFormat format,
		VkImageUsageFlagBits usage,
		FrameBufferAttachment *attachment,
		uint32_t width,
		uint32_t height)
	{
		VkImageAspectFlags aspectMask = 0;

		attachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		}
		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo image = vkTools::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent.width = width;
		image.extent.height = height;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;

		if (enableNVDedicatedAllocation)
		{
			VkDedicatedAllocationImageCreateInfoNV dedicatedImageInfo { VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_IMAGE_CREATE_INFO_NV };
			dedicatedImageInfo.dedicatedAllocation = VK_TRUE;
			image.pNext = &dedicatedImageInfo;
		}
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &attachment->image));

		VkMemoryAllocateInfo memAlloc = vkTools::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, attachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = getMemTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		if (enableNVDedicatedAllocation)
		{
			VkDedicatedAllocationMemoryAllocateInfoNV dedicatedAllocationInfo { VK_STRUCTURE_TYPE_DEDICATED_ALLOCATION_MEMORY_ALLOCATE_INFO_NV };
			dedicatedAllocationInfo.image = attachment->image;
			memAlloc.pNext = &dedicatedAllocationInfo;
		}

		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &attachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, attachment->image, attachment->mem, 0));

		VkImageViewCreateInfo imageView = vkTools::initializers::imageViewCreateInfo();
		imageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageView.format = format;
		imageView.subresourceRange = {};
		imageView.subresourceRange.aspectMask = aspectMask;
		imageView.subresourceRange.baseMipLevel = 0;
		imageView.subresourceRange.levelCount = 1;
		imageView.subresourceRange.baseArrayLayer = 0;
		imageView.subresourceRange.layerCount = 1;
		imageView.image = attachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageView, nullptr, &attachment->view));
	}

	// Prepare a new framebuffer for offscreen rendering
	// The contents of this framebuffer are then
	// blitted to our render target
	void prepareOffscreenFramebuffers()
	{
		VkCommandBuffer layoutCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
	
		frameBuffers.offscreen.setSize(width, height);

		// Color attachments
		// Attachment 0: World space positions
		createAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.attachments[0], width, height);

		// Attachment 1: World space normal
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.attachments[1], width, height);

		// Attachment 1: Packed colors, specular
		createAttachment(VK_FORMAT_R32G32B32A32_UINT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &frameBuffers.offscreen.attachments[2], width, height);

		// Depth attachment

		// Find a suitable depth format
		VkFormat attDepthFormat;
		VkBool32 validDepthFormat = vkTools::getSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormat);

		createAttachment(attDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &frameBuffers.offscreen.depth, width, height);

		VulkanExampleBase::flushCommandBuffer(layoutCmd, queue, true);

		// G-Buffer creation
		{
			std::array<VkAttachmentDescription, 4> attachmentDescs = {};

			// Init attachment properties
			for (uint32_t i = 0; i < static_cast<uint32_t>(attachmentDescs.size()); i++)
			{
				attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
				attachmentDescs[i].finalLayout = (i == 3) ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			}

			// Formats
			attachmentDescs[0].format = frameBuffers.offscreen.attachments[0].format;
			attachmentDescs[1].format = frameBuffers.offscreen.attachments[1].format;
			attachmentDescs[2].format = frameBuffers.offscreen.attachments[2].format;
			attachmentDescs[3].format = frameBuffers.offscreen.depth.format;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 3;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;

			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescs.data();
			renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;
			renderPassInfo.dependencyCount = 2;
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &frameBuffers.offscreen.renderPass));

			std::array<VkImageView, 4> attachments;
			attachments[0] = frameBuffers.offscreen.attachments[0].view;
			attachments[1] = frameBuffers.offscreen.attachments[1].view;
			attachments[2] = frameBuffers.offscreen.attachments[2].view;
			attachments[3] = frameBuffers.offscreen.depth.view;

			VkFramebufferCreateInfo fbufCreateInfo = vkTools::initializers::framebufferCreateInfo();
			fbufCreateInfo.renderPass = frameBuffers.offscreen.renderPass;
			fbufCreateInfo.pAttachments = attachments.data();
			fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			fbufCreateInfo.width = frameBuffers.offscreen.width;
			fbufCreateInfo.height = frameBuffers.offscreen.height;
			fbufCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuffers.offscreen.frameBuffer));
		}

		// Shared sampler for color attachments
		VkSamplerCreateInfo sampler = vkTools::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 0;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &colorSampler));
	}

	// Build command buffer for rendering the scene to the offscreen frame buffer 
	// and blitting it to the different texture targets
	void buildDeferredCommandBuffer(bool rebuild = false)
	{

		if ((deferredCmdBuffer == VK_NULL_HANDLE) || (rebuild))
		{
			if (rebuild)
			{
				vkFreeCommandBuffers(device, cmdPool, 1, &deferredCmdBuffer);
			}
			deferredCmdBuffer = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize offscreen rendering and usage
		VkSemaphoreCreateInfo semaphoreCreateInfo = vkTools::initializers::semaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &deferredSemaphore));

		VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

		// Clear values for all attachments written in the fragment sahder
		std::array<VkClearValue, 4> clearValues = {};
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = frameBuffers.offscreen.renderPass;
		renderPassBeginInfo.framebuffer = frameBuffers.offscreen.frameBuffer;
		renderPassBeginInfo.renderArea.extent.width = frameBuffers.offscreen.width;
		renderPassBeginInfo.renderArea.extent.height = frameBuffers.offscreen.height;
		renderPassBeginInfo.clearValueCount = clearValues.size();
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(deferredCmdBuffer, &cmdBufInfo));

		// First pass: Fill G-Buffer components (positions+depth, normals, albedo, roughness, metaliness) using MRT
		// -------------------------------------------------------------------------------------------------------

		vkCmdBeginRenderPass(deferredCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vkTools::initializers::viewport(
			(float)frameBuffers.offscreen.width,
			(float)frameBuffers.offscreen.height,
			0.0f,
			1.0f);
		vkCmdSetViewport(deferredCmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vkTools::initializers::rect2D(
			frameBuffers.offscreen.width,
			frameBuffers.offscreen.height,
			0,
			0);
		vkCmdSetScissor(deferredCmdBuffer, 0, 1, &scissor);

		VkDeviceSize offsets[1] = { 0 };

		// skysphere
		vkCmdBindPipeline(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("skysphere"));
		vkCmdBindDescriptorSets(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelineLayouts->get("skysphere"), 0, 1, resources.descriptorSets->getPtr("skysphere"), 0, NULL);
		vkCmdBindVertexBuffers(deferredCmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &meshes.skysphere.vertices.buf, offsets);
		vkCmdBindIndexBuffer(deferredCmdBuffer, meshes.skysphere.indices.buf, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(deferredCmdBuffer, meshes.skysphere.indexCount, 1, 0, 0, 0);

		vkCmdBindPipeline(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("scene.solid"));

		// Render from global buffer using index offsets
		vkCmdBindVertexBuffers(deferredCmdBuffer, VERTEX_BUFFER_BIND_ID, 1, &scene->vertexBuffer.buffer, offsets);
		vkCmdBindIndexBuffer(deferredCmdBuffer, scene->indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

		for (auto mesh : scene->meshes)
		{
			if (mesh.material->hasAlpha)
			{
				continue;
			}
			vkCmdBindDescriptorSets(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 0, NULL);
			vkCmdDrawIndexed(deferredCmdBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
		}

		vkCmdBindPipeline(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("scene.blend"));

		for (auto mesh : scene->meshes)
		{
			if (mesh.material->hasAlpha)
			{
				vkCmdBindDescriptorSets(deferredCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scene->pipelineLayout, 0, 1, &mesh.descriptorSet, 0, NULL);
				vkCmdDrawIndexed(deferredCmdBuffer, mesh.indexCount, 1, 0, mesh.indexBase, 0);
			}
		}

		vkCmdEndRenderPass(deferredCmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(deferredCmdBuffer));
	}

	void reBuildCommandBuffers()
	{
		vkDeviceWaitIdle(device);
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		VkResult err;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = VulkanExampleBase::frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::initializers::viewport(
				(float)width,
				(float)height,
				0.0f,
				1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::initializers::rect2D(
				width,
				height,
				0,
				0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelineLayouts->get("composition"), 0, 1, resources.descriptorSets->getPtr("composition"), 0, NULL);

			if (debugDisplay)
			{
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("debugdisplay"));
				vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.quad.vertices.buf, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], meshes.quad.indices.buf, 0, VK_INDEX_TYPE_UINT32);
				vkCmdDrawIndexed(drawCmdBuffers[i], meshes.quad.indexCount, 1, 0, 0, 1);
				// Move viewport to display final composition in lower right corner
				viewport.x = viewport.width * 0.5f;
				viewport.y = viewport.height * 0.5f;
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			}

			// Final composition as full screen quad
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pipelines->get("composition"));
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.quad.vertices.buf, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], meshes.quad.indices.buf, 0, VK_INDEX_TYPE_UINT32);
			vkCmdDrawIndexed(drawCmdBuffers[i], 6, 1, 0, 0, 1);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
	}

	void generateQuads()
	{
		// Setup vertices for multiple screen aligned quads
		// Used for displaying final result and debug 
		struct Vertex {
			float pos[3];
			float uv[2];
			float col[3];
			float normal[3];
			float tangent[3];
		};

		std::vector<Vertex> vertexBuffer;

		float x = 0.0f;
		float y = 0.0f;
		for (uint32_t i = 0; i < 3; i++)
		{
			// Last component of normal is used for debug display sampler index
			vertexBuffer.push_back({ { x + 1.0f, y + 1.0f, 0.0f },{ 1.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y + 1.0f, 0.0f },{ 0.0f, 1.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x,      y,      0.0f },{ 0.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, (float)i } });
			vertexBuffer.push_back({ { x + 1.0f, y,      0.0f },{ 1.0f, 0.0f },{ 1.0f, 1.0f, 1.0f },{ 0.0f, 0.0f, (float)i } });
			x += 1.0f;
			if (x > 1.0f)
			{
				x = 0.0f;
				y += 1.0f;
			}
		}

		createBuffer(
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			vertexBuffer.size() * sizeof(Vertex),
			vertexBuffer.data(),
			&meshes.quad.vertices.buf,
			&meshes.quad.vertices.mem);

		// Setup indices
		std::vector<uint32_t> indexBuffer = { 0,1,2, 2,3,0 };
		for (uint32_t i = 0; i < 3; ++i)
		{
			uint32_t indices[6] = { 0,1,2, 2,3,0 };
			for (auto index : indices)
			{
				indexBuffer.push_back(i * 4 + index);
			}
		}
		meshes.quad.indexCount = indexBuffer.size();

		createBuffer(
			VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			indexBuffer.size() * sizeof(uint32_t),
			indexBuffer.data(),
			&meshes.quad.indices.buf,
			&meshes.quad.indices.mem);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				sizeof(Vertex),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		vertices.attributeDescriptions.resize(5);
		// Location 0: Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, pos));
		// Location 1: Texture coordinates
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32_SFLOAT,
				offsetof(Vertex, uv));
		// Location 2: Color
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, color));
		// Location 3: Normal
		vertices.attributeDescriptions[3] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				3,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, normal));
		// Location 4: Tangent
		vertices.attributeDescriptions[4] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				4,
				VK_FORMAT_R32G32B32_SFLOAT,
				offsetof(Vertex, tangent));

		vertices.inputState = vkTools::initializers::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10),
			vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(
				poolSizes.size(),
				poolSizes.data(),
				6);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupLayoutsAndDescriptors()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo;
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vkTools::initializers::pipelineLayoutCreateInfo();
		VkDescriptorSetAllocateInfo descriptorAllocInfo = vkTools::initializers::descriptorSetAllocateInfo(descriptorPool, nullptr, 1);
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		std::vector<VkDescriptorImageInfo> imageDescriptors;
		VkDescriptorSet targetDS;

		// Composition
		setLayoutBindings = {
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),				// Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),		// Position texture target / Scene colormap
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),		// Normals texture target
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),		// Albedo texture target
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),				// Fragment shader uniform buffer
		};

		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			setLayoutBindings.push_back(vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 5 + i));
		}

		setLayoutCreateInfo = vkTools::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		resources.descriptorSetLayouts->add("composition", setLayoutCreateInfo);
		pipelineLayoutCreateInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("composition");
		resources.pipelineLayouts->add("composition", pipelineLayoutCreateInfo);
		descriptorAllocInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("composition");
		targetDS = resources.descriptorSets->add("composition", descriptorAllocInfo);

		imageDescriptors = {
			vkTools::initializers::descriptorImageInfo(colorSampler, frameBuffers.offscreen.attachments[0].view, VK_IMAGE_LAYOUT_GENERAL),
			vkTools::initializers::descriptorImageInfo(colorSampler, frameBuffers.offscreen.attachments[1].view, VK_IMAGE_LAYOUT_GENERAL),
			vkTools::initializers::descriptorImageInfo(colorSampler, frameBuffers.offscreen.attachments[2].view, VK_IMAGE_LAYOUT_GENERAL),
		};
		
		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			imageDescriptors.push_back(vkTools::initializers::descriptorImageInfo(shadowmapPass[i].depthSampler, shadowmapPass[i].depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL));
		}

		writeDescriptorSets = {
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.fullScreen.descriptor),		// Binding 0 : Vertex shader uniform buffer			
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imageDescriptors[0]),				// Binding 1 : Position texture target			
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2, &imageDescriptors[1]),				// Binding 2 : Normals texture target			
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3, &imageDescriptors[2]),				// Binding 3 : Albedo texture target
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4, &uniformBuffers.sceneLights.descriptor),		// Binding 5 : Fragment shader uniform buffer
		};

		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			writeDescriptorSets.push_back(vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 5 + i, &imageDescriptors[3 + i]));
		}

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Shadowmap
		setLayoutBindings = {
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)				// Vertex shader uniform buffer
		};

		setLayoutCreateInfo.pBindings = setLayoutBindings.data();
		setLayoutCreateInfo.bindingCount = setLayoutBindings.size();
		// add to descriptor set layout
		resources.descriptorSetLayouts->add("shadowmap", setLayoutCreateInfo);

		pipelineLayoutCreateInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("shadowmap");
		
		VkPushConstantRange pushConstantRange =	vkTools::initializers::pushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(int), 0);
		
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

		// add to pipeline layouts
		resources.pipelineLayouts->add("shadowmap", pipelineLayoutCreateInfo);
		descriptorAllocInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("shadowmap");
		pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

		// add and get description set
		targetDS = resources.descriptorSets->add("shadowmap", descriptorAllocInfo);
		
		writeDescriptorSets = {
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.shadowmap.descriptor)
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// G-Buffer creation (offscreen scene rendering)
		setLayoutBindings = {
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),				// Vertex shader uniform buffer
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),		// Diffuse
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),		// Roughness
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 3),		// Bump
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 4),		// Metaliness
		};
		setLayoutCreateInfo.pBindings = setLayoutBindings.data();
		setLayoutCreateInfo.bindingCount = setLayoutBindings.size();
		resources.descriptorSetLayouts->add("offscreen", setLayoutCreateInfo);
		pipelineLayoutCreateInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("offscreen");
		resources.pipelineLayouts->add("offscreen", pipelineLayoutCreateInfo);
		descriptorAllocInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("offscreen");
		targetDS = resources.descriptorSets->add("offscreen", descriptorAllocInfo);
		writeDescriptorSets = {
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.sceneMatrices.descriptor),// Binding 0 : Vertex shader uniform buffer			
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Skysphere
		setLayoutBindings = {
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vkTools::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		setLayoutCreateInfo.pBindings = setLayoutBindings.data();
		setLayoutCreateInfo.bindingCount = setLayoutBindings.size();
		resources.descriptorSetLayouts->add("skysphere", setLayoutCreateInfo);
		pipelineLayoutCreateInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("skysphere");
		resources.pipelineLayouts->add("skysphere", pipelineLayoutCreateInfo);
		descriptorAllocInfo.pSetLayouts = resources.descriptorSetLayouts->getPtr("skysphere");
		targetDS = resources.descriptorSets->add("skysphere", descriptorAllocInfo);
		VkDescriptorImageInfo imgDesc;
		imgDesc = resources.textures->get("skysphere").descriptor;
		writeDescriptorSets = {
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.sceneMatrices.descriptor),
			vkTools::initializers::writeDescriptorSet(targetDS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &imgDesc),
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		// Final composition pipeline
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo(vkTools::initializers::pipelineCreateInfo());

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.stageCount = shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

		if (enableAMDRasterizationOrder)
		{
			VkPipelineRasterizationStateRasterizationOrderAMD rasterAMD{};
			rasterAMD.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_RASTERIZATION_ORDER_AMD;
			rasterAMD.rasterizationOrder = VK_RASTERIZATION_ORDER_RELAXED_AMD;
			rasterizationState.pNext = &rasterAMD;
		}

		// Final composition pipeline
		{
			pipelineCreateInfo.layout = resources.pipelineLayouts->get("composition");
			pipelineCreateInfo.renderPass = renderPass;

			shaderStages[0] = loadShader(getAssetPath() + "shaders/composition.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			shaderStages[1] = loadShader(getAssetPath() + "shaders/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

			struct SpecializationData {
				int32_t enableSSAO = 1;
				float ambientFactor = 0.15f;
			} specializationData;

			std::vector<VkSpecializationMapEntry> specializationMapEntries;
			specializationMapEntries = {
				vkTools::initializers::specializationMapEntry(0, offsetof(SpecializationData, enableSSAO), sizeof(int32_t)),
				vkTools::initializers::specializationMapEntry(1, offsetof(SpecializationData, ambientFactor), sizeof(float)),
			};
			VkSpecializationInfo specializationInfo = vkTools::initializers::specializationInfo(specializationMapEntries.size(), specializationMapEntries.data(), sizeof(specializationData), &specializationData);
			shaderStages[1].pSpecializationInfo = &specializationInfo;

			specializationData.enableSSAO = 0;
			resources.pipelines->addGraphicsPipeline("composition", pipelineCreateInfo, pipelineCache);
		}

		// Derivate info for other pipelines
		pipelineCreateInfo.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
		pipelineCreateInfo.basePipelineIndex = -1;
		pipelineCreateInfo.basePipelineHandle = resources.pipelines->get("composition.ssao.enabled");

		// Debug display pipeline
		shaderStages[0] = loadShader(getAssetPath() + "shaders/debug.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/debug.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		resources.pipelines->addGraphicsPipeline("debugdisplay", pipelineCreateInfo, pipelineCache);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

		// Fill G-Buffer

		// Set constant parameters via specialization constants
		struct SpecializationData {
			float znear;
			float zfar;
			int32_t discard = 0;
		} specializationData;

		specializationData.znear = camera.znear;
		specializationData.zfar = camera.zfar;

		std::vector<VkSpecializationMapEntry> specializationMapEntries;
		specializationMapEntries = {
			vkTools::initializers::specializationMapEntry(0, offsetof(SpecializationData, znear), sizeof(float)),
			vkTools::initializers::specializationMapEntry(1, offsetof(SpecializationData, zfar), sizeof(float)),
			vkTools::initializers::specializationMapEntry(2, offsetof(SpecializationData, discard), sizeof(int32_t)),

		};
		VkSpecializationInfo specializationInfo = vkTools::initializers::specializationInfo(specializationMapEntries.size(), specializationMapEntries.data(), sizeof(specializationData), &specializationData);

		shaderStages[0] = loadShader(getAssetPath() + "shaders/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		pipelineCreateInfo.renderPass = frameBuffers.offscreen.renderPass;
		pipelineCreateInfo.layout = resources.pipelineLayouts->get("offscreen");

		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates = {
			vkTools::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vkTools::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vkTools::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE)
		};

		colorBlendState.attachmentCount = blendAttachmentStates.size();
		colorBlendState.pAttachments = blendAttachmentStates.data();
		resources.pipelines->addGraphicsPipeline("scene.solid", pipelineCreateInfo, pipelineCache);

		// Transparent objects (discard by alpha)
		depthStencilState.depthWriteEnable = VK_FALSE;
		rasterizationState.cullMode = VK_CULL_MODE_NONE;
		specializationData.discard = 1;
		resources.pipelines->addGraphicsPipeline("scene.blend", pipelineCreateInfo, pipelineCache);

		// Skysphere
		shaderStages[0] = loadShader(getAssetPath() + "shaders/skysphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/skysphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCreateInfo.layout = resources.pipelineLayouts->get("skysphere");
		resources.pipelines->addGraphicsPipeline("skysphere", pipelineCreateInfo, pipelineCache);

		// Shadowmap pipeline
		depthStencilState.depthWriteEnable = VK_TRUE;
		rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
		specializationData.discard = 0;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/offscreen.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/offscreen.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// No blend attachment states (no color attachments used)
		colorBlendState.attachmentCount = 0;
		// Cull front faces
		depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		// Enable depth bias
		rasterizationState.depthBiasEnable = VK_TRUE;
		// Add depth bias to dynamic state, so we can change it at runtime
		dynamicStateEnables.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
		dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				dynamicStateEnables.size(),
				0);

		pipelineCreateInfo.layout = resources.pipelineLayouts->get("shadowmap");
		pipelineCreateInfo.renderPass = shadowmapPass[0].renderPass;

		resources.pipelines->addGraphicsPipeline("shadowmap", pipelineCreateInfo, pipelineCache);
	}

	inline float lerp(float a, float b, float f)
	{
		return a + f * (b - a);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Shadowmap
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.shadowmap,
			sizeof(uboShadowmapVS));

		// Fullscreen vertex shader
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.fullScreen,
			sizeof(uboVS));

		// Deferred vertex shader
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.sceneMatrices,
			sizeof(uboSceneMatrices));

		// Deferred fragment shader
		vulkanDevice->createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.sceneLights,
			sizeof(uboFragmentLights));

		setupLights();

		// Update
		updateUniformBufferShadowmap();
		updateUniformBuffersScreen();
		updateUniformBufferDeferredMatrices();
		updateUniformBufferDeferredLights();
	}

	void updateUniformBuffersScreen()
	{
		if (debugDisplay)
		{
			uboVS.projection = glm::ortho(0.0f, 2.0f, 0.0f, 2.0f, -1.0f, 1.0f);
		}
		else
		{
			uboVS.projection = glm::ortho(0.0f, 1.0f, 0.0f, 1.0f, -1.0f, 1.0f);
		}
		uboVS.model = glm::mat4();

		VK_CHECK_RESULT(uniformBuffers.fullScreen.map());
		uniformBuffers.fullScreen.copyTo(&uboVS, sizeof(uboVS));
		uniformBuffers.fullScreen.unmap();
	}

	void updateUniformBufferDeferredMatrices()
	{
		uboSceneMatrices.projection = camera.matrices.perspective;
		uboSceneMatrices.view = camera.matrices.view;
		uboSceneMatrices.model = glm::mat4();
		uboSceneMatrices.viewportDim = glm::vec2(width, height);

		uint8_t *pData;

		VK_CHECK_RESULT(uniformBuffers.sceneMatrices.map());
		uniformBuffers.sceneMatrices.copyTo(&uboSceneMatrices, sizeof(uboSceneMatrices));
		uniformBuffers.sceneMatrices.unmap();
	}

	float rnd(float range)
	{
		return range * (rand() / double(RAND_MAX));
	}

	void setupPointLight(Light *light, glm::vec3 pos, glm::vec3 color, float radius)
	{
		light->position = glm::vec4(pos, 1.0f);
		light->dir = glm::vec4(pos, 1.0f);
		light->color = glm::vec4(color, 1.0f);
		light->lightParams.x = 0.f;
		light->lightParams.y = radius;
	}

	float zNear = 1.0f;
	float zFar = 200.0f;
	float lightFOV = 45.0f;

	void setupSpotLight(Light *light, glm::vec3 pos, glm::vec3 dir, float coneAngle, glm::vec3 color)
	{
		light->position = glm::vec4(pos, 1.0f);
		light->color = glm::vec4(color, 1.0f);
		light->dir = glm::vec4(dir, 1.f);
		light->lightParams.x = 1.f;
		light->lightParams.y = 1600.0f;

		glm::mat4 depthProjectionMatrix = glm::perspective(coneAngle, 1.0f, zNear, zFar);
		glm::mat4 depthViewMatrix = glm::lookAt(pos, pos + dir, glm::vec3(0, 1, 0));

		light->lightSpace = depthProjectionMatrix * depthViewMatrix;
	}

	// Initial light setup for the scene
	void setupLights()
	{	
		glm::vec3 center = glm::vec3(0.f, 0.0f, -15.f);
		glm::vec3 pos[3] = {glm::vec3( 0.f, -15.0f, -0.f), center + glm::vec3(30.f, -30.0f, 15.0f), center + glm::vec3(0.f, -30.0f, 30.f) };

		setupSpotLight(&uboFragmentLights.lights[0], pos[0], { 1, 0, 0}, glm::radians(lightFOV), glm::vec3(1.0f, 1.f, 1.f));
		setupSpotLight(&uboFragmentLights.lights[1], pos[0], { -1, 0, 0 }, glm::radians(lightFOV), glm::vec3(1.0f, 1.f, 0.f));
		setupSpotLight(&uboFragmentLights.lights[2], pos[1], { 0, 0, 1 }, glm::radians(lightFOV), glm::vec3(1.f, 1.0f, 1.f));
	}

	// Update fragment shader light positions for moving light sources
	void updateUniformBufferDeferredLights()
	{
		// Dynamic light
		if (attachLight)
		{
			// Attach to camera position
			uboFragmentLights.lights[0].position = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f, -1.0f, -1.0f, 1.0f);
		}
		else
		{
			// Move across the floor
			//uboFragmentLights.lights[0].position.x = -sin(glm::radians(360.0f * timer)) * 120.0f;
			//uboFragmentLights.lights[0].position.z = cos(glm::radians(360.0f * timer * 8.0f)) * 10.0f;
		}

		uboFragmentLights.viewPos = glm::vec4(camera.position, 0.0f) * glm::vec4(-1.0f);
		uboFragmentLights.view = camera.matrices.view;
		uboFragmentLights.model = glm::mat4();

		VK_CHECK_RESULT(uniformBuffers.sceneLights.map());
		uniformBuffers.sceneLights.copyTo(&uboFragmentLights, sizeof(uboFragmentLights));
		uniformBuffers.sceneLights.unmap();
	}

	void updateUniformBufferShadowmap()
	{
		for (int i = 0; i < NUM_LIGHTS; i++)
		{
			uboShadowmapVS.depthMVP[i] = uboFragmentLights.lights[i].lightSpace;
		}

		VK_CHECK_RESULT(uniformBuffers.shadowmap.map());
		uniformBuffers.shadowmap.copyTo(&uboShadowmapVS, sizeof(uboShadowmapVS));
		uniformBuffers.shadowmap.unmap();
	}

	void loadScene()
	{
		VkCommandBuffer copyCmd = VulkanExampleBase::createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		scene = new Scene(device, queue, textureLoader, &uniformBuffers.sceneMatrices);

#if defined(__ANDROID__)
		scene->assetManager = androidApp->activity->assetManager;
#endif
		scene->assetPath = getAssetPath();

        scene->load(getAssetPath() + "sponza_pbr.obj", copyCmd);
		vkFreeCommandBuffers(device, cmdPool, 1, &copyCmd);
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Signal ready for shadow semaphore

		for (int i = 0; i < NUM_LIGHTS; i++)
		{	
			submitInfo.pWaitSemaphores = i == 0 ? &semaphores.presentComplete : &shadowmapPass[i - 1].semaphore;

			submitInfo.pSignalSemaphores = &shadowmapPass[i].semaphore;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &shadowmapPass[i].commandBuffer;
			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		}

		// Signal ready with deferred semaphore
		submitInfo.pSignalSemaphores = &deferredSemaphore;
		submitInfo.pWaitSemaphores = &shadowmapPass[NUM_LIGHTS - 1].semaphore;
		
		// Submit work
		submitInfo.pCommandBuffers = &deferredCmdBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		// Scene rendering
		// Wait for deferred semaphore
		submitInfo.pWaitSemaphores = &deferredSemaphore;
		// Signal ready with render complete semaphpre
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;
		// Submit work
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();

		setupDescriptorPool();

		resources.pipelineLayouts = new PipelineLayoutList(vulkanDevice->logicalDevice);
		resources.pipelines = new PipelineList(vulkanDevice->logicalDevice);
		resources.descriptorSetLayouts = new DescriptorSetLayoutList(vulkanDevice->logicalDevice);
		resources.descriptorSets = new DescriptorSetList(vulkanDevice->logicalDevice, descriptorPool);
		resources.textures = new TextureList(vulkanDevice->logicalDevice, textureLoader);

		// todo : sep func
		deviceMemProps = deviceMemoryProperties;

		generateQuads();
		loadAssets();
		setupVertexDescriptions();

		prepareShadowmapFramebuffer();
		prepareOffscreenFramebuffers();
		prepareUniformBuffers();
		setupLayoutsAndDescriptors();
		preparePipelines();
		loadScene();
		buildShadowmapCommandBuffer();
		buildCommandBuffers();
		buildDeferredCommandBuffer();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		draw();

		if (!paused)
		{
			updateUniformBufferDeferredLights();
			updateUniformBufferShadowmap();
		}
	}

	virtual void viewChanged()
	{
		updateUniformBufferDeferredMatrices();
		updateTextOverlay();
	}

	void toggleDebugDisplay()
	{
		debugDisplay = !debugDisplay;
		reBuildCommandBuffers();
		updateUniformBuffersScreen();
	}

	void toggleSSAO()
	{
		reBuildCommandBuffers();
		buildDeferredCommandBuffer(true);
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case KEY_F1:
		case GAMEPAD_BUTTON_A:
			toggleDebugDisplay();
			updateTextOverlay();
			break;
		case KEY_F2:
			toggleSSAO();
			break;
		case KEY_L:
		case GAMEPAD_BUTTON_B:
			attachLight = !attachLight;
			break;
		}
	}

	virtual void getOverlayText(VulkanTextOverlay *textOverlay)
	{
#if defined(__ANDROID__)
		textOverlay->addText("Press \"Button A\" to toggle render targets", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
#else
		//textOverlay->addText("Press \"1\" to toggle render targets", 5.0f, 85.0f, VulkanTextOverlay::alignLeft);
#endif
		// Render targets
		if (debugDisplay)
		{
			textOverlay->addText("World Position", (float)width * 0.25f, (float)height * 0.5f - 25.0f, VulkanTextOverlay::alignCenter);
			textOverlay->addText("World normals", (float)width * 0.75f, (float)height * 0.5f - 25.0f, VulkanTextOverlay::alignCenter);
			textOverlay->addText("Color", (float)width * 0.25f, (float)height - 25.0f, VulkanTextOverlay::alignCenter);
			textOverlay->addText("Final image", (float)width * 0.75f, (float)height - 25.0f, VulkanTextOverlay::alignCenter);
		}
	}
};

VULKAN_EXAMPLE_MAIN()