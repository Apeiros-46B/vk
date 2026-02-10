#include "renderer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int2.hpp>
#include <glm/ext/vector_uint2.hpp>
#include <SDL.h>
#include <SDL_error.h>
#include <SDL_video.h>
#include <SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_handles.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>
#include <vulkan/vulkan_structs.hpp>

#include "sugar.hpp"
#include "shader.hpp"
#include "vma.hpp"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// use vulkan 1.3.0
constexpr auto VK_VER = vk::makeApiVersion(0, 1, 3, 0);
constexpr u32 MIN_IMGS = 3u;

struct Vertex {
	glm::vec2 pos;
	glm::vec3 color;

	static auto get_binding() -> vk::VertexInputBindingDescription {
		auto ret = vk::VertexInputBindingDescription{}
			.setBinding(0)
			.setStride(sizeof(Vertex))
			.setInputRate(vk::VertexInputRate::eVertex);
		return ret;
	}

	static auto get_attr_descs() -> std::array<vk::VertexInputAttributeDescription, 2> {
		return std::array{
			vk::VertexInputAttributeDescription{}
				.setBinding(0)
				.setLocation(0)
				.setFormat(vk::Format::eR32G32Sfloat)
				.setOffset(offsetof(Vertex, pos)),
			vk::VertexInputAttributeDescription{}
				.setBinding(0)
				.setLocation(1)
				.setFormat(vk::Format::eR32G32B32Sfloat)
				.setOffset(offsetof(Vertex, color)),
		};
	}
};
const std::vector<Vertex> verts = {
	{{0.0, -0.5}, {1.0, 0.0, 0.0}},
	{{0.5, 0.5}, {0.0, 1.0, 0.0}},
	{{-0.5, 0.5}, {0.0, 0.0, 1.0}},
};

