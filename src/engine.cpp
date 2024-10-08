#include "Constants.h"

#include "engine/engine.h"
#include "engine/model.h"

#include "Util.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const vector validationLayers = {"VK_LAYER_KHRONOS_validation"};
// VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME is a MacOS specific workaround
const vector deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME};

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

DEF CreateDebugUtilsMessengerEXT(
    VkInstance instance,
    const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    const VkAllocationCallbacks *pAllocator,
    VkDebugUtilsMessengerEXT *pDebugMessenger) -> VkResult {

    const auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

DEF DestroyDebugUtilsMessengerEXT(
    VkInstance instance,
    VkDebugUtilsMessengerEXT debugMessenger,
    const VkAllocationCallbacks *pAllocator) -> void {

    const auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));

    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

Engine::Engine()
    : m_CameraEye(Settings::CAMERA_EYE),
      m_CameraCenter(Settings::CAMERA_CENTER),
      m_CameraUp(Settings::CAMERA_UP),
      m_Window(nullptr),
      m_Instance(VK_NULL_HANDLE),
      m_DebugMessenger(VK_NULL_HANDLE),
      m_PhysicalDevice(VK_NULL_HANDLE),
      m_Device(VK_NULL_HANDLE),
      m_GraphicsQueue(VK_NULL_HANDLE),
      m_PresentQueue(VK_NULL_HANDLE),
      m_Surface(VK_NULL_HANDLE),
      m_SwapChain(VK_NULL_HANDLE),
      m_SwapChainImageFormat(VK_FORMAT_UNDEFINED),
      m_SwapChainExtent({.width = 0, .height = 0}),
      m_RenderPass(VK_NULL_HANDLE),
      m_DescriptorSetLayout(VK_NULL_HANDLE),
      m_PipelineLayout(VK_NULL_HANDLE),
      m_GraphicsPipeline(VK_NULL_HANDLE),
      m_CommandPool(VK_NULL_HANDLE), // Not the total frames, 0 <= m_CurrentFrameIdx < Settings::MAX_FRAMES_IN_FLIGHT
      m_CurrentFrameIdx(0),
      m_FrameCounter(0),
      m_FramebufferResized(false),
      m_DescriptorPool(VK_NULL_HANDLE),
      m_MipLevels(1),
      m_TextureImage(VK_NULL_HANDLE),
      m_TextureImageMemory(VK_NULL_HANDLE),
      m_TextureImageView(VK_NULL_HANDLE),
      m_TextureSampler(VK_NULL_HANDLE),
      m_MSAASamples(VK_SAMPLE_COUNT_1_BIT),
      m_ColorImage(VK_NULL_HANDLE),
      m_ColorImageMemory(VK_NULL_HANDLE),
      m_ColorImageView(VK_NULL_HANDLE),
      m_DepthImage(VK_NULL_HANDLE),
      m_DepthImageMemory(VK_NULL_HANDLE),
      m_DepthImageView(VK_NULL_HANDLE),
      m_TakeScreenshotNextFrame(false),
      m_EngineVersion(VK_MAKE_VERSION(1, 0, 0)),
      m_ApplicationVersion(VK_MAKE_VERSION(1, 0, 0)),
      m_Stage(Settings::STARTING_STAGE),
      m_PushConstants() {
    m_StartTime = std::chrono::high_resolution_clock::now();
}

Engine::~Engine() {
    fprintf(stdout, "Starting Engine Cleanup\n");
    cleanup();
    fprintf(stdout, "Finished Engine Cleanup\n");
}

DEF Engine::initialize() -> void {
    fprintf(stdout, "Initializing Engine application.\n");
    fprintf(stdout, "Initializing GLFW.\n");
    initWindow();
    fprintf(stdout, "Initializing Engine application.\n");
    initVulkan();

    fprintf(stdout, "\nFinished Initializing Vulkan application.\n");
    std::cout << std::flush;
}

DEF Engine::initWindow() -> void {
    fprintf(stdout, "Trying to initialize window.\n");
    if (glfwInit() == GLFW_FALSE) {
        throw runtime_error("Failed to instantiate GLFW window!");
    }

    // GLFW defaults to creating OpenGL context otherwise
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    // Disables window resizing
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    m_Window = glfwCreateWindow(
        Settings::DEFAULT_WINDOW_WIDTH,
        Settings::DEFAULT_WINDOW_HEIGHT,
        Settings::PROJECT_NAME,
        nullptr,
        nullptr);
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, framebufferResizeCallback);
    fprintf(stdout, "Successfully initialized window.\n");
}

DEF Engine::framebufferResizeCallback(GLFWwindow *window, int width, int height) -> void {
    const auto app = static_cast<Engine *>(glfwGetWindowUserPointer(window));
    app->m_FramebufferResized = true;
}

DEF Engine::validateExtensions(const vector<VkExtensionProperties> &supported_extensions, vector<const char *> required_extensions) -> bool {
    return std::ranges::all_of(required_extensions,
                               [&](const char *required_extension) {
                                   const string_view required_extension_view(required_extension);
                                   return std::ranges::any_of(supported_extensions,
                                                              [&](const auto &supported_extension) {
                                                                  return string_view(supported_extension.extensionName) == required_extension_view;
                                                              });
                               });
}

DEF Engine::createInstance() -> void {
    if constexpr(enableValidationLayers) {
        if (!checkValidationLayerSupport()) {
            throw runtime_error("Validation layers requested, but not available!");
        }
    }

    uint32_t apiVersion = 0;
    if (vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) throw runtime_error("Couldn't query InstanceVersion.");
    if (apiVersion < VK_API_VERSION_1_3) throw runtime_error("Vulkan Version is too low!");

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = Settings::PROJECT_NAME,
        .applicationVersion = m_ApplicationVersion,
        .pEngineName = "No Engine",
        .engineVersion = m_EngineVersion,
        .apiVersion = VK_API_VERSION_1_3};

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr};

    vector<const char *> requiredExtensions = getRequiredExtensions();
#if defined(__APPLE__) && defined(__arm64__) // MacOS specific workarounds
    requiredExtensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    requiredExtensions.emplace_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    if (!validateExtensions(extensions, requiredExtensions)) throw runtime_error("Required extensions are not supported!");

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if constexpr (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        populateDebugMessengerCreateInfo(debugCreateInfo);
        // Note that pNext is in general a `const void*` ptr so we can't static_cast it
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &m_Instance) != VK_SUCCESS) {
        throw runtime_error("Failed to create instance!");
    }
}

