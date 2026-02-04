#include <chrono>
#include <iostream>
#include <thread>

#include <boost/lockfree/spsc_queue.hpp>
#include <SDL_events.h>

#include "arena.hpp"
#include "renderer.hpp"
#include "sugar.hpp"

boost::lockfree::spsc_queue<FrameContext*, boost::lockfree::capacity<4>> render_queue;
boost::lockfree::spsc_queue<FrameContext*, boost::lockfree::capacity<4>> free_queue;

std::atomic<bool> is_running{true};

template<typename T, typename... Options>
void delete_all(boost::lockfree::spsc_queue<T*, Options...>& queue) {
	queue.consume_all([](T* ptr) { delete ptr; });
}

void render_loop() {
	auto renderer = Renderer();

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

	delete_all(render_queue);
}

int main() {
	auto render_thread = std::thread(render_loop);
	for (usz i = 0; i < 3; i++) {
		free_queue.push(new FrameContext());
	}

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
	is_running.store(false);
	if (render_thread.joinable()) {
		render_thread.join();
	}
	delete_all(free_queue);

	std::cout << "Exiting" << std::endl;

}
