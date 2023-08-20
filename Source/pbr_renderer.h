#pragma once

#include "algorithm"

#include "../Base/vulkan_example_base.h"
#include "../Base/vulkan_texture.h"
#include "../Base/vulkan_glTF_model_loader.h"
#include "../Base/ui.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Renderer : public VulkanExampleBase
{
public:
	enum PBRWorkflows { PBR_WORKFLOW_METALLIC_ROUGHNESS = 0, PBR_WORKFLOW_SPECULAR_GLOSINESS = 1 };

	struct TextureSet
	{
		vulkan::TextureCubeMap environmentCube;
		vulkan::Texture2D empty;
		vulkan::Texture2D lutBrdf;
		vulkan::TextureCubeMap irradianceCube;
		vulkan::TextureCubeMap prefilteredCube;
	} textureSet;

	struct Models
	{
		vkglTF::Model scene;
		vkglTF::Model skybox;
	} modelSet;

	struct UniformBufferSet
	{
		Buffer scene;
		Buffer skybox;
		Buffer params;
	};

	struct UBOMatrices
	{
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec3 camPos;
	} sceneUBO, skyboxUBO;

	struct ShaderValuesParams
	{
		glm::vec4 lightDir;
		float exposure = 4.5f;
		float gamma = 1.0f;
		float prefilteredCubeMipLevels = 0.0f;
		float scaleIBLAmbient = 1.0f;
		float debugViewInputs = 0.0f;
		float debugViewEquation = 0.0f;
	} shaderValuesParams;

	VkPipelineLayout pipelineLayout;

	struct PipelineSet
	{
		VkPipeline skybox;
		VkPipeline pbr;
		VkPipeline pbrDoubleSided;
		VkPipeline pbrAlphaBlend;
	} pipelineSet;
	VkPipeline boundPipeline = VK_NULL_HANDLE;

	struct DescriptorSetLayouts
	{
		VkDescriptorSetLayout scene;
		VkDescriptorSetLayout material;
		VkDescriptorSetLayout node;
	} descriptorSetLayouts;

	struct DescriptorSets
	{
		VkDescriptorSet scene;
		VkDescriptorSet skybox;
	};

	std::vector<DescriptorSets> descriptorSets;

	std::vector<VkCommandBuffer> commandBuffers;

	std::vector<UniformBufferSet> uniformBuffers;

	//Fences && Semaphores
	std::vector<VkFence> waitFences;
	std::vector<VkSemaphore> renderCompleteSemaphores;
	std::vector<VkSemaphore> presentCompleteSemaphores;

	const uint32_t renderAhead = 2;
	uint32_t frameIndex = 0;
	//Animation
	bool animate = true;
	int32_t animationIndex = 0;
	float animationTimer = 0.0f;
	
	//Background
	bool displayBackground = true;
	//Light
	struct LightSource
	{
		glm::vec3 color = glm::vec3(1.0f);
		glm::vec3 rotation = glm::vec3(75.0f, 40.0f, 0.0f);
	} lightSource;

	UI* ui;
	//Rotate model
	bool rotateModel = false;
	glm::vec3 modelrot = glm::vec3(0.0f);
	glm::vec3 modelPos = glm::vec3(0.0f);

	struct PushConstBlockMaterial
	{
		glm::vec4 baseColorFactor;
		glm::vec4 emissiveFactor;
		glm::vec4 diffuseFactor;
		glm::vec4 specularFactor;
		float workflow;
		int colorTextureSet;
		int PhysicalDescriptorTextureSet;
		int normalTextureSet;
		int occlusionTextureSet;
		int emissiveTextureSet;
		float metallicFactor;
		float roughnessFactor;
		float alphaMask;
		float alphaMaskCutoff;
	} pushConstBlockMaterial;
	//Environments
	std::map<std::string, std::string> environments;
	std::string selectedEnvironment = "papermill";
	//Debug
	int32_t debugViewInputs = 0;
	int32_t debugViewEquation = 0;

	Renderer() : VulkanExampleBase()
	{
		title = "Vulkan-PBR-glTF";
	}

	~Renderer()
	{
		vkDestroyPipeline(logicalDevice, pipelineSet.skybox, nullptr);
		vkDestroyPipeline(logicalDevice, pipelineSet.pbr, nullptr);
		vkDestroyPipeline(logicalDevice, pipelineSet.pbrAlphaBlend, nullptr);

		vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayouts.scene, nullptr);
		vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayouts.material, nullptr);
		vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayouts.node, nullptr);

		modelSet.scene.destroy(logicalDevice);
		modelSet.skybox.destroy(logicalDevice);

		for (auto buffer : uniformBuffers)
		{
			buffer.params.destroy();
			buffer.scene.destroy();
			buffer.skybox.destroy();
		}
		for (auto fence : waitFences)
		{
			vkDestroyFence(logicalDevice, fence, nullptr);
		}
		for (auto semaphore : renderCompleteSemaphores)
		{
			vkDestroySemaphore(logicalDevice, semaphore, nullptr);
		}
		for (auto semaphore : presentCompleteSemaphores)
		{
			vkDestroySemaphore(logicalDevice, semaphore, nullptr);
		}

		textureSet.environmentCube.destroy();
		textureSet.irradianceCube.destroy();
		textureSet.prefilteredCube.destroy();
		textureSet.lutBrdf.destroy();
		textureSet.empty.destroy();

		if (ui)
		{
			delete ui;
		}
	}
	void renderNode(vkglTF::Node* node, uint32_t cbIndex, vkglTF::Material::AlphaMode alphaMode);
	void recordCommandBuffers();

	void loadScene(std::string filename);
	void loadEnvironment(std::string filename);
	void generateCubemaps();
	void loadAssets();
	void setupNodeDescriptorSet(vkglTF::Node* node);
	void setupDescriptors();
	void preparePipelines();
	void generateBRDFLUT();
	void prepareUniformBuffers();
	void updateUniformBuffers();
	void updateParams();
	void windowResized();
	void prepare();
	void updateOverlay();
	virtual void render();
	virtual void fileDropped(std::string filename);
};