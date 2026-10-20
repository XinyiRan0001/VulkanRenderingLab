#include "LogicalDevice.h"

#include <set>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <cstring>

LogicalDevice::LogicalDevice(const PhysicalDevice& physicalDevice, bool rayQuery)
    : m_PhysicalDevice(physicalDevice),m_RayQuery(rayQuery)
{
    CreateLogicalDevice();

    std::cout << "LogicalDevice created successfully." << std::endl;
}

LogicalDevice::~LogicalDevice()
{
    if (m_Device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }
}

VkDevice LogicalDevice::GetHandle() const
{
    return m_Device;
}

VkQueue LogicalDevice::GetGraphicsQueue() const
{
    return m_GraphicsQueue;
}

VkQueue LogicalDevice::GetPresentQueue() const 
{ 
    return m_PresentQueue; 
}

void LogicalDevice::CreateLogicalDevice()
{
    QueueFamilyIndices indices = m_PhysicalDevice.FindQueueFamilies();

    if (!indices.IsComplete())
    {
        throw std::runtime_error("Selected GPU does not support required queue families.");
    }

    float queuePriority = 1.0f;

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.graphicsFamily.value();
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE;

    std::vector<const char*> deviceExtensions =
    {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    VkPhysicalDeviceBufferDeviceAddressFeatures address{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR query{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    uint32_t extensionCount=0;vkEnumerateDeviceExtensionProperties(m_PhysicalDevice.GetHandle(),nullptr,&extensionCount,nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);vkEnumerateDeviceExtensionProperties(m_PhysicalDevice.GetHandle(),nullptr,&extensionCount,available.data());
    bool extensions=true;
    for(const char* name:{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME}){
        bool found=false;for(const auto& e:available)found|=std::strcmp(e.extensionName,name)==0;
        extensions &= found;
        std::cout<<"[Ray Query Capability] "<<name<<" "<<(found?"YES":"NO")<<'\n';
        if(m_RayQuery)deviceExtensions.push_back(name);
    }
    if(extensions && m_PhysicalDevice.GetProperties().apiVersion>=VK_API_VERSION_1_2){
        VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&address;address.pNext=&acceleration;acceleration.pNext=&query;
        vkGetPhysicalDeviceFeatures2(m_PhysicalDevice.GetHandle(),&features);
    }
    const bool supported=extensions&&address.bufferDeviceAddress&&acceleration.accelerationStructure&&query.rayQuery;
    std::cout<<"[Ray Query Capability] bufferDeviceAddress="<<address.bufferDeviceAddress<<" accelerationStructure="<<acceleration.accelerationStructure<<" rayQuery="<<query.rayQuery<<" requested="<<m_RayQuery<<'\n';
    if(m_RayQuery&&!supported)throw std::runtime_error("Ray Query scene requires Vulkan 1.2+, acceleration structures, buffer device addresses and rayQuery; see capability log");
    // Enable only the features actually used, not every optional queried feature.
    address={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};address.bufferDeviceAddress=VK_TRUE;
    acceleration={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};acceleration.accelerationStructure=VK_TRUE;
    query={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};query.rayQuery=VK_TRUE;
    address.pNext=&acceleration;acceleration.pNext=&query;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.pNext=m_RayQuery?&address:nullptr;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult result = vkCreateDevice(
        m_PhysicalDevice.GetHandle(),
        &createInfo,
        nullptr,
        &m_Device
    );

    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device.");
    }

    vkGetDeviceQueue(m_Device, indices.graphicsFamily.value(), 0, &m_GraphicsQueue);
    vkGetDeviceQueue(m_Device, indices.presentFamily.value(), 0, &m_PresentQueue);

    std::cout << "Graphics Queue Family: "
        << indices.graphicsFamily.value()
        << std::endl;

    std::cout << "Present Queue Family: "
        << indices.presentFamily.value()
        << std::endl;

}
