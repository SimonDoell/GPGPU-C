#include "vulkan_helper.h"

// ============================================================================
// Read file
// ============================================================================

uint8_t* vkc_read_file(const char* filepath, size_t* out_size) {
    FILE* file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (file_size <= 0) {
        fclose(file);
        return NULL;
    }

    uint8_t* buffer = (uint8_t*)malloc((size_t)file_size);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    size_t read_size = fread(buffer, 1, (size_t)file_size, file);
    fclose(file);

    if (read_size != (size_t)file_size) {
        free(buffer);
        return NULL;
    }

    if (out_size) {
        *out_size = (size_t)file_size;
    }

    return buffer;
}

// ============================================================================
// Read SPIR-V file
// ============================================================================

uint32_t* vkc_read_spirv(const char* filepath, size_t* out_count) {
    size_t byte_size;
    uint8_t* bytes = vkc_read_file(filepath, &byte_size);

    if (!bytes) {
        return NULL;
    }

    // SPIR-V must be 4-byte aligned
    if (byte_size % 4 != 0) {
        fprintf(stderr, "Invalid SPIR-V file (not aligned): %s\n", filepath);
        free(bytes);
        return NULL;
    }

    // Check SPIR-V magic number
    uint32_t magic = *(uint32_t*)bytes;
    if (magic != 0x07230203) {
        fprintf(stderr, "Invalid SPIR-V magic number in: %s\n", filepath);
        free(bytes);
        return NULL;
    }

    if (out_count) {
        *out_count = byte_size / 4;
    }

    return (uint32_t*)bytes;
}

// ============================================================================
// File exists check
// ============================================================================

bool vkc_file_exists(const char* filepath) {
#ifdef _WIN32
    DWORD attrib = GetFileAttributesA(filepath);
    return (attrib != INVALID_FILE_ATTRIBUTES &&
            !(attrib & FILE_ATTRIBUTE_DIRECTORY));
#else
    return access(filepath, F_OK) == 0;
#endif
}

// ============================================================================
// Get executable directory
// ============================================================================

char* vkc_get_exe_dir(void) {
    char* path = NULL;

#ifdef _WIN32
    char buffer[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        // Find last backslash
        char* last_sep = strrchr(buffer, '\\');
        if (last_sep) {
            *last_sep = '\0';
            path = strdup(buffer);
        }
    }
#elif defined(__APPLE__)
    char buffer[PATH_MAX];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0) {
        char* dir = dirname(buffer);
        path = strdup(dir);
    }
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        char* dir = dirname(buffer);
        path = strdup(dir);
    }
#endif

    return path;
}

// ============================================================================
// Path relative to executable
// ============================================================================

char* vkc_path_relative_to_exe(const char* relative_path) {
    char* exe_dir = vkc_get_exe_dir();
    if (!exe_dir) {
        return strdup(relative_path);
    }

    size_t exe_len = strlen(exe_dir);
    size_t rel_len = strlen(relative_path);
    size_t total_len = exe_len + 1 + rel_len + 1;

    char* full_path = (char*)malloc(total_len);
    if (!full_path) {
        free(exe_dir);
        return NULL;
    }

    snprintf(full_path, total_len, "%s%c%s", exe_dir, PATH_SEPARATOR, relative_path);
    free(exe_dir);

    return full_path;
}




static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* callback_data,
    void* user_data)
{
    (void)type;
    (void)user_data;

    const char* severity_str = "UNKNOWN";
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        severity_str = "ERROR";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        severity_str = "WARNING";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        severity_str = "INFO";
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        severity_str = "VERBOSE";
    }

    fprintf(stderr, "[Vulkan %s] %s\n", severity_str, callback_data->pMessage);

    return VK_FALSE;
}

// ============================================================================
// Configuration
// ============================================================================

VkcConfig vkc_config_default(void) {
    VkcConfig config = {
        .app_name = "VKCompute",
        .app_version = VK_MAKE_VERSION(1, 0, 0),
        .enable_validation = true,
        .prefer_discrete_gpu = true,
        .required_queue_flags = VK_QUEUE_COMPUTE_BIT
    };
    return config;
}

// ============================================================================
// Instance creation
// ============================================================================

static VkResult create_instance(VkcContext* ctx, const VkcConfig* config) {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = config->app_name,
        .applicationVersion = config->app_version,
        .pEngineName = "VKCompute",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2
    };

    const char* extensions[8];
    uint32_t extension_count = 0;

#ifdef __APPLE__
    // macOS requires portability enumeration for MoltenVK
    extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
