
#include "pbr_renderer.h"

void Renderer::renderNode(vkglTF::Node* node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode)
{
	if (node->mesh)
	{
		// Render mesh primitives
		for (vkglTF::Primitive* primitive : node->mesh->primitives)
		{
			if (primitive->material.alphaMode == alphaMode)
			{

				VkPipeline pipeline = VK_NULL_HANDLE;
				switch (alphaMode)
				{
				case vkglTF::Material::ALPHAMODE_OPAQUE:
				case vkglTF::Material::ALPHAMODE_MASK:
					pipeline = primitive->material.doubleSided ? pipelineSet.pbrDoubleSided : pipelineSet.pbr;
					break;
				case vkglTF::Material::ALPHAMODE_BLEND:
					pipeline = pipelineSet.pbrAlphaBlend;
					break;
				}

				if (pipeline != boundPipeline)
				{
					vkCmdBindPipeline(commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
					boundPipeline = pipeline;
				}

				const std::vector<VkDescriptorSet> descriptorsets = {
					descriptorSets[cbIndex].scene,
					primitive->material.descriptorSet,
					node->mesh->uniformBuffer.descriptorSet,
				};
				vkCmdBindDescriptorSets(commandBuffers[cbIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, static_cast<uint32_t>(descriptorsets.size()), descriptorsets.data(), 0, NULL);

				// Pass material parameters as push constants
				PushConstBlockMaterial pushConstBlockMaterial{};
				pushConstBlockMaterial.emissiveFactor = primitive->material.emissiveFactor;
				// To save push constant space, availabilty and texture coordiante set are combined
				// -1 = texture not used for this material, >= 0 texture used and index of texture coordinate set
				pushConstBlockMaterial.colorTextureSet = primitive->material.baseColorTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
				pushConstBlockMaterial.normalTextureSet = primitive->material.normalTexture != nullptr ? primitive->material.texCoordSets.normal : -1;
				pushConstBlockMaterial.occlusionTextureSet = primitive->material.occlusionTexture != nullptr ? primitive->material.texCoordSets.occlusion : -1;
				pushConstBlockMaterial.emissiveTextureSet = primitive->material.emissiveTexture != nullptr ? primitive->material.texCoordSets.emissive : -1;
				pushConstBlockMaterial.alphaMask = static_cast<float>(primitive->material.alphaMode == vkglTF::Material::ALPHAMODE_MASK);
				pushConstBlockMaterial.alphaMaskCutoff = primitive->material.alphaCutoff;

				// TODO: glTF specs states that metallic roughness should be preferred, even if specular glosiness is present

				if (primitive->material.pbrWorkflows.metallicRoughness)
				{
					// Metallic roughness workflow
					pushConstBlockMaterial.workflow = static_cast<float>(PBR_WORKFLOW_METALLIC_ROUGHNESS);
					pushConstBlockMaterial.baseColorFactor = primitive->material.baseColorFactor;
					pushConstBlockMaterial.metallicFactor = primitive->material.metallicFactor;
					pushConstBlockMaterial.roughnessFactor = primitive->material.roughnessFactor;
					pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitive->material.metallicRoughnessTexture != nullptr ? primitive->material.texCoordSets.metallicRoughness : -1;
					pushConstBlockMaterial.colorTextureSet = primitive->material.baseColorTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
				}

				if (primitive->material.pbrWorkflows.specularGlossiness)
				{
					// Specular glossiness workflow
					pushConstBlockMaterial.workflow = static_cast<float>(PBR_WORKFLOW_SPECULAR_GLOSINESS);
					pushConstBlockMaterial.PhysicalDescriptorTextureSet = primitive->material.extension.specularGlossinessTexture != nullptr ? primitive->material.texCoordSets.specularGlossiness : -1;
					pushConstBlockMaterial.colorTextureSet = primitive->material.extension.diffuseTexture != nullptr ? primitive->material.texCoordSets.baseColor : -1;
					pushConstBlockMaterial.diffuseFactor = primitive->material.extension.diffuseFactor;
					pushConstBlockMaterial.specularFactor = glm::vec4(primitive->material.extension.specularFactor, 1.0f);
				}

				vkCmdPushConstants(commandBuffers[cbIndex], pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlockMaterial), &pushConstBlockMaterial);

				if (primitive->hasIndices)
				{
					vkCmdDrawIndexed(commandBuffers[cbIndex], primitive->indexCount, 1, primitive->firstIndex, 0, 0);
				}
				else
				{
					vkCmdDraw(commandBuffers[cbIndex], primitive->vertexCount, 1, 0, 0);
				}
			}
		}

	};
	for (auto child : node->children)
	{
		renderNode(child, cbIndex, alphaMode);
	}
}

void Renderer::recordCommandBuffers()
{
	VkCommandBufferBeginInfo cmdBufferBeginInfo{};
	cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	VkClearValue clearValues[3];
	if (settings.multiSampling)
	{
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };
	}
	else
	{
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
		clearValues[1].depthStencil = { 1.0f, 0 };
	}

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = renderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = width;
	renderPassBeginInfo.renderArea.extent.height = height;
	renderPassBeginInfo.clearValueCount = settings.multiSampling ? 3 : 2;
	renderPassBeginInfo.pClearValues = clearValues;

	for (uint32_t i = 0; i < commandBuffers.size(); ++i)
	{
		renderPassBeginInfo.framebuffer = frameBuffers[i];

		VkCommandBuffer currentCB = commandBuffers[i];

		VK_CHECK_RESULT(vkBeginCommandBuffer(currentCB, &cmdBufferBeginInfo));
		vkCmdBeginRenderPass(currentCB, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport{};
		viewport.width = (float)width;
		viewport.height = (float)height;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(currentCB, 0, 1, &viewport);

		VkRect2D scissor{};
		scissor.extent = { width, height };
		vkCmdSetScissor(currentCB, 0, 1, &scissor);

		VkDeviceSize offsets[1] = { 0 };

		if (displayBackground)
		{
			vkCmdBindDescriptorSets(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i].skybox, 0, nullptr);
			vkCmdBindPipeline(currentCB, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineSet.skybox);
			modelSet.skybox.draw(currentCB);
		}

		vkglTF::Model& model = modelSet.scene;

		vkCmdBindVertexBuffers(currentCB, 0, 1, &model.vertices.buffer, offsets);
		if (model.indices.buffer != VK_NULL_HANDLE)
		{
			vkCmdBindIndexBuffer(currentCB, model.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		}

		boundPipeline = VK_NULL_HANDLE;

		// Opaque primitives first
		for (auto node : model.nodes)
		{
			renderNode(node, i, vkglTF::Material::ALPHAMODE_OPAQUE);
		}
		// Alpha masked primitives
		for (auto node : model.nodes)
		{
			renderNode(node, i, vkglTF::Material::ALPHAMODE_MASK);
		}
		// Transparent primitives
		// TODO: Correct depth sorting
		for (auto node : model.nodes)
		{
			renderNode(node, i, vkglTF::Material::ALPHAMODE_BLEND);
		}

		// User interface
		ui->draw(currentCB);

		vkCmdEndRenderPass(currentCB);
		VK_CHECK_RESULT(vkEndCommandBuffer(currentCB));
	}
}

void Renderer::prepare()
{
	VulkanExampleBase::prepare();

	camera.init(width, height);

	waitFences.resize(renderAhead);
	presentCompleteSemaphores.resize(renderAhead);
	renderCompleteSemaphores.resize(renderAhead);
	commandBuffers.resize(swapchain.imageCount);
	uniformBuffers.resize(swapchain.imageCount);
	descriptorSets.resize(swapchain.imageCount);
	// Command buffer execution fences
	for (auto& waitFence : waitFences)
	{
		VkFenceCreateInfo fenceCI{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT };
		VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceCI, nullptr, &waitFence));
	}
	// Queue ordering semaphores
	for (auto& semaphore : presentCompleteSemaphores)
	{
		VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
		VK_CHECK_RESULT(vkCreateSemaphore(logicalDevice, &semaphoreCI, nullptr, &semaphore));
	}
	for (auto& semaphore : renderCompleteSemaphores)
	{
		VkSemaphoreCreateInfo semaphoreCI{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0 };
		VK_CHECK_RESULT(vkCreateSemaphore(logicalDevice, &semaphoreCI, nullptr, &semaphore));
	}
	// Command buffers
	{
		VkCommandBufferAllocateInfo cmdBufAllocateInfo{};
		cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		cmdBufAllocateInfo.commandPool = cmdPool;
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmdBufAllocateInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
		VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufAllocateInfo, commandBuffers.data()));
	}

	loadAssets();
	generateBRDFLUT();
	generateCubemaps();
	prepareUniformBuffers();
	setupDescriptors();
	preparePipelines();

	ui = new UI(device, renderPass, queue, pipelineCache, settings.sampleCount);
	updateOverlay();

	recordCommandBuffers();

	prepared = true;
}

