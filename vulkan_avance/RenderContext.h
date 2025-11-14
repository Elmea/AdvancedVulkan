#pragma once

struct VulkanRenderContext
{
	static constexpr int PENDING_FRAMES = 2;	// nombre de frames en cours de traitement
	// 2 = separation en frames paires et impaires

	VulkanDeviceContext* context;

	uint32_t graphicsQueueIndex;
	uint32_t presentQueueIndex;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	// eventuellement creer une classe VulkanFrame par ex si besoin d'encapsuler tout ca
	VkCommandPool mainCommandPool[PENDING_FRAMES];
	VkCommandBuffer mainCommandBuffers[PENDING_FRAMES];
	VkFence mainFences[PENDING_FRAMES];

	uint32_t currentFrame = 0;

	VkRenderPass renderPass = VK_NULL_HANDLE;
	VkImageSubresourceRange mainSubRange;

	// pour les transfert de donnees cpu->gpu
	Buffer stagingBuffer;

	VkCommandBuffer BeginOneTimeCommandBuffer()
	{
		VkCommandBufferAllocateInfo commandInfo = {};
		commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		commandInfo.commandPool = mainCommandPool[currentFrame];
		commandInfo.commandBufferCount = 1;

		VkCommandBuffer commandBuffer;
		vkAllocateCommandBuffers((*context).device, &commandInfo, &commandBuffer);

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(commandBuffer, &beginInfo);

		return commandBuffer;
	}

	void EndOneTimeCommandBuffer(VkCommandBuffer commandBuffer)
	{
		vkEndCommandBuffer(commandBuffer);

		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
		
		// TODO: utilisez de preference des VkFence ou des barrieres d'execution
		// car cela bloque inutilement la queue 
		// (possible de paralleliser en general, sauf si acces aux memes ressources/zones memoires) 
		vkQueueWaitIdle(graphicsQueue);

		vkFreeCommandBuffers((*context).device, mainCommandPool[currentFrame], 1, &commandBuffer);
	}
};
