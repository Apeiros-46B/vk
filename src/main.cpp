#include <chrono>
#include <iostream>
#include <thread>

#include <boost/lockfree/spsc_queue.hpp>
#include <SDL_events.h>

#include "arena.hpp"
#include "nums.hpp"
#include "renderer.hpp"

boost::lockfree::spsc_queue<FrameContext*, boost::lockfree::capacity<4>> render_queue;
boost::lockfree::spsc_queue<FrameContext*, boost::lockfree::capacity<4>> free_queue;

std::atomic<bool> is_running{true};

void render_loop(Renderer& renderer) {
	while (is_running) {
		FrameContext* ctx = nullptr;
		if (render_queue.pop(ctx)) {
			renderer.draw(ctx->packet);

			// send arena back to main
			while (!free_queue.push(ctx)) {
				// spin if main thread is lagging behind (unlikely)
				std::this_thread::yield();
			}
		} else {
			std::this_thread::yield();
		}
	}

	// drain queue to prevent memory leaks on exit
	render_queue.consume_all([](FrameContext* p) { delete p; });
}

int main() {
	auto renderer = Renderer{};
	std::thread render_thread(render_loop, std::ref(renderer));

	for (int i = 0; i < 3; i++) {
		free_queue.push(new FrameContext());
	}

	// main loop
	SDL_Event ev;
	auto t_prev = std::chrono::steady_clock::now();

	while (true) {
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_QUIT: goto quit;
				default: break;
			}
		}

		auto t_now = std::chrono::steady_clock::now();
		std::chrono::duration<flt> dt = t_now - t_prev;
		t_prev = t_now;

		FrameContext* ctx = nullptr;
		if (!free_queue.pop(ctx)) {
			// the rendering thread is holding all three frames
			// this should probably continue simulating, but for now we just skip
			continue;
		}
		ctx->arena.reset();

		ctx->packet = ctx->arena.alloc<FramePacket>();
		ctx->packet->dt = dt.count();
		// TODO: draw commands

		render_queue.push(ctx);
	}

quit:
	std::cout << "Exiting" << std::endl;

}
