#pragma once

#include <array>
#include <optional>
#include <vector>

#include <glm/ext/vector_int2.hpp>
#include <SDL_video.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "arena.hpp"
#include "scoped.hpp"
#include "sugar.hpp"

struct DrawCommand {
	// TODO
};

struct FramePacket {
	flt dt;
	std::span<DrawCommand> commands;
};

struct FrameContext {
	Arena arena;
	FramePacket* packet = nullptr;
	FrameContext() : arena(1024 * 1024) {}
};

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
	u32 qu_fam_idx;
};

struct RenderTarget {
	vk::Image img;
	vk::ImageView img_view;
	vk::Extent2D extent;
};

// it is the user's responsibility to recreate the swapchain upon receiving false/None
class Swapchain {

public:
	explicit Swapchain(
		GPU gpu,
		vk::Device dev,
		vk::SurfaceKHR surf,
		glm::ivec2 sz
	);

	bool recreate(glm::ivec2 sz);
	bool present(vk::Queue qu);
	std::optional<RenderTarget> acq_next_img(vk::Semaphore to_sig);
	vk::ImageMemoryBarrier2 base_barrier() const;

	glm::ivec2 get_size() const;
	vk::Semaphore get_semaphore() const;

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
	std::optional<u32> img_idx{};

};

class Renderer {

public:
	explicit Renderer();
	void draw(FramePacket* packet);

private:
	struct RenderSync {
		vk::CommandBuffer cmd;
		vk::UniqueSemaphore available;
		vk::UniqueSemaphore finished;
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
	u64 img_idx{0};

	// when destroying renderer, this waits until dev idle before destroying preceding fields
	ScopedWaiter waiter;

};
