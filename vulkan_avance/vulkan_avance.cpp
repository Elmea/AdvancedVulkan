#include "vk_common.h"

#include "DeviceContext.h"
#include "RenderContext.h"
#include "GraphicsApplication.h"

//#define GLFW_INCLUDE_VULKAN // on utilise volk a la place
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#define APP_NAME "Vulkan_Avance"

#define INSTANCE_COUNT 200

#define RUN_COMPUTE

//
enum MatrixBufferUsageType
{
	INSTANCE = 0,
	GLOBAL = 1,
	MATRIXBUFFER_COUNT
};

enum MaterialTextureType
{
	ENVMAP = 0,
	ALBEDOMAP = 1,
	NORMALMAP = 2,
	PBRMAP = 3,
	OCCLUSIONMAP = 4,
	EMISSIVEMAP = 5,
	MATERIALTEXTURE_COUNT
};

// frequence d'usage de chacun des descriptor sets
// peu de difference en pratique entre dynamic et perframe (dynamic = buffer circulaire par ex.)
// l'index correspond au numero du set
enum DescriptorSetType
{
	DYNAMIC = 0,
	PERFRAME = 1,
	SHARED = 2,		// pas duplique entre les frames
	DESCRIPTORSET_COUNT
};

static constexpr uint32_t DescriptorSetsDuplicatedCount = 2;
static constexpr uint32_t DescriptorSetsSharedCount = 1;

struct InstanceData
{
	glm::mat4 world;
};

struct BoidVelocity
{
	glm::vec4 velocity;
};

struct SimulationParams
{
	float deltaTime;
	float separationDistance;
	float alignmentDistance;
	float cohesionDistance;
	float separationWeight;
	float alignmentWeight;
	float cohesionWeight;
	float maxSpeed;
	float minSpeed;
	uint32_t boidCount;
	glm::vec3 boundaryMin;
	glm::vec3 boundaryMax;
};

struct SceneMatrices
{
	// Partie CPU --- (pas forcement utile de dupliquer, mais plus simple)
	// Global data
	glm::mat4 view;
	glm::mat4 projection;

	// Instance data
	glm::mat4 world;

	// Partie GPU ---
	Buffer constantBuffers[MATRIXBUFFER_COUNT];
};

// Resources d'une frame GPU qu'on associe a un command buffer principal
struct Frame
{
	// Un DescriptorSet ne peut pas etre update ou utilise par un command buffer
	// alors qu'il est "bind" par un autre command buffer
	// On va donc avoir des descriptorSets par frame/main_command_buffer
	VkDescriptorSet descriptorSet[DescriptorSetsDuplicatedCount];
	VkDescriptorSet computeDescriptorSet;
};

struct Scene
{
	// CPU scene ---
	SceneMatrices matrices;
	std::vector<Mesh> meshes;
	std::vector<Material> materials;
	std::vector<Texture> textures;

	// GPU scene --- 
	VkDescriptorPool descriptorPool;
	VkDescriptorSetLayout descriptorSetLayout[DESCRIPTORSET_COUNT]; // todo encapsuler si destructeur

	// on duplique ...
	Frame frameData[VulkanRenderContext::PENDING_FRAMES];
	// ...sauf ce qui est partage
	VkDescriptorSet sharedDescriptorSet;

	std::vector<InstanceData> cpuInstances;
	Buffer instanceSSBO[VulkanRenderContext::PENDING_FRAMES];
	uint32_t instanceCount = 0;

	VkDescriptorSetLayout computeDescriptorSetLayout;
	VkPipelineLayout computePipelineLayout;
	VkPipeline computePipeline;

	std::vector<BoidVelocity> cpuVelocities;
	Buffer velocitySSBO[VulkanRenderContext::PENDING_FRAMES];

	SimulationParams simParams;
	Buffer simParamsUBO[VulkanRenderContext::PENDING_FRAMES];
};

// juste parceque j'ai la flemme de faire des headers 
//
Scene scene;

//
// Initialisation des ressources 
//

