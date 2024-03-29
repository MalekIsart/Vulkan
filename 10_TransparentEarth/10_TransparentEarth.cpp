
// A definir en premier lieu
#if defined(_WIN32)
#define WIN32_MEAN_AND_LEAN		1
#define WIN32_EXTRA_LEAN		1
// a definir globalement pour etre pris en compte par volk.c
//#define VK_USE_PLATFORM_WIN32_KHR

// si glfw
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__APPLE__)
#elif defined(_GNUC_)
#endif

#if defined(_MSC_VER)
//
// Pensez a copier les dll dans le repertoire x64/Debug, cad:
// glfw-3.3/lib-vc2015/glfw3.dll
//
#pragma comment(lib, "glfw3dll.lib")
#pragma comment(lib, "vulkan-1.lib")
#endif

#if defined(_DEBUG)
#define VULKAN_ENABLE_VALIDATION
#endif

#include "volk/volk.h"
//#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <iostream>
#include <fstream>
#include <vector>
#include <array>

//#define OPENGL_NDC

#define GLM_FORCE_RADIANS
#ifndef OPENGL_NDC
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm/glm.hpp>
#include <glm/glm/gtc/matrix_transform.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

#ifdef _DEBUG
#define DEBUG_CHECK_VK(x) if (VK_SUCCESS != (x)) { std::cout << (#x) << std::endl; __debugbreak(); }
#else
#define DEBUG_CHECK_VK(x) 
#endif

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();

	return buffer;
}

enum PixelFormat
{
	PIXFMT_RGBA8,
	PIXFMT_SRGBA8,
	PIXFMT_RGBA16F,
	PIXFMT_RGBA32F,
	PIXFMT_DUMMY_ASPECT_DEPTH,
	PIXFMT_DEPTH32F = PIXFMT_DUMMY_ASPECT_DEPTH,
	MAX
};

enum ImageUsageBits
{
	IMAGE_USAGE_TEXTURE = 1<<0,
	IMAGE_USAGE_BITMAP = 1<<1,
	IMAGE_USAGE_RENDERTARGET = 1<<2,
	IMAGE_USAGE_RENDERPASS = 1<<6,
	IMAGE_USAGE_STAGING = 1<<7
};
typedef uint32_t ImageUsage;


struct RenderSurface
{
	VkDeviceMemory memory;
	VkImage image;
	VkImageView view;
	VkFormat format;

	bool CreateSurface(struct VulkanRenderContext& rendercontext, int width, int height, PixelFormat pixelformat, ImageUsage usage = IMAGE_USAGE_TEXTURE|IMAGE_USAGE_BITMAP);
	void Destroy(struct VulkanRenderContext& rendercontext);
};

struct Texture : RenderSurface
{
	VkSampler sampler;

	bool Load(struct VulkanRenderContext& rendercontext, const char* filepath, bool sRGB = true);
	void Destroy(struct VulkanRenderContext& rendercontext);
};

struct VulkanDeviceContext
{
	static const int MAX_DEVICE_COUNT = 9;	// arbitraire, max = IGP + 2x4 GPU max
	static const int MAX_FAMILY_COUNT = 4;	// graphics, compute, transfer, graphics+compute (ajouter sparse aussi...)
	static const int SWAPCHAIN_IMAGES = 2;

	VkDebugReportCallbackEXT debugCallback;

	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;

	VkSurfaceKHR surface;
	VkSurfaceFormatKHR surfaceFormat;
	VkSwapchainKHR swapchain;
	VkExtent2D swapchainExtent;
	VkImage swapchainImages[SWAPCHAIN_IMAGES];
	VkImageView swapchainImageViews[SWAPCHAIN_IMAGES];
	VkSemaphore acquireSemaphores[SWAPCHAIN_IMAGES];
	VkSemaphore presentSemaphores[SWAPCHAIN_IMAGES];

	VkPhysicalDeviceProperties props;
	std::vector<VkMemoryPropertyFlags> memoryFlags;

	VkShaderModule createShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			std::cout << "failed to create shader module!" << std::endl;
		}

		return shaderModule;
	}

	uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
		int i = 0;
		for (auto propertyFlags : memoryFlags) {
			if ((typeFilter & (1 << i)) && (propertyFlags & properties) == properties) {
				return i;
			}
			i++;
		}

		std::cout << "failed to find suitable memory type!" << std::endl;
		return 0;
	}
};

struct Buffer
{
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize size;
	void* data;	// != nullptr => persistent
				// optionnels
	VkDeviceSize offset;
	VkBufferUsageFlags usage;
	VkMemoryPropertyFlags properties;
};

struct VulkanRenderContext
{
	static const int PENDING_FRAMES = 2;

	VulkanDeviceContext* context;

	uint32_t graphicsQueueIndex;
	uint32_t presentQueueIndex;
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkCommandPool mainCommandPool;
	VkCommandBuffer mainCommandBuffers[PENDING_FRAMES];
	VkFence mainFences[PENDING_FRAMES];
	VkFramebuffer framebuffers[PENDING_FRAMES];
	VkRenderPass renderPass;

	VkImageSubresourceRange mainSubRange;

	Buffer stagingBuffer;

	RenderSurface depthBuffer;

	VkPipeline mainPipelineOpaque;
	VkPipeline mainPipelineCutOut;
	VkPipeline mainPipelineTransparent;
	VkPipeline mainPipelineTransparentCullFront;

	VkPipeline mainPipelineEnvMap;

	VkPipelineLayout mainPipelineLayout;
};


bool RenderSurface::CreateSurface(VulkanRenderContext& rendercontext, int width, int height, PixelFormat pixelformat, ImageUsage usage)
{
	VulkanDeviceContext& context = *rendercontext.context;

	// todo: fonction pixelformat to format
	// todo: stencil, RGBA float16/32 etc..
	switch (pixelformat)
	{
	case PIXFMT_DEPTH32F: format = VK_FORMAT_D32_SFLOAT; break; // 32 bit signed float
	case PIXFMT_RGBA32F: format = VK_FORMAT_R32G32B32A32_SFLOAT; break;
	case PIXFMT_RGBA16F: format = VK_FORMAT_R16G16B16A16_SFLOAT; break;
	case PIXFMT_SRGBA8: format = VK_FORMAT_R8G8B8A8_SRGB; break;
	case PIXFMT_RGBA8:
	default: format = VK_FORMAT_R8G8B8A8_UNORM; break;
	}

	// TODO: staging
	VkImageUsageFlags usageFlags = 0;
	if (usage & IMAGE_USAGE_TEXTURE) {
		usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		if (usage & IMAGE_USAGE_BITMAP)
			usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if (usage & IMAGE_USAGE_RENDERTARGET) {
		usageFlags |= pixelformat < PIXFMT_DEPTH32F ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		if (usage & IMAGE_USAGE_RENDERPASS)
			usageFlags |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
	}

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.flags = 0;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1; // <- 3D
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = usageFlags;
	// | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
	imageInfo.samples = (VkSampleCountFlagBits)1;//VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(context.device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
		std::cout << "error: failed to create render target image!" << std::endl;
		return false;
	}

	{
		VkMemoryRequirements memRequirements;
		vkGetImageMemoryRequirements(context.device, image, &memRequirements);

		VkMemoryAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memRequirements.size;
		// LAZILY_ALLOCATED couple a USAGE_TRANSIENT est utile sur mobile pour indiquer
		// que la zone memoire est volatile / temporaire et qu'elle peut etre utilisee
		// par toute autre partie du rendering lorsque notre render pass ne dessine pas dedans
		// sur PC cela ne semble pas supporte par tous les drivers
		VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;//LAZILY_ALLOCATED_BIT;
		allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);
		if (allocInfo.memoryTypeIndex == ~0) {
			properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, properties);
		}
		if (vkAllocateMemory(context.device, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
			std::cout << "error: failed to allocate image memory!" << std::endl;
			return false;
		}
		if (vkBindImageMemory(context.device, image, memory, 0) != VK_SUCCESS) {
			std::cout << "error: failed to bind image memory!" << std::endl;
			return false;
		}

		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = format;
		viewInfo.subresourceRange = rendercontext.mainSubRange;
		// l'IMAGE_ASPECT doit correspondre au pixel format (todo: stencil)
		viewInfo.subresourceRange.aspectMask = pixelformat < PIXFMT_DUMMY_ASPECT_DEPTH ? VK_IMAGE_ASPECT_COLOR_BIT : VK_IMAGE_ASPECT_DEPTH_BIT;
		// todo: add swizzling here when required

		if (vkCreateImageView(context.device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
			std::cout << "failed to create texture image view!" << std::endl;
			return false;
		}
	}

	return true;
}