static constexpr auto QU_PRIOS = std::array{1.0f};
static constexpr auto DEV_EXTS = std::array{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
static constexpr auto SRGB_FMTS = std::array{
	vk::Format::eR8G8B8A8Srgb,
	vk::Format::eB8G8R8A8Srgb,
};
static constexpr auto SUBRESOURCE_RANGE = vk::ImageSubresourceRange{}
	.setAspectMask(vk::ImageAspectFlagBits::eColor)
	.setLayerCount(1)
	.setLevelCount(1);

static void require_success(vk::Result res, const char* msg) {
	if (res != vk::Result::eSuccess) {
		throw std::runtime_error(msg);
	}
}

Window::Window() {
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		throw std::runtime_error(SDL_GetError());
	}
	auto ptr = SDL_CreateWindow("vk", -1, -1, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
	// auto ptr = SDL_CreateWindow("vk", -1, -1, 800, 600, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
	if (ptr == nullptr) {
		throw std::runtime_error(SDL_GetError());
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
			throw std::runtime_error("Swapchain error");
	}
}

Swapchain::Swapchain(
	GPU gpu,
	vk::Device dev,
	vk::SurfaceKHR surf,
	glm::ivec2 sz
) : gpu{gpu}, dev{dev}, surf{surf} {
	if (!this->recreate(sz)) {
		throw std::runtime_error("Failed to initialize Vulkan swapchain");
	}
}

bool Swapchain::recreate(glm::ivec2 sz) {
	if (sz.x <= 0 || sz.y <= 0) return false;

	this->dev.waitIdle();
	auto vkb_swap_ret = vkb::SwapchainBuilder{ this->gpu.pdev, this->dev, this->surf }
		.set_desired_extent(sz.x, sz.y)
		.set_desired_format(vk::SurfaceFormatKHR{
			vk::Format::eR8G8B8A8Srgb,
			vk::ColorSpaceKHR::eVkColorspaceSrgbNonlinear,
		})
		.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
		.set_desired_min_image_count(MIN_IMGS)
		.set_old_swapchain(this->inner ? *this->inner : VK_NULL_HANDLE)
		.build();

	if (!vkb_swap_ret.has_value()) {
		std::cerr << "recreation failed: " << vkb_swap_ret.error().message() << std::endl;
		return false;
	}
	auto vkb_swap = vkb_swap_ret.value();

	this->inner = vk::UniqueSwapchainKHR{vkb_swap.swapchain, this->dev};
	this->cinfo.imageExtent = vkb_swap.extent;
	this->cinfo.imageFormat = vk::Format(vkb_swap.image_format);

	auto imgs = vkb_swap.get_images();
	if (!imgs.has_value()) {
		throw std::runtime_error("Failed to get swapchain images");
	};
	this->imgs.resize(imgs->size());
	std::transform(imgs->begin(), imgs->end(), this->imgs.begin(),
		[](VkImage img) { return vk::Image(img); }
	);

	auto views = vkb_swap.get_image_views();
	if (!views.has_value()) {
		throw std::runtime_error("Failed to get swapchain image views");
	}

	this->img_views.clear();
	this->img_views.reserve(views->size());
	for (auto v : views.value()) {
		this->img_views.push_back(vk::UniqueImageView{v, this->dev});
	}

	this->render_sems.clear();
	this->render_sems.resize(this->imgs.size());
	for (auto& semaphore : this->render_sems) {
		semaphore = this->dev.createSemaphoreUnique({});
	}

	sz = get_size();
	std::cout << "new swapchain is " << sz.x << "," << sz.y << std::endl;

	return true;
}

bool Swapchain::present(vk::Queue qu) {
	if (!this->img_idx.has_value()) {
		return true;
	}

	auto img_idx = this->img_idx.value();
	auto to_wait = *this->render_sems.at(img_idx);
	auto present_info = vk::PresentInfoKHR{}
		.setSwapchains(*this->inner)
		.setImageIndices(img_idx)
		.setWaitSemaphores(to_wait);

	vk::Result res;
	try {
		res = qu.presentKHR(&present_info);
	} catch (vk::OutOfDateKHRError&) {
		res = vk::Result::eErrorOutOfDateKHR;
	}

	this->img_idx.reset();
	return !needs_recreation(res);
}

auto Swapchain::acq_next_img(vk::Semaphore to_sig) -> std::optional<RenderTarget> {
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
	return RenderTarget {
		.img = this->imgs.at(*this->img_idx),
		.img_view = *this->img_views.at(*this->img_idx),
		.extent = this->cinfo.imageExtent,
	};
}

auto Swapchain::base_barrier() const -> vk::ImageMemoryBarrier2 {
	return vk::ImageMemoryBarrier2{}
		.setImage(this->imgs.at(this->img_idx.value()))
		.setSubresourceRange(SUBRESOURCE_RANGE)
		.setSrcQueueFamilyIndex(this->gpu.qu_fam_idx)
		.setDstQueueFamilyIndex(this->gpu.qu_fam_idx);
}

auto Swapchain::get_size() const -> glm::ivec2 {
	return {this->cinfo.imageExtent.width, this->cinfo.imageExtent.height};
}

auto Swapchain::get_sem() const -> vk::Semaphore {
	return *this->render_sems.at(this->img_idx.value());
}

Renderer::Renderer(Window* win) {
	auto vkb_inst = this->init_inst(win);

	auto surf_inner = VkSurfaceKHR{};
	if (!SDL_Vulkan_CreateSurface(win->inner, *this->inst, &surf_inner)) {
		throw std::runtime_error(SDL_GetError());
	}
	this->surf = vk::UniqueSurfaceKHR{surf_inner, *this->inst};

	this->init_devs(vkb_inst);
	this->swapchain.emplace(this->gpu, *this->dev, *this->surf, win->sz);
	this->init_sync();

	this->alloc = VulkanAllocator(this->inst.get(), this->gpu.pdev, this->dev.get());

	this->init_pipeline();
}

Renderer::~Renderer() {
	if (this->dev) {
		this->dev->waitIdle();
	}
}

auto Renderer::init_inst(Window* win) -> vkb::Instance {
	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	auto inst_builder = vkb::InstanceBuilder{}
		.set_app_name("vk")
		.request_validation_layers()
		.require_api_version(1, 3, 0)
		.use_default_debug_messenger();
	for (const char* ext : win->required_exts) {
		inst_builder.enable_extension(ext);
	}

	auto vkb_inst_ret = inst_builder.build();
	if (!vkb_inst_ret.has_value()) {
		throw std::runtime_error(vkb_inst_ret.error().message());
	}

	auto vkb_inst = vkb_inst_ret.value();

	this->inst = vk::UniqueInstance{vkb_inst.instance};
	VULKAN_HPP_DEFAULT_DISPATCHER.init(*this->inst);

	return vkb_inst;
}

void Renderer::init_devs(vkb::Instance vkb_inst) {
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
	if (!phys_ret) {
		throw std::runtime_error(phys_ret.error().message());
	}

	auto vkb_phys = phys_ret.value();
	auto dev_ret = vkb::DeviceBuilder{vkb_phys}.build();
	if (!dev_ret) {
		throw std::runtime_error(dev_ret.error().message());
	}

	auto vkb_dev = dev_ret.value();
	this->dev = vk::UniqueDevice{vkb_dev.device};

	auto graphics_queue_ret = vkb_dev.get_queue(vkb::QueueType::graphics);
	if (!graphics_queue_ret) throw std::runtime_error("Failed to get graphics queue");
	this->qu = graphics_queue_ret.value();

	auto queue_fam_ret = vkb_dev.get_queue_index(vkb::QueueType::graphics);
	if (!queue_fam_ret) throw std::runtime_error("No graphics queue found");

	this->gpu = GPU {
		.pdev = vkb_phys.physical_device,
		.props = vkb_phys.properties,
		.feats = vkb_phys.features,
		.qu_fam_idx = queue_fam_ret.value()
	};
}

void Renderer::init_sync() {
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
		this->render_sync[i].img_sem = this->dev->createSemaphoreUnique({});
		this->render_sync[i].drawn = this->dev->createFenceUnique(fence_cinfo);
	}
}