void Renderer::loadAssets()
{
	struct stat info;
	if (stat(ASSETS_PATH.c_str(), &info) != 0)
	{
		std::string msg = "Could not locate asset path in \"" + ASSETS_PATH + "\".\nMake sure binary is run from correct relative directory!";
		std::cerr << msg << std::endl;
		exit(-1);
	}
	readDirectory(ENVIRONMENT_PATH, "*.ktx", environments, false);

	textureSet.empty.loadFromFile(ASSETS_PATH + "Textures/empty.ktx", VK_FORMAT_R8G8B8A8_UNORM, device, queue);

	std::string sceneFile = MODEL_PATH + "DamagedHelmet/glTF-Embedded/DamagedHelmet.gltf";
	std::string envMapFile = ENVIRONMENT_PATH + "papermill.ktx";
	for (size_t i = 0; i < args.size(); i++)
	{
		if ((std::string(args[i]).find(".gltf") != std::string::npos) || (std::string(args[i]).find(".glb") != std::string::npos))
		{
			std::ifstream file(args[i]);
			if (file.good())
			{
				sceneFile = args[i];
			}
			else
			{
				std::cout << "could not load \"" << args[i] << "\"" << std::endl;
			}
		}
		if (std::string(args[i]).find(".ktx") != std::string::npos)
		{
			std::ifstream file(args[i]);
			if (file.good())
			{
				envMapFile = args[i];
			}
			else
			{
				std::cout << "could not load \"" << args[i] << "\"" << std::endl;
			}
		}
	}

	loadScene(sceneFile.c_str());
	modelSet.skybox.loadFromFile(MODEL_PATH + "Box/glTF-Embedded/Box.gltf", device, queue);

	loadEnvironment(envMapFile.c_str());
}

void Renderer::loadScene(std::string filename)
{
	std::cout << "Loading scene from " << filename << std::endl;
	modelSet.scene.destroy(logicalDevice);
	animationIndex = 0;
	animationTimer = 0.0f;

	auto startTm = std::chrono::high_resolution_clock::now();
	modelSet.scene.loadFromFile(filename, device, queue);

	auto loadTm = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - startTm).count();
	std::cout << "Loading took " << loadTm << " ms" << std::endl;

	camera.reset();
}