DEF Engine::initVulkan() -> void {
    auto initStart = std::chrono::high_resolution_clock::now();

    size_t initVulkanIteration = 0;
    PRINT_BOLD_GREEN("* * * * * * * * * * * * * *");
    PRINT_BOLD_GREEN("*    Setting up Vulkan    *");
    PRINT_BOLD_GREEN("* * * * * * * * * * * * * *");

    PRINT_BOLD_GREEN("Instance and Debug Setup");
    VULKAN_SETUP(createInstance);
    VULKAN_SETUP(setupDebugMessenger);

    PRINT_BOLD_GREEN("Window Surface Setup");
    VULKAN_SETUP(createSurface);

    PRINT_BOLD_GREEN("Physical and Logical Device Setup");
    VULKAN_SETUP(pickPhysicalDevice);
    VULKAN_SETUP(createLogicalDevice);

    PRINT_BOLD_GREEN("Swap Chain Setup");
    VULKAN_SETUP(createSwapChain);
    VULKAN_SETUP(createImageViews);

    PRINT_BOLD_GREEN("Render Pass and Pipeline Setup");
    VULKAN_SETUP(createRenderPass);
    VULKAN_SETUP(createDescriptorSetLayout);
    VULKAN_SETUP(createGraphicsPipeline);

    VULKAN_SETUP(createCommandPool);
    VULKAN_SETUP(createColorResources);
    VULKAN_SETUP(createDepthResources);
    VULKAN_SETUP(createFramebuffers);

    VULKAN_SETUP(createTextureImage);
    VULKAN_SETUP(createTextureImageView);
    VULKAN_SETUP(createTextureSampler);

    cout << "Instantiating Models!\n";
    Transform torusTransform{
        vec3(0.0f, 0.0f, 0.0f),
        vec3(PI_DEG, 0.0f, 0.0f),
        vec3(1.0f, 1.0f, 1.0f)};
    auto torusModel = std::make_unique<ModelNT>(this, FilePaths::MODEL_BASIC_TORUS, m_Models.size(), torusTransform);
    torusModel->setRotationAnimationVector(vec3(1.0f, 0.5f, 0.0f));
    m_Models.push_back(std::move(torusModel));

    Transform sphereTransform{
        vec3(3.0f, 0.0f, 0.0f),
        vec3(0.0f, 0.0f, 0.0f),
        vec3(1.0f, 1.0f, 1.0f)};
    m_Models.push_back(std::make_unique<ModelNT>(this, FilePaths::MODEL_BASIC_SPHERE, m_Models.size(), sphereTransform));
    cout << "Successfully instantiated Models!\n";

    VULKAN_SETUP(createUniformBuffers);

    PRINT_BOLD_GREEN("Descriptor Pool and Sets Setup");
    VULKAN_SETUP(createDescriptorPool);
    VULKAN_SETUP(createDescriptorSets);

    PRINT_BOLD_GREEN("Command Buffers and Sync Objects Setup");
    VULKAN_SETUP(createCommandBuffers);
    VULKAN_SETUP(createSyncObjects);

    PRINT_BOLD_GREEN("* * * * * * * * * * * * * * * * * *");
    PRINT_BOLD_GREEN("*    Finished setting up Vulkan   *");
    PRINT_BOLD_GREEN("* * * * * * * * * * * * * * * * * *");

    auto initEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> totalElapsed = initEnd - initStart;

    fprintf(stdout, "\033[32mTotal Vulkan setup time: %.2f ms\n\033[0m", totalElapsed.count());
}

DEF Engine::hasStencilComponent(VkFormat format) -> bool {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

DEF Engine::findDepthFormat() -> VkFormat {
    return findSupportedFormat(
        {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
        VK_IMAGE_TILING_OPTIMAL,
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

DEF Engine::findSupportedFormat(
    const vector<VkFormat> &candidates,
    VkImageTiling tiling,
    VkFormatFeatureFlags features) -> VkFormat {

    for (VkFormat format : candidates) {
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, format, &props);

        if (tiling == VK_IMAGE_TILING_LINEAR) {
            if ((props.linearTilingFeatures & features) == features) {
                return format;
            }
        } else if (tiling == VK_IMAGE_TILING_OPTIMAL) {
            if ((props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }
    }

    throw std::runtime_error("failed to find supported format!");
}

DEF Engine::createDepthResources() -> void {
    VkFormat depthFormat = findDepthFormat();
    createImage(
        m_SwapChainExtent.width,
        m_SwapChainExtent.height,
        1,
        m_MSAASamples,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_DepthImage,
        m_DepthImageMemory);
    m_DepthImageView = createImageView(m_DepthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);
}

DEF Engine::createColorResources() -> void {
    VkFormat colorFormat = m_SwapChainImageFormat;

    createImage(
        m_SwapChainExtent.width,
        m_SwapChainExtent.height,
        1,
        m_MSAASamples,
        colorFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_ColorImage,
        m_ColorImageMemory);
    m_ColorImageView = createImageView(m_ColorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
}

DEF Engine::createImage(
    const uint32_t width,
    const uint32_t height,
    const uint32_t mipLevels,
    VkSampleCountFlagBits numSamples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkImage &image,
    VkDeviceMemory &imageMemory
) -> void {
    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {.width = width, .height = height, .depth = 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = numSamples,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

    if (vkCreateImage(m_Device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(m_Device, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(m_Device, image, imageMemory, 0);
}

DEF Engine::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, const uint32_t mipLevels) const -> VkImageView {
    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {
            .aspectMask = aspectFlags,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1},
    };

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(m_Device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw runtime_error("failed to create texture image view!");
    }

    return imageView;
}

DEF Engine::createTextureImage() -> void {
    if (!std::filesystem::exists(FilePaths::PAINTED_PLASTER_DIFFUSE)) {
        throw runtime_error("Texture file not found: " + std::string(FilePaths::PAINTED_PLASTER_DIFFUSE));
    }

    int texWidth = 0;
    int texHeight = 0;
    int texChannels = 0;
    stbi_uc *pixels = stbi_load(FilePaths::PAINTED_PLASTER_DIFFUSE, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) throw runtime_error("failed to load texture image!");
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(texWidth) * texHeight * 4;

    // m_MipLevels = How often we can divide max(width, height) by 2, could also take the ceil here instead of floor + 1
    m_MipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data = nullptr;
    vkMapMemory(m_Device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, imageSize);
    vkUnmapMemory(m_Device, stagingBufferMemory);

    stbi_image_free(pixels);

    createImage(
        texWidth,
        texHeight,
        m_MipLevels,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_TextureImage,
        m_TextureImageMemory);
    transitionImageLayout(
        m_TextureImage,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        m_MipLevels);
    copyBufferToImage(
        stagingBuffer,
        m_TextureImage,
        static_cast<uint32_t>(texWidth),
        static_cast<uint32_t>(texHeight));

    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);

    generateMipmaps(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB, texWidth, texHeight, m_MipLevels);
}

DEF Engine::createTextureImageView() -> void {
    m_TextureImageView = createImageView(m_TextureImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT, m_MipLevels);
}

DEF Engine::createTextureSampler() -> void {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &properties);

    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = properties.limits.maxSamplerAnisotropy,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0,
        .maxLod = static_cast<float>(m_MipLevels),
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE};

    if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_TextureSampler) != VK_SUCCESS) {
        throw runtime_error("failed to create texture sampler!");
    }
}

DEF Engine::generateMipmaps(VkImage image, VkFormat imageFormat, const int32_t texWidth, const int32_t texHeight, const uint32_t mipLevels) -> void {
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1},
    };

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        std::array srcOffsets = {
            VkOffset3D{0, 0, 0},
            VkOffset3D{mipWidth, mipHeight, 1}};
        std::array dstOffsets = {
            VkOffset3D{0, 0, 0},
            VkOffset3D{mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1}};
        VkImageBlit blit{
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = 1},
            .srcOffsets = {srcOffsets[0], srcOffsets[1]},
            .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = i, .baseArrayLayer = 0, .layerCount = 1},
            .dstOffsets = {dstOffsets[0], dstOffsets[1]}};

        vkCmdBlitImage(
            commandBuffer,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    // Need to specifically handle the last mipMap, as the don't blit from it that is not handled in the loop
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);

    endSingleTimeCommands(commandBuffer);
}

