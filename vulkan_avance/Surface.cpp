#include "vk_common.h"
#include "DeviceContext.h"
#include "RenderContext.h"

void RenderSurface::CopyImage(VkCommandBuffer commandBuffer, VkImage dest, VkImage source, uint32_t imageWidth, uint32_t imageHeight, VkImageAspectFlags aspectFlag)
{
	VkImageMemoryBarrier srcBarrier{};
	srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	srcBarrier.image = source;
	srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.subresourceRange.aspectMask = aspectFlag;
	srcBarrier.subresourceRange.baseArrayLayer = 0;
	srcBarrier.subresourceRange.layerCount = 1;
	srcBarrier.subresourceRange.levelCount = 1;
	srcBarrier.oldLayout = aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	srcBarrier.srcAccessMask = aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	srcBarrier.dstAccessMask = 0;

	vkCmdPipelineBarrier(commandBuffer,
		aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT
		, VK_PIPELINE_STAGE_TRANSFER_BIT
		, 0,
		0, nullptr,
		0, nullptr,
		1, &srcBarrier);

	VkImageMemoryBarrier dstBarrier{};
	dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	dstBarrier.image = dest;
	dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	dstBarrier.subresourceRange.aspectMask = aspectFlag;
	dstBarrier.subresourceRange.baseArrayLayer = 0;
	dstBarrier.subresourceRange.layerCount = 1;
	dstBarrier.subresourceRange.levelCount = 1;
	dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	dstBarrier.srcAccessMask = 0;
	dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &dstBarrier);

	VkImageCopy blit = {};
	blit.srcOffset = { 0, 0, 0 };
	blit.srcSubresource.aspectMask = aspectFlag;
	blit.srcSubresource.baseArrayLayer = 0;
	blit.srcSubresource.layerCount = 1;
	blit.srcSubresource.mipLevel = 0;
	blit.dstOffset = { 0, 0, 0 };
	blit.dstSubresource.aspectMask = aspectFlag;
	blit.dstSubresource.baseArrayLayer = 0;
	blit.dstSubresource.layerCount = 1;
	blit.dstSubresource.mipLevel = 0;
	blit.extent = { imageWidth, imageHeight, 1 };

	vkCmdCopyImage(commandBuffer,
		source, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		dest, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		1, &blit);

	dstBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	dstBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	dstBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	dstBarrier.dstAccessMask = 0;
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
		0, nullptr,
		0, nullptr,
		1, &dstBarrier);

	srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	srcBarrier.newLayout = aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	srcBarrier.dstAccessMask = aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT : VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT,
		aspectFlag == VK_IMAGE_ASPECT_COLOR_BIT ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
		, 0,
		0, nullptr,
		0, nullptr,
		1, &srcBarrier);
}

