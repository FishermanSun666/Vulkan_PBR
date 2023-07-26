#pragma once

#include <stdlib.h>
#include <fstream>

#include "vulkan_device.h"
#include "../Libraries/gli/gli.hpp"

namespace vulkan
{
	class Texture
	{
	public:
		VulkanDevice* device;
		VkImage image = VK_NULL_HANDLE;
		VkImageLayout imageLayout;
		VkDeviceMemory deviceMemory;
		VkImageView imageView;
		uint32_t width, height;
		uint32_t mipLevels;
		VkDescriptorImageInfo descriptor;
		VkSampler sampler;

		void updateDescriptor()
		{
			descriptor.sampler = sampler;
			descriptor.imageView = imageView;
			descriptor.imageLayout = imageLayout;
		}

		void destroy()
		{
			vkDestroyImageView(device->logicalDevice, imageView, nullptr);
			vkDestroyImage(device->logicalDevice, image, nullptr);
			if (sampler)
			{
				vkDestroySampler(device->logicalDevice, sampler, nullptr);
			}
			vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
		}

		void createSampler(VkFilter filter, VkSamplerAddressMode addressMode)
		{
			assert(device);

			VkSamplerCreateInfo samplerInfo{};
			samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
			samplerInfo.magFilter = filter;
			samplerInfo.minFilter = filter;
			samplerInfo.addressModeU = addressMode;
			samplerInfo.addressModeV = addressMode;
			samplerInfo.addressModeW = addressMode;
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = device->enabledFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
			samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			samplerInfo.unnormalizedCoordinates = VK_FALSE;
			samplerInfo.compareEnable = VK_FALSE;
			samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
			samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerInfo.mipLodBias = 0.0f;
			samplerInfo.minLod = 0.0f;
			samplerInfo.maxLod = static_cast<float>(mipLevels);
			//anisotropy
			samplerInfo.anisotropyEnable = VK_FALSE;
			samplerInfo.maxAnisotropy = 1.0f;

			VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerInfo, nullptr, &sampler));
		}

		void createImageView(VkImageViewType viewType, VkFormat format, VkImageSubresourceRange resourceRange)
		{
			assert(device);

			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = image;
			viewInfo.viewType = viewType;
			viewInfo.format = format;
			viewInfo.subresourceRange = resourceRange;

			VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewInfo, nullptr, &imageView));
		}
	};

	class Texture2D :public Texture
	{
	public:
		void loadFromFile(std::string filename, VkFormat format, VulkanDevice* device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT, VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			assert(device);

			gli::texture2d tex2D(gli::load(filename.c_str()));

			assert(!tex2D.empty());

			this->device = device;
			width = static_cast<uint32_t>(tex2D[0].extent().x);
			height = static_cast<uint32_t>(tex2D[0].extent().y);
			mipLevels = static_cast<uint32_t>(tex2D.levels());

			////Get device properties for the requested texture format.
			//VkFormatProperties formatProperties;
			//vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

			//Create a host-visible staging buffer that contains the raw image data.
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;
			
			device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, tex2D.size(), &stagingBuffer, &stagingMemory);

			//Copy texture data into staging buffer.
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
			uint8_t* data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, tex2D.data(), tex2D.size());
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			//Setup buffer copy regions for each miplevel
			std::vector<VkBufferImageCopy> bufferCopyRegions;
			uint32_t offset = 0;
			
			for (uint32_t i = 0; i < mipLevels; i++)
			{
				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = i;
				bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(tex2D[i].extent().x);
				bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(tex2D[i].extent().y);
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);

				offset += static_cast<uint32_t>(tex2D[i].size());
			}
			// Create optimal tiled target image
			if (!(imageUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = imageUsageFlags;

			device->createImage(imageCreateInfo , VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, deviceMemory);

			//subresourceRange
			VkImageSubresourceRange resourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
			//Use a separate command buffer for texture loading
			VkCommandBuffer copyCmdBuffer = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			//begin record command
			device->beginCommandBuffer(copyCmdBuffer);

			//Image barrier for optimal image(target)
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);
			
			//Copy miplevels from staging buffer
			vkCmdCopyBufferToImage(
				copyCmdBuffer,
				stagingBuffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			//Change texture image layout to shader read after all miplevels have been copied.
			this->imageLayout = imageLayout;
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, resourceRange);
			//Flush
			device->flushCommandBuffer(copyCmdBuffer, copyQueue);

			//Clean up
			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

			createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
			createImageView(VK_IMAGE_VIEW_TYPE_2D, format, resourceRange);

			updateDescriptor();
		}

		void loadFromBuffer(
			void* buffer,
			VkDeviceSize bufferSize,
			VkFormat format,
			uint32_t width,
			uint32_t height,
			VulkanDevice* device,
			VkQueue copyQueue,
			VkFilter filter = VK_FILTER_LINEAR,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			assert(device);
			assert(buffer);

			this->device = device;
			width = width;
			height = height;
			mipLevels = 1;

			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, &stagingBuffer, &stagingMemory);

			//Copy texture data into staging buffer.
			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
			uint8_t* data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, buffer, bufferSize);
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			VkBufferImageCopy bufferCopyRegion = {};
			bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			bufferCopyRegion.imageSubresource.mipLevel = 0;
			bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
			bufferCopyRegion.imageSubresource.layerCount = 1;
			bufferCopyRegion.imageExtent.width = width;
			bufferCopyRegion.imageExtent.height = height;
			bufferCopyRegion.imageExtent.depth = 1;
			bufferCopyRegion.bufferOffset = 0;

			// Create optimal tiled target image
			if (!(imageUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = imageUsageFlags;
			device->createImage(imageCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, deviceMemory);

			VkImageSubresourceRange resourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 6 };
			// Use a separate command buffer for texture loading
			VkCommandBuffer copyCmdBuffer = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			device->beginCommandBuffer(copyCmdBuffer);

			//Image barrier for optimal image(target)
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);

			//Copy miplevels from staging buffer
			vkCmdCopyBufferToImage(
				copyCmdBuffer,
				stagingBuffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				1,
				&bufferCopyRegion
			);

			//Change texture image layout to shader read after all miplevels have been copied.
			this->imageLayout = imageLayout;
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, resourceRange);
			//Flush
			device->flushCommandBuffer(copyCmdBuffer, copyQueue);

			//Create sampler
			createSampler(filter, VK_SAMPLER_ADDRESS_MODE_REPEAT);
			//Create view
			createImageView(VK_IMAGE_VIEW_TYPE_2D, format, resourceRange);
			//Clean up
			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

			updateDescriptor();
 		}
	};

	class TextureCubeMap : public Texture
	{
	public:
		void loadFromFile(
			std::string filename,
			VkFormat format,
			VulkanDevice* device,
			VkQueue copyQueue,
			VkImageUsageFlags imageUsageFlags = VK_IMAGE_USAGE_SAMPLED_BIT,
			VkImageLayout imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
		{
			gli::texture_cube texCube(gli::load(filename));

			assert(!texCube.empty());

			this->device = device;
			width = static_cast<uint32_t>(texCube.extent().x);
			height = static_cast<uint32_t>(texCube.extent().y);
			mipLevels = static_cast<uint32_t>(texCube.levels());

			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, texCube.size(), &stagingBuffer, &stagingMemory);

			VkMemoryRequirements memReqs;
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

			uint8_t* data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, texCube.data(), texCube.size());
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			std::vector<VkBufferImageCopy> bufferCopyRegions;
			size_t offset = 0;

			for (uint32_t face = 0; face < 6; face++)
			{
				for (uint32_t level = 0; level < mipLevels; level++)
				{
					VkBufferImageCopy bufferCopyRegion = {};
					bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
					bufferCopyRegion.imageSubresource.mipLevel = level;
					bufferCopyRegion.imageSubresource.baseArrayLayer = face;
					bufferCopyRegion.imageSubresource.layerCount = 1;
					bufferCopyRegion.imageExtent.width = static_cast<uint32_t>(texCube[face][level].extent().x);
					bufferCopyRegion.imageExtent.height = static_cast<uint32_t>(texCube[face][level].extent().y);
					bufferCopyRegion.imageExtent.depth = 1;
					bufferCopyRegion.bufferOffset = offset;

					bufferCopyRegions.push_back(bufferCopyRegion);

					// Increase offset into staging buffer for next level / face
					offset += texCube[face][level].size();
				}
			}

			// Create optimal tiled target image
			if (!(imageUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
			{
				imageUsageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			}
			VkImageCreateInfo imageCreateInfo{};
			imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width, height, 1 };
			imageCreateInfo.usage = imageUsageFlags;
			// Cube faces count as array layers in Vulkan
			imageCreateInfo.arrayLayers = 6;
			// This flag is required for cube map images
			imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

			device->createImage(imageCreateInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, image, deviceMemory);

			VkImageSubresourceRange resourceRange{ VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1 };
			// Use a separate command buffer for texture loading
			VkCommandBuffer copyCmdBuffer = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
			device->beginCommandBuffer(copyCmdBuffer);

			//Image barrier for optimal image(target)
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, resourceRange);

			//Copy miplevels from staging buffer
			vkCmdCopyBufferToImage(
				copyCmdBuffer,
				stagingBuffer,
				image,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				static_cast<uint32_t>(bufferCopyRegions.size()),
				bufferCopyRegions.data()
			);

			//Change texture image layout to shader read after all miplevels have been copied.
			this->imageLayout = imageLayout;
			device->recordTransitionImageLayout(copyCmdBuffer, image, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, resourceRange);
			//Flush
			device->flushCommandBuffer(copyCmdBuffer, copyQueue);

			//Create sampler
			createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
			//Create view
			createImageView(VK_IMAGE_VIEW_TYPE_CUBE, format, resourceRange);
			//Clean up
			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
			updateDescriptor();
		}
	};
}