DEF Engine::getMaxUsableSampleCount() const -> VkSampleCountFlagBits {
    VkPhysicalDeviceProperties physicalDeviceProperties;
    vkGetPhysicalDeviceProperties(m_PhysicalDevice, &physicalDeviceProperties);

    VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts & physicalDeviceProperties.limits.framebufferDepthSampleCounts;
    if (counts & VK_SAMPLE_COUNT_64_BIT) return VK_SAMPLE_COUNT_64_BIT;
    if (counts & VK_SAMPLE_COUNT_32_BIT) return VK_SAMPLE_COUNT_32_BIT;
    if (counts & VK_SAMPLE_COUNT_16_BIT) return VK_SAMPLE_COUNT_16_BIT;
    if (counts & VK_SAMPLE_COUNT_8_BIT) return VK_SAMPLE_COUNT_8_BIT;
    if (counts & VK_SAMPLE_COUNT_4_BIT) return VK_SAMPLE_COUNT_4_BIT;
    if (counts & VK_SAMPLE_COUNT_2_BIT) return VK_SAMPLE_COUNT_2_BIT;
    return VK_SAMPLE_COUNT_1_BIT;
}

DEF Engine::beginSingleTimeCommands() const -> VkCommandBuffer {
    VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

DEF Engine::endSingleTimeCommands(VkCommandBuffer commandBuffer) const -> void {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_GraphicsQueue);

    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}

void Engine::createDescriptorSets() {
    const size_t numModels = m_Models.size();
    const size_t totalSets = Settings::MAX_FRAMES_IN_FLIGHT * numModels;

    std::vector layouts(totalSets, m_DescriptorSetLayout);
    const VkDescriptorSetAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_DescriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(totalSets),
        .pSetLayouts = layouts.data()};

    m_DescriptorSets.resize(totalSets);
    if (vkAllocateDescriptorSets(m_Device, &allocInfo, m_DescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    for (size_t i = 0; i < Settings::MAX_FRAMES_IN_FLIGHT; i++) {
        for (size_t j = 0; j < numModels; j++) {
            const size_t bufferIndex = i * numModels + j;
            VkDescriptorBufferInfo bufferInfo{
                .buffer = m_UniformBuffers[bufferIndex],
                .offset = 0,
                .range = sizeof(UniformBufferObject)};

            VkDescriptorImageInfo imageInfo{
                .sampler = m_TextureSampler,
                .imageView = m_TextureImageView,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            std::array descriptorWrites{
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_DescriptorSets[bufferIndex],
                    .dstBinding = 0,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    .pBufferInfo = &bufferInfo},
                VkWriteDescriptorSet{
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = m_DescriptorSets[bufferIndex],
                    .dstBinding = 1,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .pImageInfo = &imageInfo}};

            vkUpdateDescriptorSets(
                m_Device,
                descriptorWrites.size(),
                descriptorWrites.data(),
                0,
                nullptr);
        }
    }
}

void Engine::createDescriptorPool() {
    const size_t numModels = m_Models.size();
    const size_t totalSets = Settings::MAX_FRAMES_IN_FLIGHT * numModels;

    std::array poolSizes{
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = static_cast<uint32_t>(totalSets)},
        VkDescriptorPoolSize{
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = static_cast<uint32_t>(totalSets)}};

    VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
        .maxSets = static_cast<uint32_t>(totalSets)};

    if (vkCreateDescriptorPool(m_Device, &poolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void Engine::createUniformBuffers() {
    const size_t numModels = m_Models.size();
    const size_t totalBuffers = Settings::MAX_FRAMES_IN_FLIGHT * numModels;

    m_UniformBuffers.resize(totalBuffers);
    m_UniformBuffersMemory.resize(totalBuffers);
    m_UniformBuffersMapped.resize(totalBuffers);

    for (size_t i = 0; i < Settings::MAX_FRAMES_IN_FLIGHT; i++) {
        for (size_t j = 0; j < numModels; j++) {
            constexpr VkDeviceSize bufferSize = sizeof(UniformBufferObject);
            const size_t bufferIndex = i * numModels + j;
            createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                m_UniformBuffers[bufferIndex],
                m_UniformBuffersMemory[bufferIndex]);

            if (vkMapMemory(
                    m_Device,
                    m_UniformBuffersMemory[bufferIndex],
                    0,
                    bufferSize,
                    0,
                    &m_UniformBuffersMapped[bufferIndex]) != VK_SUCCESS) {
                throw std::runtime_error("failed to map uniform buffer memory!");
            }
        }
    }
}

DEF Engine::createDescriptorSetLayout() -> void {
    constexpr VkDescriptorSetLayoutBinding uboLayoutBinding{
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};

    constexpr VkDescriptorSetLayoutBinding samplerLayoutBinding{
        .binding = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .pImmutableSamplers = nullptr};

    array bindings = {uboLayoutBinding, samplerLayoutBinding};
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()};

    if (vkCreateDescriptorSetLayout(m_Device, &layoutInfo, nullptr, &m_DescriptorSetLayout) != VK_SUCCESS) {
        throw runtime_error("failed to create descriptor set layout!");
    }
}