bool GenerateMipmaps(VkCommandBuffer commandBuffer, VkImage image, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
{
	// on suppose une image 2D donc layerCount = 1
	
	VkImageMemoryBarrier barrier{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	VkImageBlit blit = {};
	memset(&blit, 0, sizeof VkImageBlit);
	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.srcSubresource.layerCount = 1;
	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blit.dstSubresource.layerCount = 1;

	for (uint32_t i = 1; i < mipLevels; i++) 
	{
		blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
		int32_t newMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
		int32_t newMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
		blit.srcSubresource.mipLevel = i - 1;
		blit.dstOffsets[1] = { newMipWidth, newMipHeight, 1 };
		blit.dstSubresource.mipLevel = i;

		mipWidth = newMipWidth;
		mipHeight = newMipHeight;

		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr, 0, nullptr, 1, &barrier);

		vkCmdBlitImage(commandBuffer,
			image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1, &blit, VK_FILTER_LINEAR);
	}

	// dernier level (ou premier level si pas de mipmaps)
	barrier.subresourceRange.baseMipLevel = mipLevels-1;
	vkCmdPipelineBarrier(commandBuffer,
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
		0, nullptr, 0, nullptr, 1, &barrier);

	return true;
}

bool RenderSurface::CreateSurface(VulkanRenderContext& rendercontext, int width, int height, PixelFormat pixelformat, uint32_t mipLevels, ImageUsage usage)
{
	VulkanDeviceContext& context = *rendercontext.context;

	// todo: fonction pixelformat to format
	// todo: stencil, RGBA float16/32 etc..
	switch (pixelformat)
	{
	case PIXFMT_DEPTH32F: format = VK_FORMAT_D32_SFLOAT; break; // 32 bit signed float
	case PIXFMT_RGBA32F: format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
	case PIXFMT_RGB32F: format = VK_FORMAT_R32G32B32_SFLOAT; break;
	case PIXFMT_RGBA16F: format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
	case PIXFMT_SRGBA8: format = VK_FORMAT_R8G8B8A8_SRGB; break;
	case PIXFMT_RGBA8:
	default: format = VK_FORMAT_R8G8B8A8_UNORM; break;
	}

	// TODO: staging
	VkImageUsageFlags usageFlags = 0;
	if (usage & IMAGE_USAGE_TEXTURE) {
		usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (mipLevels > 1 || (usage & IMAGE_USAGE_TRANSFER))
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		if (mipLevels > 1 || (usage & IMAGE_USAGE_BITMAP))
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		// temp for blits
		if (usage & IMAGE_USAGE_RENDERTARGET)
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}
	if (usage & IMAGE_USAGE_RENDERTARGET || usage & IMAGE_USAGE_RENDERPASS) {
		usageFlags |= pixelformat < PIXFMT_DEPTH32F ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (usage & IMAGE_USAGE_RENDERPASS)
			usageFlags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.flags = 0;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1; // <- 3D
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usageFlags;
	// | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	imageInfo.samples = (VkSampleCountFlagBits)1;//VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(context.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
		std::cout << "error: failed to create render target image!" << std::endl;
		return false;
	}

	{
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(context.device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		// LAZILY_ALLOCATED couple a USAGE_TRANSIENT est utile sur mobile pour indiquer
		// que la zone memoire est volatile / temporaire et qu'elle peut etre utilisee
		// par toute autre partie du rendering lorsque notre render pass ne dessine pas dedans
		// sur PC cela ne semble pas supporte par tous les drivers
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;//LAZILY_ALLOCATED_BIT;
		allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);
		if (allocInfo.memoryTypeIndex == ~0) {
			properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);
		}
		if (vkAllocateMemory(context.device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
			std::cout << "error: failed to allocate image memory!" << std::endl;
			return false;
		}
		if (vkBindImageMemory(context.device, image, memory, 0) != VK_SUCCESS) {
			std::cout << "error: failed to bind image memory!" << std::endl;
			return false;
		}

		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };// rendercontext.mainSubRange;
		// l'IMAGE_ASPECT doit correspondre au pixel format (todo: stencil)
		viewInfo.subresourceRange.aspectMask = pixelformat < PIXFMT_DUMMY_ASPECT_DEPTH ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
		// todo: add swizzling here when required

		if (vkCreateImageView(context.device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
			std::cout << "failed to create texture image view!" << std::endl;
			return false;
		}
	}

	return true;
}

void RenderSurface::Destroy(VulkanRenderContext& rendercontext)
{
	VulkanDeviceContext& context = *rendercontext.context;
	vkDestroyImageView(context.device, view, nullptr);
	vkDestroyImage(context.device, image, nullptr);
	vkFreeMemory(context.device, memory, nullptr);
}

uint32_t LoadImage(const char* filepath, bool sRGB, uint8_t** pixels, int& w, int &h, PixelFormat& pixelFormat)
{
	uint32_t imageSize = 0;
	
	int c;
	*pixels = nullptr;
	pixelFormat = PIXFMT_RGBA8;

	if (!stbi_is_hdr(filepath))
	{
		// pour des raisons de simplicite on force en RGBA quel que ce soit le format d'origine
		*pixels = stbi_load(filepath, &w, &h, &c, STBI_rgb_alpha);
		imageSize = w * h * c;
		pixelFormat = sRGB ? PIXFMT_SRGBA8 : PIXFMT_RGBA8;
	}
	else
	{
		// pour des raisons de simplicite on force en RGBA quel que ce soit le format d'origine
		*pixels = (uint8_t*)stbi_loadf(filepath, &w, &h, &c, STBI_rgb_alpha);
		imageSize = w * h * c * sizeof(float);
		pixelFormat = PIXFMT_RGBA32F;
	}

	return imageSize;
}

void FreeImage(uint8_t* pixels)
{
	stbi_image_free(pixels);
}

uint32_t Texture::CreateTexture(VulkanRenderContext& rendercontext, int w, int h, PixelFormat pixelFormat, uint32_t mipLevels)
{
	VulkanDeviceContext& context = *rendercontext.context;
	RenderSurface::CreateSurface(rendercontext, w, h, pixelFormat, mipLevels, IMAGE_USAGE_TEXTURE | IMAGE_USAGE_BITMAP);

	uint32_t imageSize = w * h;
	switch (pixelFormat)
	{
	case PixelFormat::PIXFMT_RGBA32F: imageSize *= 4 * sizeof(float); break;
	default:
		imageSize *= 4;
	}

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = FLT_MAX;
	//samplerInfo.anisotropyEnable = VK_TRUE;
	//samplerInfo.maxAnisotropy = 16.f;
	DEBUG_CHECK_VK(vkCreateSampler(context.device, &samplerInfo, nullptr, &sampler));
	
	return imageSize;
}

bool TransferImage(VulkanRenderContext& rendercontext, VkImage image, const uint8_t* pixels, uint32_t imageSize, int w, int h, int mipLevels)
{
	// todo: decoupler la copie dans le staging buffer du transfert de l'image
	Buffer& stagingBuffer = rendercontext.stagingBuffer;
	memcpy(stagingBuffer.data, pixels, imageSize);
	// idealement, il faudrait recycler le command buffer de transfert plutot
	VkCommandBuffer commandBuffer = rendercontext.BeginOneTimeCommandBuffer();

	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 
		/*baseLevel*/0, /*levelCount*/(uint32_t)mipLevels, 
		/*baseLayer*/0, /*layerCount*/1 };

	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(commandBuffer, 
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region = {};
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, image, 
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	if (mipLevels > 1)
	{
		GenerateMipmaps(commandBuffer, image, w, h, mipLevels);
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	}
	else {
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}

	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	// todo: remplacer VK_PIPELINE_STAGE_FRAGMENT_BIT par un parametre 
	vkCmdPipelineBarrier(commandBuffer, 
		VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	rendercontext.EndOneTimeCommandBuffer(commandBuffer);

	return true;
}

bool TransferBuffer(VulkanRenderContext& rendercontext, VkBuffer buffer, const uint8_t* data, uint32_t dataSize)
{
	Buffer& stagingBuffer = rendercontext.stagingBuffer;
	memcpy(stagingBuffer.data, data, dataSize);

	VkCommandBuffer commandBuffer = rendercontext.BeginOneTimeCommandBuffer();

	VkBufferMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.buffer = buffer;
	barrier.size = dataSize;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	// on n'attend rien en particulier jusqu'a commencer les acces en ecriture vers ce buffer
	VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
		                 0, 0, nullptr, 1, &barrier, 0, nullptr);

	VkBufferCopy region = {};
	region.size = dataSize;
	vkCmdCopyBuffer(commandBuffer, stagingBuffer.buffer, buffer, 1, &region);

	// on bloque les acces en lecture depuis les shaders tant que le transfert n'est pas fini
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = 0;

	sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage,
		0, 0, nullptr, 1, &barrier, 0, nullptr);

	rendercontext.EndOneTimeCommandBuffer(commandBuffer);

	return true;
}

