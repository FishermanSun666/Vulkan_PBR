#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <fstream>
#include <vector>
#include <exception>
#include <assert.h>
#include <algorithm>
#include <cstring>

#include"vulkan/vulkan.h"

#define GLFW_INCLUDE_VULKAN

const std::string SHADER_PATH = "Shaders/";
const std::string ASSETS_PATH = "Assets";
const std::string ENVIRONMENT_PATH = "Assets/Environments/";
const std::string MODEL_PATH = "Assets/Models/";
const std::string TEXTURE_PATH = "Assets/Textures/";
const std::string FONT_PATH = "Assets/Fonts/";

#define VK_CHECK_RESULT(f)																				\
{																										\
	VkResult res = (f);																					\
	if (res != VK_SUCCESS)																				\
	{																									\
		std::cout << "Fatal : VkResult is \"" << res << "\" in " << __FILE__ << " at line " << __LINE__ << std::endl; \
		assert(res == VK_SUCCESS);																		\
	}																									\
}

#define GET_INSTANCE_PROC_ADDR(inst, entrypoint)                        \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetInstanceProcAddr(inst, "vk"#entrypoint)); \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}

#define GET_DEVICE_PROC_ADDR(dev, entrypoint)                           \
{                                                                       \
	fp##entrypoint = reinterpret_cast<PFN_vk##entrypoint>(vkGetDeviceProcAddr(dev, "vk"#entrypoint));   \
	if (fp##entrypoint == NULL)                                         \
	{																    \
		exit(1);                                                        \
	}                                                                   \
}