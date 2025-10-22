#include "renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>

#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <SDL.h>
#include <SDL_error.h>
#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_structs.hpp>

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// use vulkan 1.3.0
constexpr auto VK_VER = vk::makeApiVersion(0, 1, 3, 0);
constexpr auto MIN_IMGS = 3u;

static constexpr auto QU_PRIOS = std::array{1.0f};
static constexpr auto DEV_EXTS = std::array{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static constexpr auto SRGB_FMTS = std::array{
	vk::Format::eR8G8B8A8Srgb,
	vk::Format::eB8G8R8A8Srgb,
};

void require_success(vk::Result res, const char* msg) {
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

	auto ext_count = 0u;
	SDL_Vulkan_GetInstanceExtensions(this->inner, &ext_count, nullptr);
	this->required_exts.resize(ext_count);
	SDL_Vulkan_GetInstanceExtensions(this->inner, &ext_count, this->required_exts.data());
}

Window::~Window() {
	SDL_DestroyWindow(this->inner);
	SDL_Quit();
}

static GPU find_gpu(vk::Instance inst, vk::SurfaceKHR surf) {
	auto fallback = std::optional<GPU>{};

	// TODO: extend selection mechanism to use quantitative scoring based on vram
	for (auto pdev : inst.enumeratePhysicalDevices()) {
		auto props = pdev.getProperties();
		if (props.apiVersion < VK_VER) continue;

		auto swapchain_supported = false;
		for (auto const props : pdev.enumerateDeviceExtensionProperties()) {
			if (std::strcmp(props.extensionName.data(), VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
				swapchain_supported = true;
				break;
			}
		}
		if (!swapchain_supported) continue;

		// TODO: multi queue
		auto set_qu_fam = false;
		auto quflags = vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eTransfer;
		auto qu_fam_idx = 0u;
		for (auto const qu_fam : pdev.getQueueFamilyProperties()) {
			if ((qu_fam.queueFlags & quflags) == quflags) {
				set_qu_fam = true;
				break;
			}
			qu_fam_idx++;
		}
		if (!set_qu_fam) continue;

		auto can_present = pdev.getSurfaceSupportKHR(qu_fam_idx, surf) == vk::True;
		if (!can_present) continue;

		auto candidate = GPU{
			.pdev = pdev,
			.props = props,
			.feats = pdev.getFeatures(),
			.qu_fam_idx = qu_fam_idx,
		};
		if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
			return candidate;
		}
		fallback = candidate; // We might find a discrete GPU later
	}

	if (fallback.has_value()) {
		return fallback.value();
	}

	throw std::runtime_error{"No suitable Vulkan devices found."};
}

static vk::SurfaceFormatKHR get_surf_fmt(std::vector<vk::SurfaceFormatKHR> supported) {
	for (auto desired : SRGB_FMTS) {
		auto it = std::ranges::find_if(supported, [desired](vk::SurfaceFormatKHR& fmt) {
			return fmt.format == desired
			    && fmt.colorSpace == vk::ColorSpaceKHR::eVkColorspaceSrgbNonlinear;
		});
		if (it == supported.end()) continue;
		return *it;
	}
	return supported.front();
}

static vk::Extent2D get_img_extent(vk::SurfaceCapabilitiesKHR& caps, glm::uvec2 sz) {
	auto large = 0xffff'ffff;
	if (caps.currentExtent.width < large && caps.currentExtent.height < large) {
		return caps.currentExtent;
	}
	auto x = std::clamp(sz.x, caps.minImageExtent.width, caps.maxImageExtent.width);
	auto y = std::clamp(sz.y, caps.minImageExtent.height, caps.maxImageExtent.height);
	return vk::Extent2D{x, y};
}

static uint32_t get_img_count(vk::SurfaceCapabilitiesKHR& caps) {
	if (caps.maxImageCount < caps.minImageCount) {
		return std::max(MIN_IMGS, caps.minImageCount);
	}
	return std::clamp(MIN_IMGS, caps.minImageCount, caps.maxImageCount);
}

Swapchain::Swapchain(
	GPU gpu,
	vk::Device dev,
	vk::SurfaceKHR surf,
	glm::ivec2 sz
) : gpu{gpu}, dev{dev}, surf{surf} {
	auto surf_fmt = get_surf_fmt(gpu.pdev.getSurfaceFormatsKHR(surf));
	// TODO: prioritize some present modes, let the user choose in settings
	this->cinfo = vk::SwapchainCreateInfoKHR{}
		.setSurface(surf)
		.setImageFormat(surf_fmt.format)
		.setImageColorSpace(surf_fmt.colorSpace)
		.setImageArrayLayers(1u)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment)
		.setPresentMode(vk::PresentModeKHR::eFifo);
	if (!this->recreate(sz)) {
		throw std::runtime_error{"Failed to initialize Vulkan swapchain"};
	}
}