// ---

VulkanRenderContext* Texture::rendercontext = nullptr;
std::vector<Texture> Texture::textures;
std::vector<TextureRef> Texture::textureRefs;

bool Image::Load(const char* filepath, bool sRGB)
{
	int w, h;
	PixelFormat format;
	uint32_t imageSize = LoadImage(filepath, sRGB, &pixels, w, h, format);
	pixelFormat = (uint16_t)format;
	components = imageSize / (w*h);
	width = w;
	height = h;
	return imageSize > 0;
}
void Image::Destroy() {
	FreeImage(pixels);
}


bool Texture::Load(VulkanRenderContext& rendercontext, const uint8_t* pixels, int w, int h, PixelFormat pixelFormat)
{
	uint32_t mipLevels = (int)log2(w) + 1;
	uint32_t imageSize = CreateTexture(rendercontext, w, h, pixelFormat, mipLevels);

	TransferImage(rendercontext, image, pixels, imageSize, w, h, mipLevels);

	return true;
}

bool Texture::Load(VulkanRenderContext& rendercontext, const char* filepath, bool sRGB)
{
	uint8_t* pixels;
	
	int w, h;
	PixelFormat pixelFormat;

	uint32_t imageSize = LoadImage(filepath, sRGB, &pixels, w, h, pixelFormat);
	Load(rendercontext,pixels, w, h, pixelFormat);
	FreeImage(pixels);

	return true;
}