DEF Engine::findMemoryType(const uint32_t typeFilter, VkMemoryPropertyFlags properties) const -> uint32_t {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw runtime_error("Couldn't determine the memory type.");
}

DEF Engine::createBuffer(
    const VkDeviceSize size,
    const VkBufferUsageFlags usage,
    const VkMemoryPropertyFlags properties,
    VkBuffer &buffer,
    VkDeviceMemory &bufferMemory
) const -> void {
    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE};

    if (vkCreateBuffer(m_Device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(m_Device, buffer, &memRequirements);

    const VkMemoryAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)};

    if (vkAllocateMemory(m_Device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(m_Device, buffer, bufferMemory, 0);
}

DEF Engine::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, const VkDeviceSize size) const -> void {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    endSingleTimeCommands(commandBuffer);
}

DEF Engine::copyBufferToImage(VkBuffer buffer, VkImage image, const uint32_t width, const uint32_t height) const -> void {
    const VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    const VkBufferImageCopy region{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = width, .height = height, .depth = 1}};

    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    endSingleTimeCommands(commandBuffer);
}

DEF Engine::transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    const uint32_t mipLevels) const -> void {

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = 0,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    VkPipelineStageFlags sourceStage = 0;
    VkPipelineStageFlags destinationStage = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // Transition from an undefined layout to a layout that allows for writing by transfer operations
        barrier.srcAccessMask = 0;                            // No need to wait for anything
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // The image will be written to

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // Transition from a transfer destination layout to a shader-readable layout
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // Must wait for the transfer to complete
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;    // The image will be read by the shader

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
        // Transition from transfer source to presentation layout
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT; // Must wait for transfer reads to complete
        barrier.dstAccessMask = 0;                           // No need for further synchronization before presenting

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

    } else if (oldLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        // Transition from presentation layout to transfer source layout
        barrier.srcAccessMask = 0;                           // No need to wait for anything before transitioning
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; // The image will be read as a source for transfer

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;

    } else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    endSingleTimeCommands(commandBuffer);
}

DEF Engine::createSyncObjects() -> void {
    m_ImageAvailableSemaphores.resize(Settings::MAX_FRAMES_IN_FLIGHT);
    m_RenderFinishedSemaphores.resize(Settings::MAX_FRAMES_IN_FLIGHT);
    m_InFlightFences.resize(Settings::MAX_FRAMES_IN_FLIGHT);

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo fenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT};

    for (size_t i = 0; i < Settings::MAX_FRAMES_IN_FLIGHT; i++) {
        fprintf(stdout, "\t%zu. frame\n", i + 1);
        const VkResult result_1 = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_ImageAvailableSemaphores[i]);
        if (result_1 != VK_SUCCESS) throw runtime_error("failed to create ImageAvailable semaphore!");
        const VkResult result_2 = vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &m_RenderFinishedSemaphores[i]);
        if (result_2 != VK_SUCCESS) throw runtime_error("failed to create RenderFinished semaphore!");
        const VkResult result_3 = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFences[i]);
        if (result_3 != VK_SUCCESS) throw runtime_error("failed to create InFlight fence!");
    }
}

DEF Engine::createCommandBuffers() -> void {
    m_CommandBuffers.resize(Settings::MAX_FRAMES_IN_FLIGHT);

    const VkCommandBufferAllocateInfo allocInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_CommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size())};

    if (vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data()) != VK_SUCCESS) {
        throw runtime_error("failed to allocate command buffers!");
    }
}

void Engine::recordCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    const VkRenderPassBeginInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = m_RenderPass,
        .framebuffer = m_SwapChainFramebuffers[imageIndex],
        .renderArea = {
            .offset = {0, 0},
            .extent = m_SwapChainExtent},
        .clearValueCount = static_cast<uint32_t>(clearValues.size()),
        .pClearValues = clearValues.data()};

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_GraphicsPipeline);

    VkViewport viewport{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(m_SwapChainExtent.width),
        .height = static_cast<float>(m_SwapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{
        .offset = {0, 0},
        .extent = m_SwapChainExtent};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    updatePushConstants();
    vkCmdPushConstants(
        commandBuffer,
        m_PipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants),
        &m_PushConstants);

    for (size_t j = 0; j < m_Models.size(); j++) {
        size_t descriptorSetIndex = m_CurrentFrameIdx * m_Models.size() + j;
        VkDescriptorSet descriptorSet = m_DescriptorSets[descriptorSetIndex];

        if (descriptorSet == VK_NULL_HANDLE) {
            throw std::runtime_error("Invalid descriptor set handle!");
        }

        m_Models[j]->enqueueIntoCommandBuffer(commandBuffer, descriptorSet);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

DEF Engine::createCommandPool() -> void {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_PhysicalDevice);
    if (!queueFamilyIndices.graphicsFamily.has_value()) {
        throw runtime_error("QueueFamilyIndices GraphicsFamily has no value!");
    }
    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value()};

    if (vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        throw runtime_error("failed to create command pool!");
    }
}

DEF Engine::createFramebuffers() -> void {
    m_SwapChainFramebuffers.resize(m_SwapChainImageViews.size());
    for (size_t i = 0; i < m_SwapChainImageViews.size(); i++) {
        fprintf(stdout, "\t%zu. Framebuffers.\n", i + 1);
        array attachments = {m_ColorImageView, m_DepthImageView, m_SwapChainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = m_RenderPass,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .width = m_SwapChainExtent.width,
            .height = m_SwapChainExtent.height,
            .layers = 1};

        if (vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_SwapChainFramebuffers[i]) != VK_SUCCESS) {
            throw runtime_error("failed to create framebuffer!");
        }
    }
}

