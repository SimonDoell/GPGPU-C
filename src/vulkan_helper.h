#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan_core.h>
#include "vulkan/vulkan.h"

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define PATH_SEPARATOR '\\'
#else
    #include <unistd.h>
    #include <limits.h>
    #include <libgen.h>
    #define PATH_SEPARATOR '/'
    #ifndef PATH_MAX
        #define PATH_MAX 4096
    #endif
#endif

// -------------------------
// --- vh --- Vulkan Helpers
// -------------------------

#define vhCheckSuccess(result, error_string) do {if (result != VK_SUCCESS) {fprintf(stderr, "[ERR Line %i in '%s']: %s\n", __LINE__, __FILE__, error_string); exit(1);}} while(0)

// Read entire file into memory
// Returns allocated buffer (caller must free) or NULL on error
// Sets out_size to the number of bytes read
uint8_t* vkc_read_file(const char* filepath, size_t* out_size);

// Read file as uint32_t array (for SPIR-V)
// Returns allocated buffer (caller must free) or NULL on error
// Sets out_count to the number of uint32_t elements
uint32_t* vkc_read_spirv(const char* filepath, size_t* out_count);

// Check if file exists
bool vkc_file_exists(const char* filepath);

// Get executable directory path
// Returns allocated string (caller must free) or NULL on error
char* vkc_get_exe_dir(void);

// Build path relative to executable
// Returns allocated string (caller must free) or NULL on error
char* vkc_path_relative_to_exe(const char* relative_path);


// ============================================================================
// Configuration
// ============================================================================

typedef struct VkcConfig {
    const char* app_name;
    uint32_t app_version;
    bool enable_validation;
    bool prefer_discrete_gpu;
    uint32_t required_queue_flags;  // e.g., VK_QUEUE_COMPUTE_BIT
} VkcConfig;

// Default configuration
VkcConfig vkc_config_default(void);

// ============================================================================
// Context - Main Vulkan state
// ============================================================================

typedef struct VkcContext {
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    VkQueue compute_queue;
    uint32_t compute_queue_family;

    VkPhysicalDeviceProperties device_props;
    VkPhysicalDeviceMemoryProperties memory_props;

    VkDebugUtilsMessengerEXT debug_messenger;
    bool validation_enabled;
} VkcContext;

VkResult vkc_init(VkcContext* ctx, const VkcConfig* config);

void vkc_cleanup(VkcContext* ctx);

// ============================================================================
// Helper functions
// ============================================================================

VkResult vkc_create_command_pool(VkcContext* ctx, VkCommandPool* pool);

VkResult vkc_create_command_buffer(VkcContext* ctx, VkCommandPool pool,
                                VkCommandBuffer* cmd);

VkResult vkc_submit_and_wait(VkcContext* ctx, VkCommandBuffer cmd);

VkResult vkc_create_fence(VkcContext* ctx, VkFence* fence, bool signaled);

// ============================================================================
// Buffer helpers
// ============================================================================

typedef struct VkcBuffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void* mapped;  // Non-null if persistently mapped
} VkcBuffer;

VkResult vkc_create_buffer(VkcContext* ctx, VkDeviceSize size,
                        VkBufferUsageFlags usage,
                        VkMemoryPropertyFlags mem_props,
                        VkcBuffer* buffer);

void vkc_destroy_buffer(VkcContext* ctx, VkcBuffer* buffer);

VkResult vkc_map_buffer(VkcContext* ctx, VkcBuffer* buffer, void** data);

void vkc_unmap_buffer(VkcContext* ctx, VkcBuffer* buffer);

VkResult vkc_upload_buffer(VkcContext* ctx, VkcBuffer* buffer,
                        const void* data, VkDeviceSize size);

VkResult vkc_download_buffer(VkcContext* ctx, VkcBuffer* buffer,
                            void* data, VkDeviceSize size);

// ============================================================================
// Shader helpers
// ============================================================================

// Load SPIR-V shader from file
VkResult vkc_load_shader(VkcContext* ctx, const char* filepath,
                        VkShaderModule* shader);

VkResult vkc_create_compute_pipeline(VkcContext* ctx,
                                     VkShaderModule shader,
                                     VkPipelineLayout layout,
                                     VkPipeline* pipeline);


// ============================================================================
// Error handling macros
// ============================================================================

const char* vkc_result_string(VkResult result);

// The internal variable is deliberately named vkc__result: a plain `result`
// would shadow a caller variable of that name and capture `&result` arguments.
#define VK_CHECK(call) \
    do { \
        VkResult vkc__result = (call); \
        if (vkc__result != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error at %s:%d: %s returned %s\n", \
                    __FILE__, __LINE__, #call, vkc_result_string(vkc__result)); \
            exit(EXIT_FAILURE); \
        } \
    } while(0)

#define VK_CHECK_RETURN(call) \
    do { \
        VkResult vkc__result = (call); \
        if (vkc__result != VK_SUCCESS) { \
            fprintf(stderr, "Vulkan error at %s:%d: %s returned %s\n", \
                    __FILE__, __LINE__, #call, vkc_result_string(vkc__result)); \
            return vkc__result; \
        } \
    } while(0)

// ============================================================================
// Logging macros
// ============================================================================

#define VKC_LOG_INFO(...) \
    do { fprintf(stdout, "[INFO] "); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)

#define VKC_LOG_WARN(...) \
    do { fprintf(stderr, "[WARN] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

#define VKC_LOG_ERROR(...) \
    do { fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } while(0)

#ifdef NDEBUG
    #define VKC_LOG_DEBUG(...) ((void)0)
#else
    #define VKC_LOG_DEBUG(...) \
        do { fprintf(stdout, "[DEBUG] "); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n"); } while(0)
#endif

// ============================================================================
// Utility functions
// ============================================================================

uint32_t vkc_find_memory_type(VkPhysicalDevice physical_device,
                              uint32_t type_filter,
                              VkMemoryPropertyFlags properties);

void vkc_print_device_info(VkPhysicalDevice device);

int32_t vkc_find_compute_queue_family(VkPhysicalDevice device);

// ============================================================================
// Math helpers for compute
// ============================================================================

// Calculate workgroup count for 1D dispatch
static inline uint32_t vkc_div_ceil(uint32_t x, uint32_t y) {
    return (x + y - 1) / y;
}

// Align value up to alignment
static inline uint32_t vkc_align_up(uint32_t value, uint32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// ============================================================================
// Cross-platform timer
// ============================================================================

double vkc_get_time_ms(void);