uint32_t Texture::LoadTexture(const char* filepath, bool sRGB)
{
	Texture tex;
	tex.Load(*rendercontext, filepath, sRGB);
	uint32_t id = (uint32_t)textures.size();
	textures.push_back(tex);
	textureRefs.push_back({ &textures[id], id, filepath });
	return id;
}

uint32_t Texture::LoadTexture(const uint8_t* pixels, int w, int h, PixelFormat pixelFormat)
{
	Texture tex;
	tex.Load(*rendercontext, pixels, w, h, pixelFormat);
	uint32_t id = (uint32_t)textures.size();
	textures.push_back(tex);
	textureRefs.push_back({ &textures[id], id, "" });
	return id;
}


void Texture::Destroy(VulkanRenderContext& rendercontext)
{
	VulkanDeviceContext& context = *rendercontext.context;
	vkDestroySampler(context.device, sampler, nullptr);
	RenderSurface::Destroy(rendercontext);
}

// ---

void Buffer::Destroy(VulkanRenderContext& rendercontext)
{
	VkDevice device = rendercontext.context->device;
	vkDestroyBuffer(device, buffer, nullptr);
	if (data) {
		vkUnmapMemory(device, memory);
		data = nullptr;
	}
	if (offset == 0)
		vkFreeMemory(device, memory, nullptr);
}

bool Buffer::CreateBuffer(VulkanRenderContext& rendercontext, Buffer& bo, uint32_t size, VkBufferUsageFlags usage, const void* data, uint32_t dataSize)
{
	bo.offset = 0;
	bo.data = nullptr;

	VulkanDeviceContext& context = *rendercontext.context;
	VkMemoryRequirements bufferMemReq;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;

	bufferInfo.usage = usage;
	if (data) {
		// on doit copier les donnees du staging buffer vers ce buffer
		bufferInfo.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	bufferInfo.size = size;// =requestedSize sizeof(Vertex) * scene.meshes[0].vertices.size();	
	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &bo.buffer));
	vkGetBufferMemoryRequirements(context.device, bo.buffer, &bufferMemReq);
	bo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);

	VkMemoryAllocateInfo bufferAllocInfo = {};
	bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	bufferAllocInfo.allocationSize = bo.size;
	// ainsi le buffer actuel devra etre USAGE_TRANSFER_DST et copie avec vkCmdCopyBuffer()
	bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryTypeBits
		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &bo.memory));
	DEBUG_CHECK_VK(vkBindBufferMemory(context.device, bo.buffer, bo.memory, 0));

	if (data)
	{
		// todo: queue pour les copies 
		TransferBuffer(rendercontext, bo.buffer, (uint8_t*)data, dataSize);
	}
	return true;
}

bool Buffer::CreateMappedBuffer(VulkanRenderContext& rendercontext, Buffer& bo, uint32_t size, VkBufferUsageFlags usage, const void* data)
{
	bo.offset = 0;
	bo.data = nullptr;

	VulkanDeviceContext& context = *rendercontext.context;
	VkMemoryRequirements bufferMemReq;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;

	bufferInfo.usage = usage;
	bufferInfo.size = size;// =requestedSize sizeof(Vertex) * scene.meshes[0].vertices.size();	
	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &bo.buffer));
	vkGetBufferMemoryRequirements(context.device, bo.buffer, &bufferMemReq);
	bo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);

	VkMemoryAllocateInfo bufferAllocInfo = {};
	bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	bufferAllocInfo.allocationSize = bo.size;
	// possible d'eviter de rendre la memoire host visible en passant par un buffer intermediaire
	// ("staging buffer") qui est lui HOST_VISIBLE|COHERENT et USAGE_TRANSFER_SRC
	// ainsi le buffer actuel devra etre USAGE_TRANSFER_DST et copie avec vkCmdCopyBuffer()
	bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryTypeBits
		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &bo.memory));
	DEBUG_CHECK_VK(vkBindBufferMemory(context.device, bo.buffer, bo.memory, 0));

	// copie des donnees
	VkMappedMemoryRange mappedRange = {};
	mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mappedRange.memory = bo.memory;
	mappedRange.size = VK_WHOLE_SIZE;
	DEBUG_CHECK_VK(vkMapMemory(context.device, bo.memory, 0, VK_WHOLE_SIZE, 0, &bo.data));
	DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	if (data)
	{
		memcpy(bo.data, data, size);
		DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));
	}
	return true;
}

