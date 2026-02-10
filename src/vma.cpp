#include <stdexcept>

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_hpp_macros.hpp>

#include "vma.hpp"

VulkanAllocator::VulkanAllocator() {}

VulkanAllocator::VulkanAllocator(
	vk::Instance const inst,
	vk::PhysicalDevice const pdev,
	vk::Device const dev
) {
	auto dispatch = VULKAN_HPP_DEFAULT_DISPATCHER;
	auto vma_vk_funcs = VmaVulkanFunctions{};
	vma_vk_funcs.vkGetInstanceProcAddr = dispatch.vkGetInstanceProcAddr;
	vma_vk_funcs.vkGetDeviceProcAddr = dispatch.vkGetDeviceProcAddr;

	auto allocator_info = VmaAllocatorCreateInfo{};
  allocator_info.physicalDevice = pdev;
  allocator_info.device = dev;
  allocator_info.pVulkanFunctions = &vma_vk_funcs;
  allocator_info.instance = inst;

	auto res = vmaCreateAllocator(&allocator_info, &this->inner);
	if (res != VK_SUCCESS) {
		throw std::runtime_error("failed to initialize VMA");
	}
}

VulkanAllocator::~VulkanAllocator() {
	vmaDestroyAllocator(this->inner);
}
