# Vulkan PBR Renderer

## Description

This project is a PBR renderer implemented on a framework built with Vulkan 1.3. The specific implementation of PBR references the UE4 engine. I chose to use glTF2.0 models for the modelling aspect. For lighting, I implemented Lambertian reflection, Blinn-Phong reflection and Image Based Lighting.

![environment_test](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/environment_test.png)

## Video link

https://youtu.be/aSoyZY3B76w

## PBR

在实现PBR方面，本解决方案主要参考UE4在2013年发表的[Real Shading in Unreal Engine 4]的实现方案。选择这个方案主要是因为它是基于Cook-Torrance BRDF这一经典的基于为表面理论的模型实现的个模型目前已经被广泛接受并且运用于许多现代的渲染器和引擎中。

### Diffuse BRDF

在Diffuse BRDF方面，我选择使用Lambertian Diffuse。该公式与Burley’s diffuse model这样的模型相比，在显示效果的方面，只有轻微的差异，而计算量大大减少。
$$
f(l,v) = \frac{{C}_{diff}}{\pi}
$$

### Cook-Torrance BRDF

在BRDF模型这块我选择Cook-Torrance BRDF，这是实践中使用最广泛的模型，它仅对几何光学系统中的单层微表面上的单个散射进行建模，没有考虑多次散射，分层材质，以及衍射。展示一下该公式的一个整体：
$$
{L}_{0}(p, {\omega}_{0}) = \int^{}_{\Omega}{{k}_{d}\frac{c}{\pi}+\frac{DFG}{4({\omega}_{0}\cdot n)({\omega}_{i}\cdot n)(p,{\omega}_{i})n\cdot {\omega}_{i}d{\omega}_{i}}}
$$

#### Specular D

For the normal distribution function, the choice was the GGX / Trowbridge-Reitz, as adopted by Disney. Compared to the Blinn-Phong, the GGX more accurately simulates surfaces with high roughness. It offers stable specular highlights across various roughness levels, and the additional computational cost is relatively insignificant. Currently, GGX has become the preferred choice in many game engines, renderers, and film production tools. Let's delve into the formula for this:
$$
f(l,v)=\frac{a^2}{\pi(n\cdot h)^2(a^2 -1)^2}
$$
Code:

```c++
float microfacetDistribution(PBRInfo pbrInputs)
{
	float roughnessSq = pbrInputs.alphaRoughness * pbrInputs.alphaRoughness;
	float f = (pbrInputs.NdotH * roughnessSq - pbrInputs.NdotH) * pbrInputs.NdotH + 1.0;
	return roughnessSq / (M_PI * f * f);
}
```

#### Specular G

For the specular geometric attenuation, I opted for the Smith geometric attenuation function tailored for the GGX distribution. This choice was informed by the particular effectiveness of Smith-GGX at high roughness levels, as it can produce broad specular tails, immensely beneficial when visualizing rough materials. Across varying degrees of roughness, the GGX consistently offers stable highlights. The geometric occlusion function for Smith-GGX is:
$$
G(v,l,a) = {G}_{1}(v\cdot a)\cdot {G}_{1}(l,a)
$$
Here, G1 is the one-directional occlusion function. For the GGX distribution, its form is:
$$
{G}_{1}(v,a) = \frac{2(n\cdot v)}{(n\cdot v)+\sqrt{a^2 + (1-a^2)(n\cdot v)^2}}
$$
Code:

```glsl
float geometricOcclusion(PBRInfo pbrInputs)
{
	float NdotL = pbrInputs.NdotL;
	float NdotV = pbrInputs.NdotV;
	float r = pbrInputs.alphaRoughness;

	float attenuationL = 2.0 * NdotL / (NdotL + sqrt(r * r + (1.0 - r * r) * (NdotL * NdotL)));
	float attenuationV = 2.0 * NdotV / (NdotV + sqrt(r * r + (1.0 - r * r) * (NdotV * NdotV)));
	return attenuationL * attenuationV;
}
```

#### Specular F

对于Fresnel，我和UE4一样选择使用Schlick的Fresnel近似。这个函数是一个简化公式，对于实时渲染来说非常的高效，也是作为性能的关键考虑因素。而尽管这是一个简化的公式，Shlick近似在大多数常见的观察角度下都能产生和其他Fresnel方程相似的结果，这使得这个公式对于这个解决方案来说是最优选。这个公式也非常的简单：
$$
F(\theta) = {F}_{0} + (1-{F}_{0})(1-\cos{\theta})^5
$$
Code:

```glsl
vec3 specularReflection(PBRInfo pbrInputs)
{
	return pbrInputs.reflectance0 + (pbrInputs.reflectance90 - pbrInputs.reflectance0) * pow(clamp(1.0 - pbrInputs.VdotH, 0.0, 1.0), 5.0);
}
```

## Environment Lighting