bool Buffer::CreateDualBuffer(VulkanRenderContext& rendercontext, Buffer& vbo, Buffer& ibo, uint32_t verticesSize, const void* verticesData, uint32_t indicesSize, const void* indicesData)
{
	VulkanDeviceContext& context = *rendercontext.context;
	VkMemoryDedicatedRequirements dedReq = { VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS };
	VkMemoryRequirements2 bufferMemReq = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2 };
	bufferMemReq.pNext = &dedReq;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;

	bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	bufferInfo.size = verticesSize;// =requestedSize sizeof(Vertex) * scene.meshes[0].vertices.size();

	//VkDeviceBufferMemoryRequirementsKHR bufferReq{ VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS_KHR };
	//bufferReq.pCreateInfo = &bufferInfo;
	//vkGetDeviceBufferMemoryRequirementsKHR(context.device, &bufferReq, &bufferMemReq);

	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &vbo.buffer));
	VkBufferMemoryRequirementsInfo2 boInfo = { VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2 };
	boInfo.buffer = vbo.buffer;
	vkGetBufferMemoryRequirements2(context.device, &boInfo, &bufferMemReq);
	vbo.size = (bufferMemReq.memoryRequirements.size + bufferMemReq.memoryRequirements.alignment) & ~(bufferMemReq.memoryRequirements.alignment - 1);

	
	bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	bufferInfo.size = indicesSize;// = requestedSize sizeof(uint16_t) * scene.meshes[0].indices.size();
	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &ibo.buffer));
	vkGetBufferMemoryRequirements(context.device, ibo.buffer, &bufferMemReq.memoryRequirements);
	ibo.size = (bufferMemReq.memoryRequirements.size + bufferMemReq.memoryRequirements.alignment) & ~(bufferMemReq.memoryRequirements.alignment - 1);
	// les donnees de l'IBO commencent apres celles du VBO (en tenant compte de l'alignement) 
	ibo.offset = vbo.size;

	VkMemoryAllocateInfo bufferAllocInfo = {};
	bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	// possible d'eviter de rendre la memoire host visible en passant par un buffer intermediaire
	// ("staging buffer") qui est lui HOST_VISIBLE|COHERENT et USAGE_TRANSFER_SRC
	// ainsi le buffer actuel devra etre USAGE_TRANSFER_DST et copie avec vkCmdCopyBuffer()
	bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryRequirements.memoryTypeBits
		, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	bufferAllocInfo.allocationSize = (vbo.size + ibo.size + 0x80) & ~(0x80 - 1);
	DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &vbo.memory));
	ibo.memory = vbo.memory;
	//DEBUG_CHECK_VK(vkBindBufferMemory(context.device, vbo.buffer, vbo.memory, 0));
	//DEBUG_CHECK_VK(vkBindBufferMemory(context.device, ibo.buffer, ibo.memory, ibo.offset));
	VkBindBufferMemoryInfo bindInfos[] = {
		{VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr, vbo.buffer, vbo.memory, 0},
		{VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, nullptr, ibo.buffer, ibo.memory, ibo.offset}
	};
	DEBUG_CHECK_VK(vkBindBufferMemory2(context.device, 2, bindInfos));

	// copie des donnees
	VkMappedMemoryRange mappedRange = {};
	mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mappedRange.memory = vbo.memory;
	mappedRange.size = VK_WHOLE_SIZE;
	DEBUG_CHECK_VK(vkMapMemory(context.device, vbo.memory, 0, VK_WHOLE_SIZE, 0, &vbo.data));
	DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	memcpy(vbo.data, verticesData, verticesSize);
	// hack pointeur car j'ai la flemme de creer une variable locale (mais pas de taper ce texte)
	*((uint8_t**)&vbo.data) += ibo.offset;
	memcpy(vbo.data, indicesData, indicesSize);
	DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));
	vkUnmapMemory(context.device, vbo.memory);
	vbo.data = nullptr;

	return true;
}