void RenderSurface::Destroy(VulkanRenderContext& rendercontext)
{
	VulkanDeviceContext& context = *rendercontext.context;
	vkDestroyImageView(context.device, view, nullptr);
	vkDestroyImage(context.device, image, nullptr);
	vkFreeMemory(context.device, memory, nullptr);
}

bool Texture::Load(VulkanRenderContext& rendercontext, const char* filepath, bool sRGB)
{
	VulkanDeviceContext& context = *rendercontext.context;
	Buffer& stagingBuffer = rendercontext.stagingBuffer;

	int w, h, c;
	uint32_t imageSize = 0;
	uint8_t* pixels = nullptr;
	PixelFormat pixelFormat = PIXFMT_RGBA8;
	if (!stbi_is_hdr(filepath))
	{
		// pour des raisons de simplicite on force en RGBA quel que ce soit le format d'origine
		pixels = stbi_load(filepath, &w, &h, &c, STBI_rgb_alpha);
		imageSize = w * h * c;
		pixelFormat = sRGB ? PIXFMT_SRGBA8 : PIXFMT_RGBA8;
	}
	else
	{
		// pour des raisons de simplicite on force en RGBA quel que ce soit le format d'origine
		pixels = (uint8_t*)stbi_loadf(filepath, &w, &h, &c, STBI_rgb_alpha);
		imageSize = w * h * c * sizeof(float);
		pixelFormat = PIXFMT_RGBA32F;
	}

	if (pixels == nullptr) {
		return false;
	}

	memcpy(stagingBuffer.data, pixels, imageSize);
	
	stbi_image_free(pixels);

	RenderSurface::CreateSurface(rendercontext, w, h, pixelFormat);

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;
	DEBUG_CHECK_VK(vkCreateSampler(context.device, &samplerInfo, nullptr, &sampler));

	VkCommandBufferAllocateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	commandInfo.commandPool = rendercontext.mainCommandPool;
	commandInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(context.device, &commandInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);


	VkImageMemoryBarrier barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	VkPipelineStageFlags sourceStage;
	VkPipelineStageFlags destinationStage;

	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

	vkCmdPipelineBarrier(commandBuffer,
		sourceStage, destinationStage,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	VkBufferImageCopy region = {};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = { 0, 0, 0 };
	region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
	vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
	destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

	vkCmdPipelineBarrier( commandBuffer,
		sourceStage, destinationStage,
		0, 0, nullptr, 0, nullptr, 1, &barrier);

	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(rendercontext.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(rendercontext.graphicsQueue);

	vkFreeCommandBuffers(context.device, rendercontext.mainCommandPool, 1, &commandBuffer);

	return true;
}

void Texture::Destroy(VulkanRenderContext& rendercontext)
{
	VulkanDeviceContext& context = *rendercontext.context;
	vkDestroySampler(context.device, sampler, nullptr);
	RenderSurface::Destroy(rendercontext);
}

// scene data

struct SceneMatrices
{
	enum BufferType {
		GLOBAL = 0,
		DYNAMIC = 1,
		MAX
	};

	// Global data
	glm::mat4 view;
	glm::mat4 projection;
	
	// Instance data
	glm::mat4 world;

	Buffer constantBuffers[BufferType::MAX]; // double buffer pas obligatoire ici
};

struct Color
{
	uint8_t r, g, b, a;
};

struct Vertex
{
	glm::vec3 position;
	glm::vec2 uv;
	glm::vec3 normal;
};

struct Mesh
{
	std::vector<Vertex> vertices;
	std::vector<uint16_t> indices;

	enum BufferType {
		VBO = 0,
		IBO = 1,
		MAX
	};
	Buffer staticBuffers[BufferType::MAX];
};

struct Scene
{
	SceneMatrices matrices;
	std::vector<Mesh> meshes;

	std::vector<Texture> textures;

	// GPU scene
	// Un DescriptorSet ne peut pas etre update ou utilise par un command buffer
	// alors qu'il est "bind" par un autre command buffer
	// On va donc avoir des descriptorSets par frame/command buffer
	VkDescriptorSet descriptorSet[(SceneMatrices::BufferType::MAX + 2/*textures*/) * 2];
	VkDescriptorSetLayout descriptorSetLayout[SceneMatrices::BufferType::MAX + 1/*texture*/];
	VkDescriptorPool descriptorPool;
};

void GenerateQuadStrip(Mesh& mesh)
{
	mesh.indices.reserve(4);
	mesh.vertices.reserve(4);

	mesh.vertices.push_back({ { -1.f, +1.f, 0.f },{ 0.f, 0.f },{ 0.f, 0.f, 1.f } });
	mesh.vertices.push_back({ { -1.f, -1.f, 0.f },{ 0.f, 0.f },{ 0.f, 0.f, 1.f } });
	mesh.vertices.push_back({ { +1.f, +1.f, 0.f },{ 0.f, 0.f },{ 0.f, 0.f, 1.f } });
	mesh.vertices.push_back({ { +1.f, -1.f, 0.f },{ 0.f, 0.f },{ 0.f, 0.f, 1.f } });
	mesh.indices.push_back(0);
	mesh.indices.push_back(1);
	mesh.indices.push_back(2);
	mesh.indices.push_back(3);
}

// 
// Sphere procedurale par revolution avec une typologie TRIANGLE STRIP
// il faut inverser l'ordre des indices pour changer le winding (voir plus bas)
// La sphere est ici definie en CCW
void GenerateSphere(Mesh& mesh, int horizSegments, int vertiSegments, float sphereScale = 1.f)
{
	const float PI = 3.14159265359f;
	
	mesh.indices.reserve((vertiSegments + 1)*(horizSegments + 1));
	mesh.vertices.reserve((vertiSegments + 1)*(horizSegments + 1));

	for (unsigned int y = 0; y <= vertiSegments; ++y)
	{
		for (unsigned int x = 0; x <= horizSegments; ++x)
		{
			float xSegment = (float)x / (float)horizSegments;
			float ySegment = (float)y / (float)vertiSegments;
			float theta = ySegment * PI;
			float phi = xSegment * 2.0f * PI;
			float xPos = std::cos(phi) * std::sin(theta);
			float yPos = std::cos(theta);
			float zPos = std::sin(phi) * std::sin(theta);
			Vertex vtx{
				glm::vec3(xPos*sphereScale, yPos*sphereScale, zPos*sphereScale),
				glm::vec2(xSegment, ySegment),
				glm::vec3(xPos, yPos, zPos)
			};
			mesh.vertices.push_back(vtx);
		}
	}

	bool oddRow = false;
	for (int y = 0; y < vertiSegments; ++y)
	{
		if (!oddRow) // even rows: y == 0, y == 2; and so on
		{
			for (int x = 0; x <= horizSegments; ++x)
			{
				// (y) suivi de (y+1) -> CW
				// (y+1) suivi de (y) -> CCW
				mesh.indices.push_back((y + 1) * (horizSegments + 1) + x);
				mesh.indices.push_back(y       * (horizSegments + 1) + x);
			}
		}
		else
		{
			for (int x = horizSegments; x >= 0; --x)
			{
				// (y+1) suivi de (y) -> CW
				// (y) suivi de (y+1) -> CCW
				mesh.indices.push_back(y       * (horizSegments + 1) + x);
				mesh.indices.push_back((y + 1) * (horizSegments + 1) + x);
			}
		}
		oddRow = !oddRow;
	}
}


struct VulkanGraphicsApplication
{
	VulkanDeviceContext context;
	VulkanRenderContext rendercontext;
	GLFWwindow* window;

	Scene scene;

	uint32_t m_imageIndex;
	uint32_t m_frame;
	uint32_t m_currentFrame;

	bool Initialize();
	bool Run();
	bool Update();
	bool Begin();
	bool End();
	bool Display();
	bool Shutdown();
};

static VKAPI_ATTR VkBool32 VKAPI_CALL VulkanReportFunc(
	VkDebugReportFlagsEXT flags,
	VkDebugReportObjectTypeEXT objType,
	uint64_t obj,
	size_t location,
	int32_t code,
	const char* layerPrefix,
	const char* msg,
	void* userData)
{
	std::cout << "[VULKAN VALIDATION]" << msg << std::endl;
#ifdef _WIN32
	if (IsDebuggerPresent()) {
		OutputDebugStringA("[VULKAN VALIDATION] ");
		OutputDebugStringA(msg);
		OutputDebugStringA("\n");
	}
#endif
	return VK_FALSE;
}

bool VulkanGraphicsApplication::Initialize()
{
	// Vulkan

	DEBUG_CHECK_VK(volkInitialize());

	// instance

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "10_TransparentEarth";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "todo engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_0;

	const char *extensionNames[] = { VK_KHR_SURFACE_EXTENSION_NAME
#if defined(_WIN32)
		, VK_KHR_WIN32_SURFACE_EXTENSION_NAME
#endif
		, VK_EXT_DEBUG_REPORT_EXTENSION_NAME };
	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;
	instanceInfo.enabledExtensionCount = sizeof(extensionNames) / sizeof(char*);
	instanceInfo.ppEnabledExtensionNames = extensionNames;
#ifdef VULKAN_ENABLE_VALIDATION
	const char* layerNames[] = { "VK_LAYER_LUNARG_standard_validation" };
	instanceInfo.enabledLayerCount = 1;
	instanceInfo.ppEnabledLayerNames = layerNames;
#else
	instanceInfo.enabledExtensionCount--;
#endif
	DEBUG_CHECK_VK(vkCreateInstance(&instanceInfo, nullptr, &context.instance));
	// TODO: fallback si pas de validation possible (MoltenVK, toujours le cas ?)

	volkLoadInstance(context.instance);

#ifdef VULKAN_ENABLE_VALIDATION
	VkDebugReportCallbackCreateInfoEXT debugCallbackInfo = {};
	debugCallbackInfo.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
	debugCallbackInfo.flags = VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
		VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT | VK_DEBUG_REPORT_INFORMATION_BIT_EXT;
	debugCallbackInfo.pfnCallback = VulkanReportFunc;
	vkCreateDebugReportCallbackEXT(context.instance, &debugCallbackInfo, nullptr, &context.debugCallback);
#endif

	// render surface

#if defined(USE_GLFW_SURFACE)
	glfwCreateWindowSurface(g_Context.instance, g_Context.window, nullptr, &g_Context.surface);
#else
#if defined(_WIN32)
	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hinstance = GetModuleHandle(NULL);
	surfaceInfo.hwnd = glfwGetWin32Window(window);
	DEBUG_CHECK_VK(vkCreateWin32SurfaceKHR(context.instance, &surfaceInfo, nullptr, &context.surface));
#endif
#endif

	// device

	uint32_t num_devices = context.MAX_DEVICE_COUNT;
	std::vector<VkPhysicalDevice> physical_devices(num_devices);
	DEBUG_CHECK_VK(vkEnumeratePhysicalDevices(context.instance, &num_devices, &physical_devices[0]));

	context.physicalDevice = physical_devices[0];

	// capacites du GPU
	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(context.physicalDevice, &deviceProperties);
	// exemples d'infos utiles
	//deviceProperties.limits.maxSamplerAnisotropy;
	//deviceProperties.limits.maxPushConstantsSize;

	// enumeration des memory types
	VkPhysicalDeviceMemoryProperties memoryProperties;
	vkGetPhysicalDeviceMemoryProperties(context.physicalDevice, &memoryProperties);
	context.memoryFlags.reserve(memoryProperties.memoryTypeCount);
	for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++)
		context.memoryFlags.push_back(memoryProperties.memoryTypes[i].propertyFlags);

	rendercontext.graphicsQueueIndex = UINT32_MAX;
	uint32_t queue_families_count = context.MAX_FAMILY_COUNT;
	std::vector<VkQueueFamilyProperties> queue_family_properties(queue_families_count);
	// normalement il faut appeler la fonction une premiere fois pour recuperer le nombre exact de queues supportees.
	//voir les messages de validation
	vkGetPhysicalDeviceQueueFamilyProperties(context.physicalDevice, &queue_families_count, &queue_family_properties[0]);
	for (uint32_t i = 0; i < queue_families_count; ++i) {
		if ((queue_family_properties[i].queueCount > 0) &&
			(queue_family_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
			VkBool32 canPresentSurface;
			vkGetPhysicalDeviceSurfaceSupportKHR(context.physicalDevice, i, context.surface, &canPresentSurface);
			if (canPresentSurface)
				rendercontext.graphicsQueueIndex = i;
			break;
		}
	}

	// on suppose que la presentation se fait par la graphics queue (verifier cela avec vkGetPhysicalDeviceSurfaceSupportKHR())
	rendercontext.presentQueueIndex = rendercontext.graphicsQueueIndex;

	const float queue_priorities[] = { 1.0f };
	VkDeviceQueueCreateInfo queueCreateInfo = {};
	queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueCreateInfo.queueFamilyIndex = rendercontext.graphicsQueueIndex;
	queueCreateInfo.queueCount = 1;
	queueCreateInfo.pQueuePriorities = queue_priorities;

	const char* device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueCreateInfo;
	deviceInfo.enabledExtensionCount = 1;
	deviceInfo.ppEnabledExtensionNames = device_extensions;
	DEBUG_CHECK_VK(vkCreateDevice(context.physicalDevice, &deviceInfo, nullptr, &context.device));

	volkLoadDevice(context.device);

	vkGetDeviceQueue(context.device, rendercontext.graphicsQueueIndex, 0, &rendercontext.graphicsQueue);
	rendercontext.presentQueue = rendercontext.graphicsQueue;

	rendercontext.context = &context;

	// swap chain

	// todo: enumerer (cf validation)
	uint32_t formatCount = 10;
	VkSurfaceFormatKHR surfaceFormats[10];
	vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, surfaceFormats);
	// on sait que le moniteur converti de sRGB vers lineaire
	// mais nos shader ecrivent du lineaire (nos calculs sont lineaires)
	// il faut donc forcer l'ecriture dans le framebuffer en sRGB
	// VK_FORMAT_***_SRGB -> conversion automatique lineaire vers sRGB
	// attention ce n'est valable que pour la swapchain ici
	for (int i = 0; i < formatCount; i++) {
		if (surfaceFormats[i].format == VK_FORMAT_R8G8B8A8_SRGB ||
			surfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB) {
			context.surfaceFormat = surfaceFormats[i];
			break;
		}
	}
	if (context.surfaceFormat.format == VK_FORMAT_UNDEFINED)
		__debugbreak();

	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;   // FIFO garanti.
															   // VK_PRESENT_MODE_IMMEDIATE_KHR
															   // VK_PRESENT_MODE_FIFO_RELAXED_KHR
	uint32_t swapchainImageCount = context.SWAPCHAIN_IMAGES;

	VkSurfaceCapabilitiesKHR surfaceCapabilities;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(context.physicalDevice, context.surface, &surfaceCapabilities);
	context.swapchainExtent = surfaceCapabilities.currentExtent;
	VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // garanti
	if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
		imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT; // necessaire ici pour vkCmdClearImageColor
													   //	if (surfaceCapabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
													   //		imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT; // necessaire ici pour screenshots, read back

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = context.surface;
	swapchainInfo.minImageCount = swapchainImageCount;
	swapchainInfo.imageFormat = context.surfaceFormat.format;
	swapchainInfo.imageColorSpace = context.surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = context.swapchainExtent;
	swapchainInfo.imageArrayLayers = 1; // 2 for stereo
	swapchainInfo.imageUsage = imageUsage;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainInfo.preTransform = surfaceCapabilities.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = presentMode;
	swapchainInfo.clipped = VK_TRUE;
	DEBUG_CHECK_VK(vkCreateSwapchainKHR(context.device, &swapchainInfo, nullptr, &context.swapchain));

	vkGetSwapchainImagesKHR(context.device, context.swapchain, &swapchainImageCount, context.swapchainImages);

	rendercontext.mainSubRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	rendercontext.mainSubRange.baseMipLevel = 0;
	rendercontext.mainSubRange.levelCount = 1;
	rendercontext.mainSubRange.baseArrayLayer = 0;
	rendercontext.mainSubRange.layerCount = 1;

	VkImageViewCreateInfo imageviewInfo = {};
	imageviewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageviewInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY };
	imageviewInfo.format = context.surfaceFormat.format;
	imageviewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageviewInfo.subresourceRange = rendercontext.mainSubRange;
	for (int i = 0; i < context.SWAPCHAIN_IMAGES; i++) {
		imageviewInfo.image = context.swapchainImages[i];
		vkCreateImageView(context.device, &imageviewInfo, nullptr
			, &context.swapchainImageViews[i]);
	}

	// command buffers

	VkCommandPoolCreateInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandInfo.queueFamilyIndex = rendercontext.graphicsQueueIndex;
	vkCreateCommandPool(context.device, &commandInfo, nullptr, &rendercontext.mainCommandPool);

	VkCommandBufferAllocateInfo commandAllocInfo = {};
	commandAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	commandAllocInfo.commandPool = rendercontext.mainCommandPool;
	commandAllocInfo.commandBufferCount = 2;
	commandAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	vkAllocateCommandBuffers(context.device, &commandAllocInfo
		, rendercontext.mainCommandBuffers);

	VkFenceCreateInfo fenceInfo = {};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // SINON ON BLOQUE SUR LE WAITFORFENCE
	for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		vkCreateFence(context.device, &fenceInfo, nullptr, &rendercontext.mainFences[i]);
	}
	VkSemaphoreCreateInfo semInfo = {};
	semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (int i = 0; i < context.SWAPCHAIN_IMAGES; i++) {
		vkCreateSemaphore(context.device, &semInfo, nullptr, &context.acquireSemaphores[i]);
		vkCreateSemaphore(context.device, &semInfo, nullptr, &context.presentSemaphores[i]);
	}

	enum RenderTarget {
		SWAPCHAIN = 0,
		DEPTH = 1,
		MAX
	};

	// creation d'une render surface pour le depth buffer
	// on va donc avoir un second attachment mais de type depth/stencil
	// le depth buffer n'est utilise qu'en lecture/ecriture dans la passe principale
	// il est important de le clear en debut de passe mais inutile de conserver son contenu
	// Par contre, dans le cas ou un effet a besoin d'acceder au depth buffer, 
	// il faut specifier STORE_OP_STORE pour le champ storeOp du depth attachment
	RenderSurface& depthBuffer = rendercontext.depthBuffer;
	depthBuffer.CreateSurface(rendercontext, context.swapchainExtent.width, context.swapchainExtent.height, PIXFMT_DEPTH32F, IMAGE_USAGE_RENDERTARGET);

	VkAttachmentDescription attachDesc[RenderTarget::MAX] = {};
	int id = RenderTarget::SWAPCHAIN;
	attachDesc[id].format = context.surfaceFormat.format;
	attachDesc[id].samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc[id].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc[id].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachDesc[id].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc[id].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[id].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc[id].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	id = RenderTarget::DEPTH;
	attachDesc[id].format = rendercontext.depthBuffer.format;
	attachDesc[id].samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc[id].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc[id].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;		// on ne souhaite pas reutiliser le depth buffer ici
	attachDesc[id].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc[id].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[id].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc[id].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference swapAttachRef[2] = {};
	id = RenderTarget::SWAPCHAIN;
	swapAttachRef[id].attachment = RenderTarget::SWAPCHAIN;
	swapAttachRef[id].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	id = RenderTarget::DEPTH;
	swapAttachRef[id].attachment = RenderTarget::DEPTH;
	swapAttachRef[id].layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// TODO : subpass dependencies pour transitionner automatiquement le depth buffer

	VkSubpassDescription subpasses[1] = {};
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = swapAttachRef;
	subpasses[0].pDepthStencilAttachment = &swapAttachRef[1];

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachDesc;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = subpasses;
	vkCreateRenderPass(context.device, &renderPassInfo, nullptr, &rendercontext.renderPass);


	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = rendercontext.renderPass;
	framebufferInfo.layers = 1;
	framebufferInfo.width = context.swapchainExtent.width;
	framebufferInfo.height = context.swapchainExtent.height;
	framebufferInfo.attachmentCount = 2;	// attention, 2 attachments maintenant
	// les deux framebuffers utilisent le meme depth buffer
	VkImageView framebufferAttachments[2] = { nullptr, rendercontext.depthBuffer.view };
	for (int i = 0; i < rendercontext.PENDING_FRAMES; ++i) {
		framebufferAttachments[0] = context.swapchainImageViews[i];
		framebufferInfo.pAttachments = framebufferAttachments;
		vkCreateFramebuffer(context.device, &framebufferInfo, nullptr, &rendercontext.framebuffers[i]);
	}

	// On cree un staging buffer "global" pour charger un maximum de ressources (essentiellement statiques)
	Buffer& stagingBuffer = rendercontext.stagingBuffer;
	memset(&stagingBuffer, 0, sizeof(Buffer));
	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = 4096 * 4096 * 4 * sizeof(float);	// maximum RGBA32F en 4k = 256Mio
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingBuffer.size = bufferInfo.size;
	stagingBuffer.usage = bufferInfo.usage;
	DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &stagingBuffer.buffer));
	VkMemoryRequirements memRequirements;
	vkGetBufferMemoryRequirements(context.device, stagingBuffer.buffer, &memRequirements);
	VkMemoryAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocInfo.allocationSize = memRequirements.size;
	allocInfo.memoryTypeIndex = context.findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	DEBUG_CHECK_VK(vkAllocateMemory(context.device, &allocInfo, nullptr, &stagingBuffer.memory));
	vkBindBufferMemory(context.device, stagingBuffer.buffer, stagingBuffer.memory, 0);
	vkMapMemory(context.device, stagingBuffer.memory, 0, VK_WHOLE_SIZE, 0, &stagingBuffer.data);

	//
	// Descriptor Pool & layouts
	//


	VkDescriptorPoolSize poolSizes[2] = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SceneMatrices::MAX * rendercontext.PENDING_FRAMES },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * rendercontext.PENDING_FRAMES }
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	// on alloue strictement le minimum requis (l'ensemble du pool ici)
	descriptorPoolInfo.maxSets = (SceneMatrices::MAX + 2) * rendercontext.PENDING_FRAMES;
	descriptorPoolInfo.poolSizeCount = 2;
	descriptorPoolInfo.pPoolSizes = poolSizes;
	vkCreateDescriptorPool(context.device, &descriptorPoolInfo, nullptr, &scene.descriptorPool);

	VkPipelineLayoutCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	{
		pipelineInfo.pushConstantRangeCount = 1;
		// les specs de Vulkan garantissent 128 octets de push constants
		// voir properties.limits.maxPushConstantsSize
		// dans les faits, la plupart des drivers/GPUs ne supportent pas
		// autant d'octets de maniere optimum
		VkPushConstantRange constantRange = { VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::vec4) };
		pipelineInfo.pPushConstantRanges = &constantRange;

		pipelineInfo.setLayoutCount = 2 + 1;

		// layout : on doit decrire le format de chaque descriptor (binding, type, array count, stage)
		VkDescriptorSetLayoutBinding sceneSetBindings[2 /*UBO*/ + 2 /*SAMPLER*/];
		// set 0
		sceneSetBindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
		// set 1
		sceneSetBindings[1] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
		// set 2
		sceneSetBindings[2] = { 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		sceneSetBindings[3] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		//
		int sceneSetCount = SceneMatrices::MAX + 1;
		int sceneSetBindingsCount[] = { 1, 1, 2 };
		VkDescriptorSetLayoutCreateInfo sceneSetInfo = {};
		sceneSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		// ubos
		for (int i = 0; i < sceneSetCount; i++) {
			sceneSetInfo.bindingCount = sceneSetBindingsCount[i];
			sceneSetInfo.pBindings = &sceneSetBindings[i];
			vkCreateDescriptorSetLayout(context.device, &sceneSetInfo, nullptr, &scene.descriptorSetLayout[i]);
		}

		VkDescriptorSetAllocateInfo allocateDescInfo = {};
		allocateDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateDescInfo.descriptorPool = scene.descriptorPool;
		allocateDescInfo.descriptorSetCount = sceneSetCount;
		allocateDescInfo.pSetLayouts = scene.descriptorSetLayout;
		// on cree les descriptor sets en double buffer (il faut donc allouer 2*N sets)
		for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
			vkAllocateDescriptorSets(context.device, &allocateDescInfo, &scene.descriptorSet[i * sceneSetCount]);
		}
		pipelineInfo.pSetLayouts = scene.descriptorSetLayout;

		vkCreatePipelineLayout(context.device, &pipelineInfo, nullptr, &rendercontext.mainPipelineLayout);
	}

	//
	// Pipeline
	//

	auto vertShaderCode = readFile("shaders/mesh.vert.spv");
	auto fragShaderCode = readFile("shaders/mesh.frag.spv");

	VkShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = context.createShaderModule(fragShaderCode);

	// Common ---

	VkPipelineDepthStencilStateCreateInfo depthStencilInfo = {};
	depthStencilInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_TRUE;
	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	// ATTENTION, TRIANGLE STRIP ICI pour la sphere procedurale
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = (float)context.swapchainExtent.width;
	viewport.height = (float)context.swapchainExtent.height;
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	VkRect2D scissor = {
		{ 0, 0 },{ viewport.width, viewport.height }
	};
	VkPipelineViewportStateCreateInfo viewportInfo = {};
	viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportInfo.viewportCount = 1;
	viewportInfo.pViewports = &viewport;
	viewportInfo.scissorCount = 1;
	viewportInfo.pScissors = &scissor;

	VkPipelineRasterizationStateCreateInfo rasterizationInfo = {};
	rasterizationInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
	// l'objet est defini en CCW
	rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizationInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
	multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	//multisampleInfo.minSampleShading = 1.0f;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = false;
	// blending en mode alpha pre-multiplie, cela ressemble a de l'additif mais implique que 
	// nos valeurs RGB sont multipliees par Alpha en sortie du shader
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;//SRC_ALPHA;
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
	colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfo.attachmentCount = 1;
	colorBlendInfo.pAttachments = &colorBlendAttachment;

	VkGraphicsPipelineCreateInfo gfxPipelineInfo = {};
	gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gfxPipelineInfo.pViewportState = &viewportInfo;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	gfxPipelineInfo.pDepthStencilState = &depthStencilInfo;
	gfxPipelineInfo.pMultisampleState = &multisampleInfo;
	gfxPipelineInfo.pColorBlendState = &colorBlendInfo;
	gfxPipelineInfo.renderPass = rendercontext.renderPass;
	gfxPipelineInfo.basePipelineIndex = -1;
	gfxPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	gfxPipelineInfo.subpass = 0;

	// specific ---

	// VAO / input layout
	uint32_t stride = 0;
	VkVertexInputAttributeDescription vertexInputLayouts[3];
	vertexInputLayouts[0] = { 0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec3);
	vertexInputLayouts[1] = { 1/*location*/, 0/*binding*/, VK_FORMAT_R32G32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec2);
	vertexInputLayouts[2] = { 2/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT/*format*/, stride/*offset*/ };
	stride += sizeof(glm::vec3);
	VkVertexInputBindingDescription vertexInputBindings = {0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindings;
	vertexInputInfo.vertexAttributeDescriptionCount = _countof(vertexInputLayouts);
	vertexInputInfo.pVertexAttributeDescriptions = vertexInputLayouts;
	gfxPipelineInfo.pVertexInputState = &vertexInputInfo;

	// shaders
	VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";
	VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";
	VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

	gfxPipelineInfo.stageCount = 2;
	gfxPipelineInfo.pStages = shaderStages;
	gfxPipelineInfo.layout = rendercontext.mainPipelineLayout;

	//
	// pipeline opaque
	//
	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipelineOpaque);	
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);

	//
	// pipelines non-opaque
	//
	// pipeline cutout
	// comme optimisation, on peut n'ecrire que dans le depth buffer 
	// (idealement il faudrait faire une subpass d'ecriture du depth avec 1 seule depth attachment)
	colorBlendAttachment.colorWriteMask = 0;
	colorBlendAttachment.blendEnable = false;
	
	fragShaderCode.clear();
	fragShaderCode = readFile("shaders/mesh.cutout.frag.spv");
	fragShaderModule = context.createShaderModule(fragShaderCode);
	shaderStages[1].module = fragShaderModule;
	rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipelineCutOut);
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
	
	// pipelines transparents
	// Desactive l'ecriture dans le depth buffer et active le blending
	// si l'on souhaite ecrire les pixels opaques dans le depth buffer il faut
	// qu'un rendu en cutout precede le rendu transparent
	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilInfo.depthWriteEnable = VK_FALSE;
	colorBlendAttachment.blendEnable = true;
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	
	fragShaderCode.clear();
	fragShaderCode = readFile("shaders/mesh.transparent.frag.spv");
	fragShaderModule = context.createShaderModule(fragShaderCode);
	shaderStages[1].module = fragShaderModule;
	// pipeline cull back faces
	rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
	//gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipelineTransparent);

	// pipeline cull front faces
	rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
	//gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipelineTransparentCullFront);

	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);

	//
	// environment map cubiques
	//

	depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilInfo.depthTestEnable = VK_TRUE;
	depthStencilInfo.depthWriteEnable = VK_FALSE;
	colorBlendAttachment.blendEnable = false;
	//colorBlendInfo.pAttachments = &colorBlendAttachment;
	
	rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
	rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pVertexInputState = nullptr;

	vertShaderCode.clear();
	vertShaderCode = readFile("shaders/quad.vert.spv");
	vertShaderModule = context.createShaderModule(vertShaderCode);
	shaderStages[0].module = vertShaderModule;
	fragShaderCode.clear();
	fragShaderCode = readFile("shaders/envmap.frag.spv");
	fragShaderModule = context.createShaderModule(fragShaderCode);
	shaderStages[1].module = fragShaderModule;
	
	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipelineEnvMap);

	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);



	// Matrices world des instances
	
	scene.matrices.world = glm::translate(glm::mat4(1.f), glm::vec3((float)0, 0.f, 0.f));
	


	// par defaut la matrice lookAt de glm est main droite (repere OpenGL, +Z hors de l'ecran)
	// le repere du monde et de la camera est donc main droite !
	scene.matrices.view = glm::lookAt(glm::vec3(0.f, 0.f, 2.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
	// par defaut la matrice perspective genere un cube NDC (NDC OpenGL = main gauche, Z[-1;+1] +Y vers le haut)
	// le define "GLM_FORCE_DEPTH_ZERO_TO_ONE" permet de modifier les plans near et far NDC � [0;+1] 
	// correspondant au NDC Vulkan (mais avec +Y vers le bas)
	scene.matrices.projection = glm::perspective(glm::radians(45.f), context.swapchainExtent.width / (float)context.swapchainExtent.height, 1.f, 1000.f);
	// NDC FIX +Y : Vulkan NDC = left handed & +Y down -> signifie que le winding doit etre clockwise contrairement a OpenGL (NDC Left Handed & +Y up)
	// modele CCW : inverser NDC.Y (ici dans la matrice de projection) et definir le cullmode en COUNTER_CLOCKWISE 
	scene.matrices.projection[1][1] *= -1.f;

	scene.textures.resize(2);
	scene.textures[0].Load(rendercontext, "ocean_mask.png", false);
	// env map HDR lat-long
	//scene.textures[1].Load(rendercontext, "../data/envmaps/pisa.hdr", false);
	scene.textures[1].Load(rendercontext, "../data/envmaps/abandoned_games_room_02_1k.hdr", false);


	// UBOs
	memset(scene.matrices.constantBuffers, 0, sizeof(scene.matrices.constantBuffers));

	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
	VkMemoryRequirements bufferMemReq;

	size_t constantSizes[] = { 2 * sizeof(glm::mat4), 11 * sizeof(glm::mat4) };
	char* matrixData = (char *)&scene.matrices.view;
	for (int i = 0; i < SceneMatrices::MAX; i++)
	{
		bufferInfo.size = constantSizes[i];
		Buffer& ubo = scene.matrices.constantBuffers[i];

		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &ubo.buffer));
		vkGetBufferMemoryRequirements(context.device, ubo.buffer, &bufferMemReq);

		VkMemoryAllocateInfo bufferAllocInfo = {};
		bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		bufferAllocInfo.allocationSize = bufferMemReq.size;
		bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &ubo.memory));
		DEBUG_CHECK_VK(vkBindBufferMemory(context.device, ubo.buffer, ubo.memory, 0));
		ubo.size = bufferMemReq.size;
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = ubo.memory;
		mappedRange.size = VK_WHOLE_SIZE;
		DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
		DEBUG_CHECK_VK(vkMapMemory(context.device, ubo.memory, 0, VK_WHOLE_SIZE, 0, &ubo.data));
		memcpy(ubo.data, matrixData, bufferInfo.size);
		matrixData += bufferInfo.size;
		// comme le contenu est partiellement dynamique on conserve le buffer en persistant 
		//vkUnmapMemory(context.device, sceneBuffers[i].memory);
		DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));
	}
	{
		// meme ubos avec double buffer des descriptor sets
		// une alternative serait de creer un descriptor set pour l'ubo ou un groupe de data
		// et d'utiliser vkCopyDescriptorSets()
		VkDescriptorBufferInfo sceneBufferInfo[SceneMatrices::MAX] = {
			{ scene.matrices.constantBuffers[SceneMatrices::GLOBAL].buffer, 0, constantSizes[SceneMatrices::GLOBAL] },		// binding 0
			{ scene.matrices.constantBuffers[SceneMatrices::DYNAMIC].buffer, 0, constantSizes[SceneMatrices::DYNAMIC] }		// binding 1
		};
		
		VkWriteDescriptorSet writeDescriptorSet[SceneMatrices::MAX] = {};
		for (int i = 0; i < SceneMatrices::MAX; i++)
		{
			writeDescriptorSet[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet[i].descriptorCount = 1;
			writeDescriptorSet[i].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet[i].pBufferInfo = &sceneBufferInfo[i];
			for (int fb = 0; fb < rendercontext.PENDING_FRAMES; fb++)
			{
				writeDescriptorSet[i].dstSet = scene.descriptorSet[i + fb * (SceneMatrices::MAX + 1/*texture*/)];
				vkUpdateDescriptorSets(context.device, 1, &writeDescriptorSet[i], 0, nullptr);
			}
		}

		VkDescriptorImageInfo sceneImageInfo[2] = {
			{ scene.textures[0].sampler, scene.textures[0].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },		// binding 0
			{ scene.textures[1].sampler, scene.textures[1].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL },		// binding 1
		};
		for (int fb = 0; fb < rendercontext.PENDING_FRAMES; fb++)
		{
			writeDescriptorSet[fb].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet[fb].descriptorCount = 2;
			writeDescriptorSet[fb].pBufferInfo = nullptr;
			writeDescriptorSet[fb].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writeDescriptorSet[fb].pImageInfo = &sceneImageInfo[0];
			int texDescIndex = fb * (SceneMatrices::MAX + 1/*texture*/) + 2;
			writeDescriptorSet[fb].dstSet = scene.descriptorSet[texDescIndex];
			vkUpdateDescriptorSets(context.device, 1, &writeDescriptorSet[fb], 0, nullptr);
		}
	}

	// mesh buffers
	scene.meshes.resize(1);
	GenerateSphere(scene.meshes[0], 64, 64, 0.5f);

	memset(scene.meshes[0].staticBuffers, 0, sizeof(scene.meshes[0].staticBuffers));

	Buffer& vbo = scene.meshes[0].staticBuffers[0];
	Buffer& ibo = scene.meshes[0].staticBuffers[1];

	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.size = sizeof(Vertex) * scene.meshes[0].vertices.size();
		bufferInfo.queueFamilyIndexCount = 1;
		uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
		bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
		VkMemoryRequirements bufferMemReq;
		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &vbo.buffer));
		vkGetBufferMemoryRequirements(context.device, vbo.buffer, &bufferMemReq);
		vbo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);

		bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		bufferInfo.size = sizeof(uint16_t) * scene.meshes[0].indices.size();
		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &ibo.buffer));
		vkGetBufferMemoryRequirements(context.device, ibo.buffer, &bufferMemReq);
		ibo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);
		// les donnees de l'IBO commencent apres celles du VBO (en tenant compte de l'alignement) 
		ibo.offset = vbo.size;
	}
	// on alloue la memoire pour les deux buffers de facon a optimiser (esperons-le) les acces caches
	{
		VkMemoryAllocateInfo bufferAllocInfo = {};
		bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		bufferAllocInfo.allocationSize = vbo.size + ibo.size;
		// possible d'eviter de rendre la memoire host visible en passant par un buffer intermediaire
		// ("staging buffer") qui est lui HOST_VISIBLE|COHERENT et USAGE_TRANSFER_SRC
		// ainsi le buffer actuel devra etre USAGE_TRANSFER_DST et copie avec vkCmdCopyBuffer()
		bufferAllocInfo.memoryTypeIndex = context.findMemoryType(bufferMemReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT 
			| VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		DEBUG_CHECK_VK(vkAllocateMemory(context.device, &bufferAllocInfo, nullptr, &vbo.memory));
		ibo.memory = vbo.memory;
		DEBUG_CHECK_VK(vkBindBufferMemory(context.device, vbo.buffer, vbo.memory, 0));
		DEBUG_CHECK_VK(vkBindBufferMemory(context.device, ibo.buffer, ibo.memory, ibo.offset));
		
		// copie des donnees
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = vbo.memory;
		mappedRange.size = VK_WHOLE_SIZE;
		DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
		DEBUG_CHECK_VK(vkMapMemory(context.device, vbo.memory, 0, VK_WHOLE_SIZE, 0, &vbo.data));
		memcpy(vbo.data, scene.meshes[0].vertices.data(), sizeof(Vertex) * scene.meshes[0].vertices.size());
		// hack pointeur car j'ai la flemme de creer une variable locale (mais pas de taper ce texte)
		*((uint8_t**)&vbo.data) += ibo.offset;
		memcpy(vbo.data, scene.meshes[0].indices.data(), sizeof(uint16_t) * scene.meshes[0].indices.size());
		vkUnmapMemory(context.device, vbo.memory);
		vbo.data = nullptr;
		DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));
	}

	m_frame = 0;
	m_currentFrame = 0;

	return true;
}

