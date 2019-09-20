
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

	VkShaderModule createShaderModule(const std::vector<char>& code)
	{
		VkShaderModuleCreateInfo createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
		createInfo.codeSize = code.size();
		createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

		VkShaderModule shaderModule;
		if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
			throw std::runtime_error("failed to create shader module!");
		}

		return shaderModule;
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

struct VulkanGraphicsApplication
{
	VulkanDeviceContext context;
	VulkanRenderContext rendercontext;
	GLFWwindow* window;

	uint32_t m_frame;
	uint32_t m_currentFrame;

	bool Initialize();
	bool Run();
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
	appInfo.pApplicationName = "00_minimal";
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
	instanceInfo.enabledExtensionCount = sizeof(extensionNames)/sizeof(char*);
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

	// 1. create renderpass
	// 1.a definir les attachments (VkAttachmentDescription, un tableau)
	//		VkAttachmentDescription permet de specifier le format mais surtout
	//		les actions a effectuer lorsque
	//			l'attachment est load par la subpass (loadOp, peut etre DONT_CARE)
	//			l'attachment est store par la subpass (storeOp, peut etre DONT_CARE)
	//			les layout initial (src layout) et final (dst layout)
	// 1.b definir les subpasses (VkSubpassDescription, tableau également)
	//		VkSubpassDescription indique les attachments POUR CETTE subpass
	//			pour eviter de tout redefinir on passe par un VkAttachmentReference
	//			cette reference est ici l'INDEX de l'attachment dans le tableau 
	//			de VkAttachmentDescription
	// 2. create framebuffer
	//		Attention, 1 frame buffer par image de la swapchain ici
	//		il s'agit d'image views, en gros Image = memoire, View = texture/target
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

	VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 0;
	vertexInputInfo.pVertexBindingDescriptions = nullptr;
	vertexInputInfo.vertexAttributeDescriptionCount = 0;
	vertexInputInfo.pVertexAttributeDescriptions = nullptr;

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
		{0, 0}, {viewport.width, viewport.height}
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
#ifdef OPENGL_HANDEDNESS
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
	gfxPipelineInfo.pVertexInputState = &vertexInputInfo;
	gfxPipelineInfo.pInputAssemblyState = &inputAssemblyInfo;
	gfxPipelineInfo.pRasterizationState = &rasterizationInfo;
	gfxPipelineInfo.pMultisampleState = &multisampleInfo;
	gfxPipelineInfo.pColorBlendState = &colorBlendInfo;
	gfxPipelineInfo.renderPass = rendercontext.renderPass;
	gfxPipelineInfo.basePipelineIndex = -1;
	gfxPipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
	gfxPipelineInfo.subpass = 0;

	// specific

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

	VkPipelineLayoutCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineInfo.pushConstantRangeCount = 1;
	// les specs de Vulkan garantissent 128 octets de push constants
	// voir properties.limits.maxPushConstantsSize
	// dans les faits, la plupart des drivers/GPUs ne supportent pas
	// autant d'octets de maniere optimum
	VkPushConstantRange constantRange = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float)};
	pipelineInfo.pPushConstantRanges = &constantRange;
	vkCreatePipelineLayout(context.device, &pipelineInfo, nullptr, &rendercontext.mainPipelineLayout);

	gfxPipelineInfo.layout = rendercontext.mainPipelineLayout;

	vkCreateGraphicsPipelines(context.device, nullptr, 1, &gfxPipelineInfo
		, nullptr, &rendercontext.mainPipeline);


	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);


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

	vkDestroyPipeline(context.device, rendercontext.mainPipeline, nullptr);
	vkDestroyPipelineLayout(context.device, rendercontext.mainPipelineLayout, nullptr);

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

bool VulkanGraphicsApplication::Display()
{
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	
	static double previousTime = glfwGetTime();
	static double currentTime = glfwGetTime();

	currentTime = glfwGetTime();
	double deltaTime = currentTime - previousTime;
	std::cout << "[" << m_frame << "] frame time = " << deltaTime*1000.0 << " ms [" << 1.0/deltaTime << " fps]" << std::endl;
	previousTime = currentTime;

	//vkDeviceWaitIdle(context.device);
	VkCommandBuffer commandBuffer = rendercontext.mainCommandBuffers[m_currentFrame];

	// on bloque tant que la fence soumis avec un command buffer n'a pas ete signale
	vkWaitForFences(context.device, 1, &rendercontext.mainFences[m_currentFrame],
		false, UINT64_MAX);
	vkResetFences(context.device, 1, &rendercontext.mainFences[m_currentFrame]);
	vkResetCommandBuffer(commandBuffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
	
	VkSemaphore* acquireSem = &context.acquireSemaphores[m_currentFrame];
	VkSemaphore* presentSem = &context.presentSemaphores[m_currentFrame];

	uint32_t image_index;
	uint64_t timeout = UINT64_MAX;
	DEBUG_CHECK_VK(vkAcquireNextImageKHR(context.device, context.swapchain
		, timeout, *acquireSem/*signal des que l'image est disponible */
		, VK_NULL_HANDLE, &image_index));

	// RENDU ICI

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	VkClearValue clearValues[1];
	clearValues[0].color = { 1.0f, 1.0f, 0.0f, 1.0f };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.renderPass = rendercontext.renderPass;
	renderPassBeginInfo.framebuffer = rendercontext.framebuffers[image_index];
	renderPassBeginInfo.renderArea.extent = context.swapchainExtent;
	renderPassBeginInfo.clearValueCount = 1;
	renderPassBeginInfo.pClearValues = clearValues;
	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rendercontext.mainPipeline);
	float time = (float)currentTime;
	vkCmdPushConstants(commandBuffer, rendercontext.mainPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float), &time);
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);

	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VkPipelineStageFlags stageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	submitInfo.pWaitDstStageMask = &stageMask;
	submitInfo.pWaitSemaphores = acquireSem;	// on attend acquireSem
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = presentSem;	// on signal presentSem des qu'on est pret
	submitInfo.signalSemaphoreCount = 1;
	vkQueueSubmit(rendercontext.graphicsQueue, 1, &submitInfo
		, rendercontext.mainFences[m_currentFrame]);
	// la fence precedente se signale des que le command buffer est totalement traite 
	// par la queue

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = presentSem;// presentation semaphore ici synchro avec le dernier vkQueueSubmit;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &context.swapchain;
	presentInfo.pImageIndices = &image_index;
	DEBUG_CHECK_VK(vkQueuePresentKHR(rendercontext.presentQueue, &presentInfo));

	m_frame++;
	m_currentFrame = m_frame % rendercontext.PENDING_FRAMES;

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
	app.window = glfwCreateWindow(800, 600, "02_triangle", NULL, NULL);
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
		app.Display();

		/* Poll for and process events */
		glfwPollEvents();
	}

	app.Shutdown();

	glfwTerminate();
	return 0;
}