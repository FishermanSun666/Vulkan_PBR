#pragma once

#include <optional>

#include "vulkan_base.h"

#if defined(VK_USE_PLATFORM_MACOS_MVK) && (VK_HEADER_VERSION >= 216)
#include <vulkan/vulkan_beta.h>
#endif

namespace vulkan
{

	struct VulkanDevice
	{
		VkPhysicalDevice physicalDevice;
		VkDevice logicalDevice;
		VkPhysicalDeviceProperties properties;
		VkPhysicalDeviceFeatures features;
		VkPhysicalDeviceFeatures enabledFeatures;
		VkPhysicalDeviceMemoryProperties memoryProperties;
		std::vector<VkQueueFamilyProperties> queueFamilyProperties;
		VkCommandPool commandPool = VK_NULL_HANDLE;

		struct QueueFamilyIndices
		{
			std::optional<uint32_t> graphicsFamily;
			std::optional<uint32_t> computeFamily;
			bool isComplete()
			{
				return graphicsFamily.has_value() && computeFamily.has_value();
			}
			void init(VkPhysicalDevice device)
			{
				// Assign index to queue families that could be found
				uint32_t queueFamilyCount = 0;
				vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
				std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
				vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
				// Find at least one queue family that supports VK_QUEUE_GRAPHICS_BIT
				int i = 0;
				for (const auto& queueFamily : queueFamilies)
				{
					if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
					{
						graphicsFamily = i;
					}
					else if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT)
					{
						// Try to find a queue family index that supports compute but not graphics
						if (!(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT))
						{
							computeFamily = i;
						}
						// For other queue types or if no separate compute queue is present, return the first one to support the requested flags
						if (!computeFamily.has_value())
						{
							computeFamily = i;
						}
					}
					i++;
				}
			}
		} queueFamilyIndices;

		VulkanDevice(VkPhysicalDevice physicalDevice)
		{
			if (!physicalDevice)
			{
				throw std::runtime_error("VulkanDevice initialisation error!");
			}
			this->physicalDevice = physicalDevice;       

			vkGetPhysicalDeviceProperties(physicalDevice, &properties);
			vkGetPhysicalDeviceFeatures(physicalDevice, &features);
			vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
			queueFamilyIndices.init(physicalDevice);
			if (!queueFamilyIndices.isComplete())
			{
				throw std::runtime_error("VulkanDevice initializing queue family error!");
			}
		}
		
		~VulkanDevice()
		{
			if (commandPool)
			{
				vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
			}
			if (logicalDevice)
			{
				vkDestroyDevice(logicalDevice, nullptr);
			}
		}