bool VulkanGraphicsApplication::Shutdown()
{
	if (context.instance == VK_NULL_HANDLE)
		return false;
	if (context.device == VK_NULL_HANDLE)
		return false;

	vkDeviceWaitIdle(context.device);

	for (int i = 0; i < scene.textures.size(); i++)
		scene.textures[i].Destroy(rendercontext);

	// VBO / IBO
	for (int i = 0; i < Mesh::BufferType::MAX; i++)
	{
		// il faut detruire le buffer en premier afin de relacher la reference sur la memoire
		vkDestroyBuffer(context.device, scene.meshes[0].staticBuffers[i].buffer, nullptr);
	}
	// on sait que le buffer VBO contient la memoire pour l'ensemble des data du mesh
	vkFreeMemory(context.device, scene.meshes[0].staticBuffers[0].memory, nullptr);

	// UBO
	for (int i = 0; i < SceneMatrices::BufferType::MAX; i++)
	{
		// il faut detruire le buffer en premier afin de relacher la reference sur la memoire
		vkDestroyBuffer(context.device, scene.matrices.constantBuffers[i].buffer, nullptr);
		// unmap des buffers persistents
		if (scene.matrices.constantBuffers[i].data) {
			vkUnmapMemory(context.device, scene.matrices.constantBuffers[i].memory);
			scene.matrices.constantBuffers[0].data = nullptr;
		}
		vkFreeMemory(context.device, scene.matrices.constantBuffers[i].memory, nullptr);
	}

	// pipelines
	vkDestroyPipeline(context.device, rendercontext.mainPipelineEnvMap, nullptr);
	vkDestroyPipeline(context.device, rendercontext.mainPipelineTransparentCullFront, nullptr);
	vkDestroyPipeline(context.device, rendercontext.mainPipelineTransparent, nullptr);
	vkDestroyPipeline(context.device, rendercontext.mainPipelineCutOut, nullptr);
	vkDestroyPipeline(context.device, rendercontext.mainPipelineOpaque, nullptr);
	vkDestroyPipelineLayout(context.device, rendercontext.mainPipelineLayout, nullptr);

	// destruction du staging buffer
	vkDestroyBuffer(context.device, rendercontext.stagingBuffer.buffer, nullptr);
	vkUnmapMemory(context.device, rendercontext.stagingBuffer.memory);
	vkFreeMemory(context.device, rendercontext.stagingBuffer.memory, nullptr);


	// double buffer, mais vkFree pas utile ici car la destruction est automatique
	//vkFreeDescriptorSets(context.device, sceneDescriptorPool, 2, sceneSet);
	vkResetDescriptorPool(context.device, scene.descriptorPool, 0);

	for (int i = 0; i < SceneMatrices::MAX + 1; i++)
		vkDestroyDescriptorSetLayout(context.device, scene.descriptorSetLayout[i], nullptr);
	vkDestroyDescriptorPool(context.device, scene.descriptorPool, nullptr);

	for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		vkDestroyFence(context.device, rendercontext.mainFences[i], nullptr);
	}

	vkDestroyImageView(context.device, rendercontext.depthBuffer.view, nullptr);
	vkDestroyImage(context.device, rendercontext.depthBuffer.image, nullptr);
	vkFreeMemory(context.device, rendercontext.depthBuffer.memory, nullptr);

	for (int i = 0; i < context.SWAPCHAIN_IMAGES; i++) {
		vkDestroyFramebuffer(context.device, rendercontext.framebuffers[i], nullptr);
		vkDestroyImageView(context.device, context.swapchainImageViews[i], nullptr);
		vkDestroySemaphore(context.device, context.acquireSemaphores[i], nullptr);
		vkDestroySemaphore(context.device, context.presentSemaphores[i], nullptr);
	}

	vkDestroyRenderPass(context.device, rendercontext.renderPass, nullptr);

	vkFreeCommandBuffers(context.device, rendercontext.mainCommandPool
		, 2, rendercontext.mainCommandBuffers);

	vkDestroyCommandPool(context.device, rendercontext.mainCommandPool, nullptr);

	vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);

	vkDestroyDevice(context.device, nullptr);

	vkDestroySurfaceKHR(context.instance, context.surface, nullptr);

