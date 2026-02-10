#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

struct VulkanAllocator {
	VmaAllocator inner;

	VulkanAllocator();
	VulkanAllocator(
		vk::Instance const inst,
		vk::PhysicalDevice const pdev,
		vk::Device const dev
	);
	~VulkanAllocator();
};