#endif

    if (config->enable_validation) {
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    }

    // Validation layers
    const char* layers[] = {
        "VK_LAYER_KHRONOS_validation"
    };
    uint32_t layer_count = 0;

    if (config->enable_validation) {
        uint32_t available_count;
        vkEnumerateInstanceLayerProperties(&available_count, NULL);

        VkLayerProperties* available = malloc(sizeof(VkLayerProperties) * available_count);
        vkEnumerateInstanceLayerProperties(&available_count, available);

        bool found = false;
        for (uint32_t i = 0; i < available_count; i++) {
            if (strcmp(available[i].layerName, layers[0]) == 0) {
                found = true;
                break;
            }
        }
        free(available);

        if (found) {
            layer_count = 1;
            ctx->validation_enabled = true;
            VKC_LOG_INFO("Validation layers enabled");
        } else {
            VKC_LOG_WARN("Validation layer not found, continuing without validation");
        }
    }

    VkInstanceCreateFlags flags = 0;
#ifdef __APPLE__
    flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    VkInstanceCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .flags = flags,
        .enabledExtensionCount = extension_count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount = layer_count,
        .ppEnabledLayerNames = layers
    };

    VK_CHECK_RETURN(vkCreateInstance(&create_info, NULL, &ctx->instance));

    // Create debug messenger
    if (ctx->validation_enabled) {
        VkDebugUtilsMessengerCreateInfoEXT debug_info = {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = debug_callback
        };

        PFN_vkCreateDebugUtilsMessengerEXT func =
            (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                ctx->instance, "vkCreateDebugUtilsMessengerEXT");

        if (func) {
            func(ctx->instance, &debug_info, NULL, &ctx->debug_messenger);
        }
    }

    return VK_SUCCESS;
}

// ============================================================================
// Physical device selection
// ============================================================================

static VkResult select_physical_device(VkcContext* ctx, const VkcConfig* config) {
    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, NULL);

    if (device_count == 0) {
        VKC_LOG_ERROR("No Vulkan-capable GPU found!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkPhysicalDevice* devices = malloc(sizeof(VkPhysicalDevice) * device_count);
    vkEnumeratePhysicalDevices(ctx->instance, &device_count, devices);

    // Score devices
    int best_score = -1;
    VkPhysicalDevice best_device = VK_NULL_HANDLE;

    for (uint32_t i = 0; i < device_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        int32_t compute_family = vkc_find_compute_queue_family(devices[i]);
        if (compute_family < 0) {
            continue;
        }

        int score = 0;

        // Prefer discrete GPUs
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }

        // Higher API version is better
        score += VK_API_VERSION_MINOR(props.apiVersion) * 10;

        if (score > best_score || !config->prefer_discrete_gpu) {
            best_score = score;
            best_device = devices[i];
        }
    }

    free(devices);

    if (best_device == VK_NULL_HANDLE) {
        VKC_LOG_ERROR("No suitable GPU found!");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    ctx->physical_device = best_device;
    vkGetPhysicalDeviceProperties(ctx->physical_device, &ctx->device_props);
    vkGetPhysicalDeviceMemoryProperties(ctx->physical_device, &ctx->memory_props);

    VKC_LOG_INFO("Selected GPU: %s", ctx->device_props.deviceName);

    return VK_SUCCESS;
}

// ============================================================================
// Logical device creation
// ============================================================================

static VkResult create_device(VkcContext* ctx) {
    ctx->compute_queue_family = (uint32_t)vkc_find_compute_queue_family(ctx->physical_device);

    float queue_priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = ctx->compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority
    };

    VkPhysicalDeviceFeatures features = {0};

    // Device extensions
    const char* device_extensions[8];
    uint32_t device_extension_count = 0;

#ifdef __APPLE__
    // macOS requires portability subset for MoltenVK
    device_extensions[device_extension_count++] = "VK_KHR_portability_subset";
#endif

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
        .pEnabledFeatures = &features,
        .enabledExtensionCount = device_extension_count,
        .ppEnabledExtensionNames = device_extensions
    };

    VK_CHECK_RETURN(vkCreateDevice(ctx->physical_device, &device_info, NULL, &ctx->device));

    vkGetDeviceQueue(ctx->device, ctx->compute_queue_family, 0, &ctx->compute_queue);

    return VK_SUCCESS;
}

// ============================================================================
// Public API
// ============================================================================

VkResult vkc_init(VkcContext* ctx, const VkcConfig* config) {
    memset(ctx, 0, sizeof(VkcContext));

    VkcConfig default_config = vkc_config_default();
    if (!config) {
        config = &default_config;
    }

    VK_CHECK_RETURN(create_instance(ctx, config));
    VK_CHECK_RETURN(select_physical_device(ctx, config));
    VK_CHECK_RETURN(create_device(ctx));

    VKC_LOG_INFO("Vulkan initialized successfully");

    return VK_SUCCESS;
}