bool Swapchain::recreate(glm::ivec2 sz) {
	if (sz.x <= 0 || sz.y <= 0) return false;

	auto caps = this->gpu.pdev.getSurfaceCapabilitiesKHR(this->surf);
	this->cinfo
		.setImageExtent(get_img_extent(caps, sz))
		.setMinImageCount(get_img_count(caps))
		.setOldSwapchain(this->inner ? *this->inner : vk::SwapchainKHR{})
		.setQueueFamilyIndices(this->gpu.qu_fam_idx);
	assert(
		this->cinfo.imageExtent.width > 0
		&& this->cinfo.imageExtent.height > 0
		&& this->cinfo.minImageCount >= MIN_IMGS
	);

	this->dev.waitIdle();
	this->inner = this->dev.createSwapchainKHRUnique(this->cinfo);

	this->populate_imgs();
	this->create_img_views();
	this->create_semaphores();

	sz = get_size();
	std::cout << "new swapchain is " << sz.x << "," << sz.y << std::endl;

	return true;
}

glm::ivec2 Swapchain::get_size() const {
	return {this->cinfo.imageExtent.width, this->cinfo.imageExtent.height};
}

void Swapchain::populate_imgs() {
	uint32_t img_count;
	vk::Result res;

	res = this->dev.getSwapchainImagesKHR(*this->inner, &img_count, nullptr);
	require_success(res, "Failed to get swapchain images");

	this->imgs.resize(img_count);
	res = this->dev.getSwapchainImagesKHR(*this->inner, &img_count, this->imgs.data());
	require_success(res, "Failed to get swapchain images");
}

void Swapchain::create_img_views() {
	auto subresource_range = vk::ImageSubresourceRange{}
		.setAspectMask(vk::ImageAspectFlagBits::eColor)
		.setLayerCount(1)
		.setLevelCount(1);
	auto img_view_cinfo = vk::ImageViewCreateInfo{}
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(this->cinfo.imageFormat)
		.setSubresourceRange(subresource_range);

	this->img_views.clear();
	this->img_views.reserve(this->imgs.size());
	for (auto img : this->imgs) {
		img_view_cinfo.setImage(img);
		this->img_views.push_back(this->dev.createImageViewUnique(img_view_cinfo));
	}
}

void Swapchain::create_semaphores() {
	this->semaphores.clear();
	this->semaphores.resize(this->imgs.size());
	for (auto& semaphore : this->semaphores) {
		semaphore = this->dev.createSemaphoreUnique({});
	}
}

Renderer::Renderer() {
	VULKAN_HPP_DEFAULT_DISPATCHER.init();
	auto loader_ver = vk::enumerateInstanceVersion();
	if (loader_ver < VK_VER) {
		throw std::runtime_error{"Vulkan loader does not support Vulkan 1.3"};
	}
	auto app_info = vk::ApplicationInfo{}
		.setPApplicationName("vk")
		.setApiVersion(VK_VER);
	auto inst_cinfo = vk::InstanceCreateInfo{}
		.setPApplicationInfo(&app_info)
		.setPEnabledExtensionNames(this->win.required_exts);
	this->inst = vk::createInstanceUnique(inst_cinfo);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this->inst);

	auto surf_inner = VkSurfaceKHR{};
	if (!SDL_Vulkan_CreateSurface(this->win.inner, *this->inst, &surf_inner)) {
		throw std::runtime_error{SDL_GetError()};
	}
	this->surf = vk::UniqueSurfaceKHR{surf_inner, *this->inst};

	this->gpu = find_gpu(*this->inst, *this->surf);

	// TODO: multi queue
	auto qu_cinfo = vk::DeviceQueueCreateInfo{}
		.setQueueFamilyIndex(this->gpu.qu_fam_idx)
		.setQueueCount(1u)
		.setQueuePriorities(QU_PRIOS);
	auto enabled_feats = vk::PhysicalDeviceFeatures{}
		.setFillModeNonSolid(this->gpu.feats.fillModeNonSolid)
		.setWideLines(this->gpu.feats.wideLines)
		.setSamplerAnisotropy(this->gpu.feats.samplerAnisotropy)
		.setSampleRateShading(this->gpu.feats.sampleRateShading);
	auto sync2_feat = vk::PhysicalDeviceSynchronization2Features{vk::True};
	auto dyn_render_feat = vk::PhysicalDeviceDynamicRenderingFeatures{vk::True};
	sync2_feat.setPNext(&dyn_render_feat);

	auto dev_cinfo = vk::DeviceCreateInfo{}
		.setPEnabledExtensionNames(DEV_EXTS)
		.setQueueCreateInfos(qu_cinfo)
		.setPEnabledFeatures(&enabled_feats)
		.setPNext(&sync2_feat);
	
	this->dev = this->gpu.pdev.createDeviceUnique(dev_cinfo);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this->dev);
	this->waiter = *this->dev;

	this->qu = dev->getQueue(this->gpu.qu_fam_idx, 0u);

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