DEF Engine::createRenderPass() -> void {
    const VkAttachmentDescription colorAttachment{
        .format = m_SwapChainImageFormat,
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    const VkAttachmentDescription depthAttachment{
        .format = findDepthFormat(),
        .samples = m_MSAASamples,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    const VkAttachmentDescription colorAttachmentResolve{
        .format = m_SwapChainImageFormat,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};

    constexpr VkAttachmentReference colorAttachmentRef{
        .attachment = 0,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    constexpr VkAttachmentReference depthAttachmentRef{
        .attachment = 1,
        .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    constexpr VkAttachmentReference colorAttachmentResolveRef{
        .attachment = 2,
        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

    VkSubpassDescription description{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachmentRef,
        .pResolveAttachments = &colorAttachmentResolveRef,
        .pDepthStencilAttachment = &depthAttachmentRef};

    VkSubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};

    array attachments = {colorAttachment, depthAttachment, colorAttachmentResolve};
    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = static_cast<uint32_t>(attachments.size()),
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &description,
        .dependencyCount = 1,
        .pDependencies = &dependency};

    if (vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        throw runtime_error("failed to create render pass!");
    }
}

DEF Engine::createGraphicsPipeline() -> void {
    fprintf(stdout, "Trying to create Shader modules.\n");
    fprintf(stdout, "Trying to read .spv files.\n");
    vector<char> vertShaderCode = Util::readFile(FilePaths::SHADER_VERT_PHONG_STAGES);
    vector<char> fragShaderCode = Util::readFile(FilePaths::SHADER_FRAG_PHONG_STAGES);

    fprintf(stdout, "\tTrying to create Vertex Shader.\n");
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main"};

    fprintf(stdout, "\tTrying to create Fragment Shader.\n");
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fragShaderModule,
        .pName = "main"};

    array shaderStages = {vertShaderStageInfo, fragShaderStageInfo};
    fprintf(stdout, "Successfully created the shader modules.\n");

    fprintf(stdout, "Trying to Initialize Fixed Functions.\n");
    fprintf(stdout, "\tInitializing Vertex Input.\n");
    auto bindingDescription = VertexNT::getBindingDescription();
    auto attributeDescriptions = VertexNT::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = &bindingDescription,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()};

    fprintf(stdout, "\tInitializing Input Assembly.\n");
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE};

    vector dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()};

    VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1};

    fprintf(stdout, "\tInitializing Rasterizer.\n");
    VkPipelineRasterizationStateCreateInfo rasterizer{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
        .depthBiasConstantFactor = 0.0f,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = 0.0f,
        .lineWidth = 1.0f};

    VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE};

    fprintf(stdout, "\tInitializing Multisampling.\n");
    VkPipelineMultisampleStateCreateInfo multisampling{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = m_MSAASamples,
        .sampleShadingEnable = VK_FALSE};

    fprintf(stdout, "\tInitializing Color Blending.\n");
    VkPipelineColorBlendAttachmentState colorBlendAttachment{
        .blendEnable = VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };

    VkPipelineColorBlendStateCreateInfo colorBlending{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = 1,
        .pAttachments = &colorBlendAttachment,
        .blendConstants = {0.0F, 0.0F, 0.0F, 0.0F}};

    // Define the push constant range
    VkPushConstantRange pushConstantRange = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(PushConstants)};

    fprintf(stdout, "\tInitializing Render Pipeline.\n");
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &m_DescriptorSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstantRange};

    if (vkCreatePipelineLayout(m_Device, &pipelineLayoutInfo, nullptr, &m_PipelineLayout) != VK_SUCCESS) {
        throw runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = m_PipelineLayout,
        .renderPass = m_RenderPass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE};

    if (vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_GraphicsPipeline) != VK_SUCCESS) {
        throw runtime_error("failed to create graphics pipeline!");
    }

    fprintf(stdout, "Cleaning up shader modules.\n");
    vkDestroyShaderModule(m_Device, fragShaderModule, nullptr);
    vkDestroyShaderModule(m_Device, vertShaderModule, nullptr);
}

DEF Engine::createShaderModule(const vector<char> &code) const -> VkShaderModule {
    const VkShaderModuleCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t *>(code.data()),
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_Device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

DEF Engine::createImageViews() -> void {
    m_SwapChainImageViews.resize(m_SwapChainImages.size());

    for (uint32_t i = 0; i < m_SwapChainImages.size(); i++) {
        m_SwapChainImageViews[i] = createImageView(m_SwapChainImages[i], m_SwapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);
    }
}

void Engine::createSwapChain() {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(m_PhysicalDevice);

    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

    // Limit the number of swap chain images to MAX_FRAMES_IN_FLIGHT
    uint32_t imageCount = Settings::MAX_FRAMES_IN_FLIGHT;

    // Ensure imageCount is within the allowed range
    if (imageCount < swapChainSupport.capabilities.minImageCount) {
        imageCount = swapChainSupport.capabilities.minImageCount;
    }
    if (swapChainSupport.capabilities.maxImageCount > 0 &&
        imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = m_Surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };

    QueueFamilyIndices queueIndices = findQueueFamilies(m_PhysicalDevice);
    std::array<uint32_t, 2> queueFamilyIndices;
    if (queueIndices.graphicsFamily.has_value() && queueIndices.presentationFamily.has_value()) {
        queueFamilyIndices = {queueIndices.graphicsFamily.value(), queueIndices.presentationFamily.value()};
    } else {
        throw std::runtime_error("QueueFamilyIndices not complete!");
    }

    if (queueIndices.graphicsFamily != queueIndices.presentationFamily) {
        fprintf(stdout, "Setting imageSharingMode to Concurrent.\n");
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    } else {
        fprintf(stdout, "Setting imageSharingMode to Exclusive.\n");
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0;
        createInfo.pQueueFamilyIndices = nullptr;
    }

    if (vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_SwapChain) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, nullptr);
    m_SwapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &imageCount, m_SwapChainImages.data());

    m_SwapChainImageFormat = surfaceFormat.format;
    m_SwapChainExtent = extent;
}

