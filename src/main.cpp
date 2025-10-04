#include <SDL3/SDL.h>
#include <iostream>
#include <memory>
#include <string>

class SDL3InputDemo {
private:
    std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{nullptr, SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)> renderer{nullptr, SDL_DestroyRenderer};

    bool running = true;
    int mouseX = 0;
    int mouseY = 0;
    bool mousePressed = false;

    void handleEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                handleKeyPress(event.key.key);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mousePressed = true;
                std::cout << "Mouse clicked at (" << mouseX << ", " << mouseY << ")\n";
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                mousePressed = false;
                std::cout << "Mouse released at (" << mouseX << ", " << mouseY << ")\n";
                break;

            case SDL_EVENT_MOUSE_MOTION:
                mouseX = static_cast<int>(event.motion.x);
                mouseY = static_cast<int>(event.motion.y);
                if (mousePressed) {
                    std::cout << "Mouse dragged to (" << mouseX << ", " << mouseY << ")\n";
                }
                break;
        }
    }

    void handleKeyPress(SDL_Keycode key) {
        switch (key) {
            case SDLK_ESCAPE:
            case SDLK_Q:
                running = false;
                break;
            case SDLK_UP:
                std::cout << "UP arrow pressed\n";
                break;
            case SDLK_DOWN:
                std::cout << "DOWN arrow pressed\n";
                break;
            case SDLK_LEFT:
                std::cout << "LEFT arrow pressed\n";
                break;
            case SDLK_RIGHT:
                std::cout << "RIGHT arrow pressed\n";
                break;
            case SDLK_SPACE:
                std::cout << "SPACE pressed\n";
                break;
        }
    }

    void render() {
        // Clear screen with dark background
        SDL_SetRenderDrawColor(renderer.get(), 30, 30, 40, 255);
        SDL_RenderClear(renderer.get());

        // Draw a circle at mouse position when pressed
        if (mousePressed) {
            SDL_SetRenderDrawColor(renderer.get(), 100, 200, 255, 255);
            drawCircle(mouseX, mouseY, 10);
        }

        SDL_RenderPresent(renderer.get());
    }

    void drawCircle(int centerX, int centerY, int radius) {
        for (int w = 0; w < radius * 2; w++) {
            for (int h = 0; h < radius * 2; h++) {
                int dx = radius - w;
                int dy = radius - h;
                if ((dx * dx + dy * dy) <= (radius * radius)) {
                    SDL_RenderPoint(renderer.get(), centerX + dx, centerY + dy);
                }
            }
        }
    }

public:
    bool initialize() {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL initialization failed: " << SDL_GetError() << "\n";
            return false;
        }

        window.reset(SDL_CreateWindow(
            "SDL3 Input Demo",
            800, 600,
            SDL_WINDOW_RESIZABLE
        ));

        if (!window) {
            std::cerr << "Window creation failed: " << SDL_GetError() << "\n";
            return false;
        }

        renderer.reset(SDL_CreateRenderer(window.get(), nullptr));

        if (!renderer) {
            std::cerr << "Renderer creation failed: " << SDL_GetError() << "\n";
            return false;
        }

        return true;
    }

    void run() {
        std::cout << "SDL3 Input Demo Running...\n";
        std::cout << "Controls:\n";
        std::cout << "  - Arrow keys: Move\n";
        std::cout << "  - Mouse: Click and move\n";
        std::cout << "  - ESC or Q: Quit\n";

        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handleEvent(event);
            }

            render();
            SDL_Delay(16); // ~60 FPS
        }

        std::cout << "SDL3 Input Demo closed.\n";
    }

    ~SDL3InputDemo() {
        SDL_Quit();
    }
};

int main(int argc, char* argv[]) {
    SDL3InputDemo demo;

    if (!demo.initialize()) {
        return 1;
    }

    demo.run();
    return 0;
}
