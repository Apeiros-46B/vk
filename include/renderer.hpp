#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/ext/vector_int2.hpp>
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
	void create_semaphores();

	GPU gpu;
	vk::Device dev;
	vk::SurfaceKHR surf;
	vk::SwapchainCreateInfoKHR cinfo;
	vk::UniqueSwapchainKHR inner;
	std::vector<vk::Image> imgs{};
	std::vector<vk::UniqueImageView> img_views{};
	std::vector<vk::UniqueSemaphore> semaphores{};

};

class Renderer {

public:
	explicit Renderer();

private:
	struct RenderSync {
		vk::CommandBuffer cmd;
		vk::UniqueSemaphore draw;
		vk::UniqueFence drawn;
	};

	Window win{};
	vk::UniqueInstance inst;
	vk::UniqueSurfaceKHR surf;

	GPU gpu;
	vk::UniqueDevice dev;
	vk::Queue qu;

	std::optional<Swapchain> swapchain{};

	vk::UniqueCommandPool render_cmd_pool;
	std::array<RenderSync, 2> render_sync{};
	size_t cur_frame{0};

	// when destroying renderer, this waits until dev idle before destroying preceding fields
	ScopedWaiter waiter;

};
