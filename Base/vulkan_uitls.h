#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <fstream>
#include <map>
#include <iostream>
#include <string>

#include "vulkan_device.h"


struct Buffer
{
	VkDevice device;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkDescriptorBufferInfo descriptor;
	int32_t count = 0;
	void* mapped = nullptr;
	VkDeviceSize limitedSize;

	void create(vulkan::VulkanDevice* device, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, bool mapping = true)
	{
		this->device = device->logicalDevice;
		limitedSize = device->properties.limits.nonCoherentAtomSize;
		device->createBuffer(usageFlags, memoryPropertyFlags, size, &buffer, &memory);
		descriptor = { buffer, 0, size };
		if (mapping)
		{
			map();
		}
	}

	void destroy()
	{
		if (mapped) {
			unmap();
		}
		vkDestroyBuffer(device, buffer, nullptr);
		vkFreeMemory(device, memory, nullptr);
		buffer = VK_NULL_HANDLE;
		memory = VK_NULL_HANDLE;
	}

	void map()
	{
		if (!device)
		{
			throw std::runtime_error("fail to map buffer memory, device was not initialized.");
		}
		VK_CHECK_RESULT(vkMapMemory(device, memory, 0, descriptor.range, 0, &mapped));
	}

	void unmap()
	{
		if (!device)
		{
			throw std::runtime_error("fail to unmap buffer memory, device was not initialized.");
		}
		if (mapped)
		{
			vkUnmapMemory(device, memory);
			mapped = nullptr;
		}
	}

	void flush(VkDeviceSize size = VK_WHOLE_SIZE)
	{
		VkMappedMemoryRange mappedRange{};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = memory;
		mappedRange.size = (size + limitedSize - 1) & ~(limitedSize - 1);
		VK_CHECK_RESULT(vkFlushMappedMemoryRanges(device, 1, &mappedRange));
	}
};

VkPipelineShaderStageCreateInfo loadShader(VkDevice device, std::string filename, VkShaderStageFlagBits stage)
{
	VkPipelineShaderStageCreateInfo stageCreateInfo{};
	stageCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageCreateInfo.stage = stage;
	stageCreateInfo.pName = "main";

	std::ifstream file(SHADER_PATH + filename, std::ios::binary | std::ios::in | std::ios::ate);
	if (!file.is_open())
	{
		throw std::runtime_error("fail to open file: " + filename);
	}
	size_t size = file.tellg();
	if (size <= 0)
	{
		throw std::runtime_error("empty file: " + filename);
	}
	file.seekg(0, std::ios::beg);
	char* codeBuffer = new char[size];
	file.read(codeBuffer, size);
	file.close();
		
	VkShaderModuleCreateInfo moduleCreateInfo{};
	moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleCreateInfo.codeSize = size;
	VK_CHECK_RESULT(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &stageCreateInfo.module));
		
	delete[] codeBuffer;

	return stageCreateInfo;
}

void readDirectory(const std::string& directory, const std::string& pattern, std::map<std::string, std::string>& filelist, bool recursive)
{
	std::string searchpattern(directory + "/" + pattern);
	WIN32_FIND_DATA data;
	HANDLE hFind;
	if ((hFind = FindFirstFile(reinterpret_cast<LPCTSTR>(searchpattern.c_str()), &data)) != INVALID_HANDLE_VALUE)
	{
		do
		{
			char* cFileName = reinterpret_cast<char*>(data.cFileName);
			std::string filename(cFileName);
			filename.erase(filename.find_last_of("."), std::string::npos);
			filelist[filename] = directory + "/" + cFileName;
		}
		while (0 != FindNextFile(hFind, &data));
		FindClose(hFind);
	}
	if (recursive)
	{
		std::string dirpattern = directory + "/*";
		if ((hFind = FindFirstFile(reinterpret_cast<LPCTSTR>(dirpattern.c_str()), &data)) != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				{
					char subdir[MAX_PATH];
					char* cFileName = reinterpret_cast<char*>(data.cFileName);
					strcpy(subdir, directory.c_str());
					strcat(subdir, "/");
					strcat(subdir, cFileName);
					if ((strcmp(cFileName, ".") != 0) && (strcmp(cFileName, "..") != 0))
					{
						readDirectory(subdir, pattern, filelist, recursive);
					}
				}
			}
			while (FindNextFile(hFind, &data) != 0);
			FindClose(hFind);
		}
	}
}
