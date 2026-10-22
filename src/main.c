#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <vulkan/vulkan_core.h>
#include "vulkan/vulkan.h"
#include "vk_init.h"
#include "vk_utils.h"
#include "file_utils.h"
#include "vulkan_helper.h"

#ifdef NDEBUG
    const bool debug = false;
#else
    const bool debug = true;
#endif

// uint32_t find_memory_type(uint32_t type_filter,
//                           VkMemoryPropertyFlags properties) {
//     VkPhysicalDeviceMemoryProperties mem_props;
//     vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
//
//     for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
//         if ((type_filter & (1 << i)) &&
//             (mem_props.memoryTypes[i].propertyFlags & properties)
//                 == properties) {
//             return i;
//         }
//     }
//     return UINT32_MAX;  // Not found
// }


// // Map memory
// void* mapped;
// vkMapMemory(device, memory, 0, buffer_size, 0, &mapped);
//
// // Copy data
// memcpy(mapped, source_data, buffer_size);
//
// // Unmap (optional if you'll write again)
// vkUnmapMemory(device, memory);



VkInstance create_instance(const char* applicationName) {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationName,
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "No Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_2,
    };

    VkInstanceCreateInfo instance_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };

    VkInstance instance;
    VkResult result = vkCreateInstance(&instance_info, NULL, &instance);

    vhCheckSuccess(result, "Failed to create Vulkan instance!");

    return instance;
}



int main(void) {
    VkInstance instance = create_instance("Hello compute");
    printf("Vulkan instance created!\n");

    uint32_t device_count = 0;
    vkEnumeratePhysicalDevices(instance, &device_count, NULL);

    if (!device_count) {
        fprintf(stderr, "No GPU's with Vulkan support found!\n");
        return 1;
    }

    VkPhysicalDevice devices[device_count];
    vkEnumeratePhysicalDevices(instance, &device_count, devices);

    printf("Searching for a discrete GPU...\n\n");
    uint32_t physical_device_index = UINT32_MAX;

    for (uint32_t i = 0; i < device_count; ++i) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(devices[i], &props);

        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU && physical_device_index == UINT32_MAX) {
            physical_device_index = i;
        }

        printf("GPU %u:\t%s\n", i, props.deviceName);
        printf("\tType: %s\n", (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? "Discrete GPU" : (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? "Integrated GPU" : "Other")));
        printf("\tAPI Version: %u.%u.%u\n\n", VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
    }

    if (physical_device_index == UINT32_MAX) {
        fprintf(stderr, "No discrete GPU found!\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    VkPhysicalDevice physical_device = devices[physical_device_index];

    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, NULL);

    VkQueueFamilyProperties queue_families[queue_family_count];
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, queue_families);

    uint32_t compute_queue_family = UINT32_MAX;

    for (uint32_t i = 0; i < queue_family_count; ++i) {
        if (queue_families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            printf("\tQueue family %u: supports COMPUTE (%u queue(s))\n", i, queue_families[i].queueCount);
            if (compute_queue_family == UINT32_MAX)
                compute_queue_family = i;
        }
    }

    if (compute_queue_family == UINT32_MAX) {
        fprintf(stderr, "No COMPUTE queue family found!\n");
        vkDestroyInstance(instance, NULL);
        return 1;
    }

    float queue_priority = 1.0f;

    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &queue_priority,
    };

    VkDeviceCreateInfo device_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };

    VkDevice device;
    VkResult result = vkCreateDevice(physical_device, &device_info, NULL, &device);

    vhCheckSuccess(result, "Failed to create device!");

    VkQueue compute_queue;
    vkGetDeviceQueue(device, compute_queue_family, 0, &compute_queue);


    VkBufferCreateInfo buffer_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = 1024 * sizeof(float),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkBuffer buffer;
    result = vkCreateBuffer(device, &buffer_info, NULL, &buffer);
    vhCheckSuccess(result, "Failed to create buffer!");

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, buffer, &mem_reqs);

    uint32_t memory_type = vkc_find_memory_type(physical_device, mem_reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_reqs.size,
        .memoryTypeIndex = memory_type,
    };

    VkDeviceMemory memory;
    result = vkAllocateMemory(device, &alloc_info, NULL, &memory);
    vhCheckSuccess(result, "Failed to allocate memory!");

    result = vkBindBufferMemory(device, buffer, memory, 0);
    vhCheckSuccess(result, "Failed to bind buffer memory!");

    vkDeviceWaitIdle(device);

    vkDestroyBuffer(device, buffer, NULL);
    vkFreeMemory(device, memory, NULL);

    vkDestroyDevice(device, NULL);
    vkDestroyInstance(instance, NULL);
    return 0;
}