void Renderer::loadEnvironment(std::string filename) {
	std::cout << "Loading environment from " << filename << std::endl;
	if (textureSet.environmentCube.image)
	{
		textureSet.environmentCube.destroy();
		textureSet.irradianceCube.destroy();
		textureSet.prefilteredCube.destroy();
	}
	textureSet.environmentCube.loadFromFile(filename, VK_FORMAT_R16G16B16A16_SFLOAT, device, queue);
	generateCubemaps();
}
//Generate a BRDF integration map storing roughness/NdotV as a look-up-table
void Renderer::generateBRDFLUT()
{
	auto tStart = std::chrono::high_resolution_clock::now();

	const VkFormat format = VK_FORMAT_R16G16_SFLOAT;
	const int32_t dim = 512;

	// Image
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = format;
	imageCI.extent.width = dim;
	imageCI.extent.height = dim;
	imageCI.extent.depth = 1;
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	device->createImage(imageCI, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureSet.lutBrdf.image, textureSet.lutBrdf.deviceMemory);

	// View
	VkImageViewCreateInfo viewCI{};
	viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewCI.format = format;
	viewCI.subresourceRange = {};
	viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewCI.subresourceRange.levelCount = 1;
	viewCI.subresourceRange.layerCount = 1;
	viewCI.image = textureSet.lutBrdf.image;
	VK_CHECK_RESULT(vkCreateImageView(logicalDevice, &viewCI, nullptr, &textureSet.lutBrdf.imageView));

	// Sampler
	VkSamplerCreateInfo samplerCI{};
	samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerCI.magFilter = VK_FILTER_LINEAR;
	samplerCI.minFilter = VK_FILTER_LINEAR;
	samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerCI.minLod = 0.0f;
	samplerCI.maxLod = 1.0f;
	samplerCI.maxAnisotropy = 1.0f;
	samplerCI.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	VK_CHECK_RESULT(vkCreateSampler(logicalDevice, &samplerCI, nullptr, &textureSet.lutBrdf.sampler));

	// FB, Att, RP, Pipe, etc.
	VkAttachmentDescription attDesc{};
	// Color attachment
	attDesc.format = format;
	attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
	attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attDesc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

	VkSubpassDescription subpassDescription{};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;

	// Use subpass dependencies for layout transitions
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

	// Create the actual renderpass
	VkRenderPassCreateInfo renderPassCI{};
	renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassCI.attachmentCount = 1;
	renderPassCI.pAttachments = &attDesc;
	renderPassCI.subpassCount = 1;
	renderPassCI.pSubpasses = &subpassDescription;
	renderPassCI.dependencyCount = 2;
	renderPassCI.pDependencies = dependencies.data();

	VkRenderPass renderpass;
	VK_CHECK_RESULT(vkCreateRenderPass(logicalDevice, &renderPassCI, nullptr, &renderpass));

	VkFramebufferCreateInfo framebufferCI{};
	framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferCI.renderPass = renderpass;
	framebufferCI.attachmentCount = 1;
	framebufferCI.pAttachments = &textureSet.lutBrdf.imageView;
	framebufferCI.width = dim;
	framebufferCI.height = dim;
	framebufferCI.layers = 1;

	VkFramebuffer framebuffer;
	VK_CHECK_RESULT(vkCreateFramebuffer(logicalDevice, &framebufferCI, nullptr, &framebuffer));

	// Desriptors
	VkDescriptorSetLayout descriptorsetlayout;
	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
	descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	VK_CHECK_RESULT(vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

	// Pipeline layout
	VkPipelineLayout pipelinelayout;
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = 1;
	pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
	VK_CHECK_RESULT(vkCreatePipelineLayout(logicalDevice, &pipelineLayoutCI, nullptr, &pipelinelayout));

	// Pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.front = depthStencilStateCI.back;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
	emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = pipelinelayout;
	pipelineCI.renderPass = renderpass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &emptyInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = 2;
	pipelineCI.pStages = shaderStages.data();

	// Look-up-table (from BRDF) pipeline		
	shaderStages = {
		loadShader(logicalDevice, "genbrdflut.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		loadShader(logicalDevice, "genbrdflut.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	VkPipeline pipeline;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	for (auto shaderStage : shaderStages)
	{
		vkDestroyShaderModule(logicalDevice, shaderStage.module, nullptr);
	}

	// Render
	VkClearValue clearValues[1];
	clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };

	VkRenderPassBeginInfo renderPassBeginInfo{};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = renderpass;
	renderPassBeginInfo.renderArea.extent.width = dim;
	renderPassBeginInfo.renderArea.extent.height = dim;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;
	renderPassBeginInfo.framebuffer = framebuffer;

	VkCommandBuffer cmdBuf = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	device->beginCommandBuffer(cmdBuf);
	vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport{};
	viewport.width = (float)dim;
	viewport.height = (float)dim;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;

	VkRect2D scissor{};
	scissor.extent.width = dim;
	scissor.extent.height = dim;

	vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
	vkCmdSetScissor(cmdBuf, 0, 1, &scissor);
	vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	vkCmdDraw(cmdBuf, 3, 1, 0, 0);
	vkCmdEndRenderPass(cmdBuf);
	device->flushCommandBuffer(cmdBuf, queue);

	vkQueueWaitIdle(queue);

	vkDestroyPipeline(logicalDevice, pipeline, nullptr);
	vkDestroyPipelineLayout(logicalDevice, pipelinelayout, nullptr);
	vkDestroyRenderPass(logicalDevice, renderpass, nullptr);
	vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
	vkDestroyDescriptorSetLayout(logicalDevice, descriptorsetlayout, nullptr);

	textureSet.lutBrdf.descriptor.imageView = textureSet.lutBrdf.imageView;
	textureSet.lutBrdf.descriptor.sampler = textureSet.lutBrdf.sampler;
	textureSet.lutBrdf.descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	textureSet.lutBrdf.device = device;

	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	std::cout << "Generating BRDF LUT took " << tDiff << " ms" << std::endl;
}
//Offline generation for the cup maps used for PBR lighting
void Renderer::generateCubemaps()
{
	enum Target { IRRADIANCE = 0, PREFILTEREDENV = 1 };

	std::vector<glm::mat4> cubeMatrices = {
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			glm::rotate(glm::mat4(1.0f), glm::radians(180.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
	};

	for (uint32_t target = 0; target < PREFILTEREDENV + 1; target++)
	{

		vulkan::TextureCubeMap cubemap;

		cubemap.device = device;

		auto startTm = std::chrono::high_resolution_clock::now();

		VkFormat format;
		int32_t dim;

		switch (target)
		{
		case IRRADIANCE:
			format = VK_FORMAT_R32G32B32A32_SFLOAT;
			dim = 64;
			break;
		case PREFILTEREDENV:
			format = VK_FORMAT_R16G16B16A16_SFLOAT;
			dim = 512;
			break;
		};

		const uint32_t numMips = static_cast<uint32_t>(floor(log2(dim))) + 1;

		// Create target cubemap
		cubemap.initImage(dim, numMips, format);

		// FB, Att, RP, Pipe, etc.
		VkAttachmentDescription attDesc{};
		// Color attachment
		attDesc.format = format;
		attDesc.samples = VK_SAMPLE_COUNT_1_BIT;
		attDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attDesc.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription{};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;

		// Use subpass dependencies for layout transitions
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

		// Renderpass
		VkRenderPassCreateInfo renderPassCI{};
		renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCI.attachmentCount = 1;
		renderPassCI.pAttachments = &attDesc;
		renderPassCI.subpassCount = 1;
		renderPassCI.pSubpasses = &subpassDescription;
		renderPassCI.dependencyCount = 2;
		renderPassCI.pDependencies = dependencies.data();
		VkRenderPass renderpass;
		VK_CHECK_RESULT(vkCreateRenderPass(logicalDevice, &renderPassCI, nullptr, &renderpass));

		struct Offscreen
		{
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
			VkFramebuffer framebuffer;
		} offscreen;

		// Create offscreen framebuffer
		{
			// Image
			VkImageCreateInfo imageCI{};
			imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCI.imageType = VK_IMAGE_TYPE_2D;
			imageCI.format = format;
			imageCI.extent.width = dim;
			imageCI.extent.height = dim;
			imageCI.extent.depth = 1;
			imageCI.mipLevels = 1;
			imageCI.arrayLayers = 1;
			imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCI.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
			imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			device->createImage(imageCI, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, offscreen.image, offscreen.memory);

			// View
			VkImageSubresourceRange recourceRange = { VK_IMAGE_ASPECT_COLOR_BIT , 0, 1, 0, 1 };
			VkImageViewCreateInfo viewCI{};
			viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewCI.format = format;
			viewCI.flags = 0;
			viewCI.subresourceRange = recourceRange;
			viewCI.image = offscreen.image;
			VK_CHECK_RESULT(vkCreateImageView(logicalDevice, &viewCI, nullptr, &offscreen.view));

			// Framebuffer
			VkFramebufferCreateInfo framebufferCI{};
			framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCI.renderPass = renderpass;
			framebufferCI.attachmentCount = 1;
			framebufferCI.pAttachments = &offscreen.view;
			framebufferCI.width = dim;
			framebufferCI.height = dim;
			framebufferCI.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(logicalDevice, &framebufferCI, nullptr, &offscreen.framebuffer));

			VkCommandBuffer layoutCmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			device->beginCommandBuffer(layoutCmd);
			device->recordTransitionImageLayout(layoutCmd, offscreen.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, recourceRange);
			device->flushCommandBuffer(layoutCmd, queue, true);
		}

		// Descriptors
		VkDescriptorSetLayout descriptorsetlayout;
		VkDescriptorSetLayoutBinding setLayoutBinding = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorSetLayoutCI.pBindings = &setLayoutBinding;
		descriptorSetLayoutCI.bindingCount = 1;
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorsetlayout));

		// Descriptor Pool
		VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };
		VkDescriptorPoolCreateInfo descriptorPoolCI{};
		descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolCI.poolSizeCount = 1;
		descriptorPoolCI.pPoolSizes = &poolSize;
		descriptorPoolCI.maxSets = 2;
		VkDescriptorPool descriptorpool;
		VK_CHECK_RESULT(vkCreateDescriptorPool(logicalDevice, &descriptorPoolCI, nullptr, &descriptorpool));

		// Descriptor sets
		VkDescriptorSet descriptorset;
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorpool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorsetlayout;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &descriptorset));
		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = descriptorset;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pImageInfo = &textureSet.environmentCube.descriptor;
		vkUpdateDescriptorSets(logicalDevice, 1, &writeDescriptorSet, 0, nullptr);

		struct PushBlockIrradiance
		{
			glm::mat4 mvp;
			float deltaPhi = (2.0f * float(M_PI)) / 180.0f;
			float deltaTheta = (0.5f * float(M_PI)) / 64.0f;
		} pushBlockIrradiance;

		struct PushBlockPrefilterEnv
		{
			glm::mat4 mvp;
			float roughness;
			uint32_t numSamples = 32u;
		} pushBlockPrefilterEnv;

		// Pipeline layout
		VkPipelineLayout pipelinelayout;
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		switch (target)
		{
		case IRRADIANCE:
			pushConstantRange.size = sizeof(PushBlockIrradiance);
			break;
		case PREFILTEREDENV:
			pushConstantRange.size = sizeof(PushBlockPrefilterEnv);
			break;
		};

		VkPipelineLayoutCreateInfo pipelineLayoutCI{};
		pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipelineLayoutCI.setLayoutCount = 1;
		pipelineLayoutCI.pSetLayouts = &descriptorsetlayout;
		pipelineLayoutCI.pushConstantRangeCount = 1;
		pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(logicalDevice, &pipelineLayoutCI, nullptr, &pipelinelayout));

		// Pipeline
		// Vertex input state
		VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
		VkVertexInputAttributeDescription vertexInputAttribute = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };

		VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
		vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInputStateCI.vertexBindingDescriptionCount = 1;
		vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
		vertexInputStateCI.vertexAttributeDescriptionCount = 1;
		vertexInputStateCI.pVertexAttributeDescriptions = &vertexInputAttribute;
		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
		inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		//Rasterization
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
		rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		rasterizationStateCI.lineWidth = 1.0f;
		//Blend
		VkPipelineColorBlendAttachmentState blendAttachmentState{};
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blendAttachmentState.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
		colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;

		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
		depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencilStateCI.depthTestEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		depthStencilStateCI.front = depthStencilStateCI.back;
		depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

		VkPipelineViewportStateCreateInfo viewportStateCI{};
		viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportStateCI.viewportCount = 1;
		viewportStateCI.scissorCount = 1;

		VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
		multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI{};
		dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
		dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI{};
		pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCI.layout = pipelinelayout;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pVertexInputState = &vertexInputStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = 2;
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.renderPass = renderpass;

		shaderStages[0] = loadShader(logicalDevice, "filtercube.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		switch (target)
		{
		case IRRADIANCE:
			shaderStages[1] = loadShader(logicalDevice, "irradiancecube.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			break;
		case PREFILTEREDENV:
			shaderStages[1] = loadShader(logicalDevice, "prefilterenvmap.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			break;
		};
		VkPipeline pipeline;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
		for (auto shaderStage : shaderStages)
		{
			vkDestroyShaderModule(logicalDevice, shaderStage.module, nullptr);
		}

		// Render cubemap
		VkClearValue clearValues[1];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };

		VkRenderPassBeginInfo renderPassBeginInfo{};
		renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		renderPassBeginInfo.renderPass = renderpass;
		renderPassBeginInfo.framebuffer = offscreen.framebuffer;
		renderPassBeginInfo.renderArea.extent.width = dim;
		renderPassBeginInfo.renderArea.extent.height = dim;
		renderPassBeginInfo.clearValueCount = 1;
		renderPassBeginInfo.pClearValues = clearValues;

		VkCommandBuffer cmdBuf = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		device->beginCommandBuffer(cmdBuf);

		VkViewport viewport{};
		viewport.width = (float)dim;
		viewport.height = (float)dim;
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;

		VkRect2D scissor{};
		scissor.extent.width = dim;
		scissor.extent.height = dim;

		VkImageSubresourceRange subresourceRange{ VK_IMAGE_ASPECT_COLOR_BIT , 0, numMips, 0, 6 };

		// Change image layout for all cubemap faces to transfer destination
		{
			device->beginCommandBuffer(cmdBuf);
			device->recordTransitionImageLayout(cmdBuf, cubemap.image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);
			device->flushCommandBuffer(cmdBuf, queue, false);
		}

		for (uint32_t m = 0; m < numMips; m++)
		{
			for (uint32_t f = 0; f < 6; f++)
			{

				device->beginCommandBuffer(cmdBuf);

				viewport.width = static_cast<float>(dim * std::pow(0.5f, m));
				viewport.height = static_cast<float>(dim * std::pow(0.5f, m));
				vkCmdSetViewport(cmdBuf, 0, 1, &viewport);
				vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

				// Render scene from cube face's point of view
				vkCmdBeginRenderPass(cmdBuf, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				// Pass parameters for current pass using a push constant block
				switch (target)
				{
				case IRRADIANCE:
					pushBlockIrradiance.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * cubeMatrices[f];
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockIrradiance), &pushBlockIrradiance);
					break;
				case PREFILTEREDENV:
					pushBlockPrefilterEnv.mvp = glm::perspective((float)(M_PI / 2.0), 1.0f, 0.1f, 512.0f) * cubeMatrices[f];
					pushBlockPrefilterEnv.roughness = (float)m / (float)(numMips - 1);
					vkCmdPushConstants(cmdBuf, pipelinelayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushBlockPrefilterEnv), &pushBlockPrefilterEnv);
					break;
				};

				vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
				vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelinelayout, 0, 1, &descriptorset, 0, NULL);

				VkDeviceSize offsets[1] = { 0 };

				modelSet.skybox.draw(cmdBuf);

				vkCmdEndRenderPass(cmdBuf);

				device->recordTransitionImageLayout(cmdBuf, offscreen.image, format, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

				// Copy region for transfer from framebuffer to cube face
				VkImageCopy copyRegion{};

				copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
				copyRegion.srcOffset = { 0, 0, 0 };
				copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, m, f, 1 };
				copyRegion.dstOffset = { 0, 0, 0 };
				copyRegion.extent.width = static_cast<uint32_t>(viewport.width);
				copyRegion.extent.height = static_cast<uint32_t>(viewport.height);
				copyRegion.extent.depth = 1;
				vkCmdCopyImage(
					cmdBuf,
					offscreen.image,
					VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
					cubemap.image,
					VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					1,
					&copyRegion);

				device->recordTransitionImageLayout(cmdBuf, offscreen.image, format, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 });

				device->flushCommandBuffer(cmdBuf, queue, false);
			}
		}

		{
			device->beginCommandBuffer(cmdBuf);
			device->recordTransitionImageLayout(cmdBuf, cubemap.image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange, static_cast<VkAccessFlagBits>(VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT));

			device->flushCommandBuffer(cmdBuf, queue, false);
		}


		vkDestroyRenderPass(logicalDevice, renderpass, nullptr);
		vkDestroyFramebuffer(logicalDevice, offscreen.framebuffer, nullptr);
		vkFreeMemory(logicalDevice, offscreen.memory, nullptr);
		vkDestroyImageView(logicalDevice, offscreen.view, nullptr);
		vkDestroyImage(logicalDevice, offscreen.image, nullptr);
		vkDestroyDescriptorPool(logicalDevice, descriptorpool, nullptr);
		vkDestroyDescriptorSetLayout(logicalDevice, descriptorsetlayout, nullptr);
		vkDestroyPipeline(logicalDevice, pipeline, nullptr);
		vkDestroyPipelineLayout(logicalDevice, pipelinelayout, nullptr);

		cubemap.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		cubemap.updateDescriptor();

		switch (target)
		{
		case IRRADIANCE:
			textureSet.irradianceCube = cubemap;
			break;
		case PREFILTEREDENV:
			textureSet.prefilteredCube = cubemap;
			shaderValuesParams.prefilteredCubeMipLevels = static_cast<float>(numMips);
			break;
		};

		auto endTm = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(endTm - startTm).count();
		std::cout << "Generating cube map with " << numMips << " mip levels took " << tDiff << " ms" << std::endl;
	}
}
//Prepare and initialize uniform buffers containing shader parameters
void Renderer::prepareUniformBuffers()
{
	for (auto& uniformBuffer : uniformBuffers)
	{
		uniformBuffer.scene.create(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(sceneUBO));
		uniformBuffer.skybox.create(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(skyboxUBO));
		uniformBuffer.params.create(device, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, sizeof(shaderValuesParams));
	}
	updateUniformBuffers();
}

