#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace hle {

static constexpr int LCD_H_RES = 240;
static constexpr int LCD_V_RES = 320;

class Display {
public:
    void draw_bitmap(int x0, int y0, int x1, int y1, const uint16_t *data) {
        const int width = x1 - x0;
        const int height = y1 - y0;
        if (data == nullptr || width <= 0 || height <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (int row = 0; row < height; ++row) {
            const int y = y0 + row;
            if (y < 0 || y >= LCD_V_RES) {
                continue;
            }
            for (int col = 0; col < width; ++col) {
                const int x = x0 + col;
                if (x >= 0 && x < LCD_H_RES) {
                    m_pixels[static_cast<size_t>(y * LCD_H_RES + x)] =
                        data[static_cast<size_t>(row * width + col)];
                }
            }
        }
        m_dirty = true;
    }

    void draw_bitmap_strided(int x0, int y0, int x1, int y1, const uint8_t *data,
                             size_t src_stride_bytes) {
        const int width = x1 - x0;
        const int height = y1 - y0;
        if (data == nullptr || width <= 0 || height <= 0) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        for (int row = 0; row < height; ++row) {
            const int y = y0 + row;
            if (y < 0 || y >= LCD_V_RES) {
                continue;
            }

            const auto *src_row =
                reinterpret_cast<const uint16_t *>(data + (row * src_stride_bytes));
            for (int col = 0; col < width; ++col) {
                const int x = x0 + col;
                if (x >= 0 && x < LCD_H_RES) {
                    m_pixels[static_cast<size_t>(y * LCD_H_RES + x)] = src_row[col];
                }
            }
        }
        m_dirty = true;
    }

    void fill_screen(uint16_t color) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pixels.fill(color);
        m_dirty = true;
    }

    bool copy_pixels_if_dirty(void *out_pixels, size_t out_stride_bytes) {
        constexpr size_t row_bytes = LCD_H_RES * sizeof(uint16_t);
        if (out_pixels == nullptr || out_stride_bytes < row_bytes) {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_dirty) {
            return false;
        }

        auto *dst = static_cast<uint8_t *>(out_pixels);
        const auto *src = reinterpret_cast<const uint8_t *>(m_pixels.data());
        for (int y = 0; y < LCD_V_RES; ++y) {
            memcpy(dst + (static_cast<size_t>(y) * out_stride_bytes),
                   src + (static_cast<size_t>(y) * row_bytes), row_bytes);
        }
        m_dirty = false;
        return true;
    }

    int width() const { return LCD_H_RES; }
    int height() const { return LCD_V_RES; }

    bool save_ppm(const char *path) {
        if (path == nullptr) {
            return false;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        FILE *file = fopen(path, "wb");
        if (file == nullptr) {
            return false;
        }

        fprintf(file, "P6\n%d %d\n255\n", LCD_H_RES, LCD_V_RES);
        for (const uint16_t pixel : m_pixels) {
            const uint8_t red = static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
            const uint8_t green = static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
            const uint8_t blue = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
            fputc(red, file);
            fputc(green, file);
            fputc(blue, file);
        }

        const bool is_write_ok = ferror(file) == 0;
        const bool is_close_ok = fclose(file) == 0;
        return is_write_ok && is_close_ok;
    }

    static Display &instance() {
        static Display s_instance;
        return s_instance;
    }

private:
    std::array<uint16_t, LCD_H_RES * LCD_V_RES> m_pixels{};
    bool m_dirty = true;
    std::mutex m_mutex;
};

}  // namespace hle