bool VulkanGraphicsApplication::Prepare()
{
	// creer les semaphores
	// il faut 2 semaphores par image : 1 pour l'acquire (present) et 1 pour le rendu 
	VkSemaphoreCreateInfo semCreateInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

	for (uint32_t i = 0; i < context.swapchainImageCount; i++) {
		DEBUG_CHECK_VK(vkCreateSemaphore(context.device, &semCreateInfo, nullptr, &context.presentSemaphores[i]));
	}
	VkSemaphoreTypeCreateInfo semTypeCreateInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	semTypeCreateInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	semTypeCreateInfo.initialValue = 0;
	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		DEBUG_CHECK_VK(vkCreateSemaphore(context.device, &semCreateInfo, nullptr, &context.renderSemaphores[i]));
	}

	// creer le command pool
	// utilisez VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT si vous souhaitez reset les command buffers individuellement
	// Je vous donne ici un exemple de reset du CommandPool (cf BeginRender)
	VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
	cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolCreateInfo.flags = 0;// VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++)
		DEBUG_CHECK_VK(vkCreateCommandPool(context.device, &cmdPoolCreateInfo, nullptr, &rendercontext.mainCommandPool[i]));

	VkFenceCreateInfo fenceCreateInfo = {};
	fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	VkCommandBufferAllocateInfo cmdAllocInfo = {};
	cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmdAllocInfo.commandBufferCount = 1;
	cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	// les Fences qui vont servir a signaler la disponibilite de chaque command buffer 'main'
	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++)
	{
		cmdAllocInfo.commandPool = rendercontext.mainCommandPool[i];
		// creer les fences (en ETAT SIGNALEE)
		DEBUG_CHECK_VK(vkCreateFence(context.device, &fenceCreateInfo, nullptr, &rendercontext.mainFences[i]));
		// creer les command buffers (1 par frame)
		DEBUG_CHECK_VK(vkAllocateCommandBuffers(context.device, &cmdAllocInfo, &rendercontext.mainCommandBuffers[i]));
	}

	rendercontext.context = &context;

	// 2. creer la render pass

	// creation d'une render surface pour le depth buffer
	// on va donc avoir un second attachment mais de type depth/stencil
	// le depth buffer n'est utilise qu'en lecture/ecriture dans la passe principale
	// il est important de le clear en debut de passe mais inutile de conserver son contenu
	// Par contre, dans le cas ou un effet a besoin d'acceder au depth buffer, 
	// il faut alors specifier STORE_OP_STORE pour le champ storeOp du depth attachment
	colorBuffer.CreateSurface(rendercontext, context.swapchainExtent.width, context.swapchainExtent.height, PIXFMT_SRGBA8, 1, IMAGE_USAGE_RENDERTARGET | IMAGE_USAGE_TEXTURE);
	depthBuffer.CreateSurface(rendercontext, context.swapchainExtent.width, context.swapchainExtent.height, PIXFMT_DEPTH32F, 1, IMAGE_USAGE_RENDERTARGET);

	// 2.a configurer les attachments
	VkAttachmentDescription attachments[2];
	for (uint32_t id = 0; id < 2; id++) {
		attachments[id].samples = VK_SAMPLE_COUNT_1_BIT;
		attachments[id].flags = 0;
		attachments[id].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;// ou DONT_CARE pour une ressource temporaire;
		attachments[id].storeOp = VK_ATTACHMENT_STORE_OP_STORE;// ou DONT_CARE;
		attachments[id].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachments[id].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachments[id].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	}
	attachments[RenderTarget::SWAPCHAIN].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	attachments[RenderTarget::SWAPCHAIN].format = context.surfaceFormat.format;
	attachments[RenderTarget::DEPTH].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[RenderTarget::DEPTH].format = depthBuffer.format;
	attachments[RenderTarget::DEPTH].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	VkAttachmentReference references[2];
	uint32_t id = RenderTarget::SWAPCHAIN;
	references[id].attachment = RenderTarget::SWAPCHAIN;
	references[id].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	id = RenderTarget::DEPTH;
	references[id].attachment = RenderTarget::DEPTH;
	references[id].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// 2.b configurer les subpass
	VkSubpassDescription subpasses[1];
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].flags = 0;
	subpasses[0].pColorAttachments = references;
	subpasses[0].pDepthStencilAttachment = &references[1];
	subpasses[0].pResolveAttachments = nullptr;		// autant que color+depth
	subpasses[0].inputAttachmentCount = 0;
	subpasses[0].pInputAttachments = nullptr;
	subpasses[0].preserveAttachmentCount = 0;
	subpasses[0].pPreserveAttachments = nullptr;
	// 2.c configurer la render pass
	VkRenderPassCreateInfo renderPassCreateInfo = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	renderPassCreateInfo.attachmentCount = 2;
	renderPassCreateInfo.pAttachments = attachments;
	renderPassCreateInfo.subpassCount = 1;
	renderPassCreateInfo.pSubpasses = subpasses;
	VkSubpassDependency depInfo = {};
	depInfo.srcSubpass = VK_SUBPASS_EXTERNAL;
	depInfo.dstSubpass = 0;
	depInfo.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	depInfo.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	depInfo.srcAccessMask = VK_ACCESS_NONE;
	depInfo.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	renderPassCreateInfo.dependencyCount = 1;
	renderPassCreateInfo.pDependencies = &depInfo;
	DEBUG_CHECK_VK(vkCreateRenderPass(context.device, &renderPassCreateInfo, nullptr, &rendercontext.renderPass));

	// 1. recuperer les image views correspondant aux images de la swap chain

	VkImageViewCreateInfo viewCreateInfo = {};
	viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCreateInfo.format = context.surfaceFormat.format;
	viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCreateInfo.subresourceRange.layerCount = 1;
	viewCreateInfo.subresourceRange.levelCount = 1;

	// 3. creer le framebuffer
	VkFramebufferAttachmentImageInfo fbAttachImageInfo[2] = {};
	VkFormat viewFormats[] = { context.surfaceFormat.format };
	fbAttachImageInfo[0].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
	fbAttachImageInfo[0].usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	fbAttachImageInfo[0].width = context.swapchainExtent.width;
	fbAttachImageInfo[0].height = context.swapchainExtent.height;
	fbAttachImageInfo[0].layerCount = 1;
	fbAttachImageInfo[0].pViewFormats = viewFormats;
	fbAttachImageInfo[0].viewFormatCount = 1;
	VkFormat depthviewFormats[] = { depthBuffer.format };
	fbAttachImageInfo[1].sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
	fbAttachImageInfo[1].usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	fbAttachImageInfo[1].width = context.swapchainExtent.width;
	fbAttachImageInfo[1].height = context.swapchainExtent.height;
	fbAttachImageInfo[1].layerCount = 1;
	fbAttachImageInfo[1].pViewFormats = depthviewFormats;
	fbAttachImageInfo[1].viewFormatCount = 1;

	VkFramebufferAttachmentsCreateInfo fbAttachsCreateInfo = {};
	fbAttachsCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
	fbAttachsCreateInfo.attachmentImageInfoCount = 2;
	fbAttachsCreateInfo.pAttachmentImageInfos = fbAttachImageInfo;

	VkFramebufferCreateInfo fbCreateInfo = {};
	fbCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	fbCreateInfo.width = context.swapchainExtent.width;
	fbCreateInfo.height = context.swapchainExtent.height;
	fbCreateInfo.layers = 1;
	fbCreateInfo.renderPass = rendercontext.renderPass;
	fbCreateInfo.flags |= VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
	fbCreateInfo.pNext = &fbAttachsCreateInfo;
	fbCreateInfo.attachmentCount = 2;

	VkImageView framebufferAttachments[2] = { nullptr, depthBuffer.view };

	for (uint32_t i = 0; i < context.swapchainImageCount; i++) {
		viewCreateInfo.image = context.swapchainImages[i].image;
		DEBUG_CHECK_VK(vkCreateImageView(context.device, &viewCreateInfo, nullptr, &context.swapchainImages[i].view));
	}
	DEBUG_CHECK_VK(vkCreateFramebuffer(context.device, &fbCreateInfo, nullptr, &context.framebuffer));

	// Staging buffer

	// On cree un staging buffer "global" pour charger un maximum de ressources (essentiellement statiques)
	Buffer& stagingBuffer = rendercontext.stagingBuffer;
	memset(&stagingBuffer, 0, sizeof(Buffer));
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = 4096 * 4096 * 4 * sizeof(float);	// maximum 1 texture RGBA32F en 4k = 256Mio
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBuffer.size = bufferInfo.size;
	stagingBuffer.usage = bufferInfo.usage;
	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &stagingBuffer.buffer));
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(context.device, stagingBuffer.buffer, &memRequirements);
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	DEBUG_CHECK_VK(vkAllocateMemory(context.device, &allocInfo, nullptr, &stagingBuffer.memory));
	DEBUG_CHECK_VK(vkBindBufferMemory(context.device, stagingBuffer.buffer, stagingBuffer.memory, 0));
	DEBUG_CHECK_VK(vkMapMemory(context.device, stagingBuffer.memory, 0, VK_WHOLE_SIZE, 0, &stagingBuffer.data));

	std::array<VkDescriptorPoolSize, 4> poolSizes;
	poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (MATRIXBUFFER_COUNT + 1) * rendercontext.PENDING_FRAMES };
	poolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6 };
	poolSizes[2] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (MATRIXBUFFER_COUNT + 2) * rendercontext.PENDING_FRAMES };
	poolSizes[3] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * rendercontext.PENDING_FRAMES };

	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.maxSets = (MATRIXBUFFER_COUNT + 4) * rendercontext.PENDING_FRAMES;
	descriptorPoolInfo.poolSizeCount = poolSizes.size();
	descriptorPoolInfo.pPoolSizes = poolSizes.data();
	DEBUG_CHECK_VK(vkCreateDescriptorPool(context.device, &descriptorPoolInfo, nullptr, &scene.descriptorPool));

	int sceneSetCount = 0;
	int sceneSetBindingsCount[16];
	// layout : on doit decrire le format de chaque descriptor (binding, type, array count, stage)
	VkDescriptorSetLayoutBinding sceneSetBindings[MATRIXBUFFER_COUNT /*UBO*/ + MATERIALTEXTURE_COUNT /*SAMPLER*/];
	//
	VkDescriptorSetLayoutCreateInfo sceneSetInfo = {};
	sceneSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;

	// set 0
	sceneSetBindingsCount[sceneSetCount] = 0;
	sceneSetBindings[0] = { 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
	sceneSetBindingsCount[sceneSetCount]++;
	++sceneSetCount;

	// set 1
	sceneSetBindingsCount[sceneSetCount] = 0;
	sceneSetBindings[1] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
	sceneSetBindingsCount[sceneSetCount]++;
	++sceneSetCount;

	uint32_t frameSetCount = sceneSetCount;
	// set 2
	sceneSetBindingsCount[sceneSetCount] = 0;
	for (uint32_t i = 0; i < MATERIALTEXTURE_COUNT; i++) {
		sceneSetBindings[i + 2] = { i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		sceneSetBindingsCount[sceneSetCount]++;
	}
	++sceneSetCount;

	uint32_t commonSets = sceneSetCount - frameSetCount;

	// on cree 3 descriptor set layouts, 1 par set
	for (int i = 0; i < sceneSetCount; i++) {
		sceneSetInfo.bindingCount = sceneSetBindingsCount[i];
		sceneSetInfo.pBindings = &sceneSetBindings[i];
		DEBUG_CHECK_VK(vkCreateDescriptorSetLayout(context.device, &sceneSetInfo, nullptr, &scene.descriptorSetLayout[i]));
	}

	VkDescriptorSetLayoutBinding computeBindings[3];
	computeBindings[0].binding = 0;
	computeBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	computeBindings[0].descriptorCount = 1;
	computeBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	computeBindings[0].pImmutableSamplers = nullptr;

	computeBindings[1].binding = 1;
	computeBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	computeBindings[1].descriptorCount = 1;
	computeBindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	computeBindings[1].pImmutableSamplers = nullptr;

	computeBindings[2].binding = 2;
	computeBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	computeBindings[2].descriptorCount = 1;
	computeBindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	computeBindings[2].pImmutableSamplers = nullptr;

	VkDescriptorSetLayoutCreateInfo computeLayoutInfo = {};
	computeLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	computeLayoutInfo.bindingCount = 3;
	computeLayoutInfo.pBindings = computeBindings;
	DEBUG_CHECK_VK(vkCreateDescriptorSetLayout(context.device, &computeLayoutInfo, nullptr, &scene.computeDescriptorSetLayout));

	// Frame data (double buffer)

	VkDescriptorSetAllocateInfo allocateDescInfo = {};
	allocateDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocateDescInfo.descriptorPool = scene.descriptorPool;
	allocateDescInfo.descriptorSetCount = frameSetCount;
	allocateDescInfo.pSetLayouts = scene.descriptorSetLayout;
	// on cree les descriptor sets en double buffer (il faut donc allouer 2*N sets)
	for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		DEBUG_CHECK_VK(vkAllocateDescriptorSets(context.device, &allocateDescInfo, &scene.frameData[i].descriptorSet[0]));
	}

	allocateDescInfo.descriptorSetCount = commonSets;
	allocateDescInfo.pSetLayouts = &scene.descriptorSetLayout[frameSetCount];
	DEBUG_CHECK_VK(vkAllocateDescriptorSets(context.device, &allocateDescInfo, &scene.sharedDescriptorSet));

	allocateDescInfo.descriptorSetCount = 1;
	allocateDescInfo.pSetLayouts = &scene.computeDescriptorSetLayout;
	for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		DEBUG_CHECK_VK(vkAllocateDescriptorSets(context.device, &allocateDescInfo, &scene.frameData[i].computeDescriptorSet));
	}


	VkPipelineLayoutCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineInfo.pushConstantRangeCount = 0;
	pipelineInfo.setLayoutCount = 1;
	pipelineInfo.setLayoutCount += 2;

	pipelineInfo.pSetLayouts = &scene.descriptorSetLayout[0];
	DEBUG_CHECK_VK(vkCreatePipelineLayout(context.device, &pipelineInfo, nullptr, &mainPipelineLayout));

	VkPipelineLayoutCreateInfo computePipelineLayoutInfo = {};
	computePipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computePipelineLayoutInfo.setLayoutCount = 1;
	computePipelineLayoutInfo.pSetLayouts = &scene.computeDescriptorSetLayout;
	DEBUG_CHECK_VK(vkCreatePipelineLayout(context.device, &computePipelineLayoutInfo, nullptr, &scene.computePipelineLayout));

	auto vertShaderCode = readFile("shaders/Instancing_Test.vert.spv");
	auto fragShaderCode = readFile("shaders/mesh.frag.spv");

	VkShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = context.createShaderModule(fragShaderCode);

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
	depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_TRUE;
	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)context.swapchainExtent.width;
	viewport.height = (float)context.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = {
		{ 0, 0 },{ (int)viewport.width, (int)viewport.height }
	};
	VkPipelineViewportStateCreateInfo viewportInfo = {};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
	rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
	multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = false;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
	colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfo.attachmentCount = 1;
	colorBlendInfo.pAttachments = &colorBlendAttachment;

	VkGraphicsPipelineCreateInfo gfxPipelineInfo = {};
	gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gfxPipelineInfo.pViewportState = &viewportInfo;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	gfxPipelineInfo.pDepthStencilState = &depthStencilInfo;
	gfxPipelineInfo.pMultisampleState = &multisampleInfo;
	gfxPipelineInfo.pColorBlendState = &colorBlendInfo;
	gfxPipelineInfo.renderPass = rendercontext.renderPass;
	gfxPipelineInfo.basePipelineIndex = -1;
	gfxPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	gfxPipelineInfo.subpass = 0;

	//
	// pipelines opaques
	//

	// 
	// shaders
	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";
	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";
	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	gfxPipelineInfo.stageCount = 2;
	gfxPipelineInfo.pStages = shaderStages;
	gfxPipelineInfo.layout = mainPipelineLayout;

	// VAO / input layout
	uint32_t stride = 0;
	VkVertexInputAttributeDescription vertexInputLayouts[4];
	vertexInputLayouts[0] = { 0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec3);
	vertexInputLayouts[1] = { 1/*location*/, 0/*binding*/, VK_FORMAT_R32G32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec2);
	vertexInputLayouts[2] = { 2/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec3);
	// tangent
	vertexInputLayouts[3] = { 3/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32A32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec4);
	VkVertexInputBindingDescription vertexInputBindings = { 0, stride, VK_VERTEX_INPUT_RATE_VERTEX };
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindings;
	vertexInputInfo.vertexAttributeDescriptionCount = _countof(vertexInputLayouts);
	vertexInputInfo.pVertexAttributeDescriptions = vertexInputLayouts;
	gfxPipelineInfo.pVertexInputState = &vertexInputInfo;


	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &mainPipelineOpaque);

	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);

	//
	// environment map cubiques
	//

	vertShaderCode.clear();
	vertShaderCode = readFile("shaders/envmap.vert.spv");
	vertShaderModule = context.createShaderModule(vertShaderCode);
	shaderStages[0].module = vertShaderModule;
	fragShaderCode.clear();
	fragShaderCode = readFile("shaders/envmap.frag.spv");
	fragShaderModule = context.createShaderModule(fragShaderCode);
	shaderStages[1].module = fragShaderModule;

	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_FALSE;
	colorBlendAttachment.blendEnable = false;

	VkPipelineVertexInputStateCreateInfo dummyVertexInputInfo = {};
	dummyVertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	dummyVertexInputInfo.vertexBindingDescriptionCount = 0;
	dummyVertexInputInfo.pVertexBindingDescriptions = nullptr;
	dummyVertexInputInfo.vertexAttributeDescriptionCount = 0;
	dummyVertexInputInfo.pVertexAttributeDescriptions = nullptr;

	rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pVertexInputState = &dummyVertexInputInfo;

	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &mainPipelineEnvMap);

	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);

	auto compShaderCode = readFile("shaders/boid.comp.spv");
	VkShaderModule compShaderModule = context.createShaderModule(compShaderCode);

	VkPipelineShaderStageCreateInfo compShaderStageInfo = {};
	compShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	compShaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	compShaderStageInfo.module = compShaderModule;
	compShaderStageInfo.pName = "main";

	VkComputePipelineCreateInfo computePipelineInfo = {};
	computePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	computePipelineInfo.stage = compShaderStageInfo;
	computePipelineInfo.layout = scene.computePipelineLayout;

	DEBUG_CHECK_VK(vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1, &computePipelineInfo, nullptr, &scene.computePipeline));

	vkDestroyShaderModule(context.device, compShaderModule, nullptr);

	//
	// Ressources ---
	//

	// Matrices world des instances

	scene.matrices.world = glm::translate(glm::mat4(1.f), glm::vec3((float)0, 0.f, 0.f));

	scene.matrices.view = glm::lookAt(glm::vec3(0.f, 0.f, 3.5f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
	scene.matrices.projection = glm::perspective(glm::radians(45.f), context.swapchainExtent.width / (float)context.swapchainExtent.height, 1.f, 1000.f);
	scene.matrices.projection[1][1] *= -1.f;

	// UBOs INSTANCE et GLOBAL
	size_t constantSizes[] = { 1 * sizeof(glm::mat4), 2 * sizeof(glm::mat4) };

	memset(scene.matrices.constantBuffers, 0, sizeof(scene.matrices.constantBuffers));

	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
	VkMemoryRequirements bufferMemReq;

	char* matrixData = (char*)&scene.matrices.view;
	for (uint32_t i = 0; i < MATRIXBUFFER_COUNT; i++)
	{
		bufferInfo.size = constantSizes[i];
		Buffer& ubo = scene.matrices.constantBuffers[i];

		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &ubo.buffer));
		vkGetBufferMemoryRequirements(context.device, ubo.buffer, &bufferMemReq);

		VkMemoryAllocateInfo bufferAllocInfo = {};
		bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		bufferAllocInfo.allocationSize = bufferMemReq.size;
		bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &ubo.memory));
		DEBUG_CHECK_VK(vkBindBufferMemory(context.device, ubo.buffer, ubo.memory, 0));
		ubo.size = bufferMemReq.size;
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = ubo.memory;
		mappedRange.size = ubo.size;
		DEBUG_CHECK_VK(vkMapMemory(context.device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &ubo.data));
		memcpy(ubo.data, matrixData, bufferInfo.size);
		matrixData += bufferInfo.size;
	}

	// models
	Texture::rendercontext = &rendercontext;
	Texture::SetupManager();

	// mesh 
	Material material;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	Mesh::ParseGLTF(vertices, indices, material, "../data/DamagedHelmet/DamagedHelmet.gltf");
	scene.meshes.resize(scene.meshes.size() + 1);
	scene.meshes[0].indexCount = (uint32_t)indices.size();
	scene.meshes[0].vertexCount = (uint32_t)vertices.size();
	uint32_t verticesSize = (uint32_t)vertices.size() * sizeof(Vertex);
	uint32_t indicesSize = (uint32_t)indices.size() * sizeof(uint32_t);
	Buffer::CreateDualBuffer(rendercontext, scene.meshes[0].staticBuffers[0], scene.meshes[0].staticBuffers[1]
		, verticesSize, vertices.data(), indicesSize, indices.data());

	scene.materials.push_back(material);

	// textures

	scene.textures.resize(8);
	scene.textures[0].Load(rendercontext, "../data/envmaps/pisa.hdr", false);


	{
		// memes ubos avec double buffer des descriptor sets
		VkDescriptorBufferInfo sceneBufferInfo[MATRIXBUFFER_COUNT];
		sceneBufferInfo[0] = { scene.matrices.constantBuffers[MatrixBufferUsageType::INSTANCE].buffer, 0, constantSizes[MatrixBufferUsageType::INSTANCE] };
		sceneBufferInfo[1] = { scene.matrices.constantBuffers[MatrixBufferUsageType::GLOBAL].buffer, 0, constantSizes[MatrixBufferUsageType::GLOBAL] };

		VkWriteDescriptorSet writeDescriptorSet[DescriptorSetsDuplicatedCount] = {};
		for (int i = 1; i < MATRIXBUFFER_COUNT; i++)
		{
			writeDescriptorSet[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet[i].pImageInfo = nullptr;
			writeDescriptorSet[i].descriptorCount = 1;
			writeDescriptorSet[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet[i].pBufferInfo = &sceneBufferInfo[i];
			for (int fb = 0; fb < rendercontext.PENDING_FRAMES; fb++)
			{
				writeDescriptorSet[i].dstSet = scene.frameData[fb].descriptorSet[i];
				vkUpdateDescriptorSets(context.device, 1, &writeDescriptorSet[i], 0, nullptr);
			}
		}

		{
			VkDescriptorImageInfo sceneImageInfo[MATERIALTEXTURE_COUNT];

			uint32_t textureIds[] = { scene.materials[0].diffuseTexture, scene.materials[0].normalTexture, scene.materials[0].roughnessTexture, scene.materials[0].ambientTexture, scene.materials[0].emissiveTexture };
			sceneImageInfo[0] = { scene.textures[0].sampler, scene.textures[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			for (int i = 1; i < MATERIALTEXTURE_COUNT; i++)
			{
				uint32_t id = textureIds[i - 1];
				sceneImageInfo[i] = { Texture::textures[id].sampler, Texture::textures[id].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			};

			int texDescIndex = DescriptorSetType::SHARED;
			VkWriteDescriptorSet writeSharedDescriptorSet{};
			writeSharedDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeSharedDescriptorSet.pBufferInfo = nullptr;
			writeSharedDescriptorSet.descriptorCount = MATERIALTEXTURE_COUNT;
			writeSharedDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeSharedDescriptorSet.pImageInfo = &sceneImageInfo[0];
			writeSharedDescriptorSet.dstSet = scene.sharedDescriptorSet;
			vkUpdateDescriptorSets(context.device, 1, &writeSharedDescriptorSet, 0, nullptr);
		}
	}

	scene.instanceCount = INSTANCE_COUNT;
	scene.cpuInstances.resize(scene.instanceCount);
	scene.cpuVelocities.resize(scene.instanceCount);

	for (uint32_t i = 0; i < scene.instanceCount; i++)
	{
		float x = ((rand() % 1000) / 1000.0f - 0.5f) * 25.0f;
		float y = ((rand() % 1000) / 1000.0f - 0.5f) * 25.0f;
		float z = ((rand() % 1000) / 1000.0f - 0.5f) * 25.0f;

		// Random velocity
		float vx = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f;
		float vy = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f;
		float vz = ((rand() % 1000) / 1000.0f - 0.5f) * 2.0f;

		glm::vec3 velocity = glm::normalize(glm::vec3(vx, vy, vz)) * 5.0f;
		scene.cpuVelocities[i].velocity = glm::vec4(velocity, 0.0f);

		glm::vec3 forward = glm::normalize(velocity);
		glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
		if (abs(glm::dot(forward, worldUp)) > 0.99f) {
			worldUp = glm::vec3(1.0f, 0.0f, 0.0f);
		}
		glm::vec3 right = glm::normalize(glm::cross(worldUp, forward));
		glm::vec3 up = glm::cross(forward, right);

		scene.cpuInstances[i].world = glm::mat4(
			glm::vec4(right, 0.0f),
			glm::vec4(up, 0.0f),
			glm::vec4(forward, 0.0f),
			glm::vec4(x, y, z, 1.0f)
		);
	}

	scene.simParams.separationDistance = 5.0f;
	scene.simParams.alignmentDistance = 10.0f;
	scene.simParams.cohesionDistance = 10.0f;
	scene.simParams.separationWeight = 1.5f;
	scene.simParams.alignmentWeight = 1.0f;
	scene.simParams.cohesionWeight = 1.0f;
	scene.simParams.maxSpeed = 15.0f;
	scene.simParams.minSpeed = 5.0f;
	scene.simParams.boidCount = scene.instanceCount;
	scene.simParams.boundaryMin = glm::vec3(-25.0f, -25.0f, -25.0f);
	scene.simParams.boundaryMax = glm::vec3(25.0f, 25.0f, 25.0f);

	VkBufferCreateInfo ssboInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	ssboInfo.size = sizeof(InstanceData) * scene.instanceCount;
	ssboInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	ssboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	for (uint32_t f = 0; f < rendercontext.PENDING_FRAMES; f++)
	{
		Buffer& ssbo = scene.instanceSSBO[f];

		vkCreateBuffer(context.device, &ssboInfo, nullptr, &ssbo.buffer);

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(context.device, ssbo.buffer, &memReq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memReq.size;
		alloc.memoryTypeIndex = context.findMemoryType(
			memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		vkAllocateMemory(context.device, &alloc, nullptr, &ssbo.memory);
		vkBindBufferMemory(context.device, ssbo.buffer, ssbo.memory, 0);
		vkMapMemory(context.device, ssbo.memory, 0, VK_WHOLE_SIZE, 0, &ssbo.data);

		memcpy(ssbo.data, scene.cpuInstances.data(), sizeof(InstanceData) * scene.instanceCount);
	}

	ssboInfo.size = sizeof(BoidVelocity) * scene.instanceCount;
	for (uint32_t f = 0; f < rendercontext.PENDING_FRAMES; f++)
	{
		Buffer& velSSBO = scene.velocitySSBO[f];

		vkCreateBuffer(context.device, &ssboInfo, nullptr, &velSSBO.buffer);

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(context.device, velSSBO.buffer, &memReq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memReq.size;
		alloc.memoryTypeIndex = context.findMemoryType(
			memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		vkAllocateMemory(context.device, &alloc, nullptr, &velSSBO.memory);
		vkBindBufferMemory(context.device, velSSBO.buffer, velSSBO.memory, 0);
		vkMapMemory(context.device, velSSBO.memory, 0, VK_WHOLE_SIZE, 0, &velSSBO.data);

		memcpy(velSSBO.data, scene.cpuVelocities.data(), sizeof(BoidVelocity) * scene.instanceCount);
	}

	VkBufferCreateInfo paramUBOInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	paramUBOInfo.size = sizeof(SimulationParams);
	paramUBOInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	paramUBOInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	for (uint32_t f = 0; f < rendercontext.PENDING_FRAMES; f++)
	{
		Buffer& paramUBO = scene.simParamsUBO[f];

		vkCreateBuffer(context.device, &paramUBOInfo, nullptr, &paramUBO.buffer);

		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(context.device, paramUBO.buffer, &memReq);

		VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		alloc.allocationSize = memReq.size;
		alloc.memoryTypeIndex = context.findMemoryType(
			memReq.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		);

		vkAllocateMemory(context.device, &alloc, nullptr, &paramUBO.memory);
		vkBindBufferMemory(context.device, paramUBO.buffer, paramUBO.memory, 0);
		vkMapMemory(context.device, paramUBO.memory, 0, VK_WHOLE_SIZE, 0, &paramUBO.data);
	}

	VkDescriptorBufferInfo instanceBufferInfo;
	for (uint32_t f = 0; f < rendercontext.PENDING_FRAMES; f++)
	{
		instanceBufferInfo = {
			scene.instanceSSBO[f].buffer,
			0,
			VK_WHOLE_SIZE
		};

		VkWriteDescriptorSet instanceWrite{};
		instanceWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		instanceWrite.dstSet = scene.frameData[f].descriptorSet[0];
		instanceWrite.dstBinding = 0;
		instanceWrite.descriptorCount = 1;
		instanceWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		instanceWrite.pBufferInfo = &instanceBufferInfo;

		vkUpdateDescriptorSets(context.device, 1, &instanceWrite, 0, nullptr);
	}

	for (uint32_t f = 0; f < rendercontext.PENDING_FRAMES; f++)
	{
		VkDescriptorBufferInfo computeBufferInfos[3];

		computeBufferInfos[0].buffer = scene.instanceSSBO[f].buffer;
		computeBufferInfos[0].offset = 0;
		computeBufferInfos[0].range = VK_WHOLE_SIZE;

		computeBufferInfos[1].buffer = scene.velocitySSBO[f].buffer;
		computeBufferInfos[1].offset = 0;
		computeBufferInfos[1].range = VK_WHOLE_SIZE;

		computeBufferInfos[2].buffer = scene.simParamsUBO[f].buffer;
		computeBufferInfos[2].offset = 0;
		computeBufferInfos[2].range = VK_WHOLE_SIZE;

		VkWriteDescriptorSet computeWrites[3] = {};

		computeWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		computeWrites[0].dstSet = scene.frameData[f].computeDescriptorSet;
		computeWrites[0].dstBinding = 0;
		computeWrites[0].descriptorCount = 1;
		computeWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		computeWrites[0].pBufferInfo = &computeBufferInfos[0];

		computeWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		computeWrites[1].dstSet = scene.frameData[f].computeDescriptorSet;
		computeWrites[1].dstBinding = 1;
		computeWrites[1].descriptorCount = 1;
		computeWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		computeWrites[1].pBufferInfo = &computeBufferInfos[1];

		computeWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		computeWrites[2].dstSet = scene.frameData[f].computeDescriptorSet;
		computeWrites[2].dstBinding = 2;
		computeWrites[2].descriptorCount = 1;
		computeWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		computeWrites[2].pBufferInfo = &computeBufferInfos[2];

		vkUpdateDescriptorSets(context.device, 3, computeWrites, 0, nullptr);
	}

	return true;
}

// tout detruire ici
void VulkanGraphicsApplication::Terminate()
{
	// destruction des buffers
	for (auto& mesh : scene.meshes) {
		for (int i = 0; i < Mesh::BufferType::BO_MAX; i++) {
			Buffer& buffer = mesh.staticBuffers[i];
			buffer.Destroy(rendercontext);
		}
	}

	// destruction des textures
	Texture::PurgeTextures();

	for (uint32_t i = 0; i < scene.textures.size(); i++) {
		scene.textures[i].Destroy(rendercontext);
	}

	// destruction des UBO
	for (uint32_t i = 0; i < MATRIXBUFFER_COUNT; i++) {
		scene.matrices.constantBuffers[i].Destroy(rendercontext);
	}

	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		scene.velocitySSBO[i].Destroy(rendercontext);
		scene.simParamsUBO[i].Destroy(rendercontext);
	}

	vkDestroyPipeline(context.device, scene.computePipeline, nullptr);
	vkDestroyPipelineLayout(context.device, scene.computePipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(context.device, scene.computeDescriptorSetLayout, nullptr);

	// destruction des descriptor sets et layouts
	for (uint32_t i = 0; i < DESCRIPTORSET_COUNT; i++) {
		vkDestroyDescriptorSetLayout(context.device, scene.descriptorSetLayout[i], nullptr);
	}
	vkDestroyDescriptorPool(context.device, scene.descriptorPool, nullptr);

	// destruction des pipelines
	vkDestroyPipeline(context.device, mainPipelineEnvMap, nullptr);
	vkDestroyPipeline(context.device, mainPipelineOpaque, nullptr);
	vkDestroyPipelineLayout(context.device, mainPipelineLayout, nullptr);

	// destruction du staging buffer
	vkDestroyBuffer(context.device, rendercontext.stagingBuffer.buffer, nullptr);
	vkUnmapMemory(context.device, rendercontext.stagingBuffer.memory);
	vkFreeMemory(context.device, rendercontext.stagingBuffer.memory, nullptr);

	vkDestroyRenderPass(context.device, rendercontext.renderPass, nullptr);

	// destruction du depth buffer
	vkDestroyImageView(context.device, colorBuffer.view, nullptr);
	vkDestroyImage(context.device, colorBuffer.image, nullptr);
	vkFreeMemory(context.device, colorBuffer.memory, nullptr);

	vkDestroyImageView(context.device, depthBuffer.view, nullptr);
	vkDestroyImage(context.device, depthBuffer.image, nullptr);
	vkFreeMemory(context.device, depthBuffer.memory, nullptr);

	vkDestroyFramebuffer(context.device, context.framebuffer, nullptr);
	for (uint32_t i = 0; i < context.swapchainImageCount; i++)
	{
		vkDestroyImageView(context.device, context.swapchainImages[i].view, nullptr);
	}

	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		vkDestroyFence(context.device, rendercontext.mainFences[i], nullptr);
	}

	// note: detruire le command pool detruit automatiquement les command buffers
	for (uint32_t i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		vkDestroyCommandPool(context.device, rendercontext.mainCommandPool[i], nullptr);
		vkDestroySemaphore(context.device, context.renderSemaphores[i], nullptr);
		scene.instanceSSBO[i].Destroy(rendercontext);
	}
	for (uint32_t i = 0; i < context.swapchainImageCount; i++) {
		vkDestroySemaphore(context.device, context.presentSemaphores[i], nullptr);
	}
}

bool VulkanGraphicsApplication::Begin()
{
	uint64_t timeout = UINT64_MAX;
	VkResult res;
	res = vkWaitForFences(context.device, 1, &rendercontext.mainFences[rendercontext.currentFrame], VK_TRUE, timeout);
	vkResetFences(context.device, 1, &rendercontext.mainFences[rendercontext.currentFrame]);

	vkResetCommandPool(context.device, rendercontext.mainCommandPool[rendercontext.currentFrame], VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT);

	DEBUG_CHECK_VK(vkAcquireNextImageKHR(context.device, context.swapchain, timeout, context.presentSemaphores[context.semaphoreIndex], VK_NULL_HANDLE, &m_imageIndex));

	return true;
}

bool VulkanGraphicsApplication::End()
{
	uint64_t timeout = UINT64_MAX;

	VkSubmitInfo submitInfo = {};
	uint32_t stageMask[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	submitInfo.pWaitDstStageMask = stageMask;
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &rendercontext.mainCommandBuffers[rendercontext.currentFrame];

	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &context.presentSemaphores[context.semaphoreIndex];

	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &context.renderSemaphores[context.semaphoreIndex];
	vkQueueSubmit(rendercontext.graphicsQueue, 1, &submitInfo, rendercontext.mainFences[rendercontext.currentFrame]);

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &context.renderSemaphores[context.semaphoreIndex];
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &context.swapchain;
	presentInfo.pImageIndices = &m_imageIndex;
	DEBUG_CHECK_VK(vkQueuePresentKHR(rendercontext.presentQueue, &presentInfo));

	context.semaphoreIndex++;
	context.semaphoreIndex = context.semaphoreIndex % context.swapchainImageCount;

	m_frame++;
	rendercontext.currentFrame = m_frame % rendercontext.PENDING_FRAMES;

	return true;
}

float mouseSpeed = 1.f;
float acceleration = 10.f;
float damping = 0.2f;
float maxSpeed = 1.f;
float fastCoef = 10.f;
glm::vec3 moveSpeed(0.0f);

static glm::vec3 mouseDelta;
static glm::dvec3 currentMouse;
static glm::vec3 prevMouse;
static bool ballEnabled = false;
static bool moveEnabled = false;

bool VulkanGraphicsApplication::Update()
{
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	glfwGetFramebufferSize(window, &width, &height);
	while (width == 0 || height == 0)
	{
		glfwWaitEvents();
		glfwGetFramebufferSize(window, &width, &height);
	}
	static double previousTime = glfwGetTime() - 0.017f;
	static double currentTime = glfwGetTime();

	currentTime = glfwGetTime();
	double deltaTime = currentTime - previousTime;
	std::cout << "[" << m_frame << "] frame time = " << deltaTime * 1000.0 << " ms [" << 1.0 / deltaTime << " fps]" << std::endl;
	previousTime = currentTime;

	float time = (float)currentTime;

	glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);

	static float currentX = 0.f;
	static float currentY = 0.f;

	static glm::vec3 cam_forward = glm::vec3(0.f, 0.f, 1.f);
	static glm::vec3 target = glm::vec3(0.f);
	static glm::vec3 camPos = cam_forward * 10.f;

	if (ballEnabled)
	{
		const float lookSpeed = 45.f * deltaTime;
		currentX += mouseDelta.x * lookSpeed;
		currentY += mouseDelta.y * lookSpeed;

		if (currentY > 89.f)
			currentY = 89.f;

		if (currentY < -89.f)
			currentY = -89.f;

		cam_forward.z = cos(glm::radians(currentX)) * cos(glm::radians(currentY));
		cam_forward.y = sin(glm::radians(currentY));
		cam_forward.x = sin(glm::radians(currentX)) * cos(glm::radians(currentY));
	}

	glm::vec3 forward = cam_forward;
	glm::vec3 right = glm::cross(up, forward);
	glm::vec3 newUp = glm::cross(forward, right);

	glm::vec3 delta = glm::vec3(0.f);
	if (moveEnabled || mouseDelta.z != 0.f)
	{
		delta = mouseDelta * mouseSpeed;
	}
	delta.z *= fastCoef;

	glm::vec3 accel(0.0f);
	accel += delta * (float)deltaTime;
	if (accel != glm::vec3(0.0f)) {
		moveSpeed += accel * acceleration * (float)deltaTime;
		if (glm::length(moveSpeed) > maxSpeed) {
			moveSpeed = glm::normalize(moveSpeed) * maxSpeed;
		}
	}
	else {
		moveSpeed -= moveSpeed * std::min((1.0f / damping) * (float)deltaTime, 1.0f);
	}

	camPos += (right * moveSpeed.x + newUp * moveSpeed.y + forward * moveSpeed.z);

	target = camPos - cam_forward;
	scene.matrices.view = glm::lookAt(camPos, target, up);

	mouseDelta = glm::vec3(0.f);

	scene.simParams.deltaTime = (float)deltaTime;

	return true;
}

bool VulkanGraphicsApplication::Display()
{
	uint32_t f = rendercontext.currentFrame;
	
	Buffer& paramUBO = scene.simParamsUBO[f];
	VkMappedMemoryRange mappedRange = {};
	mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mappedRange.memory = paramUBO.memory;
	mappedRange.size = sizeof(SimulationParams);
	DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	memcpy(paramUBO.data, &scene.simParams, sizeof(SimulationParams));
	DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));

	char* viewData = (char*)&scene.matrices.view;
	Buffer& uboVP = scene.matrices.constantBuffers[MatrixBufferUsageType::GLOBAL];
	mappedRange.memory = uboVP.memory;
	mappedRange.size = sizeof(glm::mat4) * 2;
	DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	memcpy(uboVP.data, viewData, sizeof(glm::mat4) * 2);
	DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));

	// "begin" du command buffer
	VkCommandBuffer commandBuffer = rendercontext.mainCommandBuffers[rendercontext.currentFrame];
	VkCommandBufferBeginInfo cmdBeginInfo = {};
	cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &cmdBeginInfo);

#ifdef RUN_COMPUTE
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, scene.computePipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
		scene.computePipelineLayout, 0, 1, &scene.frameData[f].computeDescriptorSet, 0, nullptr);

	uint32_t workgroupCount = (scene.instanceCount + 255) / 256;
	vkCmdDispatch(commandBuffer, workgroupCount, 1, 1);

	VkBufferMemoryBarrier bufferBarrier = {};
	bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	bufferBarrier.buffer = scene.instanceSSBO[f].buffer;
	bufferBarrier.offset = 0;
	bufferBarrier.size = VK_WHOLE_SIZE;

	vkCmdPipelineBarrier(
		commandBuffer,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
		0,
		0, nullptr,
		1, &bufferBarrier,
		0, nullptr
	);
#endif

	VkImageView framebufferAttachments[2] = { context.swapchainImages[m_imageIndex].view, depthBuffer.view };
	VkRenderPassAttachmentBeginInfo renderPassAttachmentBeginInfo = {};
	renderPassAttachmentBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
	renderPassAttachmentBeginInfo.attachmentCount = 2;
	renderPassAttachmentBeginInfo.pAttachments = framebufferAttachments;

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.clearValueCount = 1;
	VkClearValue clearValues[2];
	clearValues[0].color = { 0.1f, 0.1f, 0.15f, 1.0f };
	clearValues[1].depthStencil = { 1.0f, 0 };

	renderPassBeginInfo.framebuffer = context.framebuffer;
	renderPassBeginInfo.renderPass = rendercontext.renderPass;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.renderArea.extent = context.swapchainExtent;
	renderPassBeginInfo.pNext = &renderPassAttachmentBeginInfo;
	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mainPipelineLayout, DescriptorSetType::DYNAMIC, 2, &scene.frameData[rendercontext.currentFrame].descriptorSet[0], 0, nullptr);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mainPipelineLayout, DescriptorSetType::SHARED, 1, &scene.sharedDescriptorSet, 0, nullptr);

	VkDeviceSize offsets[] = { 0 };

	// "Passe" Opaques & Cutouts & Environnement
	{
		VkBuffer buffers[] = { scene.meshes[0].staticBuffers[Mesh::BufferType::VBO].buffer };
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
		vkCmdBindIndexBuffer(commandBuffer, scene.meshes[0].staticBuffers[Mesh::BufferType::IBO].buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mainPipelineOpaque);
		vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indexCount, scene.instanceCount, 0, 0, 0);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mainPipelineEnvMap);
		vkCmdDraw(commandBuffer, 4, 1, 0, 0);
	}

	vkCmdEndRenderPass(commandBuffer);

	vkEndCommandBuffer(commandBuffer);
	return true;
}