void Renderer::setupDescriptors()
{
	/*
		Descriptor Pool
	*/
	uint32_t imageSamplerCount = 0;
	uint32_t materialCount = 0;
	uint32_t meshCount = 0;

	// Environment samplers (radiance, irradiance, brdf lut)
	imageSamplerCount += 3;

	std::vector<vkglTF::Model*> modellist = { &modelSet.skybox, &modelSet.scene };
	for (auto& model : modellist)
	{
		for (auto& material : model->materials)
		{
			imageSamplerCount += 5;
			materialCount++;
		}
		for (auto node : model->linearNodes)
		{
			if (node->mesh)
			{
				meshCount++;
			}
		}
	}

	std::vector<VkDescriptorPoolSize> poolSizes = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, (4 + meshCount) * swapchain.imageCount },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, imageSamplerCount * swapchain.imageCount }
	};
	VkDescriptorPoolCreateInfo descriptorPoolCI{};
	descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolCI.poolSizeCount = 2;
	descriptorPoolCI.pPoolSizes = poolSizes.data();
	descriptorPoolCI.maxSets = (2 + materialCount + meshCount) * swapchain.imageCount;
	VK_CHECK_RESULT(vkCreateDescriptorPool(logicalDevice, &descriptorPoolCI, nullptr, &descriptorPool));

	/*
		Descriptor sets
	*/

	// Scene (matrices and environment maps)
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
		descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.scene));

		for (auto i = 0; i < descriptorSets.size(); i++)
		{

			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &descriptorSets[i].scene));

			std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};

			writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[0].descriptorCount = 1;
			writeDescriptorSets[0].dstSet = descriptorSets[i].scene;
			writeDescriptorSets[0].dstBinding = 0;
			writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].scene.descriptor;

			writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSets[1].descriptorCount = 1;
			writeDescriptorSets[1].dstSet = descriptorSets[i].scene;
			writeDescriptorSets[1].dstBinding = 1;
			writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

			writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[2].descriptorCount = 1;
			writeDescriptorSets[2].dstSet = descriptorSets[i].scene;
			writeDescriptorSets[2].dstBinding = 2;
			writeDescriptorSets[2].pImageInfo = &textureSet.irradianceCube.descriptor;

			writeDescriptorSets[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[3].descriptorCount = 1;
			writeDescriptorSets[3].dstSet = descriptorSets[i].scene;
			writeDescriptorSets[3].dstBinding = 3;
			writeDescriptorSets[3].pImageInfo = &textureSet.prefilteredCube.descriptor;

			writeDescriptorSets[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSets[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSets[4].descriptorCount = 1;
			writeDescriptorSets[4].dstSet = descriptorSets[i].scene;
			writeDescriptorSets[4].dstBinding = 4;
			writeDescriptorSets[4].pImageInfo = &textureSet.lutBrdf.descriptor;

			vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}
	}

	// Material (samplers)
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			{ 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
			{ 4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr },
		};
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
		descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
		descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.material));

		// Per-Material descriptor sets
		for (auto& material : modelSet.scene.materials)
		{
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
			descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			descriptorSetAllocInfo.descriptorPool = descriptorPool;
			descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.material;
			descriptorSetAllocInfo.descriptorSetCount = 1;
			VK_CHECK_RESULT(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &material.descriptorSet));

			std::vector<VkDescriptorImageInfo> imageDescriptors = {
				textureSet.empty.descriptor,
				textureSet.empty.descriptor,
				material.normalTexture ? material.normalTexture->descriptor : textureSet.empty.descriptor,
				material.occlusionTexture ? material.occlusionTexture->descriptor : textureSet.empty.descriptor,
				material.emissiveTexture ? material.emissiveTexture->descriptor : textureSet.empty.descriptor
			};

			// TODO: glTF specs states that metallic roughness should be preferred, even if specular glosiness is present

			if (material.pbrWorkflows.metallicRoughness)
			{
				if (material.baseColorTexture)
				{
					imageDescriptors[0] = material.baseColorTexture->descriptor;
				}
				if (material.metallicRoughnessTexture)
				{
					imageDescriptors[1] = material.metallicRoughnessTexture->descriptor;
				}
			}

			if (material.pbrWorkflows.specularGlossiness)
			{
				if (material.extension.diffuseTexture)
				{
					imageDescriptors[0] = material.extension.diffuseTexture->descriptor;
				}
				if (material.extension.specularGlossinessTexture)
				{
					imageDescriptors[1] = material.extension.specularGlossinessTexture->descriptor;
				}
			}

			std::array<VkWriteDescriptorSet, 5> writeDescriptorSets{};
			for (size_t i = 0; i < imageDescriptors.size(); i++)
			{
				writeDescriptorSets[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				writeDescriptorSets[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
				writeDescriptorSets[i].descriptorCount = 1;
				writeDescriptorSets[i].dstSet = material.descriptorSet;
				writeDescriptorSets[i].dstBinding = static_cast<uint32_t>(i);
				writeDescriptorSets[i].pImageInfo = &imageDescriptors[i];
			}

			vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
		}

		// Model node (matrices)
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
				{ 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr },
			};
			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI{};
			descriptorSetLayoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
			descriptorSetLayoutCI.pBindings = setLayoutBindings.data();
			descriptorSetLayoutCI.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(logicalDevice, &descriptorSetLayoutCI, nullptr, &descriptorSetLayouts.node));

			// Per-Node descriptor set
			for (auto& node : modelSet.scene.nodes)
			{
				setupNodeDescriptorSet(node);
			}
		}

	}

	// Skybox (fixed set)
	for (auto i = 0; i < uniformBuffers.size(); i++)
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.scene;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &descriptorSets[i].skybox));

		std::array<VkWriteDescriptorSet, 3> writeDescriptorSets{};

		writeDescriptorSets[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSets[0].descriptorCount = 1;
		writeDescriptorSets[0].dstSet = descriptorSets[i].skybox;
		writeDescriptorSets[0].dstBinding = 0;
		writeDescriptorSets[0].pBufferInfo = &uniformBuffers[i].skybox.descriptor;

		writeDescriptorSets[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSets[1].descriptorCount = 1;
		writeDescriptorSets[1].dstSet = descriptorSets[i].skybox;
		writeDescriptorSets[1].dstBinding = 1;
		writeDescriptorSets[1].pBufferInfo = &uniformBuffers[i].params.descriptor;

		writeDescriptorSets[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSets[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writeDescriptorSets[2].descriptorCount = 1;
		writeDescriptorSets[2].dstSet = descriptorSets[i].skybox;
		writeDescriptorSets[2].dstBinding = 2;
		writeDescriptorSets[2].pImageInfo = &textureSet.prefilteredCube.descriptor;

		vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}
}

void Renderer::setupNodeDescriptorSet(vkglTF::Node* node)
{
	if (node->mesh)
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo{};
		descriptorSetAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		descriptorSetAllocInfo.descriptorPool = descriptorPool;
		descriptorSetAllocInfo.pSetLayouts = &descriptorSetLayouts.node;
		descriptorSetAllocInfo.descriptorSetCount = 1;
		VK_CHECK_RESULT(vkAllocateDescriptorSets(logicalDevice, &descriptorSetAllocInfo, &node->mesh->uniformBuffer.descriptorSet));

		VkWriteDescriptorSet writeDescriptorSet{};
		writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writeDescriptorSet.descriptorCount = 1;
		writeDescriptorSet.dstSet = node->mesh->uniformBuffer.descriptorSet;
		writeDescriptorSet.dstBinding = 0;
		writeDescriptorSet.pBufferInfo = &node->mesh->uniformBuffer.descriptor;

		vkUpdateDescriptorSets(logicalDevice, 1, &writeDescriptorSet, 0, nullptr);
	}
	for (auto& child : node->children)
	{
		setupNodeDescriptorSet(child);
	}
}

void Renderer::preparePipelines()
{
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI{};
	inputAssemblyStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineRasterizationStateCreateInfo rasterizationStateCI{};
	rasterizationStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationStateCI.lineWidth = 1.0f;

	VkPipelineColorBlendAttachmentState blendAttachmentState{};
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlendStateCI{};
	colorBlendStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendStateCI.attachmentCount = 1;
	colorBlendStateCI.pAttachments = &blendAttachmentState;

	VkPipelineDepthStencilStateCreateInfo depthStencilStateCI{};
	depthStencilStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilStateCI.depthTestEnable = VK_FALSE;
	depthStencilStateCI.depthWriteEnable = VK_FALSE;
	depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilStateCI.front = depthStencilStateCI.back;
	depthStencilStateCI.back.compareOp = VK_COMPARE_OP_ALWAYS;

	VkPipelineViewportStateCreateInfo viewportStateCI{};
	viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportStateCI.viewportCount = 1;
	viewportStateCI.scissorCount = 1;

	VkPipelineMultisampleStateCreateInfo multisampleStateCI{};
	multisampleStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;

	if (settings.multiSampling)
	{
		multisampleStateCI.rasterizationSamples = settings.sampleCount;
		multisampleStateCI.sampleShadingEnable = VK_TRUE;
		multisampleStateCI.minSampleShading = 0.2f;
	}

	std::vector<VkDynamicState> dynamicStateEnables = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};
	VkPipelineDynamicStateCreateInfo dynamicStateCI{};
	dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicStateCI.pDynamicStates = dynamicStateEnables.data();
	dynamicStateCI.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

	// Pipeline layout
	const std::vector<VkDescriptorSetLayout> setLayouts = {
		descriptorSetLayouts.scene, descriptorSetLayouts.material, descriptorSetLayouts.node
	};
	VkPipelineLayoutCreateInfo pipelineLayoutCI{};
	pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutCI.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutCI.pSetLayouts = setLayouts.data();
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.size = sizeof(PushConstBlockMaterial);
	pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pipelineLayoutCI.pushConstantRangeCount = 1;
	pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;
	VK_CHECK_RESULT(vkCreatePipelineLayout(logicalDevice, &pipelineLayoutCI, nullptr, &pipelineLayout));

	// Vertex bindings an attributes
	VkVertexInputBindingDescription vertexInputBinding = { 0, sizeof(vkglTF::Model::Vertex), VK_VERTEX_INPUT_RATE_VERTEX };
	std::vector<VkVertexInputAttributeDescription> vertexInputAttributes = {
		{ 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 },
		{ 1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3 },
		{ 2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6 },
		{ 3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 8 },
		{ 4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 10 },
		{ 5, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 14 },
		{ 6, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 18 }
	};
	VkPipelineVertexInputStateCreateInfo vertexInputStateCI{};
	vertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputStateCI.vertexBindingDescriptionCount = 1;
	vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBinding;
	vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexInputAttributes.size());
	vertexInputStateCI.pVertexAttributeDescriptions = vertexInputAttributes.data();

	// Pipelines
	std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

	VkGraphicsPipelineCreateInfo pipelineCI{};
	pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineCI.layout = pipelineLayout;
	pipelineCI.renderPass = renderPass;
	pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
	pipelineCI.pVertexInputState = &vertexInputStateCI;
	pipelineCI.pRasterizationState = &rasterizationStateCI;
	pipelineCI.pColorBlendState = &colorBlendStateCI;
	pipelineCI.pMultisampleState = &multisampleStateCI;
	pipelineCI.pViewportState = &viewportStateCI;
	pipelineCI.pDepthStencilState = &depthStencilStateCI;
	pipelineCI.pDynamicState = &dynamicStateCI;
	pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
	pipelineCI.pStages = shaderStages.data();

	// Skybox pipeline (background cube)
	shaderStages = {
		loadShader(logicalDevice, "skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		loadShader(logicalDevice, "skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipelineSet.skybox));
	for (auto shaderStage : shaderStages)
	{
		vkDestroyShaderModule(logicalDevice, shaderStage.module, nullptr);
	}

	// PBR pipeline
	shaderStages = {
		loadShader(logicalDevice, "pbr.vert.spv", VK_SHADER_STAGE_VERTEX_BIT),
		loadShader(logicalDevice, "pbr_khr.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT)
	};
	depthStencilStateCI.depthWriteEnable = VK_TRUE;
	depthStencilStateCI.depthTestEnable = VK_TRUE;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipelineSet.pbr));
	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipelineSet.pbrDoubleSided));

	rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
	blendAttachmentState.blendEnable = VK_TRUE;
	blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
	blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
	VK_CHECK_RESULT(vkCreateGraphicsPipelines(logicalDevice, pipelineCache, 1, &pipelineCI, nullptr, &pipelineSet.pbrAlphaBlend));

	for (auto shaderStage : shaderStages)
	{
		vkDestroyShaderModule(logicalDevice, shaderStage.module, nullptr);
	}
}