void Engine::cleanupSwapChain() {
    vkDestroyImageView(m_Device, m_ColorImageView, nullptr);
    vkDestroyImage(m_Device, m_ColorImage, nullptr);
    vkFreeMemory(m_Device, m_ColorImageMemory, nullptr);

    vkDestroyImageView(m_Device, m_DepthImageView, nullptr);
    vkDestroyImage(m_Device, m_DepthImage, nullptr);
    vkFreeMemory(m_Device, m_DepthImageMemory, nullptr);

    for (const auto framebuffer : m_SwapChainFramebuffers) {
        vkDestroyFramebuffer(m_Device, framebuffer, nullptr);
    }
    m_SwapChainFramebuffers.clear();

    const auto commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());
    if (commandBufferCount > 0) {
        vkFreeCommandBuffers(m_Device, m_CommandPool, commandBufferCount, m_CommandBuffers.data());
        m_CommandBuffers.clear();
    }

    vkDestroyPipeline(m_Device, m_GraphicsPipeline, nullptr);
    m_GraphicsPipeline = VK_NULL_HANDLE;

    vkDestroyPipelineLayout(m_Device, m_PipelineLayout, nullptr);
    m_PipelineLayout = VK_NULL_HANDLE;

    vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
    m_RenderPass = VK_NULL_HANDLE;

    for (const auto imageView : m_SwapChainImageViews) {
        vkDestroyImageView(m_Device, imageView, nullptr);
    }
    m_SwapChainImageViews.clear();

    vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
    m_SwapChain = VK_NULL_HANDLE;

    // Destroy uniform buffers
    for (size_t i = 0; i < m_UniformBuffers.size(); i++) {
        if (m_UniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_UniformBuffers[i], nullptr);
            m_UniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (m_UniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(m_Device, m_UniformBuffersMemory[i], nullptr);
            m_UniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }
    m_UniformBuffers.clear();
    m_UniformBuffersMemory.clear();
    m_UniformBuffersMapped.clear();

    // Destroy descriptor pool
    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    m_DescriptorSets.clear();
}
void Engine::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_Window, &width, &height);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(m_Device);

    cleanupSwapChain();

    createSwapChain();
    createImageViews();
    createRenderPass();
    createGraphicsPipeline();
    createColorResources();
    createDepthResources();
    createFramebuffers();

    // Recreate uniform buffers and descriptor sets
    createUniformBuffers();
    createDescriptorPool();
    createDescriptorSets();

    // No need to recreate command buffers if you reset them each frame
}

DEF Engine::createSurface() -> void {
    if (glfwCreateWindowSurface(m_Instance, m_Window, nullptr, &m_Surface) != VK_SUCCESS) {
        throw runtime_error("failed to create window surface!");
    }
}

DEF Engine::createLogicalDevice() -> void {
    auto [graphicsFamily, presentationFamily] = findQueueFamilies(m_PhysicalDevice);

    vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    set<uint32_t> uniqueQueueFamilies;
    if (graphicsFamily.has_value() && presentationFamily.has_value()) {
        uniqueQueueFamilies = {graphicsFamily.value(), presentationFamily.value()};
    } else {
        throw runtime_error("QueueFamilyIndices are not fully defined.");
    }

    // Between 0.0 and 1.0
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority};
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{.samplerAnisotropy = VK_TRUE};

    VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data(),
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures};

    createInfo.enabledLayerCount = 0;
    if constexpr (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }

    if (vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device) != VK_SUCCESS) {
        throw runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(m_Device, graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, presentationFamily.value(), 0, &m_PresentQueue);
}

DEF Engine::pickPhysicalDevice() -> void {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) throw runtime_error("failed to find GPUs with Vulkan support!");
    vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    size_t i = 0;
    const auto found = std::ranges::find_if(devices,
                                            [&](const auto &device) {
                                                fprintf(stdout, "Checking %zu. device", ++i);
                                                return isDeviceSuitable(device);
                                            });
    if (found == devices.end()) throw runtime_error("failed to find a suitable GPU!");

    m_PhysicalDevice = *found;
    m_MSAASamples = getMaxUsableSampleCount();
}

DEF Engine::isDeviceSuitable(VkPhysicalDevice device) const -> bool {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    if constexpr (Settings::ALLOW_DEVICE_WITHOUT_INTEGRATED_GPU) {
        fprintf(stdout, "'%s' flag is set so we don't check if it's a discrete GPU.\n", "Settings::ALLOW_DEVICE_WITHOUT_INTEGRATED_GPU");
    } else {
        if (deviceProperties.deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            fprintf(stderr, "Device is unsuitable because it's not a discrete GPU!.");
        } else {
            fprintf(stdout, "Device is a discrete GPU!.");
        }
    }

    if constexpr (Settings::ALLOW_DEVICE_WITHOUT_GEOMETRY_SHADER) {
        fprintf(stdout, "'%s' flag is set so we don't check if it supports geometry shaders.\n", "Settings::ALLOW_DEVICE_WITHOUT_GEOMETRY_SHADER");
    } else {
        if (!deviceFeatures.geometryShader) {
            fprintf(stderr, "Device is unsuitable because it does not support Geometry Shaders!.\n");
            return false;
        } else {
            fprintf(stdout, "Device supports Geometry Shaders.\n");
        }
    }

    QueueFamilyIndices indices = findQueueFamilies(device);
    if (!indices.isComplete()) {
        fprintf(stdout, "Device is unsuitable because its QueueFamily is incomplete!\n");
        return false;
    }
    fprintf(stdout, "QueueFamily of the Device is complete.\n");

    if (!checkDeviceExtensionSupport(device)) {
        fprintf(stdout, "Device is unsuitable because it does not support the necessary extensions!\n");
        return false;
    }
    fprintf(stdout, "Device supports the necessary extensions.\n");

    auto [capabilities, formats, presentModes] = querySwapChainSupport(device);
    if (formats.empty() || presentModes.empty()) {
        fprintf(stdout, "Device is unsuitable because its swapChain is not adequate!\n");
        return false;
    }

    VkPhysicalDeviceFeatures supportedFeatures;
    vkGetPhysicalDeviceFeatures(device, &supportedFeatures);
    if (!supportedFeatures.samplerAnisotropy) {
        fprintf(stdout, "Device is unsuitable because does not support sampler Anisotropy (that is very surprising)!\n");
        return false;
    }

    return true;
}

DEF Engine::checkDeviceExtensionSupport(VkPhysicalDevice device) -> bool {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    vector<VkExtensionProperties> availiableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availiableExtensions.data());

    set<string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto &[extensionName, specVersion] : availiableExtensions) {
        requiredExtensions.erase(extensionName);
    }

    return requiredExtensions.empty();
}

DEF Engine::findQueueFamilies(VkPhysicalDevice device) const -> QueueFamilyIndices {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto &queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &presentSupport);
        if (presentSupport) {
            indices.presentationFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }
        i++;
    }
    if (!indices.isComplete()) throw runtime_error("findQueueFamilies couldn't find the necessary values!\n");
    return indices;
}

DEF Engine::mainLoop() -> void {
    fprintf(stdout, "Starting the main loop.\n");
    while (!glfwWindowShouldClose(m_Window)) {
        glfwPollEvents();
        drawFrame();
    }
}

