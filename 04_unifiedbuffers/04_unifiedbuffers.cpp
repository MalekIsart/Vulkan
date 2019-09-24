
// A definir en premier lieu
#if defined(_WIN32)
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

struct VulkanRenderContext
{
	static const int PENDING_FRAMES = 2;

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


	VkPipeline mainPipeline;
	VkPipelineLayout mainPipelineLayout;
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

// scene data

struct SceneMatrices
{
	glm::mat4 world;
	glm::mat4 view;
	glm::mat4 projection;
	
	Buffer constantBuffers[1]; // double buffer pas obligatoire ici
};

struct Color
{
	uint8_t r, g, b, a;
};

struct Vertex
{
	glm::vec3 position;
	Color color;
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

	// GPU scene
	// Un DescriptorSet ne peut pas etre update ou utilise par un command buffer
	// alors qu'il est "bind" par un autre command buffer
	// On va donc avoir des descriptorSets par frame/command buffer
	VkDescriptorSet descriptorSet[2];
	VkDescriptorSetLayout descriptorSetLayout;
	VkDescriptorPool descriptorPool;
};

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
	appInfo.pApplicationName = "04_unifiedbuffers";
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

	// swap chain

	// todo: enumerer (cf validation)
	uint32_t formatCount = 1;
	VkSurfaceFormatKHR surfaceFormats[1];
	vkGetPhysicalDeviceSurfaceFormatsKHR(context.physicalDevice, context.surface, &formatCount, surfaceFormats);
	context.surfaceFormat = surfaceFormats[0];
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
		COLOR = 1,
		MAX
	};

	VkAttachmentDescription attachDesc[RenderTarget::MAX] = {};
	attachDesc[0].format = context.surfaceFormat.format;
	attachDesc[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachDesc[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachDesc[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachDesc[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachDesc[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachDesc[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachDesc[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference swapAttachRef[1] = {};
	swapAttachRef[0].attachment = RenderTarget::SWAPCHAIN;
	swapAttachRef[0].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpasses[1] = {};
	subpasses[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpasses[0].colorAttachmentCount = 1;
	subpasses[0].pColorAttachments = swapAttachRef;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = attachDesc;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = subpasses;
	vkCreateRenderPass(context.device, &renderPassInfo, nullptr, &rendercontext.renderPass);


	VkFramebufferCreateInfo framebufferInfo = {};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = rendercontext.renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.layers = 1;
	framebufferInfo.width = context.swapchainExtent.width;
	framebufferInfo.height = context.swapchainExtent.height;
	for (int i = 0; i < rendercontext.PENDING_FRAMES; ++i) {
		framebufferInfo.pAttachments = &context.swapchainImageViews[i];
		vkCreateFramebuffer(context.device, &framebufferInfo, nullptr, &rendercontext.framebuffers[i]);
	}

	// Pipeline

	auto vertShaderCode = readFile("shaders/triangle.vert.spv");
	auto fragShaderCode = readFile("shaders/triangle.frag.spv");

	VkShaderModule vertShaderModule = context.createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = context.createShaderModule(fragShaderCode);

	// Common ---

	VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo = {};
	inputAssemblyInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
#ifdef OPENGL_NDC
	rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
#else
	rasterizationInfo.frontFace = VK_FRONT_FACE_CLOCKWISE;
#endif
	rasterizationInfo.lineWidth = 1.f;

	VkPipelineMultisampleStateCreateInfo multisampleInfo = {};
	multisampleInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	//multisampleInfo.minSampleShading = 1.0f;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	VkPipelineColorBlendStateCreateInfo colorBlendInfo = {};
	colorBlendInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlendInfo.attachmentCount = 1;
	colorBlendInfo.pAttachments = &colorBlendAttachment;

	VkGraphicsPipelineCreateInfo gfxPipelineInfo = {};
	gfxPipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	gfxPipelineInfo.pViewportState = &viewportInfo;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	gfxPipelineInfo.pMultisampleState = &multisampleInfo;
	gfxPipelineInfo.pColorBlendState = &colorBlendInfo;
	gfxPipelineInfo.renderPass = rendercontext.renderPass;
	gfxPipelineInfo.basePipelineIndex = -1;
	gfxPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	gfxPipelineInfo.subpass = 0;

	// specific ---

	// VAO / input layout
	uint32_t stride = 0;
	VkVertexInputAttributeDescription vertexInputLayouts[2];
	vertexInputLayouts[0] = { 0/*location*/, 0/*binding*/, VK_FORMAT_R32G32B32_SFLOAT/*format*/, 0/*offset*/ };
	stride += sizeof(glm::vec3);
	vertexInputLayouts[1] = { 1/*location*/, 0/*binding*/, VK_FORMAT_R8G8B8A8_UNORM/*format*/, stride/*offset*/ };
	stride += sizeof(Color);
	VkVertexInputBindingDescription vertexInputBindings = {0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &vertexInputBindings;
	vertexInputInfo.vertexAttributeDescriptionCount = 2;
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


	VkDescriptorPoolSize poolSizes[2] = {
		{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 * rendercontext.PENDING_FRAMES},
		{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 * rendercontext.PENDING_FRAMES}
	};
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.maxSets = 1 * rendercontext.PENDING_FRAMES;
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
		VkPushConstantRange constantRange = { VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) };
		pipelineInfo.pPushConstantRanges = &constantRange;

		pipelineInfo.setLayoutCount = 1;
		// layout : on doit decrire le format de chaque descriptor (binding, type, stage)
		VkDescriptorSetLayoutBinding sceneSetBindings[4];
		sceneSetBindings[0] = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr };
		sceneSetBindings[1] = { 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
		// todo
		//
		VkDescriptorSetLayoutCreateInfo sceneSetInfo = {};
		sceneSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		sceneSetInfo.bindingCount = 2;
		sceneSetInfo.pBindings = sceneSetBindings;
		vkCreateDescriptorSetLayout(context.device, &sceneSetInfo, nullptr, &scene.descriptorSetLayout);

		VkDescriptorSetAllocateInfo allocateDescInfo = {};
		allocateDescInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocateDescInfo.descriptorPool = scene.descriptorPool;
		allocateDescInfo.descriptorSetCount = 1;
		allocateDescInfo.pSetLayouts = &scene.descriptorSetLayout;
		// on cree deux descriptor sets car on double buffer (il faut donc allouer 2*N*sets)
		// c'est sceneSet[currentFrame] qui devra etre utilise (bind) avec le pipeline correspondant
		for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
			vkAllocateDescriptorSets(context.device, &allocateDescInfo, &scene.descriptorSet[i]);
		}
		pipelineInfo.pSetLayouts = &scene.descriptorSetLayout;

		vkCreatePipelineLayout(context.device, &pipelineInfo, nullptr, &rendercontext.mainPipelineLayout);
	}


	gfxPipelineInfo.layout = rendercontext.mainPipelineLayout;

	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipeline);


	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);

	scene.matrices.world = glm::mat4(1.f);
	scene.matrices.view = glm::lookAt(glm::vec3(0.f, 0.f, 2.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
	scene.matrices.projection = glm::perspective(45.f, context.swapchainExtent.width / (float)context.swapchainExtent.height, 1.f, 1000.f);

	// UBOs
	memset(scene.matrices.constantBuffers, 0, sizeof(scene.matrices.constantBuffers));

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.size = sizeof(SceneMatrices);
	bufferInfo.queueFamilyIndexCount = 1;
	uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
	bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
	VkMemoryRequirements bufferMemReq;
	Buffer& ubo = scene.matrices.constantBuffers[0];
	{
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
		memcpy(ubo.data, &scene.matrices, bufferInfo.size);
		// comme le contenu est partiellement dynamique on converse le buffer en persistant 
		//vkUnmapMemory(context.device, sceneBuffers[i].memory);
		DEBUG_CHECK_VK(vkFlushMappedMemoryRanges(context.device, 1, &mappedRange));
	}
	{
		// un meme ubo pour deux descriptor sets
		// une alternative serait de creer un descriptor set pour l'ubo ou un groupe de data
		// et d'utiliser vkCopyDescriptorSets()
		VkDescriptorBufferInfo sceneBufferInfo = { ubo.buffer, 0, sizeof(SceneMatrices) };
		for (int i = 0; i < rendercontext.PENDING_FRAMES; i++)
		{
			VkWriteDescriptorSet writeDescriptorSet = {};
			writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			writeDescriptorSet.descriptorCount = 1;
			writeDescriptorSet.dstBinding = 0;
			writeDescriptorSet.pBufferInfo = &sceneBufferInfo;
			writeDescriptorSet.dstSet = scene.descriptorSet[i];
			vkUpdateDescriptorSets(context.device, 1, &writeDescriptorSet, 0, nullptr);
		}
	}

	// mesh buffers
	scene.meshes.resize(1);
	scene.meshes[0] = { 
		{
		{ {0.0f, -0.5f, 0.f}, {255,0,0,255} },
		{ {+0.5f, 0.5f, 0.f}, {0,255,0,255} },
		{ {-0.5f, 0.5f, 0.f}, {0,0,255,255} }
		},
	{ 0, 1, 2 }
	};

	memset(scene.meshes[0].staticBuffers, 0, sizeof(scene.meshes[0].staticBuffers));

	Buffer& vbo = scene.meshes[0].staticBuffers[0];
	Buffer& ibo = scene.meshes[0].staticBuffers[1];

	{
		VkBufferCreateInfo bufferInfo = {};
		bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
		bufferInfo.size = sizeof(Vertex) * 3;
		bufferInfo.queueFamilyIndexCount = 1;
		uint32_t queueFamilyIndices[] = { rendercontext.graphicsQueueIndex };
		bufferInfo.pQueueFamilyIndices = queueFamilyIndices;
		VkMemoryRequirements bufferMemReq;
		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &vbo.buffer));
		vkGetBufferMemoryRequirements(context.device, vbo.buffer, &bufferMemReq);
		vbo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);

		bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
		bufferInfo.size = sizeof(uint16_t) * 3;
		DEBUG_CHECK_VK(vkCreateBuffer(context.device, &bufferInfo, nullptr, &ibo.buffer));
		vkGetBufferMemoryRequirements(context.device, ibo.buffer, &bufferMemReq);
		ibo.size = (bufferMemReq.size + bufferMemReq.alignment) & ~(bufferMemReq.alignment - 1);
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

	// VBO / IBO
	for (int i = 0; i < Mesh::BufferType::MAX; i++)
	{
		// il faut detruire le buffer en premier afin de relacher la reference sur la memoire
		vkDestroyBuffer(context.device, scene.meshes[0].staticBuffers[i].buffer, nullptr);
	}
	// on sait que le buffer VBO contient la memoire pour l'ensemble des data du mesh
	vkFreeMemory(context.device, scene.meshes[0].staticBuffers[0].memory, nullptr);

	// UBO

	// il faut detruire le buffer en premier afin de relacher la reference sur la memoire
	vkDestroyBuffer(context.device, scene.matrices.constantBuffers[0].buffer, nullptr);
	// unmap des buffers persistents
	if (scene.matrices.constantBuffers[0].data) {
		vkUnmapMemory(context.device, scene.matrices.constantBuffers[0].memory);
		scene.matrices.constantBuffers[0].data = nullptr;
	}
	vkFreeMemory(context.device, scene.matrices.constantBuffers[0].memory, nullptr);

	// pipeline

	vkDestroyPipeline(context.device, rendercontext.mainPipeline, nullptr);
	vkDestroyPipelineLayout(context.device, rendercontext.mainPipelineLayout, nullptr);

	// double buffer, mais vkFree pas utile ici car la destruction est automatique
	//vkFreeDescriptorSets(context.device, sceneDescriptorPool, 2, sceneSet);
	vkResetDescriptorPool(context.device, scene.descriptorPool, 0);

	vkDestroyDescriptorSetLayout(context.device, scene.descriptorSetLayout, nullptr);
	vkDestroyDescriptorPool(context.device, scene.descriptorPool, nullptr);

	for (int i = 0; i < rendercontext.PENDING_FRAMES; i++) {
		vkDestroyFence(context.device, rendercontext.mainFences[i], nullptr);
	}

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
	mappedRange.memory = scene.matrices.constantBuffers[0/*m_currentFrame*/].memory;
	mappedRange.size = sizeof(glm::mat4);
	// on peut eviter vkInvalidateMappedMemoryRanges car le buffer ne devrait pas etre modifie par le GPU
	//DEBUG_CHECK_VK(vkInvalidateMappedMemoryRanges(context.device, 1, &mappedRange));
	// on peut se passer de invalidate+flush lorsque la memoire du buffer est HOST_COHERENT_BIT
	// cependant cela implique que l'ensemble du buffer sera toujours synchronise en caches
	// alors qu'ici on ne souhaite mettre a jour qu'une partie (world matrix), donc il faut synchro manuellement
	memcpy(scene.matrices.constantBuffers[0/*m_currentFrame*/].data, &scene.matrices, sizeof(glm::mat4));
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

	VkClearValue clearValues[1];
	clearValues[0].color = { 1.0f, 1.0f, 0.0f, 1.0f };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = rendercontext.renderPass;
	renderPassBeginInfo.framebuffer = rendercontext.framebuffers[m_imageIndex];
	renderPassBeginInfo.renderArea.extent = context.swapchainExtent;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;
	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipeline);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipelineLayout, 0, 1, &scene.descriptorSet[0/*m_currentFrame*/], 0, nullptr);

	//vkCmdPushConstants(commandBuffer, rendercontext.mainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &time);
	
	VkDeviceSize offsets[] = { 0 };
	VkBuffer buffers[] = { scene.meshes[0].staticBuffers[Mesh::BufferType::VBO].buffer };
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, scene.meshes[0].staticBuffers[Mesh::BufferType::IBO].buffer
		, 0, VK_INDEX_TYPE_UINT16);
	vkCmdDrawIndexed(commandBuffer, scene.meshes[0].indices.size(), 1, 0, 0, 0);
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
	app.window = glfwCreateWindow(800, 600, "04_unifiedbuffers", NULL, NULL);
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