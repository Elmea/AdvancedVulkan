#pragma once

#if defined(_DEBUG)
#define VULKAN_ENABLE_VALIDATION
#endif

#include "volk/volk.h"

#ifdef _DEBUG
#define DEBUG_CHECK_VK(x) if (VK_SUCCESS != (x)) { std::cout << (#x) << std::endl; __debugbreak(); }
#else
#define DEBUG_CHECK_VK(x) 
#endif

//#define OPENGL_NDC

#define GLM_FORCE_RADIANS
#ifndef OPENGL_NDC
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>

#include "stb/stb_image.h"

enum PixelFormat
{
	PIXFMT_RGBA8,
	PIXFMT_SRGBA8,
	PIXFMT_RGBA16F,
	PIXFMT_RGB32F,
	PIXFMT_RGBA32F,
	PIXFMT_DUMMY_ASPECT_DEPTH,
	PIXFMT_DEPTH32F = PIXFMT_DUMMY_ASPECT_DEPTH,
	PIXFMT_MAX
};

enum ImageUsageBits
{
	IMAGE_USAGE_TEXTURE = 1 << 0,
	IMAGE_USAGE_BITMAP = 1 << 1,
	IMAGE_USAGE_RENDERTARGET = 1 << 2,
	IMAGE_USAGE_TRANSFER = 1 << 5,
	IMAGE_USAGE_RENDERPASS = 1 << 6,
	IMAGE_USAGE_STAGING = 1 << 7
};
typedef uint32_t ImageUsage;

#include <iostream>
#include <fstream>
#include <vector>
#include <array>

struct SwapchainImage
{
	VkImage image;
	VkImageView view;
};

struct RenderSurface
{
	VkDeviceMemory memory;
	VkImage image;
	VkImageView view;
	VkFormat format;

	static void CopyImage(VkCommandBuffer commandBuffer, VkImage dest, VkImage source, uint32_t imageWidth, uint32_t imageHeight, VkImageAspectFlags aspectFlag);

	bool CreateSurface(struct VulkanRenderContext& rendercontext, int width, int height, PixelFormat pixelformat, uint32_t mipLevels = 1, ImageUsage usage = IMAGE_USAGE_TEXTURE | IMAGE_USAGE_BITMAP);
	void Destroy(struct VulkanRenderContext& rendercontext);
};

struct TextureRef
{
	struct Texture* tex;
	uint32_t id;
	const char* name;
};

struct Image
{
	uint16_t components, pixelFormat;
	uint16_t width, height;
	uint8_t* pixels;

	bool Load(const char* filepath, bool sRGB = true);
	void Destroy();
};

struct Texture : RenderSurface
{
	VkSampler sampler;

	//bool GenerateMipmaps(VkCommandBuffer commandBuffer, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
	bool Load(struct VulkanRenderContext& rendercontext, const char* filepath, bool sRGB = true);
	bool Load(struct VulkanRenderContext& rendercontext, const uint8_t* pixels, int w, int h, PixelFormat pixelFormat);
	void Destroy(struct VulkanRenderContext& rendercontext);
	uint32_t CreateTexture(struct VulkanRenderContext& rendercontext, int w, int h, PixelFormat pixelFormat, uint32_t mipLevels);

	// version tres basique d'un texture manager
	static VulkanRenderContext* rendercontext;
	static uint32_t LoadTexture(const char* filepath, bool sRGB = true);
	static uint32_t LoadTexture(const uint8_t* pixels, int w, int h, PixelFormat pixelFormat);
	static std::vector<TextureRef> textureRefs;
	static std::vector<Texture> textures;
	static void SetupManager();
	static uint32_t CheckExist(const char* path);
	static void PurgeTextures();
};

struct Buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize size;
	void* data;	// != nullptr => persistent
				// optionnels
	VkDeviceSize offset;
	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags properties;

	static bool CreateBuffer(struct VulkanRenderContext& rendercontext, Buffer& bo, uint32_t size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, const void* data = nullptr, uint32_t dataSize = 0);
	static bool CreateMappedBuffer(struct VulkanRenderContext& rendercontext, Buffer& bo, uint32_t size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, const void* data = nullptr);
	static bool CreateDualBuffer(struct VulkanRenderContext& rendercontext, Buffer& vbo, Buffer& ibo, uint32_t verticesSize, const void* verticesData, uint32_t indicesSize, const void* indicesData);
	void Destroy(struct VulkanRenderContext& rendercontext);
};

struct Vertex
{
	glm::vec3 position;			//  3x4 octets = 12
	glm::vec2 texcoords;		// +2x4 octets = 32
	glm::vec3 normal;			// +3x4 octets = 24
	glm::vec4 tangent;   // optionnel, seulement si vous implementez le normal mapping
};

struct Material
{
	glm::vec3 diffuseColor;
	glm::vec3 emissiveColor;
	float roughness;	// perceptual
	float metalness;
	uint32_t diffuseTexture;
	uint32_t normalTexture;
	uint32_t roughnessTexture;
	uint32_t ambientTexture;
	uint32_t emissiveTexture;

	static Material defaultMaterial;
};


struct Mesh
{
	enum BufferType {
		VBO = 0,
		IBO = 1,
		BO_MAX
	};
	uint32_t vertexCount;
	uint32_t indexCount;
	Buffer staticBuffers[BufferType::BO_MAX];

	static bool ParseGLTF(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices, Material& material, const char* filepath);
};


enum RenderTarget {
	SWAPCHAIN = 0,
	//COLOR,
	DEPTH,
	RT_MAX
};