void Engine::drawFrame() {
    vkWaitForFences(m_Device, 1, &m_InFlightFences[m_CurrentFrameIdx], VK_TRUE, NO_TIMEOUT);

    uint32_t imageIndex = 0;
    const VkResult resultNextImage = vkAcquireNextImageKHR(
        m_Device,
        m_SwapChain,
        NO_TIMEOUT,
        m_ImageAvailableSemaphores[m_CurrentFrameIdx],
        VK_NULL_HANDLE,
        &imageIndex);

    if (resultNextImage == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapChain();
        return;
    }
    if (resultNextImage != VK_SUCCESS && resultNextImage != VK_SUBOPTIMAL_KHR) throw std::runtime_error("failed to acquire swap chain image!");

    vkResetFences(m_Device, 1, &m_InFlightFences[m_CurrentFrameIdx]);

    vkResetCommandBuffer(m_CommandBuffers[m_CurrentFrameIdx], 0);
    recordCommandBuffers(m_CommandBuffers[m_CurrentFrameIdx], imageIndex);

    for (size_t i = 0; i < m_Models.size(); i++) {
        UniformBufferObject ubo = m_Models[i]->getUBO();
        const size_t bufferIndex = getCurrentFrameIdx() * m_Models.size() + i;
        memcpy(m_UniformBuffersMapped[bufferIndex], &ubo, sizeof(ubo));
    }

    array waitSemaphores = {m_ImageAvailableSemaphores[m_CurrentFrameIdx]};
    array waitStages = {static_cast<VkPipelineStageFlags>(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT)};
    array signalSemaphores = {m_RenderFinishedSemaphores[m_CurrentFrameIdx]};

    VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = waitSemaphores.data(),
        .pWaitDstStageMask = waitStages.data(),
        .commandBufferCount = 1,
        .pCommandBuffers = &m_CommandBuffers[m_CurrentFrameIdx],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = signalSemaphores.data()};

    if (vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFences[m_CurrentFrameIdx]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    array swapChains = {m_SwapChain};
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = signalSemaphores.data(),
        .swapchainCount = 1,
        .pSwapchains = swapChains.data(),
        .pImageIndices = &imageIndex,
        .pResults = nullptr};

    const VkResult resultQueue = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
    if (resultQueue == VK_ERROR_OUT_OF_DATE_KHR || resultQueue == VK_SUBOPTIMAL_KHR || m_FramebufferResized) {
        m_FramebufferResized = false;
        recreateSwapChain();
    } else if (resultQueue != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }

    m_CurrentFrameIdx = (m_CurrentFrameIdx + 1) % Settings::MAX_FRAMES_IN_FLIGHT;
    m_FrameCounter += 1;
}

DEF Engine::captureFramebuffer(uint32_t imageIndex) const -> void {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

    uint32_t width = m_SwapChainExtent.width;
    uint32_t height = m_SwapChainExtent.height;
    auto imageSize = width * height * 4;

    VkImage image = m_SwapChainImages[imageIndex];

    // Create a staging buffer to copy the image data into
    createBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    // Transition the image to be copied
    transitionImageLayout(
        image,
        m_SwapChainImageFormat,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        1);

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = width,
        .bufferImageHeight = height,
        .imageSubresource = {
            VK_IMAGE_ASPECT_COLOR_BIT, // aspectMask
            0,                         // mipLevel
            0,                         // baseArrayLayer
            1                          // layerCount
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {width, height, 1},
    };

    // Copy the image to the staging buffer
    vkCmdCopyImageToBuffer(
        commandBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        1,
        &region);

    endSingleTimeCommands(commandBuffer);

    // Transition the image back to present layout
    transitionImageLayout(
        image,
        m_SwapChainImageFormat,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        1);

    // Map the buffer memory so we can read from it
    void *data = nullptr;
    vkMapMemory(m_Device, stagingBufferMemory, 0, imageSize, 0, &data);

    const auto *pixelData = static_cast<const uint8_t *>(data);

    std::ostringstream filename_builder;
    filename_builder << "./Screencaps/Raw/" << m_FrameCounter << ".bin";

    std::string filename = filename_builder.str();

    std::cout << "Saving to file: " << filename << "\n";
    std::ofstream output(filename, std::ios::binary);

    if (output.is_open()) {
        output.write(reinterpret_cast<const char *>(&width), sizeof(width));
        output.write(reinterpret_cast<const char *>(&height), sizeof(height));
        output.write(reinterpret_cast<const char *>(pixelData), imageSize);
        output.close();
    } else {
        std::cerr << "Failed to open file: " << filename << "\n";
    }

    vkUnmapMemory(m_Device, stagingBufferMemory);
    vkDestroyBuffer(m_Device, stagingBuffer, nullptr);
    vkFreeMemory(m_Device, stagingBufferMemory, nullptr);
}

void Engine::cleanup() {
    fprintf(stdout, "Starting the cleanup.\n");
    fprintf(stdout, "Starting the Vulkan cleanup.\n");
    fprintf(stdout, "Engine destructor has been called, waiting for the device to idle.\n");
    vkDeviceWaitIdle(m_Device);
    fprintf(stdout, "Finished waiting.\n");

    cleanupSwapChain();

    for (size_t i = 0; i < Settings::MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(m_Device, m_ImageAvailableSemaphores[i], nullptr);
        vkDestroySemaphore(m_Device, m_RenderFinishedSemaphores[i], nullptr);
        vkDestroyFence(m_Device, m_InFlightFences[i], nullptr);
    }
    m_ImageAvailableSemaphores.clear();
    m_RenderFinishedSemaphores.clear();
    m_InFlightFences.clear();

    vkDestroySampler(m_Device, m_TextureSampler, nullptr);
    m_TextureSampler = VK_NULL_HANDLE;

    vkDestroyImageView(m_Device, m_TextureImageView, nullptr);
    m_TextureImageView = VK_NULL_HANDLE;

    vkDestroyImage(m_Device, m_TextureImage, nullptr);
    vkFreeMemory(m_Device, m_TextureImageMemory, nullptr);
    m_TextureImage = VK_NULL_HANDLE;
    m_TextureImageMemory = VK_NULL_HANDLE;

    vkDestroyDescriptorSetLayout(m_Device, m_DescriptorSetLayout, nullptr);
    m_DescriptorSetLayout = VK_NULL_HANDLE;

    for (auto &model : m_Models) {
        model.reset();
    }
    m_Models.clear();

    vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
    m_CommandPool = VK_NULL_HANDLE;

    if (enableValidationLayers) {
        DestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        m_DebugMessenger = VK_NULL_HANDLE;
    }
    vkDestroyDevice(m_Device, nullptr);
    m_Device = VK_NULL_HANDLE;

    vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
    m_Surface = VK_NULL_HANDLE;

    vkDestroyInstance(m_Instance, nullptr);
    m_Instance = VK_NULL_HANDLE;

    fprintf(stdout, "Finished the Vulkan cleanup.\n");

    fprintf(stdout, "Started the the GLFW cleanup.\n");
    glfwDestroyWindow(m_Window);
    m_Window = nullptr;

    glfwTerminate();
    fprintf(stdout, "Finished the GLFW cleanup.\n");
    fprintf(stdout, "Finished the cleanup.\n");
}

