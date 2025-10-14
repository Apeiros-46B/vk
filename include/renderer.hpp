#pragma once

#include <cstdint>
#include <glm/ext/vector_int2.hpp>
#include <vector>

#include <glm/glm.hpp>
#include <SDL_video.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "scoped.hpp"

struct Window {
	SDL_Window* inner;
	glm::ivec2 sz{};
	std::vector<const char*> required_exts{};

	explicit Window();
	~Window();
};

struct GPU {
	vk::PhysicalDevice pdev;
	vk::PhysicalDeviceProperties props;
	vk::PhysicalDeviceFeatures feats;
	uint32_t qu_fam_idx;
};

class Swapchain {

public:
	explicit Swapchain(
		GPU gpu,
		vk::Device dev,
		vk::SurfaceKHR surf,
		glm::ivec2 sz
	);

	bool recreate(glm::ivec2 sz);
	glm::ivec2 get_size() const;

private:
	void populate_imgs();
	void create_img_views();

	GPU gpu;
	vk::Device dev;
	vk::SurfaceKHR surf;
	vk::SwapchainCreateInfoKHR cinfo;
	vk::UniqueSwapchainKHR inner;
	std::vector<vk::Image> imgs{};
	std::vector<vk::UniqueImageView> img_views{};

};

class Renderer {

public:
	explicit Renderer();

private:
	Window win{};
	vk::UniqueInstance inst;
	vk::UniqueSurfaceKHR surf;

	GPU gpu;
	vk::UniqueDevice dev;
	vk::Queue qu;

	std::optional<Swapchain> swapchain{};

	// when destroying renderer, this waits until dev idle before destroying preceding fields
	ScopedWaiter waiter;

};
