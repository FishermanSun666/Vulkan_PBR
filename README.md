# Vulkan PBR Renderer

## Description

This project is a physically-based rendering (PBR) renderer implemented using Vulkan(1.3), a low-level graphics API. The PBR technique aims to achieve realistic lighting and material representation in computer graphics. With Vulkan's high performance and efficiency, this renderer delivers stunning visuals and optimal performance.

![environment_test](D:\PersonalProject\Vulkan_PBR\ScreenShoot\environment_test.png)

## Key Features

- [x] Loading glTF 2.0 models

  - [x] Full node hierarchy
  - [x] Full PBR material support
    - [x] Metallic-Roughness
    - [x] Specular-Glossiness workflow

  <img src="D:\PersonalProject\Vulkan_PBR\ScreenShoot\boombox.png" alt="boombox" style="zoom:23%;" />

  - [x] Animations
    - [x] Articulated (translate, rotate, scale)
    - [x] Skinned

  ![animation](D:\PersonalProject\Vulkan_PBR\ScreenShoot\animation.gif)

- [x] PBR

- [x] IBL

### Dependencies:

- [GLM](https://github.com/g-truc/glm)
- [GLFW 3](https://github.com/glfw/glfw)
- [stb_image](https://github.com/nothings/stb)
- [Dear ImGUI](https://github.com/ocornut/imgui)
- [TinyGltf](https://github.com/syoyo/tinygltf)
- [Renderdoc API](https://github.com/baldurk/renderdoc)
- [cmft](https://github.com/dariomanesku/cmft)