#ifdef VULKAN_ENABLE_VALIDATION
	vkDestroyDebugReportCallbackEXT(context.instance, context.debugCallback, nullptr);
#endif

	vkDestroyInstance(context.instance, nullptr);

#ifndef GLFW_INCLUDE_VULKAN

#endif

	return true;
}

bool VulkanGraphicsApplication::Update()
{
	// update ---

	int width, height;
	glfwGetWindowSize(window, &width, &height);

	static double previousTime = glfwGetTime();
	static double currentTime = glfwGetTime();

	currentTime = glfwGetTime();
	double deltaTime = currentTime - previousTime;
	std::cout << "[" << m_frame << "] frame time = " << deltaTime*1000.0 << " ms [" << 1.0 / deltaTime << " fps]" << std::endl;
	previousTime = currentTime;

	float time = (float)currentTime;

	scene.matrices.world = glm::rotate(glm::mat4(1.f), time, glm::vec3(0.f, 1.f, 0.f));

	return true;
}



bool VulkanGraphicsApplication::Begin()
{
	// render ---
	VkFence* commandFence = &rendercontext.mainFences[m_currentFrame];
	// on bloque tant que la fence soumis avec un command buffer n'a pas ete signale
	vkWaitForFences(context.device, 1, commandFence,
		false, UINT64_MAX);
	vkResetFences(context.device, 1, commandFence);
	
	vkResetCommandBuffer(rendercontext.mainCommandBuffers[m_currentFrame], VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);

	return true;
}

