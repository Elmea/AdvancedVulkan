#pragma once

#include "vk_common.h"
#include "DeviceContext.h"
#include "RenderContext.h"

struct GLFWwindow;

struct VulkanGraphicsApplication
{
	const char* name;
	VulkanDeviceContext context;
	VulkanRenderContext rendercontext;
	GLFWwindow* window;

	// rendu hors ecran
	RenderSurface colorBuffer;
	RenderSurface depthBuffer;

	VkPipeline mainPipelineOpaque;

	VkPipeline mainPipelineEnvMap;

	// on partage la meme signaure (les memes inputs) entre ces deux pipelines
	VkPipelineLayout mainPipelineLayout;

	//

	uint32_t m_imageIndex;
	uint32_t m_frame;

	bool Initialize(const char *);
	bool Prepare();
	bool Run();
	bool Update();
	bool Begin();
	bool End();
	bool Display();
	void Terminate();
	bool Shutdown();

	static std::vector<char> readFile(const std::string& filename) {
		std::ifstream file(filename, std::ios::ate | std::ios::binary);

		if (!file.is_open()) {
			throw std::runtime_error("failed to open file!");
		}

		size_t fileSize = (size_t)file.tellg();
		std::vector<char> buffer(fileSize);

		file.seekg(0);
		file.read(buffer.data(), fileSize);

		file.close();

		return buffer;
	}
};