void vkc_cleanup(VkcContext* ctx) {
    if (ctx->device) {
        vkDeviceWaitIdle(ctx->device);
        vkDestroyDevice(ctx->device, NULL);
    }

    if (ctx->debug_messenger && ctx->instance) {
        PFN_vkDestroyDebugUtilsMessengerEXT func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                ctx->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(ctx->instance, ctx->debug_messenger, NULL);
        }
    }

    if (ctx->instance) {
        vkDestroyInstance(ctx->instance, NULL);
    }

    memset(ctx, 0, sizeof(VkcContext));
    VKC_LOG_INFO("Vulkan cleaned up");
}

VkResult vkc_create_command_pool(VkcContext* ctx, VkCommandPool* pool) {
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = ctx->compute_queue_family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    return vkCreateCommandPool(ctx->device, &pool_info, NULL, pool);
}

VkResult vkc_create_command_buffer(VkcContext* ctx, VkCommandPool pool,
                                VkCommandBuffer* cmd) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };

    return vkAllocateCommandBuffers(ctx->device, &alloc_info, cmd);
}

VkResult vkc_submit_and_wait(VkcContext* ctx, VkCommandBuffer cmd) {
    VkFence fence;
    VK_CHECK_RETURN(vkc_create_fence(ctx, &fence, false));

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd
    };

    VK_CHECK_RETURN(vkQueueSubmit(ctx->compute_queue, 1, &submit_info, fence));
    VK_CHECK_RETURN(vkWaitForFences(ctx->device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(ctx->device, fence, NULL);

    return VK_SUCCESS;
}

VkResult vkc_create_fence(VkcContext* ctx, VkFence* fence, bool signaled) {
    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0
    };

    return vkCreateFence(ctx->device, &fence_info, NULL, fence);
}

// ============================================================================
// Buffer helpers
// ============================================================================

VkResult vkc_create_buffer(VkcContext* ctx, VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags mem_props,
                        VkcBuffer* buffer) {
    memset(buffer, 0, sizeof(VkcBuffer));
    buffer->size = size;

    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VK_CHECK_RETURN(vkCreateBuffer(ctx->device, &buffer_info, NULL, &buffer->buffer));

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(ctx->device, buffer->buffer, &mem_req);

    uint32_t mem_type = vkc_find_memory_type(ctx->physical_device,
                                            mem_req.memoryTypeBits,
                                            mem_props);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = mem_type
    };

    VK_CHECK_RETURN(vkAllocateMemory(ctx->device, &alloc_info, NULL, &buffer->memory));
    VK_CHECK_RETURN(vkBindBufferMemory(ctx->device, buffer->buffer, buffer->memory, 0));

    return VK_SUCCESS;
}

void vkc_destroy_buffer(VkcContext* ctx, VkcBuffer* buffer) {
    if (buffer->mapped) {
        vkc_unmap_buffer(ctx, buffer);
    }
    if (buffer->buffer) {
        vkDestroyBuffer(ctx->device, buffer->buffer, NULL);
    }
    if (buffer->memory) {
        vkFreeMemory(ctx->device, buffer->memory, NULL);
    }
    memset(buffer, 0, sizeof(VkcBuffer));
}

VkResult vkc_map_buffer(VkcContext* ctx, VkcBuffer* buffer, void** data) {
    VkResult result = vkMapMemory(ctx->device, buffer->memory, 0, buffer->size, 0, data);
    if (result == VK_SUCCESS) {
        buffer->mapped = *data;
    }
    return result;
}

void vkc_unmap_buffer(VkcContext* ctx, VkcBuffer* buffer) {
    if (buffer->mapped) {
        vkUnmapMemory(ctx->device, buffer->memory);
        buffer->mapped = NULL;
    }
}

VkResult vkc_upload_buffer(VkcContext* ctx, VkcBuffer* buffer,
                        const void* data, VkDeviceSize size) {
    void* mapped;
    VK_CHECK_RETURN(vkc_map_buffer(ctx, buffer, &mapped));
    memcpy(mapped, data, (size_t)size);
    vkc_unmap_buffer(ctx, buffer);
    return VK_SUCCESS;
}

VkResult vkc_download_buffer(VkcContext* ctx, VkcBuffer* buffer,
                            void* data, VkDeviceSize size) {
    void* mapped;
    VK_CHECK_RETURN(vkc_map_buffer(ctx, buffer, &mapped));
    memcpy(data, mapped, (size_t)size);
    vkc_unmap_buffer(ctx, buffer);
    return VK_SUCCESS;
}

// ============================================================================
// Shader helpers
// ============================================================================