void Renderer::init_pipeline() {
	auto code = read_file("build/triangle.spv");
	auto module = this->dev->createShaderModuleUnique({
		{}, code.size(), reinterpret_cast<const u32*>(code.data())
	});

	auto vert_stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eVertex, *module, "vertexMain");
	auto frag_stage = vk::PipelineShaderStageCreateInfo({}, vk::ShaderStageFlagBits::eFragment, *module, "fragmentMain");
	auto shader_stages = std::array{vert_stage, frag_stage};

	// vertices hardcoded in shader for now
	auto binding_desc = Vertex::get_binding();
	auto attr_descs = Vertex::get_attr_descs();
	auto vertex_input = vk::PipelineVertexInputStateCreateInfo{}
		.setVertexBindingDescriptions(binding_desc)
		.setVertexAttributeDescriptions(attr_descs);

	auto input_assembly = vk::PipelineInputAssemblyStateCreateInfo({}, vk::PrimitiveTopology::eTriangleList);

	auto viewport_state = vk::PipelineViewportStateCreateInfo({}, 1, nullptr, 1, nullptr);

	auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
		.setPolygonMode(vk::PolygonMode::eFill)
		.setCullMode(vk::CullModeFlagBits::eBack)
		.setFrontFace(vk::FrontFace::eClockwise)
		.setLineWidth(1.0f);

	auto multisample = vk::PipelineMultisampleStateCreateInfo{}.setRasterizationSamples(vk::SampleCountFlagBits::e1);

	auto color_blend_attachment = vk::PipelineColorBlendAttachmentState{}
		.setBlendEnable(true)
		.setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
		.setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
		.setColorBlendOp(vk::BlendOp::eAdd)
		.setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
		.setDstAlphaBlendFactor(vk::BlendFactor::eZero)
		.setColorBlendOp(vk::BlendOp::eAdd)
		.setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

	auto color_blend = vk::PipelineColorBlendStateCreateInfo{}.setAttachments(color_blend_attachment);

	auto layout_info = vk::PipelineLayoutCreateInfo{};
	this->pipeline_layout = this->dev->createPipelineLayoutUnique(layout_info);

	auto dynamic_states = std::array{vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	auto dynamic_info = vk::PipelineDynamicStateCreateInfo{}.setDynamicStates(dynamic_states);

	auto swap_format = this->swapchain->cinfo.imageFormat;

	auto pipeline_rendering_info = vk::PipelineRenderingCreateInfo{}
		.setColorAttachmentFormats(swap_format);
	// .setDepthAttachmentFormat(...) // If you had a depth buffer

	auto pipeline_info = vk::GraphicsPipelineCreateInfo{}
		.setPNext(&pipeline_rendering_info)
		.setStages(shader_stages)
		.setPVertexInputState(&vertex_input)
		.setPInputAssemblyState(&input_assembly)
		.setPViewportState(&viewport_state)
		.setPRasterizationState(&rasterizer)
		.setPMultisampleState(&multisample)
		.setPColorBlendState(&color_blend)
		.setPDynamicState(&dynamic_info)
		.setLayout(*this->pipeline_layout)
		.setRenderPass(nullptr);

	auto result = this->dev->createGraphicsPipelineUnique(nullptr, pipeline_info);
	if (result.result != vk::Result::eSuccess) {
		throw std::runtime_error("Failed to create pipeline");
	}
	this->pipeline = std::move(result.value);
}

void Renderer::draw(FramePacket* pkt) {
	auto i = this->img_idx++ % this->render_sync.size();
	auto sync = &this->render_sync[i];
	auto img = this->acq_render_target(sync, pkt);
	if (!img.has_value()) {
		return;
	}

	sync->cmd.reset();

	auto info = vk::CommandBufferBeginInfo{}
		.setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
	sync->cmd.begin(info);

	this->transition_for_render(sync->cmd);
	this->render(img.value(), sync->cmd, pkt);
	this->transition_for_present(sync->cmd);

	sync->cmd.end();

	this->submit_and_present(sync);
}

