#include <stdio.h>
#include <stdlib.h>
#include <vulkan/vulkan_core.h>
#include "vulkan/vulkan.h"

// -------------------------
// --- vh --- Vulkan Helpers
// -------------------------

#define vhCheckSuccess(result, error_string) do {if (result != VK_SUCCESS) {fprintf(stderr, "[ERR Line %i in '%s']: %s\n", __LINE__, __FILE__, error_string); exit(1);}} while(0)

typedef struct {
    VkInstance instance;

} vhContext;

VkInstance vhCreateInstance() {

}
