# Vulkan PBR Renderer

## Description

This project is a PBR renderer implemented on a framework built with Vulkan 1.3. The specific implementation of PBR references the UE4 engine. I chose to use glTF2.0 models for the modelling aspect. For lighting, I implemented Lambertian reflection, Blinn-Phong reflection and Image Based Lighting.

![environment_test](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/environment_test.png)

## Video link

https://youtu.be/aSoyZY3B76w

## PBR

In terms of PBR (Physically Based Rendering) implementation, this solution primarily draws inspiration from the approach presented in [Real Shading in Unreal Engine 4], published by UE4 in 2013. The choice of this methodology stems from its foundation on the Cook-Torrance BRDF, a classic model rooted in surface theory. This model has garnered widespread acceptance and has been integrated into numerous contemporary rendering engines and platforms.

### Diffuse BRDF

For the Diffuse BRDF, I opted for the Lambertian Diffuse. When juxtaposed with models like Burley's diffuse, the visual differences are subtle, yet the computational overhead is substantially reduced.
```math
f(l,v) = \frac{{C}_{diff}}{\pi}
```

### Cook-Torrance BRDF

In the realm of BRDF models, I have chosen the Cook-Torrance BRDF. This model is the most commonly employed in practice. It models the scattering from a single layer of microsurfaces in a geometric optical system, neglecting considerations of multiple scatterings, layered materials, and diffraction. Let's delve into the holistic representation of this formula:
```math
{L}_{0}(p,{\omega}_{0})=\int^{}_{\Omega}{{k}_{d}\frac{c}{\pi}+\frac{DFG}{4({\omega}_{0}\cdot\n)({\omega}_{i}\cdot n)(p,{\omega}_{i})n\cdot{\omega}_{i}d{\omega}_{i}}}
```
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

For the Fresnel component, I, along with UE4, have opted to use Schlick's Fresnel approximation. This function serves as a simplified equation, making it especially efficient for real-time rendering and is a pivotal consideration in terms of performance. Despite its simplification, the Schlick approximation yields results akin to other Fresnel equations across the majority of commonly observed angles. This positions the equation as an optimal choice for this solution. The formula itself is also quite straightforward:
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

Moving on to the lighting component, the foundational principles behind Lambertian Reflection and Blinn-Phong Reflection are, I believe, well-understood, so I won't delve too deeply into them. The primary focus here is on Image-Based Lighting (IBL). The implementation is divided into several segments: Diffuse Environmental Lighting, Importance Sampling, Pre-processing of the Environment Map, and Environmental BRDF.

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

Drawing from prevalent practices in the gaming industry, a Split Sum Approximation approach is employed. Here, the formula
$$
\int_{\Omega}{L}_{i}(l)f(l,v)\cos{{\theta}_{l}\cdot dl \approx \frac{1}{N}\sum_{k=1}^{N}\frac{{L}_{i}({l}_{k})f({l}_{k},v)\cos{{\theta}_{{l}_{k}}}}{p({l}_{k},v)}}
$$
is divided into two constituent formulas, one representing the mean luminance 
$$
\frac{1}{N}\sum_{k=1}^{N}{L}_{i}({l}_{k})
$$
and the other representing the Environmental BRDF
$$
\frac{1}{N}\sum_{k=1}^{N}\frac{f({l}_{k},v)\cos{{\theta}_{{l}_{k}}}}{p({l}_{k},v)}
$$
Once the split is accomplished, offline pre-computation is performed individually for both components to align with the rendering results of the offline rendering reference value.

### Pre-filtered environment map

Let's first delve into the implementation of the first component. This formula can be conceptualized as obtaining the mean of L~i~(l~k~). Based on the assumption *n*=*v*=*r*, it is contingent solely on the surface roughness and reflection vector. The industry standard for implementing this component is quite uniform, evident in platforms such as UE4 and COD: Black Ops 2. The strategy primarily leans on pre-filtering the environment map and subsequently leveraging multi-level blurring mipmaps to store the blurred environmental highlights.

![tex_prefiltered_cube_mipchain_0](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_0.png)

![tex_prefiltered_cube_mipchain_1](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_1.png)

![tex_prefiltered_cube_mipchain_2](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_2.png)

![tex_prefiltered_cube_mipchain_3](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_3.png)

![tex_prefiltered_cube_mipchain_4](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/tex_prefiltered_cube_mipchain_4.png)

### Environment BRDF

The second component pertains to the hemispherical-directional reflectance of the specular reflection term, which can be understood as the Environmental Bidirectional Reflectance Distribution Function (BRDF). This is influenced by the zenith angle *θ*, roughness *α*, and the Fresnel term *F*.

![BRDFLUT](https://github.com/FishermanSun666/Vulkan_PBR/blob/master/ScreenShoot/BRDFLUT.png)

For this solution, we've opted to follow the methodology presented by UE4, which entails generating a BRDF Look-Up Texture (LUT). This LUT represents an intrinsic mapping relationship between roughness, cos*θ*, and the intensity of environmental BRDF specular reflection. This relationship can be pre-computed offline, allowing for efficient real-time lookups during rendering.

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



