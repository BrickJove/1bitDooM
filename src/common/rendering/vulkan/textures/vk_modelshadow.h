#pragma once

// Jupiter3D: depth maps of the models and of the level seen from the dynamic lights (Vulkan backend, see
// gl_modelshadow.cpp for the OpenGL version). Same atlas layout: 3 x 2 tiles, one per cube face.

#include <memory>
#include <vector>
#include "zvulkan/vulkanobjects.h"
#include "vulkan/textures/vk_imagetransition.h"

class VulkanRenderDevice;

class VkModelShadow
{
public:
	static constexpr int MaxMaps = 4;
	static constexpr int TileSize = 1024;

	VkModelShadow(VulkanRenderDevice* fb);
	~VkModelShadow();

	// models
	bool Begin(int index);
	void Face(int face);
	void End();

	// level
	void SetWorld(const float* verts, unsigned nverts, const unsigned int* elems, unsigned nelems, const void* key);
	bool RenderWorld(int index, const float* lightPos, float farp, const float* faceVP);

	// the images for the post processing (a dummy when the map was never rendered)
	VkTextureImage* GetModelTexture(int index);
	VkTextureImage* GetWorldTexture(int index);

private:
	struct Map
	{
		VkTextureImage Depth;
		VkTextureImage Color;	// the render state needs a color attachment, it is never used
		std::unique_ptr<VulkanFramebuffer> WorldFramebuffer;
		bool Created = false;
	};

	void CreateDepthImage(VkTextureImage& image, int width, int height, const char* name);
	void CreateModelMap(int index);
	void CreateWorldMap(int index);
	bool CreateWorldPipeline();
	VkTextureImage* GetDummy();

	VulkanRenderDevice* fb = nullptr;

	Map Models[MaxMaps];
	Map Worlds[MaxMaps];
	VkTextureImage Dummy;
	bool ModelActive = false;

	std::unique_ptr<VulkanRenderPass> WorldRenderPass;
	std::unique_ptr<VulkanPipelineLayout> WorldPipelineLayout;
	std::unique_ptr<VulkanShader> WorldVertexShader;
	std::unique_ptr<VulkanShader> WorldFragmentShader;
	std::unique_ptr<VulkanPipeline> WorldPipeline;
	bool WorldFailed = false;

	std::unique_ptr<VulkanBuffer> MeshVertices;
	std::unique_ptr<VulkanBuffer> MeshIndices;
	unsigned MeshVertexCount = 0;
	unsigned MeshIndexCount = 0;
	const void* MeshKey = nullptr;
	unsigned MeshGeneration = 0;

	struct WorldCache
	{
		float Pos[3] = { 0, 0, 0 };
		float FarP = 0;
		unsigned Generation = 0;
		bool Valid = false;
	} Cache[MaxMaps];
};
