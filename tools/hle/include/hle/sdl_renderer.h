#pragma once

#include <SDL2/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "hle/hle_display.h"

namespace hle {

class SDLRenderer {
public:
    explicit SDLRenderer(int scale = 3) : m_scale(scale > 0 ? scale : 1) {}

    ~SDLRenderer() { shutdown(); }

    bool init() {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        m_is_sdl_initialized = true;

        m_window = SDL_CreateWindow("TentacleOS HLE", SDL_WINDOWPOS_UNDEFINED,
                                    SDL_WINDOWPOS_UNDEFINED, LCD_H_RES * m_scale,
                                    LCD_V_RES * m_scale, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
        if (m_window == nullptr) {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            shutdown();
            return false;
        }

        m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
        if (m_renderer == nullptr) {
            m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (m_renderer == nullptr) {
            fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
            shutdown();
            return false;
        }

        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB565,
                                      SDL_TEXTUREACCESS_STREAMING, LCD_H_RES, LCD_V_RES);
        if (m_texture == nullptr) {
            fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
            shutdown();
            return false;
        }

        if (SDL_RenderSetLogicalSize(m_renderer, LCD_H_RES, LCD_V_RES) != 0) {
            fprintf(stderr, "SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
            shutdown();
            return false;
        }
        return true;
    }

    void handle_events(uint8_t &keys_held, bool &quit) {
        quit = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            } else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_q &&
                       (event.key.keysym.mod & KMOD_CTRL) != 0) {
                quit = true;
            } else if (event.type == SDL_WINDOWEVENT &&
                       (event.window.event == SDL_WINDOWEVENT_EXPOSED ||
                        event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                m_needs_redraw = true;
            }
        }

        const uint8_t *state = SDL_GetKeyboardState(nullptr);
        m_held_keys = 0;
        if (state[SDL_SCANCODE_UP] || state[SDL_SCANCODE_W]) {
            m_held_keys |= KEY_UP;
        }
        if (state[SDL_SCANCODE_DOWN] || state[SDL_SCANCODE_S]) {
            m_held_keys |= KEY_DOWN;
        }
        if (state[SDL_SCANCODE_LEFT] || state[SDL_SCANCODE_A]) {
            m_held_keys |= KEY_LEFT;
        }
        if (state[SDL_SCANCODE_RIGHT] || state[SDL_SCANCODE_D]) {
            m_held_keys |= KEY_RIGHT;
        }
        if (state[SDL_SCANCODE_RETURN] || state[SDL_SCANCODE_KP_ENTER] ||
            state[SDL_SCANCODE_SPACE]) {
            m_held_keys |= KEY_OK;
        }
        if (state[SDL_SCANCODE_BACKSPACE] || state[SDL_SCANCODE_ESCAPE]) {
            m_held_keys |= KEY_BACK;
        }
        keys_held = m_held_keys;
    }

    void render() {
        if (m_texture == nullptr || m_renderer == nullptr) {
            return;
        }

        void *pixels = nullptr;
        int pitch = 0;
        if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) != 0) {
            fprintf(stderr, "SDL_LockTexture failed: %s\n", SDL_GetError());
            return;
        }
        const bool is_updated =
            Display::instance().copy_pixels_if_dirty(pixels, static_cast<size_t>(pitch));
        SDL_UnlockTexture(m_texture);

        if (!is_updated && !m_needs_redraw) {
            return;
        }
        SDL_RenderClear(m_renderer);
        SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
        SDL_RenderPresent(m_renderer);
        m_needs_redraw = false;
    }

    void shutdown() {
        if (m_texture != nullptr) {
            SDL_DestroyTexture(m_texture);
            m_texture = nullptr;
        }
        if (m_renderer != nullptr) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        if (m_window != nullptr) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        if (m_is_sdl_initialized) {
            SDL_Quit();
            m_is_sdl_initialized = false;
        }
    }

    uint8_t held_keys() const { return m_held_keys; }

    static constexpr uint8_t KEY_UP = 1 << 0;
    static constexpr uint8_t KEY_DOWN = 1 << 1;
    static constexpr uint8_t KEY_LEFT = 1 << 2;
    static constexpr uint8_t KEY_RIGHT = 1 << 3;
    static constexpr uint8_t KEY_OK = 1 << 4;
    static constexpr uint8_t KEY_BACK = 1 << 5;

private:
    SDL_Window *m_window = nullptr;
    SDL_Renderer *m_renderer = nullptr;
    SDL_Texture *m_texture = nullptr;
    int m_scale;
    uint8_t m_held_keys = 0;
    bool m_needs_redraw = true;
    bool m_is_sdl_initialized = false;
};

}  // namespace hle
