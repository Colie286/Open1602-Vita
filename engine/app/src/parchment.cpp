// SPDX-License-Identifier: GPL-3.0-or-later
#include "parchment.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace o1602::app {

namespace {

/// A grid of random values, read smoothly between its points.
class Noise {
public:
    Noise(std::int32_t width, std::int32_t height, std::int32_t cell, std::uint32_t& state)
        : m_cell(cell),
          m_w(width / cell + 2),
          m_h(height / cell + 2),
          m_values(static_cast<std::size_t>(m_w) * static_cast<std::size_t>(m_h)) {
        for (float& v : m_values) {
            state = state * 1664525U + 1013904223U;  // the LCG of Numerical Recipes
            v = static_cast<float>(state >> 24) / 255.0F;
        }
    }

    [[nodiscard]] float at(std::int32_t x, std::int32_t y) const {
        const float fx = static_cast<float>(x) / static_cast<float>(m_cell);
        const float fy = static_cast<float>(y) / static_cast<float>(m_cell);
        const auto ix = static_cast<std::int32_t>(fx);
        const auto iy = static_cast<std::int32_t>(fy);
        const float tx = smooth(fx - static_cast<float>(ix));
        const float ty = smooth(fy - static_cast<float>(iy));
        const auto v = [&](std::int32_t gx, std::int32_t gy) {
            return m_values[static_cast<std::size_t>(std::min(gy, m_h - 1)) * static_cast<std::size_t>(m_w) +
                            static_cast<std::size_t>(std::min(gx, m_w - 1))];
        };
        const float top = v(ix, iy) + (v(ix + 1, iy) - v(ix, iy)) * tx;
        const float bottom = v(ix, iy + 1) + (v(ix + 1, iy + 1) - v(ix, iy + 1)) * tx;
        return top + (bottom - top) * ty;
    }

private:
    static float smooth(float t) { return t * t * (3.0F - 2.0F * t); }

    std::int32_t m_cell;
    std::int32_t m_w;
    std::int32_t m_h;
    std::vector<float> m_values;
};

struct Stop {
    float at;
    std::array<float, 3> rgb;
};
/// Dark to light sandstone, as the side panel.
constexpr std::array<Stop, 5> kStops = {{
    {0.0F, {150, 112, 70}},
    {90.0F, {190, 150, 100}},
    {130.0F, {208, 172, 120}},
    {170.0F, {222, 196, 150}},
    {255.0F, {236, 222, 190}},
}};

std::array<std::uint8_t, 3> colour(float v) {
    v = std::clamp(v, 0.0F, 255.0F);
    for (std::size_t i = 1; i < kStops.size(); ++i) {
        if (v <= kStops[i].at) {
            const float t = (v - kStops[i - 1].at) / (kStops[i].at - kStops[i - 1].at);
            std::array<std::uint8_t, 3> out{};
            for (std::size_t k = 0; k < 3; ++k) {
                out[k] =
                    static_cast<std::uint8_t>(kStops[i - 1].rgb[k] + (kStops[i].rgb[k] - kStops[i - 1].rgb[k]) * t);
            }
            return out;
        }
    }
    return {236, 222, 190};
}

}  // namespace

std::vector<std::uint8_t> parchment_pixels(std::int32_t width, std::int32_t height, std::uint32_t seed) {
    width = std::max(width, 1);
    height = std::max(height, 1);
    // Clouds of four sizes, large ones strongest.
    struct Layer {
        std::int32_t cell;
        float weight;
    };
    constexpr std::array<Layer, 4> kLayers = {{{64, 0.45F}, {24, 0.30F}, {8, 0.15F}, {2, 0.10F}}};
    std::uint32_t state = seed;
    std::vector<Noise> layers;
    for (const Layer& l : kLayers) {
        layers.emplace_back(width, height, l.cell, state);
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (std::int32_t y = 0; y < height; ++y) {
        for (std::int32_t x = 0; x < width; ++x) {
            float v = 0.0F;
            for (std::size_t i = 0; i < layers.size(); ++i) {
                v += layers[i].at(x, y) * 255.0F * kLayers[i].weight;
            }
            const std::array<std::uint8_t, 3> c = colour((v - 128.0F) * 0.9F + 168.0F);
            const std::size_t p =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
            out[p] = c[0];
            out[p + 1] = c[1];
            out[p + 2] = c[2];
            out[p + 3] = 255;
        }
    }
    return out;
}

}  // namespace o1602::app
