#include "vk_common.h"

// A definir en premier lieu
#if defined(_WIN32)
// a definir globalement pour etre pris en compte par volk.c
//#define VK_USE_PLATFORM_WIN32_KHR

// si glfw
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#elif defined(_GNUC_)
#endif

#if defined(_MSC_VER)
//
// Pensez a copier les dll dans le repertoire x64/Debug, cad:
// glfw-3.3/lib-vc2015/glfw3.dll
//
#pragma comment(lib, "glfw3dll.lib")
#pragma comment(lib, "vulkan-1.lib")
#endif

//#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "GraphicsApplication.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void* userData)
{
	printf("[Validation layer] %s\n", callbackData->pMessage);
#ifdef _WIN32
	if (IsDebuggerPresent()) {
		OutputDebugStringA("[Validation layer] ");
		OutputDebugStringA(callbackData->pMessage);
		OutputDebugStringA("\n");
	}
#endif
	return VK_FALSE;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanReportFunc(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objType,
	uint64_t obj,
	size_t location,
	int32_t code,
	const char* layerPrefix,
	const char* msg,
	void* userData)
{
	std::cout << "[VULKAN VALIDATION]" << msg << std::endl;
#ifdef _WIN32
	if (IsDebuggerPresent()) {
		OutputDebugStringA("[VULKAN VALIDATION] ");
		OutputDebugStringA(msg);
		OutputDebugStringA("\n");
	}
#endif
	return VK_FALSE;
}

bool VulkanGraphicsApplication::Initialize(const char* appName)
{
	m_imageIndex = 0;
	name = appName;

	// Vulkan

	DEBUG_CHECK_VK(volkInitialize());

	// instance
	uint32_t version;
	DEBUG_CHECK_VK(vkEnumerateInstanceVersion(&version));
	uint32_t minorVersion = (version >> 12) & 0x3FF;

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = name;
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "todo engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_MAKE_VERSION(1, minorVersion, 0);

	const char *extensionNames[] = { VK_KHR_SURFACE_EXTENSION_NAME
#if defined(_WIN32)
		, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
		, VK_EXT_DEBUG_REPORT_EXTENSION_NAME };
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = sizeof(extensionNames) / sizeof(char*);
	instanceInfo.ppEnabledExtensionNames = extensionNames;
#ifdef VULKAN_ENABLE_VALIDATION
	const char* layerNames[] = { "VK_LAYER_KHRONOS_validation" };
	instanceInfo.enabledLayerCount = 1;
	instanceInfo.ppEnabledLayerNames = layerNames;
#else
	instanceInfo.enabledExtensionCount--;
#endif
	DEBUG_CHECK_VK(vkCreateInstance(&instanceInfo, nullptr, &context.instance));
	// TODO: fallback si pas de validation possible (MoltenVK, toujours le cas ?)

	volkLoadInstance(context.instance);

#ifdef VULKAN_ENABLE_VALIDATION
	VkDebugUtilsMessengerCreateInfoEXT debugUtilsMessengerInfo = {};
	debugUtilsMessengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;	
	debugUtilsMessengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	debugUtilsMessengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	debugUtilsMessengerInfo.pfnUserCallback = &vulkanDebugCallback;
	//vkCreateDebugUtilsMessengerEXT(context.instance, &debugUtilsMessengerInfo, nullptr, &context.debugMessenger);
	VkDebugReportCallbackCreateInfoEXT debugCallbackInfo = {};
	debugCallbackInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	debugCallbackInfo.flags = VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
		VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
	debugCallbackInfo.pfnCallback = VulkanReportFunc;
	vkCreateDebugReportCallbackEXT(context.instance, &debugCallbackInfo, nullptr, &context.debugCallback);
#endif

	// render surface

#if defined(USE_GLFW_SURFACE)
	glfwCreateWindowSurface(g_Context.instance, g_Context.window, nullptr, &g_Context.surface);
#else
#if defined(_WIN32)
	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hinstance = GetModuleHandle(NULL);
	surfaceInfo.hwnd = glfwGetWin32Window(window);
	DEBUG_CHECK_VK(vkCreateWin32SurfaceKHR(context.instance, &surfaceInfo, nullptr, &context.surface));
#endif
#endif

	// device

	uint32_t num_devices = context.MAX_DEVICE_COUNT;
	std::vector<VkPhysicalDevice> physical_devices(num_devices);
	DEBUG_CHECK_VK(vkEnumeratePhysicalDevices(context.instance, &num_devices, &physical_devices[0]));

	// on prend le premier ... TODO
	context.physicalDevice = physical_devices[1];

	// TODO : VK_EXT_sampler_filter_minmax

	uint32_t extensionCount = 0;
	vkEnumerateDeviceExtensionProperties(context.physicalDevice, 0, &extensionCount, 0);
	std::vector<VkExtensionProperties> extensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(context.physicalDevice, 0, &extensionCount, extensions.data());
	// todo: check available extensions

	// todo: VK_FORMAT_FEATURE_TRANSFER_SRC/DST_BIT_KHR
	VkPhysicalDeviceVulkan12Features vulkan12Features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceDynamicRenderingFeatures dynamicRenderingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES };
	dynamicRenderingFeatures.pNext = &vulkan12Features;
	// ces features font deja parties de vulkan12Features
	//VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES };
	//timelineSemaphoreFeatures.pNext = &dynamicRenderingFeatures;
	//VkPhysicalDeviceImagelessFramebufferFeatures imagelessFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGELESS_FRAMEBUFFER_FEATURES };	
	//imagelessFeatures.pNext = &timelineSemaphoreFeatures;
	VkPhysicalDeviceInlineUniformBlockFeatures inlineUBOFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES };
	inlineUBOFeatures.pNext = &dynamicRenderingFeatures;// &imagelessFeatures;
	VkPhysicalDeviceFeatures2 deviceFeatures2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };	
	deviceFeatures2.pNext = &inlineUBOFeatures;
	vkGetPhysicalDeviceFeatures2(context.physicalDevice, &deviceFeatures2);

	// on a besoin de :
	//vulkan12Features.drawIndirectCount;
	//deviceFeatures2.features.multiDrawIndirect;
	//deviceFeatures2.features.samplerAnisotropy;

	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(context.physicalDevice, VK_FORMAT_B8G8R8A8_SRGB, &formatProperties);

	if (vkGetPhysicalDeviceProperties2) 
	{
		VkPhysicalDevicePushDescriptorPropertiesKHR pushDescriptorProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR };
		VkPhysicalDeviceDescriptorIndexingProperties indexingProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES };
		indexingProperties.pNext = &pushDescriptorProperties;
		VkPhysicalDeviceSubgroupProperties subgroupProperties{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
		subgroupProperties.pNext = &indexingProperties;

		VkPhysicalDeviceProperties2 deviceProperties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &subgroupProperties };
		vkGetPhysicalDeviceProperties2(context.physicalDevice, &deviceProperties2);
		// Example of checking if supported in fragment shader
		if ((subgroupProperties.supportedStages & VK_SHADER_STAGE_FRAGMENT_BIT) != 0) {
			// fragment shaders supported
		}

		// Example of checking if ballot is supported
		if ((subgroupProperties.supportedOperations & VK_SUBGROUP_FEATURE_BALLOT_BIT) != 0) {
			// ballot subgroup operations supported
		}

		// todo:
		// VkDeviceCreateInfo info.pEnabledFeatures = &features;
	}

	// enumeration des memory types
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &memoryProperties);
	context.memoryFlags.reserve(memoryProperties.memoryTypeCount);
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		context.memoryFlags.push_back(memoryProperties.memoryTypes[i].propertyFlags);

	rendercontext.graphicsQueueIndex = UINT32_MAX;
	uint32_t queue_families_count = context.MAX_FAMILY_COUNT;
	std::vector<VkQueueFamilyProperties> queue_family_properties(queue_families_count);
	// normalement il faut appeler la fonction une premiere fois pour recuperer le nombre exact de queues supportees.
	//voir les messages de validation
	vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &queue_families_count, &queue_family_properties[0]);
	for (uint32_t i = 0; i < queue_families_count; ++i) {
		if ((queue_family_properties[i].queueCount > 0) &&
			(queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
			(queue_family_properties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
			VkBool32 canPresentSurface;
			vkGetPhysicalDeviceSurfaceSupportKHR(context.physicalDevice, i, context.surface, &canPresentSurface);
			if (canPresentSurface)
				rendercontext.graphicsQueueIndex = i;
			break;
		}
	}

	// on suppose que la presentation se fait par la graphics queue (verifier cela avec vkGetPhysicalDeviceSurfaceSupportKHR())
	rendercontext.presentQueueIndex = rendercontext.graphicsQueueIndex;

	const float queue_priorities[] = { 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = rendercontext.graphicsQueueIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = queue_priorities;

	const char* device_extensions[] = { 
		VK_KHR_SWAPCHAIN_EXTENSION_NAME, 
		VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
		VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, 
		VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME, 
		"VK_KHR_dynamic_rendering" 
	};
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceInfo.enabledExtensionCount = _countof(device_extensions);
	deviceInfo.ppEnabledExtensionNames = device_extensions;
	deviceInfo.pNext = &deviceFeatures2;//deviceInfo.pEnabledFeatures
	DEBUG_CHECK_VK(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device));

	//volkLoadDevice(context.device);

	vkGetDeviceQueue(context.device, rendercontext.graphicsQueueIndex, 0, &rendercontext.graphicsQueue);
	rendercontext.presentQueue = rendercontext.graphicsQueue;

	// swap chain
	
	uint32_t formatCount = 32;
	vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, 0);
	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, surfaceFormats.data());
	context.surfaceFormat = surfaceFormats[0];
	for (uint32_t i = 0; i < formatCount; i++) {
		//if (surfaceFormats[i].format == VK_FORMAT_R8G8B8A8_UNORM || surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_UNORM) {
		// ici je prefere que la conversion LINEAIRE(UNORM)->SRGB soit fait automatiquement par le GPU 
		// au lieu d'ajouter manuellement le pow(x,2.2) dans les shaders, mais ce n'est pas forcement ideal
		if (surfaceFormats[i].format == VK_FORMAT_R8G8B8A8_SRGB || surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
			context.surfaceFormat = surfaceFormats[i];
			break;
		}
	}
	if (context.surfaceFormat.format == VK_FORMAT_UNDEFINED)
		__debugbreak();

	uint32_t presentModeCount = 8;
	vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, 0);
	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	vkGetPhysicalDeviceSurfacePresentModesKHR(context.physicalDevice, context.surface, &presentModeCount, presentModes.data());
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_RELAXED_KHR;   // FIFO est toujours garanti.
	context.swapchainImageCount = context.SWAPCHAIN_IMAGES;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &surfaceCapabilities);
	context.swapchainExtent = surfaceCapabilities.currentExtent;
	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // garanti
	if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // necessaire ici pour vkCmdClearImageColor
													   //	if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
													   //		imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // necessaire ici pour screenshots, read back

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = context.surface;
	swapchainInfo.minImageCount = context.swapchainImageCount;
	swapchainInfo.imageFormat = context.surfaceFormat.format;
	swapchainInfo.imageColorSpace = context.surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = context.swapchainExtent;
	swapchainInfo.imageArrayLayers = 1; // 2 for stereo
	swapchainInfo.imageUsage = imageUsage;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	DEBUG_CHECK_VK(vkCreateSwapchainKHR(context.device, &swapchainInfo, nullptr, &context.swapchain));

	DEBUG_CHECK_VK(vkGetSwapchainImagesKHR(context.device, context.swapchain, &context.swapchainImageCount, nullptr));
	assert(swapchainInfo.minImageCount == context.swapchainImageCount);
	std::vector<VkImage> images(context.swapchainImageCount);
	DEBUG_CHECK_VK(vkGetSwapchainImagesKHR(context.device, context.swapchain, &context.swapchainImageCount, images.data()));
	for (uint32_t i = 0; i < context.swapchainImageCount; i++) {
		context.swapchainImages[i].image = images[i];
	}

	m_frame = 0;

	return Prepare();
}

