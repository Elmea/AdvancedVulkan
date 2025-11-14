#pragma once

#include "vk_common.h"

struct VulkanDeviceContext
{
	static constexpr int MAX_DEVICE_COUNT = 4;	// arbitraire, exemple GPU integre (IGP) + 3 GPU max
	static constexpr int MAX_FAMILY_COUNT = 4;	// graphics, compute, transfer, graphics+compute (ajouter sparse aussi...)
	static constexpr int SWAPCHAIN_IMAGES = 2;	// 3 si triple-buffering (en mailbox ou fifo-relaxed par ex.)

	VkDebugReportCallbackEXT debugCallback;
	VkDebugUtilsMessengerEXT debugMessenger;

	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;

	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surfaceFormat;
	VkSwapchainKHR swapchain;
	VkExtent2D swapchainExtent;
	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	SwapchainImage swapchainImages[SWAPCHAIN_IMAGES];
	VkSemaphore presentSemaphores[SWAPCHAIN_IMAGES];
	VkSemaphore renderSemaphores[SWAPCHAIN_IMAGES];

	uint32_t semaphoreIndex = 0; // modulo SWAPCHAIN_IMAGES
	uint32_t swapchainImageCount = SWAPCHAIN_IMAGES;

	VkPhysicalDeviceProperties props;
	std::vector<VkMemoryPropertyFlags> memoryFlags;

	bool setObjectName(void* object, VkObjectType objType, const char* name) {
		VkDebugUtilsObjectNameInfoEXT nameInfo = { VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, 0, objType, (uint64_t)object, name };
		return vkSetDebugUtilsObjectNameEXT(device, &nameInfo) == VK_SUCCESS;
	}

	VkShaderModule createShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			std::cout << "failed to create shader module!" << std::endl;
		}

		return shaderModule;
	}

	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) 
	{
		int i = 0;
		for (auto propertyFlags : memoryFlags) {
			if ((typeFilter & (1 << i)) && (propertyFlags & properties) == properties) {
				return i;
			}
			i++;
		}

		std::cout << "failed to find suitable memory type!" << std::endl;
		return 0;
	}
};