void scrollCallback(GLFWwindow* window, double delta_x, double delta_y)
{
	prevMouse.z = float(currentMouse.z);
	currentMouse.z += delta_y;
	mouseDelta.z = prevMouse.z - float(currentMouse.z);
}

void cursorCallback(GLFWwindow* window, double pos_x, double pos_y)
{
	prevMouse = glm::vec3(currentMouse.x, currentMouse.y, 0.0);
	currentMouse.x = pos_x; currentMouse.y = pos_y;

	mouseDelta = glm::vec3(prevMouse.x - currentMouse.x, -(prevMouse.y - currentMouse.y), mouseDelta.z);
}

void mouseCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (action == GLFW_PRESS)
	{
		prevMouse = glm::vec3(currentMouse.x, currentMouse.y, 0.0);

		glfwGetCursorPos(window, &currentMouse.x, &currentMouse.y);

		if (button == GLFW_MOUSE_BUTTON_LEFT)
			ballEnabled = true;

		if (button == GLFW_MOUSE_BUTTON_RIGHT)
			moveEnabled = true;
	}

	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
		ballEnabled = false;
	if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE)
		moveEnabled = false;
}

int main(void)
{
	/* Initialize the library */
	if (!glfwInit())
		return -1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	VulkanGraphicsApplication app;

	/* Create a windowed mode window and its OpenGL context */
	app.window = glfwCreateWindow(1024, 768, APP_NAME, NULL, NULL);
	if (!app.window)
	{
		glfwTerminate();
		return -1;
	}

	glfwSetMouseButtonCallback(app.window, mouseCallback);
	glfwSetCursorPosCallback(app.window, cursorCallback);
	glfwSetScrollCallback(app.window, scrollCallback);

	app.Initialize(APP_NAME);

	glfwGetCursorPos(app.window, &currentMouse.x, &currentMouse.y);

	/* Loop until the user closes the window */
	while (!glfwWindowShouldClose(app.window))
	{
		/* Render here */
		app.Run();

		/* Poll for and process events */
		glfwPollEvents();
	}

	app.Shutdown();

	glfwTerminate();
	return 0;
}