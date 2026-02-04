#include "renderer.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <SDL.h>
#include <SDL_error.h>
#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "nums.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// use vulkan 1.3.0
constexpr auto VK_VER = vk::makeApiVersion(0, 1, 3, 0);
constexpr u32 MIN_IMGS = 3u;

static constexpr auto QU_PRIOS = std::array{1.0f};
static constexpr auto DEV_EXTS = std::array{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static constexpr auto SRGB_FMTS = std::array{
	vk::Format::eR8G8B8A8Srgb,
	vk::Format::eB8G8R8A8Srgb,
};
static constexpr auto SUBRESOURCE_RANGE = vk::ImageSubresourceRange{}
	.setAspectMask(vk::ImageAspectFlagBits::eColor)
	.setLayerCount(1)
	.setLayerCount(1);

static void require_success(vk::Result res, const char* msg) {
	if (res != vk::Result::eSuccess) {
		throw std::runtime_error{msg};
	}
}

Window::Window() {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		throw std::runtime_error{SDL_GetError()};
	}
	auto ptr = SDL_CreateWindow("vk", -1, -1, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
	if (ptr == nullptr) {
		throw std::runtime_error{SDL_GetError()};
	}
	this->inner = ptr;

	SDL_Vulkan_GetDrawableSize(this->inner, &this->sz.x, &this->sz.y);

	u32 ext_count = 0u;
	SDL_Vulkan_GetInstanceExtensions(this->inner, &ext_count, nullptr);
	this->required_exts.resize(ext_count);
	SDL_Vulkan_GetInstanceExtensions(this->inner, &ext_count, this->required_exts.data());
}

Window::~Window() {
	SDL_DestroyWindow(this->inner);
	SDL_Quit();
}

static bool needs_recreation(vk::Result res) {
	switch (res) {
		case vk::Result::eSuccess:
		case vk::Result::eSuboptimalKHR:
			return false;
		case vk::Result::eErrorOutOfDateKHR:
			return true;
		default:
			throw std::runtime_error{"Swapchain error"};
	}
}

Swapchain::Swapchain(
	GPU gpu,
	vk::Device dev,
	vk::SurfaceKHR surf,
	glm::ivec2 sz
) : gpu{gpu}, dev{dev}, surf{surf} {
	if (!this->recreate(sz)) {
		throw std::runtime_error{"Failed to initialize Vulkan swapchain"};
	}
}

bool Swapchain::recreate(glm::ivec2 sz) {
	if (sz.x <= 0 || sz.y <= 0) return false;

	this->dev.waitIdle();
	vkb::SwapchainBuilder builder{this->gpu.pdev, this->dev, this->surf};
	auto vkb_swap_ret = builder
		.set_desired_extent(sz.x, sz.y)
		.set_desired_format(vk::SurfaceFormatKHR{vk::Format::eR8G8B8A8Srgb, vk::ColorSpaceKHR::eVkColorspaceSrgbNonlinear})
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_min_image_count(MIN_IMGS)
		.set_old_swapchain(this->inner ? *this->inner : VK_NULL_HANDLE)
		.build();

	if (!vkb_swap_ret) {
		std::cerr << "Swapchain recreation failed: " << vkb_swap_ret.error().message() << std::endl;
		return false;
	}

	vkb::Swapchain vkb_swap = vkb_swap_ret.value();

	this->inner = vk::UniqueSwapchainKHR{vkb_swap.swapchain, this->dev};
	this->cinfo.imageExtent = vkb_swap.extent;
	this->cinfo.imageFormat = vk::Format(vkb_swap.image_format);

	auto images = vkb_swap.get_images();
	if (!images) throw std::runtime_error("Failed to get swapchain images");
	this->imgs.resize(images->size());
	std::transform(images->begin(), images->end(), this->imgs.begin(),
		[](VkImage img) { return vk::Image(img); }
	);

	auto views = vkb_swap.get_image_views();
	if (!views) throw std::runtime_error("Failed to get swapchain image views");

	this->img_views.clear();
	for (auto v : views.value()) {
		this->img_views.push_back(vk::UniqueImageView{v, this->dev});
	}

	this->create_semaphores();

	sz = get_size();
	std::cout << "new swapchain is " << sz.x << "," << sz.y << std::endl;

	return true;
}

bool Swapchain::present(vk::Queue qu) {
	if (!this->img_idx.has_value()) return true; // Nothing to present

	u32 img_idx = this->img_idx.value();
	auto wait_semaphore = *this->semaphores.at(img_idx);
	auto present_info = vk::PresentInfoKHR{}
		.setSwapchains(*this->inner)
		.setImageIndices(img_idx)
		.setWaitSemaphores(wait_semaphore);

	vk::Result res;
	try {
		res = qu.presentKHR(&present_info);
	} catch (vk::OutOfDateKHRError&) {
		res = vk::Result::eErrorOutOfDateKHR;
	}

	this->img_idx.reset();
	return !needs_recreation(res);
}

std::optional<RenderTarget> Swapchain::acq_next_img(vk::Semaphore to_sig) {
	assert(!this->img_idx.has_value());
	u32 img_idx = 0u;

	vk::Result res;
	try {
		res = this->dev.acquireNextImageKHR(
			*this->inner,
			std::numeric_limits<u64>::max(),
			to_sig,
			{},
			&img_idx
		);
	} catch (vk::OutOfDateKHRError&) {
		res = vk::Result::eErrorOutOfDateKHR;
	}

	if (needs_recreation(res)) {
		return {};
	}

	this->img_idx = img_idx;
	return RenderTarget{
		.img = this->imgs.at(*this->img_idx),
		.img_view = *this->img_views.at(*this->img_idx),
		.extent = this->cinfo.imageExtent,
	};
}

vk::ImageMemoryBarrier2 Swapchain::base_barrier() const {
	return vk::ImageMemoryBarrier2{}
	.setImage(this->imgs.at(this->img_idx.value()))
	.setSubresourceRange(SUBRESOURCE_RANGE)
	.setSrcQueueFamilyIndex(this->gpu.qu_fam_idx)
	.setDstQueueFamilyIndex(this->gpu.qu_fam_idx);
}

glm::ivec2 Swapchain::get_size() const {
	return {this->cinfo.imageExtent.width, this->cinfo.imageExtent.height};
}

void Swapchain::create_semaphores() {
	this->semaphores.clear();
	this->semaphores.resize(this->imgs.size());
	for (auto& semaphore : this->semaphores) {
		semaphore = this->dev.createSemaphoreUnique({});
	}
}

vk::Semaphore Swapchain::get_semaphore() const {
	return *this->semaphores.at(this->img_idx.value());
}

Renderer::Renderer() {
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	auto inst_builder = vkb::InstanceBuilder{}
		.set_app_name("vk")
		.request_validation_layers()
		.require_api_version(1, 3, 0)
		.use_default_debug_messenger();
	for (const char* ext : this->win.required_exts) {
		inst_builder.enable_extension(ext);
	}

	auto vkb_inst_ret = inst_builder.build();
	if (!vkb_inst_ret) throw std::runtime_error(vkb_inst_ret.error().message());

	auto vkb_inst = vkb_inst_ret.value();

	this->inst = vk::UniqueInstance{vkb_inst.instance};
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this->inst);

	auto surf_inner = VkSurfaceKHR{};
	if (!SDL_Vulkan_CreateSurface(this->win.inner, *this->inst, &surf_inner)) {
		throw std::runtime_error{SDL_GetError()};
	}
	this->surf = vk::UniqueSurfaceKHR{surf_inner, *this->inst};

	auto required_features = vk::PhysicalDeviceFeatures{}
		.setFillModeNonSolid(true)
		.setWideLines(true)
		.setSamplerAnisotropy(true)
		.setSampleRateShading(true);

	auto features13 = vk::PhysicalDeviceVulkan13Features{}
		.setSynchronization2(true)
		.setDynamicRendering(true);

	auto phys_ret = vkb::PhysicalDeviceSelector{vkb_inst}
		.set_surface(*this->surf)
		.set_minimum_version(1, 3)
		.set_required_features(required_features)
		.set_required_features_13(features13)
		.select();
	if (!phys_ret) throw std::runtime_error(phys_ret.error().message());
	auto vkb_phys = phys_ret.value();

	auto dev_ret = vkb::DeviceBuilder{vkb_phys}.build();
	if (!dev_ret) throw std::runtime_error(dev_ret.error().message());
	auto vkb_dev = dev_ret.value();

	this->dev = vk::UniqueDevice{vkb_dev.device};
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this->dev);

	this->waiter = *this->dev;

	auto graphics_queue_ret = vkb_dev.get_queue(vkb::QueueType::graphics);
	if (!graphics_queue_ret) throw std::runtime_error("Failed to get graphics queue");
	this->qu = graphics_queue_ret.value();

	auto queue_fam_ret = vkb_dev.get_queue_index(vkb::QueueType::graphics);
	if (!queue_fam_ret) throw std::runtime_error("No graphics queue found");

	this->gpu = GPU{
		.pdev = vkb_phys.physical_device,
		.props = vkb_phys.properties,
		.feats = vkb_phys.features,
		.qu_fam_idx = queue_fam_ret.value()
	};

	this->swapchain.emplace(this->gpu, *this->dev, *this->surf, this->win.sz);

	auto cmd_pool_cinfo = vk::CommandPoolCreateInfo{}
		.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(this->gpu.qu_fam_idx);
	this->render_cmd_pool = this->dev->createCommandPoolUnique(cmd_pool_cinfo);

	auto cmd_buf_ainfo = vk::CommandBufferAllocateInfo{}
		.setCommandPool(*this->render_cmd_pool)
		.setCommandBufferCount(this->render_sync.size())
		.setLevel(vk::CommandBufferLevel::ePrimary);
	auto cmd_bufs = this->dev->allocateCommandBuffers(cmd_buf_ainfo);
	assert(cmd_bufs.size() == this->render_sync.size());

	auto fence_cinfo = vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled};
	for (size_t i = 0u; i < cmd_bufs.size(); i++) {
		this->render_sync[i].cmd = cmd_bufs[i];
		this->render_sync[i].draw = this->dev->createSemaphoreUnique({});
		this->render_sync[i].drawn = this->dev->createFenceUnique(fence_cinfo);
	}
}

void Renderer::draw(FramePacket* packet) {
	std::cout << packet->dt << std::endl;
}