bool VulkanGraphicsApplication::Shutdown()
{
	if (context.instance == VK_NULL_HANDLE)
		return false;
	if (context.device == VK_NULL_HANDLE)
		return false;

	vkDeviceWaitIdle(context.device);

	Terminate();
	
	vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);

	vkDestroyDevice(context.device, nullptr);

	vkDestroySurfaceKHR(context.instance, context.surface, nullptr);

#ifdef VULKAN_ENABLE_VALIDATION
	vkDestroyDebugReportCallbackEXT(context.instance, context.debugCallback, nullptr);
	//vkDestroyDebugUtilsMessengerEXT(context.instance, context.debugMessenger, nullptr);
#endif

	vkDestroyInstance(context.instance, nullptr);

#ifndef GLFW_INCLUDE_VULKAN

#endif

	return true;
}

bool VulkanGraphicsApplication::Run()
{
	Update();
	Begin();
	Display();
	End();

	return true;
}



uint32_t Texture::CheckExist(const char* path)
{
	// "ranged-for" du C++. Equivalent d'un "foreach" en C#
	/*for (Texture& tex : textures) {
		if (tex.name == path)
			return tex.id;
	}*/
	return 0;
}

void Texture::SetupManager()
{
	// création d'une texture par defaut, 1x1 blanche
	if (textures.size() == 0) {
		const uint8_t data[] = { 255,255,255,255 };
		LoadTexture(data, 1, 1, PixelFormat::PIXFMT_SRGBA8);
		//uint32_t textureID = CreateTextureRGBA(1, 1, data);
		//textureRefs.push_back({ , textureID, ""});
	}
}

void Texture::PurgeTextures()
{
	for (auto& texture : textures)
	{
		texture.Destroy(*rendercontext);
	}
	// change le nombre d'element (size) à zero, mais conserve la meme capacite
	textures.clear();
	// force capacite = size
	textures.shrink_to_fit();
}