void Renderer::updateUniformBuffers()
{
	// Scene
	sceneUBO.projection = camera.matrices.perspective;
	sceneUBO.view = camera.matrices.view;

	// Center and scale model
	float scale = (1.0f / std::max(modelSet.scene.aabb[0][0], std::max(modelSet.scene.aabb[1][1], modelSet.scene.aabb[2][2]))) * 0.5f;
	glm::vec3 translate = -glm::vec3(modelSet.scene.aabb[3][0], modelSet.scene.aabb[3][1], modelSet.scene.aabb[3][2]);
	translate += -0.5f * glm::vec3(modelSet.scene.aabb[0][0], modelSet.scene.aabb[1][1], modelSet.scene.aabb[2][2]);

	sceneUBO.model = glm::mat4(1.0f);
	sceneUBO.model[0][0] = scale;
	sceneUBO.model[1][1] = scale;
	sceneUBO.model[2][2] = scale;
	sceneUBO.model = glm::translate(sceneUBO.model, translate);

	sceneUBO.camPos = glm::vec3(
		-camera.position.z * sin(glm::radians(camera.rotation.y)) * cos(glm::radians(camera.rotation.x)),
		-camera.position.z * sin(glm::radians(camera.rotation.x)),
		camera.position.z * cos(glm::radians(camera.rotation.y)) * cos(glm::radians(camera.rotation.x))
	);

	// Skybox
	skyboxUBO.projection = camera.matrices.perspective;
	skyboxUBO.view = camera.matrices.view;
	skyboxUBO.model = glm::mat4(glm::mat3(camera.matrices.view));
}

