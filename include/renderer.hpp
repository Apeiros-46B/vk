#pragma once

#include <array>
#include <glm/ext/vector_uint2.hpp>
#include <optional>
#include <vector>

#include <glm/ext/vector_int2.hpp>
#include <SDL_video.h>
#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "arena.hpp"
#include "sugar.hpp"

struct DrawCommand {
	// TODO
};

struct FramePacket {
	flt t;
	flt dt;
	glm::ivec2 drawable_sz;
	std::span<DrawCommand> commands;
};

struct FrameContext {
	Arena arena;
	FramePacket* pkt = nullptr;
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
	u32 img_idx;
};

// it is the user's responsibility to recreate the swapchain upon receiving false/None
class Swapchain {
	friend class Renderer;

public:
	explicit Swapchain(
		GPU gpu,
		vk::Device dev,
		vk::SurfaceKHR surf,
		glm::ivec2 sz
	);

	bool recreate(glm::ivec2 sz);
	bool present(vk::Queue qu);
	auto acq_next_img(vk::Semaphore to_sig) -> std::optional<RenderTarget>;
	auto base_barrier() const -> vk::ImageMemoryBarrier2;
	auto get_size() const -> glm::ivec2;
	auto get_sem() const -> vk::Semaphore;

private:
	GPU gpu;
	vk::Device dev;
	vk::SurfaceKHR surf;
	vk::SwapchainCreateInfoKHR cinfo;
	vk::UniqueSwapchainKHR inner;
	std::vector<vk::Image> imgs{};
	std::vector<vk::UniqueImageView> img_views{};
	std::vector<vk::UniqueSemaphore> render_sems{}; // signalled when render done
	std::optional<u32> img_idx{};

};

class Renderer {

public:
	explicit Renderer(Window* win);
	~Renderer();

	void draw(FramePacket* pkt);

private:
	struct RenderSync {
		vk::CommandBuffer cmd;
		vk::UniqueSemaphore img_sem; // signalled when img acquired
		vk::UniqueFence drawn;
	};

	auto init_inst(Window* win) -> vkb::Instance;
	void init_devs(vkb::Instance vkb_inst);
	void init_sync();
	void init_pipeline();

	auto acq_render_target(RenderSync* sync, FramePacket* pkt) -> std::optional<RenderTarget>;
	void transition_for_render(vk::CommandBuffer cmd) const;
	void render(RenderTarget& img, vk::CommandBuffer cmd, FramePacket* pkt);
	void transition_for_present(vk::CommandBuffer cmd) const;
	void submit_and_present(RenderSync* sync);

	vk::UniqueInstance inst;
	vk::UniqueSurfaceKHR surf;

	GPU gpu;
	vk::UniqueDevice dev;
	vk::Queue qu;

	std::optional<Swapchain> swapchain{};

	vk::UniqueCommandPool render_cmd_pool;
	std::array<RenderSync, 2> render_sync{};
	u64 img_idx{0};

	vk::UniquePipelineLayout pipeline_layout;
	vk::UniquePipeline pipeline;

};
