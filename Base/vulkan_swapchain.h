#pragma once

#include "vulkan_device.h"

struct SwapchainBuffer
{
	VkImage image;
	VkImageView view;
};

class VulkanSwapchain
{
private:
	VkInstance instance;
	vulkan::VulkanDevice* device;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	//Function pointers;
	PFN_vkGetPhysicalDeviceSurfaceSupportKHR fpGetPhysicalDeviceSurfaceSupportKHR;
	PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR fpGetPhysicalDeviceSurfaceCapabilitiesKHR;
	PFN_vkGetPhysicalDeviceSurfaceFormatsKHR fpGetPhysicalDeviceSurfaceFormatsKHR;
	PFN_vkGetPhysicalDeviceSurfacePresentModesKHR fpGetPhysicalDeviceSurfacePresentModesKHR;
	PFN_vkCreateSwapchainKHR fpCreateSwapchainKHR;
	PFN_vkDestroySwapchainKHR fpDestroySwapchainKHR;
	PFN_vkGetSwapchainImagesKHR fpGetSwapchainImagesKHR;
	PFN_vkAcquireNextImageKHR fpAcquireNextImageKHR;
	PFN_vkQueuePresentKHR fpQueuePresentKHR;
public:
	VkFormat colorFormat;
	VkColorSpaceKHR colorSpace;
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	uint32_t imageCount;
	std::vector<VkImage> images;
	std::vector<SwapchainBuffer> buffers;
	VkExtent2D extent = {};
	uint32_t queueNodeIndex = UINT32_MAX;
	VkPresentModeKHR presentMode;

	struct SwapChainSupportDetail
	{
		bool inited = false;
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	}supportDetail;

	~VulkanSwapchain()
	{
		cleanup();
	}

	void initSurface(VkSurfaceKHR surface)
	{
		assert(device);

		this->surface = surface;
		//Get available presenting queue family properties.
		VkBool32 presentSupport = false;
		fpGetPhysicalDeviceSurfaceSupportKHR(device->physicalDevice, device->queueFamilyIndices.graphicsFamily.value(), surface, &presentSupport);
		assert(presentSupport);

		queueNodeIndex = device->queueFamilyIndices.graphicsFamily.value();

		querySurfaceSupport();

		chooseSwapSurfaceFormat();
	}