void Renderer::updateParams()
{
	shaderValuesParams.lightDir = glm::vec4(
		sin(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
		sin(glm::radians(lightSource.rotation.y)),
		cos(glm::radians(lightSource.rotation.x)) * cos(glm::radians(lightSource.rotation.y)),
		0.0f);
}

void Renderer::windowResized()
{
	recordCommandBuffers();
	vkDeviceWaitIdle(logicalDevice);
	updateUniformBuffers();
	updateOverlay();
}
//Update ImGui user interface
void Renderer::updateOverlay()
{
	ImGuiIO& io = ImGui::GetIO();

	ImVec2 lastDisplaySize = io.DisplaySize;
	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DeltaTime = frameTimer;

	io.MousePos = ImVec2(mousePos.x, mousePos.y);
	io.MouseDown[0] = mouseButtons.left;
	io.MouseDown[1] = mouseButtons.right;

	ui->pushConstBlock.scale = glm::vec2(2.0f / io.DisplaySize.x, 2.0f / io.DisplaySize.y);
	ui->pushConstBlock.translate = glm::vec2(-1.0f);

	bool updateShaderParams = false;
	bool updateCBs = false;
	float scale = 1.0f;

	ImGui::NewFrame();

	ImGui::SetNextWindowPos(ImVec2(10, 10));
	ImGui::SetNextWindowSize(ImVec2(200 * scale, (modelSet.scene.animations.size() > 0 ? 440 : 360) * scale), ImGuiSetCond_Always);
	ImGui::Begin("Vulkan glTF 2.0 PBR", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::PushItemWidth(100.0f * scale);

	ui->text("www.saschawillems.de");
	ui->text("%.1d fps (%.2f ms)", lastFPS, (1000.0f / lastFPS));

	if (ui->header("Scene"))
	{
		if (ui->button("Open gltf file"))
		{
			std::string filename = "";
#if defined(_WIN32)
			char buffer[MAX_PATH];
			OPENFILENAME ofn;
			ZeroMemory(&buffer, sizeof(buffer));
			ZeroMemory(&ofn, sizeof(ofn));
			ofn.lStructSize = sizeof(ofn);
			ofn.lpstrFilter = "glTF files\0*.gltf;*.glb\0";
			ofn.lpstrFile = buffer;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrTitle = "Select a glTF file to load";
			ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
			if (GetOpenFileNameA(&ofn))
			{
				filename = buffer;
			}
#endif
			if (!filename.empty())
			{
				vkDeviceWaitIdle(logicalDevice);
				loadScene(filename);
				setupDescriptors();
				updateCBs = true;
			}
		}
		if (ui->combo("Environment", selectedEnvironment, environments))
		{
			vkDeviceWaitIdle(logicalDevice);
			loadEnvironment(environments[selectedEnvironment]);
			setupDescriptors();
			updateCBs = true;
		}
	}

	if (ui->header("Environment"))
	{
		if (ui->checkbox("Background", &displayBackground))
		{
			updateShaderParams = true;
		}
		if (ui->slider("Exposure", &shaderValuesParams.exposure, 0.1f, 10.0f))
		{
			updateShaderParams = true;
		}
		if (ui->slider("Gamma", &shaderValuesParams.gamma, 0.1f, 4.0f))
		{
			updateShaderParams = true;
		}
		if (ui->slider("IBL", &shaderValuesParams.scaleIBLAmbient, 0.0f, 1.0f))
		{
			updateShaderParams = true;
		}
	}

	if (ui->header("Debug view"))
	{
		const std::vector<std::string> debugNamesInputs = {
			"none", "Base color", "Normal", "Occlusion", "Emissive", "Metallic", "Roughness"
		};
		if (ui->combo("Inputs", &debugViewInputs, debugNamesInputs))
		{
			shaderValuesParams.debugViewInputs = static_cast<float>(debugViewInputs);
			updateShaderParams = true;
		}
		const std::vector<std::string> debugNamesEquation = {
			"none", "Diff (l,n)", "F (l,h)", "G (l,v,h)", "D (h)", "Specular"
		};
		if (ui->combo("PBR equation", &debugViewEquation, debugNamesEquation))
		{
			shaderValuesParams.debugViewEquation = static_cast<float>(debugViewEquation);
			updateShaderParams = true;
		}
	}

	if (modelSet.scene.animations.size() > 0)
	{
		if (ui->header("Animations"))
		{
			ui->checkbox("Animate", &animate);
			std::vector<std::string> animationNames;
			for (auto animation : modelSet.scene.animations)
			{
				animationNames.push_back(animation.name);
			}
			ui->combo("Animation", &animationIndex, animationNames);
		}
	}

	ImGui::PopItemWidth();
	ImGui::End();
	ImGui::Render();

	ImDrawData* imDrawData = ImGui::GetDrawData();

	// Check if ui buffers need to be recreated
	if (imDrawData)
	{
		VkDeviceSize vertexBufferSize = imDrawData->TotalVtxCount * sizeof(ImDrawVert);
		VkDeviceSize indexBufferSize = imDrawData->TotalIdxCount * sizeof(ImDrawIdx);

		bool updateBuffers = (ui->vertexBuffer.buffer == VK_NULL_HANDLE) || (ui->vertexBuffer.count != imDrawData->TotalVtxCount) || (ui->indexBuffer.buffer == VK_NULL_HANDLE) || (ui->indexBuffer.count != imDrawData->TotalIdxCount);

		if (updateBuffers)
		{
			vkDeviceWaitIdle(logicalDevice);
			if (ui->vertexBuffer.buffer)
			{
				ui->vertexBuffer.destroy();
			}
			ui->vertexBuffer.create(device, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, vertexBufferSize);
			ui->vertexBuffer.count = imDrawData->TotalVtxCount;
			if (ui->indexBuffer.buffer)
			{
				ui->indexBuffer.destroy();
			}
			ui->indexBuffer.create(device, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, indexBufferSize);
			ui->indexBuffer.count = imDrawData->TotalIdxCount;
		}

		// Upload data
		ImDrawVert* vtxDst = (ImDrawVert*)ui->vertexBuffer.mapped;
		ImDrawIdx* idxDst = (ImDrawIdx*)ui->indexBuffer.mapped;
		for (int n = 0; n < imDrawData->CmdListsCount; n++)
		{
			const ImDrawList* cmd_list = imDrawData->CmdLists[n];
			memcpy(vtxDst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idxDst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtxDst += cmd_list->VtxBuffer.Size;
			idxDst += cmd_list->IdxBuffer.Size;
		}

		ui->vertexBuffer.flush();
		ui->indexBuffer.flush();

		updateCBs = updateCBs || updateBuffers;
	}

	if (lastDisplaySize.x != io.DisplaySize.x || lastDisplaySize.y != io.DisplaySize.y)
	{
		updateCBs = true;
	}

	if (updateCBs)
	{
		vkDeviceWaitIdle(logicalDevice);
		recordCommandBuffers();
		vkDeviceWaitIdle(logicalDevice);
	}

	if (updateShaderParams)
	{
		updateParams();
	}
}

void Renderer::render()
{
	if (!prepared)
	{
		return;
	}

	updateOverlay();

	VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &waitFences[frameIndex], VK_TRUE, UINT64_MAX));
	VK_CHECK_RESULT(vkResetFences(logicalDevice, 1, &waitFences[frameIndex]));

	VkResult acquire = swapchain.acquireNextImage(presentCompleteSemaphores[frameIndex], &currentBuffer);
	if ((acquire == VK_ERROR_OUT_OF_DATE_KHR) || (acquire == VK_SUBOPTIMAL_KHR))
	{
		windowResize();
	}
	else
	{
		VK_CHECK_RESULT(acquire);
	}

	// Update UBOs
	updateUniformBuffers();
	UniformBufferSet currentUB = uniformBuffers[currentBuffer];
	memcpy(currentUB.scene.mapped, &sceneUBO, sizeof(sceneUBO));
	memcpy(currentUB.params.mapped, &shaderValuesParams, sizeof(shaderValuesParams));
	memcpy(currentUB.skybox.mapped, &skyboxUBO, sizeof(skyboxUBO));

	const VkPipelineStageFlags waitDstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = &waitDstStageMask;
	submitInfo.pWaitSemaphores = &presentCompleteSemaphores[frameIndex];
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &renderCompleteSemaphores[frameIndex];
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pCommandBuffers = &commandBuffers[currentBuffer];
	submitInfo.commandBufferCount = 1;
	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, waitFences[frameIndex]));

	VkResult present = swapchain.queuePresent(queue, currentBuffer, renderCompleteSemaphores[frameIndex]);
	if (!((present == VK_SUCCESS) || (present == VK_SUBOPTIMAL_KHR)))
	{
		if (present == VK_ERROR_OUT_OF_DATE_KHR)
		{
			windowResize();
			return;
		}
		else
		{
			VK_CHECK_RESULT(present);
		}
	}

	frameIndex += 1;
	frameIndex %= renderAhead;

	if (!paused)
	{
		if (rotateModel)
		{
			modelrot.y += frameTimer * 35.0f;
			if (modelrot.y > 360.0f)
			{
				modelrot.y -= 360.0f;
			}
		}
		if ((animate) && (modelSet.scene.animations.size() > 0))
		{
			animationTimer += frameTimer;
			if (animationTimer > modelSet.scene.animations[animationIndex].end)
			{
				animationTimer -= modelSet.scene.animations[animationIndex].end;
			}
			modelSet.scene.updateAnimation(animationIndex, animationTimer);
		}
		updateParams();
		if (rotateModel)
		{
			updateUniformBuffers();
		}
	}
	if (camera.updated)
	{
		updateUniformBuffers();
	}
}