DEF Engine::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo) -> void {
    createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback};
}

DEF Engine::setupDebugMessenger() -> void {
    if constexpr (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);

    VkResult result = CreateDebugUtilsMessengerEXT(m_Instance, &createInfo, nullptr, &m_DebugMessenger);
    if (result != VK_SUCCESS) {
        throw runtime_error("failed to set up debug messenger!");
    }
}

DEF Engine::getRequiredExtensions() -> vector<const char *> {
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::span glfwExtensionSpan(glfwExtensions, glfwExtensionCount);

    vector extensions(glfwExtensionSpan.begin(), glfwExtensionSpan.end());
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

DEF Engine::checkValidationLayerSupport() -> bool {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    vector<VkLayerProperties> availableLayers(layerCount);
    (void)vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char *layerName : validationLayers) {
        string_view layerNameView(layerName);
        const bool layerFound = std::ranges::any_of(availableLayers,
                                                    [&](const auto &layerProperties) {
                                                        return layerNameView == string_view(layerProperties.layerName);
                                                    });

        if (!layerFound) return false;
    }

    return true;
}

VKAPI_ATTR DEF VKAPI_CALL Engine::debugCallback(
    [[maybe_unused]] VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    [[maybe_unused]] VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    [[maybe_unused]] void *pUserData) -> VkBool32 {

    std::cerr << "validation layer: " << pCallbackData->pMessage << "";
    return VK_FALSE;
}

DEF Engine::querySwapChainSupport(VkPhysicalDevice device) const -> SwapChainSupportDetails {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device,
        m_Surface,
        &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device,
        m_Surface,
        &formatCount,
        details.formats.data());
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(
            device,
            m_Surface,
            &formatCount,
            details.formats.data());
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device,
        m_Surface,
        &presentModeCount,
        nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device,
            m_Surface,
            &presentModeCount,
            details.presentModes.data());
    }

    return details;
}

DEF Engine::chooseSwapSurfaceFormat(const vector<VkSurfaceFormatKHR> &availableFormats) -> VkSurfaceFormatKHR {
    if (availableFormats.empty()) fprintf(stderr, "No surface formats available!");

    const auto found = std::ranges::find_if(availableFormats,
                                            [](const VkSurfaceFormatKHR &availableFormat) {
                                                return availableFormat.format == Settings::PREFERRED_SURFACE_FORMAT.format &&
                                                       availableFormat.colorSpace == Settings::PREFERRED_SURFACE_FORMAT.colorSpace;
                                            });
    if ((found != availableFormats.end())) return *found;
    return availableFormats[0];
}

DEF Engine::chooseSwapPresentMode(const vector<VkPresentModeKHR> &availablePresentModes) -> VkPresentModeKHR {
    if (availablePresentModes.empty()) throw std::runtime_error("No presentation modes available!");

    const auto found = std::ranges::find_if(availablePresentModes,
                                            [](const VkPresentModeKHR &availablePresentMode) {
                                                return availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR; // Render as quickly as possible
                                            });
    if ((found != availablePresentModes.end())) return *found;
    return availablePresentModes[0];
}

DEF Engine::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) const -> VkExtent2D {
    const bool isExtentUndefined = capabilities.currentExtent.width == std::numeric_limits<uint32_t>::max();
    if (!isExtentUndefined) return capabilities.currentExtent;

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_Window, &width, &height);
    const VkExtent2D actualExtent = {
        .width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        .height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };

    return actualExtent;
}

DEF Engine::setCameraPosition(const vec3 position) -> void { m_CameraEye = position; }
DEF Engine::moveCamera(const vec3 direction) -> void { setCameraPosition(m_CameraEye + direction); }
DEF Engine::getCameraLookDirection() const -> vec3 { return normalize(m_CameraCenter - m_CameraEye); }
DEF Engine::moveCameraForward(const float amount) -> void { moveCamera(getCameraLookDirection() * amount); }

DEF Engine::moveCameraRight(const float amount) -> void {
    const vec3 rightDirection = normalize(cross(getCameraLookDirection(), m_CameraUp));
    const vec3 movement = rightDirection * amount;
    m_CameraEye += movement;
    m_CameraCenter += movement;
}

DEF Engine::lookAround(const float yawOffset, const float pitchOffset) -> void {
    // Calculate the current look direction
    vec3 lookDirection = getCameraLookDirection();

    // Calculate the right direction (for pitch calculation)
    const vec3 rightDirection = normalize(cross(lookDirection, m_CameraUp));

    // Apply yaw rotation (around the up vector)
    // Rotate the look direction around the up vector for left/right movement
    const mat4 yawRotation = rotate(mat4(1.0f), glm::radians(yawOffset), m_CameraUp);
    lookDirection = vec3(yawRotation * vec4(lookDirection, 0.0f));

    // Apply pitch rotation (around the right direction)
    // Rotate the look direction around the right vector for up/down movement
    const mat4 pitchRotation = rotate(mat4(1.0f), glm::radians(pitchOffset), rightDirection);
    lookDirection = vec3(pitchRotation * vec4(lookDirection, 0.0f));

    const float currentPitch = asin(lookDirection.y);
    if (currentPitch > Settings::CAMERA_MAX_PITCH) {
        lookDirection.y = sin(Settings::CAMERA_MAX_PITCH);
    } else if (currentPitch < -Settings::CAMERA_MAX_PITCH) {
        lookDirection.y = -sin(Settings::CAMERA_MAX_PITCH);
    }

    // Update the camera center based on the new look-direction
    m_CameraCenter = m_CameraEye + normalize(lookDirection);
}

DEF Engine::updatePushConstants() -> void {
    m_PushConstants.cameraCenter = m_CameraCenter;
    m_PushConstants.cameraEye = m_CameraEye;
    m_PushConstants.cameraUp = m_CameraUp;
    m_PushConstants.stage = m_Stage;

    const std::chrono::time_point currentTime = std::chrono::high_resolution_clock::now();
    const float deltaTime = std::chrono::duration<float>(currentTime - m_StartTime).count();

    m_PushConstants.time = deltaTime;
}

DEF Engine::update(const float frameTime) const -> void {
    for (auto &model : m_Models) {
        model->update(frameTime);
    }
}