	void updateSurfaceCapabilities()
	{
		//Getting supported capabilities.
		fpGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physicalDevice, surface, &supportDetail.capabilities);
	}

	void querySurfaceSupport()
	{
		updateSurfaceCapabilities();
		//Getting format.
		uint32_t formatCount;
		fpGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, surface, &formatCount, nullptr);
		assert(0 != formatCount);
		supportDetail.formats.resize(formatCount);
		fpGetPhysicalDeviceSurfaceFormatsKHR(device->physicalDevice, surface, &formatCount, supportDetail.formats.data());
		//Getting present modes.
		uint32_t presentModeCount;
		fpGetPhysicalDeviceSurfacePresentModesKHR(device->physicalDevice, surface, &presentModeCount, nullptr);
		assert(0 != presentModeCount);
		supportDetail.presentModes.resize(presentModeCount);
		fpGetPhysicalDeviceSurfacePresentModesKHR(device->physicalDevice, surface, &presentModeCount, supportDetail.presentModes.data());
		//Set status flag
		supportDetail.inited = true;
	}

	void chooseSwapSurfaceFormat()
	{
		assert(supportDetail.inited);

		for (const auto& availableFormat : supportDetail.formats)
		{
			//top choice
			if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				colorFormat = availableFormat.format;
				colorSpace = availableFormat.colorSpace;
				return;
			}
		}
		colorFormat = supportDetail.formats[0].format;
		colorSpace = supportDetail.formats[0].colorSpace;
	}

	void chooseSwapPresentMode(bool vsync)
	{
		assert(supportDetail.inited);
		presentMode = VK_PRESENT_MODE_FIFO_KHR;
		if (vsync)
		{
			return;
		}
		//Choose triple buffer mode
		for (const auto& availablePresentMode : supportDetail.presentModes)
		{
			if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				return;
			}
		}
	}

	void connect(VkInstance instance, vulkan::VulkanDevice* device)
	{
		assert(device);

		this->instance = instance;
		this->device = device;
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
		GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);
		GET_DEVICE_PROC_ADDR(device->logicalDevice, CreateSwapchainKHR);
		GET_DEVICE_PROC_ADDR(device->logicalDevice, DestroySwapchainKHR);
		GET_DEVICE_PROC_ADDR(device->logicalDevice, GetSwapchainImagesKHR);
		GET_DEVICE_PROC_ADDR(device->logicalDevice, AcquireNextImageKHR);
		GET_DEVICE_PROC_ADDR(device->logicalDevice, QueuePresentKHR);
	}

	//Create the swapchain and get it's images with given width and height

	void create(uint32_t* width, uint32_t* height, bool vsync = false)
	{
		assert(device);

		updateSurfaceCapabilities();

		//If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain.
		if (supportDetail.capabilities.currentExtent.width == (uint32_t)-1)
		{
			//If the surface size is undefined, the size is set to the size of the images requested.
			extent.width = *width;
			extent.height = *height;
		}
		else
		{
			//If the surface size is defined, the swapchain size must match.
			extent = supportDetail.capabilities.currentExtent;
			*width = supportDetail.capabilities.currentExtent.width;
			*height = supportDetail.capabilities.currentExtent.height;
		}

		//Select a present mode for the swapchain.
		chooseSwapPresentMode(vsync);
		//Getting image count.
		uint32_t idealImageCount = supportDetail.capabilities.minImageCount + 1;
		//0 is a special value that means that there is no maximum
		if (supportDetail.capabilities.maxImageCount > 0 && idealImageCount > supportDetail.capabilities.maxImageCount)
		{
			idealImageCount = supportDetail.capabilities.maxImageCount;
		}
		// Find the transformation of the surface
		VkSurfaceTransformFlagsKHR preTransform;
		if (supportDetail.capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		{
			// We prefer a non-rotated transform
			preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		}
		else
		{
			preTransform = supportDetail.capabilities.currentTransform;
		}

		// Find a supported composite alpha format (not all devices support alpha opaque)
		VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		// Simply select the first composite alpha format available
		std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags = {
			VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
			VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
			VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
		};
		for (auto& compositeAlphaFlag : compositeAlphaFlags)
		{
			if (supportDetail.capabilities.supportedCompositeAlpha & compositeAlphaFlag)
			{
				compositeAlpha = compositeAlphaFlag;
				break;
			};
		}

		VkSwapchainCreateInfoKHR createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.pNext = NULL;
		createInfo.surface = surface;
		createInfo.minImageCount = idealImageCount;
		createInfo.imageFormat = colorFormat;
		createInfo.imageColorSpace = colorSpace;
		createInfo.imageExtent = { extent.width, extent.height };
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		createInfo.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
		createInfo.imageArrayLayers = 1;
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = NULL;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.compositeAlpha = compositeAlpha;

		// Set additional usage flag for blitting from the swapchain images if supported.
		VkFormatProperties formatProps;
		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, colorFormat, &formatProps);
		if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR) || (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT))
		{
			createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}

		VK_CHECK_RESULT(fpCreateSwapchainKHR(device->logicalDevice, &createInfo, nullptr, &swapchain));

		VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device->logicalDevice, swapchain, &imageCount, NULL));

		//Get the swap chain images.
		images.resize(imageCount);
		VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device->logicalDevice, swapchain, &imageCount, images.data()));

		//Get the swapchain buffers containing the image and image view
		buffers.resize(imageCount);
		for (uint32_t i = 0; i < imageCount; i++)
		{
			VkImageViewCreateInfo colorAttachmentView = {};
			colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			colorAttachmentView.pNext = NULL;
			colorAttachmentView.format = colorFormat;
			colorAttachmentView.components = {
				VK_COMPONENT_SWIZZLE_R,
				VK_COMPONENT_SWIZZLE_G,
				VK_COMPONENT_SWIZZLE_B,
				VK_COMPONENT_SWIZZLE_A
			};
			VkImageSubresourceRange resourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
			colorAttachmentView.subresourceRange = resourceRange;
			colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
			colorAttachmentView.flags = 0;

			buffers[i].image = images[i];

			colorAttachmentView.image = buffers[i].image;

			VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &colorAttachmentView, nullptr, &buffers[i].view));
		}
	}

	VkResult acquireNextImage(VkSemaphore semaphore, uint32_t* imageIndex)
	{
		if (VK_NULL_HANDLE == swapchain)
		{
			return VK_ERROR_OUT_OF_DATE_KHR;
		}

		//By setting timeout to UINT64_MAX we will always wait until the next iamge has been acquired or an actual error is thrown.

		return fpAcquireNextImageKHR(device->logicalDevice, swapchain, UINT64_MAX, semaphore, (VkFence)nullptr, imageIndex);
	}

	//Queue an image for presentation
	VkResult queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore = VK_NULL_HANDLE)
	{
		VkPresentInfoKHR presentInfo = {};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.pNext = NULL;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &swapchain;
		presentInfo.pImageIndices = &imageIndex;
		//Check if a wait semaphore has been specified to wait for before presenting the image
		if (waitSemaphore != VK_NULL_HANDLE)
		{
			presentInfo.pWaitSemaphores = &waitSemaphore;
			presentInfo.waitSemaphoreCount = 1;
		}
		return fpQueuePresentKHR(queue, &presentInfo);
	}

	void cleanup()
	{
		if (swapchain != VK_NULL_HANDLE)
		{
			for (uint32_t i = 0; i < imageCount; i++)
			{
				vkDestroyImageView(device->logicalDevice, buffers[i].view, nullptr);
			}
			fpDestroySwapchainKHR(device->logicalDevice, swapchain, nullptr);
			swapchain = VK_NULL_HANDLE;
		}
	}
};