bool VulkanGraphicsApplication::Display()
{
	// COMMAND BUFFER SAFE ICI
	VkCommandBuffer commandBuffer = rendercontext.mainCommandBuffers[m_currentFrame];

	VkMappedMemoryRange mappedRange = {};
	mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	mappedRange.memory = scene.matrices.constantBuffers[0].memory;
	mappedRange.size = sizeof(glm::mat4);
	// on peut eviter vkInvalidateMappedMemoryRanges car le buffer ne devrait pas etre modifie par le GPU
	//DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	// on peut se passer de invalidate+flush lorsque la memoire du buffer est HOST_COHERENT_BIT
	// cependant cela implique que l'ensemble du buffer sera toujours synchronise en caches
	// alors qu'ici on ne souhaite mettre a jour qu'une partie (world matrix), donc il faut synchro manuellement
	memcpy(scene.matrices.constantBuffers[0/*m_currentFrame*/].data, &scene.matrices, sizeof(glm::mat4));
	DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));

	mappedRange.memory = scene.matrices.constantBuffers[1].memory;
	mappedRange.size = sizeof(glm::mat4);
	memcpy(scene.matrices.constantBuffers[1/*m_currentFrame*/].data, &scene.matrices.world, sizeof(glm::mat4));
	DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));


	VkSemaphore* acquireSem = &context.acquireSemaphores[m_currentFrame];
	VkSemaphore* presentSem = &context.presentSemaphores[m_currentFrame];

	uint64_t timeout = UINT64_MAX;
	DEBUG_CHECK_VK(vkAcquireNextImageKHR(context.device, context.swapchain
		, timeout, *acquireSem/*signal des que l'image est disponible */
		, VK_NULL_HANDLE, &m_imageIndex));

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkClearValue clearValues[2];
	clearValues[0].color = { 1.0f, 1.0f, 0.0f, 1.0f };
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = rendercontext.renderPass;
	renderPassBeginInfo.framebuffer = rendercontext.framebuffers[m_imageIndex];
	renderPassBeginInfo.renderArea.extent = context.swapchainExtent;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;
	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineLayout, 0, 3, &scene.descriptorSet[3*m_currentFrame], 0, nullptr);

	
	VkDeviceSize offsets[] = { 0 };
	VkBuffer buffers[] = { scene.meshes[0].staticBuffers[Mesh::BufferType::VBO].buffer };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, scene.meshes[0].staticBuffers[Mesh::BufferType::IBO].buffer
		, 0, VK_INDEX_TYPE_UINT16);
	// en utilisant l'hardware instancing
	//vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indices.size(), 11*11, 0, 0, 0);
	// en utilisant les push constants
	struct PushData
	{
		float roughness;	// perceptuelle
		float reflectance;	// perceptuelle (non metaux)
		float metalness;	
		int index;
	} pushData;

	pushData.index = 0;
	pushData.metalness = 0.0f;
	pushData.reflectance = 0.5f;
	pushData.roughness = 0.1f;
	vkCmdPushConstants(commandBuffer, rendercontext.mainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT| VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushData), &pushData);

	// "Passe" Opaques & Cutouts & Environnement
	{
		// opaque en premier (rien ici)
		//vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineOpaque);
		
		// dessine en cutout afin que les parties opaques figurent bien dans le depth buffer
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineCutOut);
		vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indices.size(), 1, 0, 0, 0);

		// env map (background) apres tout le reste
		//vkCmdBindVertexBuffers(commandBuffer, 0, 0, nullptr, nullptr);
		
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineEnvMap);
		vkCmdDraw(commandBuffer, 4, 1, 0, 0);
	}
	
	// "Passe" Transparents, toujours en dernier
	{
		//vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

		// dessine les back faces en premier
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineTransparentCullFront);
		vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indices.size(), 1, 0, 0, 0);
		// puis les front faces
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineTransparent);
		vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indices.size(), 1, 0, 0, 0);
	}

	vkCmdEndRenderPass(commandBuffer);

	vkEndCommandBuffer(commandBuffer);

	return true;
}