接下来是光照的部分，对于基础的Lambertian 反射和Blinn-Phong 反射的实现原理，我相信已经众所周知了，我不过多赘述。主要还是IBL的部分，实现总共分为几个部分：漫反射环境光照、重要性采样，预处理环境贴图，环境BRDF。

### Irradiance Environment Mapping

For the diffuse environmental lighting component, I opted for the traditional Irradiance Environment Mapping technique. This technique computes the irradiance for the diffuse portion of an object. While it's not exclusive to physically based rendering, it proves instrumental for diffuse illumination in PBR, given its capability to swiftly estimate the impact of environmental light on an object. First, let's look at the foundational equation:
$$
E(n)=\int^{}_{\omega \in hemishphere}{L(\omega)\cdot \cos{\theta}\cdot\sin{\theta}\cdot d\omega}
$$
Code:

```glsl
#define PI 3.1415926535897932384626433832795

void main()
{
	vec3 N = normalize(inPos);
	vec3 up = vec3(0.0, 1.0, 0.0);
	vec3 right = normalize(cross(up, N));
	up = cross(N, right);

	const float TWO_PI = PI * 2.0;
	const float HALF_PI = PI * 0.5;

	vec3 color = vec3(0.0);
	uint sampleCount = 0u;
	for (float phi = 0.0; phi < TWO_PI; phi += consts.deltaPhi) {
		for (float theta = 0.0; theta < HALF_PI; theta += consts.deltaTheta) {
			vec3 tempVec = cos(phi) * right + sin(phi) * up;
			vec3 sampleVector = cos(theta) * N + sin(theta) * tempVec;
			color += texture(samplerEnv, sampleVector).rgb * cos(theta) * sin(theta);
			sampleCount++;
		}
	}
	outColor = vec4(PI * color / float(sampleCount), 1.0);
}
```

### Offline Rendering

参考目前游戏业界主流的做法是，基于Split Sum Approximation的思路，将公式 
$$
\int_{\Omega}{L}_{i}(l)f(l,v)\cos{{\theta}_{l}\cdot dl \approx \frac{1}{N}\sum_{k=1}^{N}\frac{{L}_{i}({l}_{k})f({l}_{k},v)\cos{{\theta}_{{l}_{k}}}}{p({l}_{k},v)}}
$$
拆分为光亮度的均值
$$
\frac{1}{N}\sum_{k=1}^{N}{L}_{i}({l}_{k})
$$
和环境BRDF的
$$
\frac{1}{N}\sum_{k=1}^{N}\frac{f({l}_{k},v)\cos{{\theta}_{{l}_{k}}}}{p({l}_{k},v)}
$$
两项公式。

完成拆分之后，分别对两项进行离线预计算，去匹配离线渲染参考值的渲染结果。

### Pre-filtered environment map

首先是第一项的实现，该公式可以被理解为对Li(lk)求均值。经过n = v = r 的假设，仅取决于表面的粗糙度和反射向量。这一项业界的做法比较统一（包括UE4和COD：Black Ops2等）。采用的方案主要借助通过预过滤环境贴图，然后用多级模糊的mipmap来存储模糊的环境高光。

![tex_prefiltered_cube_mipchain_0](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_0.png)

![tex_prefiltered_cube_mipchain_1](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_1.png)

![tex_prefiltered_cube_mipchain_2](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_2.png)

![tex_prefiltered_cube_mipchain_3](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_3.png)

![tex_prefiltered_cube_mipchain_4](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_4.png)

### Environment BRDF

第二项也就是镜面反射项的半球方向反射率（hemispherical-directional reflectance），可以理解为环境BRDF。其取决于仰角θ，粗糙度α和Fresnel项F。本解决方案选择参考UE4的做法，生成一个BRDFLUT。

![BRDFLUT](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/BRDFLUT.png)

这是关于roughness、cosθ与环境BRDF镜面反射强度的固有映射关系。可以离线预计算。

## Key Features

- [x] Loading glTF 2.0 models

  - [x] Full node hierarchy
  - [x] Full PBR material support
    - [x] Metallic-Roughness
    - [x] Specular-Glossiness workflow

  <img src="https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/boombox.png" alt="boombox" style="zoom:23%;" />

  - [x] Animations
    - [x] Articulated (translate, rotate, scale)
    - [x] Skinned

  ![animation](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/animation.gif)

- [x] PBR

  - [x] Metallic-Roughness workflow
  - [x] Specular-Glossiness workflow


- [x] Environment Lighting
  - [x] Diffuse
  - [x] Blin-Phong
  - [x] Image Based Lighting(IBL)

### Dependencies:

- [GLM](https://github.com/g-truc/glm)
- [GLFW 3](https://github.com/glfw/glfw)
- [stb_image](https://github.com/nothings/stb)
- [Dear ImGUI](https://github.com/ocornut/imgui)
- [TinyGltf](https://github.com/syoyo/tinygltf)
- [Renderdoc API](https://github.com/baldurk/renderdoc)
- [cmft](https://github.com/dariomanesku/cmft)