auto Renderer::acq_render_target(
	RenderSync* sync,
	FramePacket* pkt
) -> std::optional<RenderTarget> {
	auto res = this->dev->waitForFences(1, &sync->drawn.get(), true, 1'000'000'000);
	require_success(res, "wait for fence failed");

	auto img = this->swapchain->acq_next_img(sync->img_sem.get());
	if (!img.has_value()) {
		if (!this->swapchain->recreate(pkt->drawable_sz)) {
			throw std::runtime_error("failed to recreate swapchain");
		}
		return {};
	}

	// image acquired, now it is safe to reset the fence
	res = this->dev->resetFences(1, &sync->drawn.get());
	require_success(res, "reset fence failed");

	return img;
}

void Renderer::transition_for_render(vk::CommandBuffer cmd) const {
	auto draw_barrier = this->swapchain->base_barrier()
		.setSrcStageMask(vk::PipelineStageFlagBits2::eTopOfPipe)
		.setDstStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
		.setSrcAccessMask(vk::AccessFlagBits2::eNone)
		.setDstAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
		.setOldLayout(vk::ImageLayout::eUndefined)
		.setNewLayout(vk::ImageLayout::eColorAttachmentOptimal);
	auto dep_info = vk::DependencyInfo{}.setImageMemoryBarriers(draw_barrier);
	cmd.pipelineBarrier2(dep_info);
}

void Renderer::render(RenderTarget& img, vk::CommandBuffer cmd, FramePacket* pkt) {
	auto color = vk::ClearColorValue{}.setFloat32({0.0, 0.0, 0.0, 1.0});
	auto attach_info = vk::RenderingAttachmentInfo{}
		.setImageView(img.img_view)
		.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
		.setClearValue(color)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore);
	auto render_info = vk::RenderingInfo{}
		.setRenderArea({{0, 0}, img.extent})
		.setLayerCount(1)
		.setColorAttachments(attach_info);
	cmd.beginRendering(render_info);

	cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, *this->pipeline);

	// set dynamic states
	auto viewport = vk::Viewport{}
		.setX(0.0f)
		.setY(0.0f)
		.setWidth(static_cast<flt>(img.extent.width))
		.setHeight(static_cast<flt>(img.extent.height))
		.setMinDepth(0.0f)
		.setMaxDepth(1.0f);
	cmd.setViewport(0, viewport);

	auto scissor = vk::Rect2D{}
		.setOffset({0, 0})
		.setExtent(img.extent);
	cmd.setScissor(0, scissor);

	cmd.draw(3, 1, 0, 0);

	cmd.endRendering();
}

void Renderer::transition_for_present(vk::CommandBuffer cmd) const {
	auto present_barrier = this->swapchain->base_barrier()
		.setSrcStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput)
		.setDstStageMask(vk::PipelineStageFlagBits2::eBottomOfPipe)
		.setSrcAccessMask(vk::AccessFlagBits2::eColorAttachmentWrite)
		.setDstAccessMask(vk::AccessFlagBits2::eNone)
		.setOldLayout(vk::ImageLayout::eColorAttachmentOptimal)
		.setNewLayout(vk::ImageLayout::ePresentSrcKHR);
	auto dep_info = vk::DependencyInfo{}.setImageMemoryBarriers(present_barrier);
	cmd.pipelineBarrier2(dep_info);
}

void Renderer::submit_and_present(RenderSync* sync) {
	auto cmd_info = vk::CommandBufferSubmitInfo{sync->cmd};
	auto wait_info = vk::SemaphoreSubmitInfo{}
		.setSemaphore(sync->img_sem.get())
		.setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);
	auto sig_info = vk::SemaphoreSubmitInfo{}
		.setSemaphore(this->swapchain->get_sem())
		.setStageMask(vk::PipelineStageFlagBits2::eColorAttachmentOutput);
	auto submit_info = vk::SubmitInfo2{}
		.setWaitSemaphoreInfos(wait_info)
		.setCommandBufferInfos(cmd_info)
		.setSignalSemaphoreInfos(sig_info);
	auto res = this->qu.submit2(
		1,
		&submit_info,
		sync->drawn.get(),
		VULKAN_HPP_DEFAULT_DISPATCHER
	);
	require_success(res, "failed to submit to queue");

	this->swapchain->present(this->qu);
}