bool VulkanGraphicsApplication::End()
{
	VkSemaphore* acquireSem = &context.acquireSemaphores[m_currentFrame];
	VkSemaphore* presentSem = &context.presentSemaphores[m_currentFrame];

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &rendercontext.mainCommandBuffers[m_currentFrame];
	VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &stageMask;
	submitInfo.pWaitSemaphores = acquireSem;	// on attend acquireSem
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = presentSem;	// on signal presentSem des qu'on est pret
	submitInfo.signalSemaphoreCount = 1;
	vkQueueSubmit(rendercontext.graphicsQueue, 1, &submitInfo
		, rendercontext.mainFences[m_currentFrame]);
	// la fence precedente se signale des que le command buffer est totalement traite par la queue

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = presentSem;// presentation semaphore ici synchro avec le dernier vkQueueSubmit
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &context.swapchain;
	presentInfo.pImageIndices = &m_imageIndex;
	DEBUG_CHECK_VK(vkQueuePresentKHR(rendercontext.presentQueue, &presentInfo));

	m_frame++;
	m_currentFrame = m_frame % rendercontext.PENDING_FRAMES;

	return true;
}

bool VulkanGraphicsApplication::Run()
{
	Update();
	Begin();
	Display();
	End();

	return true;
}

int main(void)
{
	/* Initialize the library */
	if (!glfwInit())
		return -1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	VulkanGraphicsApplication app;

	/* Create a windowed mode window and its OpenGL context */
	app.window = glfwCreateWindow(800, 600, "10_TransparentEarth", NULL, NULL);
	if (!app.window)
	{
		glfwTerminate();
		return -1;
	}

	app.Initialize();

	/* Loop until the user closes the window */
	while (!glfwWindowShouldClose(app.window))
	{
		/* Render here */
		app.Run();

		/* Poll for and process events */
		glfwPollEvents();
	}

	app.Shutdown();

	glfwTerminate();
	return 0;
}