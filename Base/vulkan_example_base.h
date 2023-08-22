#pragma once

#include <chrono>
#include <sstream>
#include <array>
#include <numeric>
#include <set>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <sys/stat.h>
#include "vulkan_swapchain.h"
#include "keycodes.h"
#include "camera.h"
#include "keycodes.h"
#include "imgui/imgui.h"

class VulkanExampleBase
{
	struct ImageInfo
	{
		VkImage image;
		VkImageView view;
		VkDeviceMemory memory;
	};
private:
	float fpsTimer = 0.0f;
	uint32_t frameCounter = 0;
	uint32_t destWidth;
	uint32_t destHeight;
	bool resizing = false;
	void handleMouseMove(int32_t x, int32_t y);
	PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallback;
	PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallback;
	VkDebugReportCallbackEXT debugReportCallback;
	struct MultisampleTarget
	{
		ImageInfo color;
		ImageInfo depth;
	} multisampleTarget;
protected:
	VkInstance instance;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties deviceProperties;
	VkPhysicalDeviceFeatures deviceFeatures;
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
	VkDevice logicalDevice;
	vulkan::VulkanDevice* device;
	VkQueue queue;
	VkFormat depthFormat;
	VkCommandPool cmdPool;
	VkRenderPass renderPass;
	std::vector<VkFramebuffer>frameBuffers;
	uint32_t currentBuffer = 0;
	VkDescriptorPool descriptorPool;
	VkPipelineCache pipelineCache;
	VulkanSwapchain swapchain;
	std::string title = "PBR Renderer";
	std::string name = "pbrRenderer";
	void windowResize(bool manual = false);
public:
	static std::vector<const char*> args;
	bool prepared = false;
	uint32_t width = 1920;
	uint32_t height = 1080;
	float frameTimer = 1.0f;
	Camera camera;
	glm::vec2 mousePos;
	bool paused = false;
	uint32_t lastFPS = 0;

	struct Settings
	{
		bool validation = false;
		bool fullscreen = false;
		bool vsync = false;
		bool multiSampling = true;
		bool SpecularGlossiness = false;
		VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_8_BIT;
	} settings;

	ImageInfo depthStencil;

	struct GamePadState
	{
		glm::vec2 axisLeft = glm::vec2(0.0f);
		glm::vec2 axisRight = glm::vec2(0.0f);
	} gamePadState;

	struct MouseButtons
	{
		bool left = false;
		bool right = false;
		bool middle = false;
	} mouseButtons;

	VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined (_WIN32)
	HWND window;
	HINSTANCE windowInstance;
	HWND setupWindow(HINSTANCE hinstace, WNDPROC wndproc);
	void handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	NSWindow* window;
	NSWindow* setupWindow();
	void mouseDragged(float x, float y);
	void windowWillResize(float x, float y);
	void windowDidResize();
#endif

	VulkanExampleBase();
	virtual ~VulkanExampleBase();

	void initVulkan();
	void pickPhysicalDevice();

	virtual VkResult createInstance(bool enableValidation);
	virtual void render() = 0;
	virtual void windowResized();
	virtual void setupFrameBuffer();
	virtual void prepare();
	virtual void fileDropped(std::string filename);

	void initSurface();
	void initSwapchain();
	void createSwapchain();
	void recreateSwapchain();

	void renderLoop();
	void renderFrame();
};