VkResult vkc_load_shader(VkcContext* ctx, const char* filepath,
                        VkShaderModule* shader) {
    size_t code_size;
    uint32_t* code = vkc_read_spirv(filepath, &code_size);

    if (!code) {
        VKC_LOG_ERROR("Failed to load shader: %s", filepath);
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkShaderModuleCreateInfo create_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size * sizeof(uint32_t),
        .pCode = code
    };

    VkResult result = vkCreateShaderModule(ctx->device, &create_info, NULL, shader);
    free(code);

    return result;
}

VkResult vkc_create_compute_pipeline(VkcContext* ctx,
                                    VkShaderModule shader,
                                    VkPipelineLayout layout,
                                    VkPipeline* pipeline) {
    VkPipelineShaderStageCreateInfo stage_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = shader,
        .pName = "main"
    };

    VkComputePipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage_info,
        .layout = layout
    };

    return vkCreateComputePipelines(ctx->device, VK_NULL_HANDLE, 1,
                                    &pipeline_info, NULL, pipeline);
}



#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #ifndef _POSIX_C_SOURCE
    #define _POSIX_C_SOURCE 199309L
    #endif
    #include <time.h>
#endif

// ============================================================================
// VkResult to string conversion
// ============================================================================

const char* vkc_result_string(VkResult result) {
    switch (result) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        case VK_ERROR_INVALID_EXTERNAL_HANDLE: return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
        case VK_ERROR_FRAGMENTATION: return "VK_ERROR_FRAGMENTATION";
        case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS: return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
        case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
        default: return "UNKNOWN_VK_RESULT";
    }
}

// ============================================================================
// Memory type finding
// ============================================================================

uint32_t vkc_find_memory_type(VkPhysicalDevice physical_device,
                            uint32_t type_filter,
                            VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    VKC_LOG_ERROR("Failed to find suitable memory type!");
    return 0;
}

// ============================================================================
// Device info
// ============================================================================

void vkc_print_device_info(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(device, &props);

    const char* device_type = "Unknown";
    switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: device_type = "Integrated GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: device_type = "Discrete GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: device_type = "Virtual GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU: device_type = "CPU"; break;
        default: break;
    }

    VKC_LOG_INFO("=== GPU Info ===");
    VKC_LOG_INFO("  Name: %s", props.deviceName);
    VKC_LOG_INFO("  Type: %s", device_type);
    VKC_LOG_INFO("  API Version: %d.%d.%d",
                VK_API_VERSION_MAJOR(props.apiVersion),
                VK_API_VERSION_MINOR(props.apiVersion),
                VK_API_VERSION_PATCH(props.apiVersion));
    VKC_LOG_INFO("  Driver Version: %d.%d.%d",
                VK_API_VERSION_MAJOR(props.driverVersion),
                VK_API_VERSION_MINOR(props.driverVersion),
                VK_API_VERSION_PATCH(props.driverVersion));

    // Compute limits
    VKC_LOG_INFO("=== Compute Limits ===");
    VKC_LOG_INFO("  Max Compute Workgroup Count: [%u, %u, %u]",
                props.limits.maxComputeWorkGroupCount[0],
                props.limits.maxComputeWorkGroupCount[1],
                props.limits.maxComputeWorkGroupCount[2]);
    VKC_LOG_INFO("  Max Compute Workgroup Size: [%u, %u, %u]",
                props.limits.maxComputeWorkGroupSize[0],
                props.limits.maxComputeWorkGroupSize[1],
                props.limits.maxComputeWorkGroupSize[2]);
    VKC_LOG_INFO("  Max Compute Workgroup Invocations: %u",
                props.limits.maxComputeWorkGroupInvocations);
    VKC_LOG_INFO("  Max Compute Shared Memory: %u bytes",
                props.limits.maxComputeSharedMemorySize);
}

// ============================================================================
// Queue family finding
// ============================================================================

int32_t vkc_find_compute_queue_family(VkPhysicalDevice device) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, NULL);

    VkQueueFamilyProperties* queue_families =
        malloc(sizeof(VkQueueFamilyProperties) * queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families);

    int32_t compute_family = -1;
    int32_t dedicated_compute = -1;

    for (uint32_t i = 0; i < queue_family_count; i++) {
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            compute_family = (int32_t)i;

            // Prefer dedicated compute queue (no graphics)
            if (!(queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                dedicated_compute = (int32_t)i;
            }
        }
    }

    free(queue_families);

    return (dedicated_compute >= 0) ? dedicated_compute : compute_family;
}

// ============================================================================
// Cross-platform high-resolution timer
// ============================================================================

double vkc_get_time_ms(void) {
#ifdef _WIN32
    static LARGE_INTEGER frequency = {0};
    static int initialized = 0;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1000.0 / (double)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
#endif
}
