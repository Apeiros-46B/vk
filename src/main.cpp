#include <chrono>
#include <iostream>

#include <SDL_events.h>

#include "renderer.hpp"

int main() {
	auto renderer = Renderer{};

	// main loop
	SDL_Event ev;
	auto t_prev = std::chrono::system_clock::now();

	while (true) {
		while (SDL_PollEvent(&ev)) {
			switch (ev.type) {
				case SDL_QUIT: goto quit;
				default: break;
			}
		}

		auto t_now = std::chrono::system_clock::now();
		auto dt = t_now - t_prev;

		renderer.draw();

		t_prev = t_now;
	}

quit:
	std::cout << "Exiting" << std::endl;

}