		//Get the index of a memory type that has all the requested property bits set.
		uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties, VkBool32* memTypeFound = nullptr)
		{

			for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
			{
				//specify the bit field of memory types that are suitable.

				if ((typeFilter & (1 << i)) && (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
				{
					if (memTypeFound)
					{
						*memTypeFound = true;
					}
					return i;
				}
			}

			if (memTypeFound)
			{
				*memTypeFound = false;
				return 0;
			}
			else
			{
				throw std::runtime_error("failed to find suitable memory type!");
			}
		}

		VkResult createLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, VkQueueFlags requestedQueueTypes = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)
		{
			std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

			const float defaultQueuePriority(0.0f);

			// Graphics queue
			if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
			{
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}

			//Dedicated compute queue
			if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
			{
				if (queueFamilyIndices.computeFamily.value() != queueFamilyIndices.graphicsFamily.value())
				{
					VkDeviceQueueCreateInfo queueInfo{};
					queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
					queueInfo.queueFamilyIndex = queueFamilyIndices.computeFamily.value();
					queueInfo.queueCount = 1;
					queueInfo.pQueuePriorities = &defaultQueuePriority;
					queueCreateInfos.push_back(queueInfo);
				}
			}

			//Create the logical device representation.
			std::vector<const char*> deviceExtensions(enabledExtensions);
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

			VkDeviceCreateInfo deviceCreateInfo = {};
			deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
			deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
			deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
			deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

			if (deviceExtensions.size() > 0)
			{
				deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
				deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();
			}

			VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);

			if (result == VK_SUCCESS)
			{
				commandPool = createCommandPool(queueFamilyIndices.graphicsFamily.value());
			}

			this->enabledFeatures = enabledFeatures;

			return result;
		}

		//Create a buffer on the device
		VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer* buffer, VkDeviceMemory* memory, void* data = nullptr)
		{
			//Create the buffer handle
			VkBufferCreateInfo bufferCreateInfo{};
			bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
			bufferCreateInfo.usage = usageFlags;
			bufferCreateInfo.size = size;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			if (VK_SUCCESS != vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, buffer))
			{
				throw std::runtime_error("failed to create buffer!");
			}

			//Create the memory backing up the buffer handle
			VkMemoryRequirements memReqs;
			VkMemoryAllocateInfo memAlloc{};
			memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memReqs);
			memAlloc.allocationSize = memReqs.size;
			//Find a memory type index that fits the properties of the buffer.
			memAlloc.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
			VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, memory));

			//If a pointer to the buffer data has been passed, map the buffer and copy over the data.
			if (data != nullptr)
			{
				void* mapped;
				VK_CHECK_RESULT(vkMapMemory(logicalDevice, *memory, 0, size, 0, &mapped));
				memcpy(mapped, data, size);
				//If host coherency hasn't been requested, do a manual flush to make writes visible.
				if ((memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
				{
					VkMappedMemoryRange mappedRange{};
					mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
					mappedRange.memory = *memory;
					mappedRange.offset = 0;
					mappedRange.size = size;
					vkFlushMappedMemoryRanges(logicalDevice, 1, &mappedRange);
				}
				vkUnmapMemory(logicalDevice, *memory);
			}

			//Attach the memory to the buffer object.
			VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, *buffer, *memory, 0));

			return VK_SUCCESS;
		}

		VkCommandPool createCommandPool(uint32_t queueFamlyIndex, VkCommandPoolCreateFlags createFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT)
		{
			VkCommandPoolCreateInfo	createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			createInfo.queueFamilyIndex = queueFamlyIndex;
			createInfo.flags = createFlags;
			VkCommandPool pool;

			VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &createInfo, nullptr, &pool));

			return pool;
		}

		//Allocate a command buffer from the command pool.

		VkCommandBuffer createCommandBuffer(VkCommandBufferLevel level)
		{
			if (!commandPool)
			{
				throw std::runtime_error("failed to create command buffer! command pool is not ready!");
			}
			VkCommandBufferAllocateInfo allocateInfo{};
			allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocateInfo.commandPool = commandPool;
			allocateInfo.level = level;
			allocateInfo.commandBufferCount = 1;

			VkCommandBuffer buffer;
			VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &allocateInfo, &buffer));

			return buffer;
		}

		void beginCommandBuffer(VkCommandBuffer buffer)
		{
			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			VK_CHECK_RESULT(vkBeginCommandBuffer(buffer, &beginInfo));
		}

		void endCommandBuffer(VkCommandBuffer buffer)
		{
			VK_CHECK_RESULT(vkEndCommandBuffer(buffer));
		}

		//Finish command buffer recoding and submit it to a queue.
		void flushCommandBuffer(VkCommandBuffer buffer, VkQueue queue, bool freeMemory = true)
		{
			endCommandBuffer(buffer);

			VkSubmitInfo submitInfo{};
			submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submitInfo.commandBufferCount = 1;
			submitInfo.pCommandBuffers = &buffer;

			//Create fence to ensure that the command buffer has finished executing
			VkFenceCreateInfo fenceCreateInfo{};
			fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
			VkFence fence;
			VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceCreateInfo, nullptr, &fence));
			//Submit to the queue
			VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
			//Wait for the fence to signal that command buffer has finished executing
			VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, UINT64_MAX));

			vkDestroyFence(logicalDevice, fence, nullptr);

			if (freeMemory)
			{
				vkFreeCommandBuffers(logicalDevice, commandPool, 1, &buffer);
			}
		}

		void createImage(VkImageCreateInfo& imageInfo, VkMemoryPropertyFlags properties,  VkImage& image, VkDeviceMemory& imageMemory, bool memTypeFound = false)
		{

			if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &image) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create image!");
			}

			VkMemoryRequirements memRequirements;
			vkGetImageMemoryRequirements(logicalDevice, image, &memRequirements);

			VkMemoryAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			allocInfo.allocationSize = memRequirements.size;
			if (memTypeFound)
			{
				VkBool32 lazyMemTypePresent;
				allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties, &lazyMemTypePresent);
				if (!lazyMemTypePresent)
				{
					allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
				}
			}

			if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to allocate image memory!");
			}

			vkBindImageMemory(logicalDevice, image, imageMemory, 0);
		}

		void recordTransitionImageLayout(VkCommandBuffer commandBuffer, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, VkImageSubresourceRange resourceRange)
		{
			VkImageMemoryBarrier barrier{};
			barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

			barrier.image = image;
			barrier.subresourceRange = resourceRange;

			VkPipelineStageFlags sourceStage;
			VkPipelineStageFlags destinationStage;

			if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

				if (hasStencilComponent(format))
				{
					barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
				}
			}
			else
			{
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			}

			if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

				sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL || newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL))
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

				sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
				destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
			{
				barrier.srcAccessMask = 0;
				barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

				sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
				destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
			}
			else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

				sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
				destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			}
			else
			{
				throw std::invalid_argument("unsupported layout transition!");
			}


			vkCmdPipelineBarrier(
				commandBuffer,
				sourceStage, destinationStage,
				0,
				0, nullptr,
				0, nullptr,
				1, &barrier
			);
		}

		bool hasStencilComponent(VkFormat format)
		{
			return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
		}
	}; 

	

}