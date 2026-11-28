// Pictures read from files (PNG, JPEG and the other formats stb_image reads) as RGBA8, and RGBA8 written as PNG.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rexfabric::ui {

// RGBA8, rows top first, tightly packed; empty when it can't be read.
std::vector<uint8_t> DecodeImageRGBA(const uint8_t* data, size_t size, int& width, int& height);

// A PNG file's bytes of RGBA8 pixels laid out as DecodeImageRGBA gives them; empty when it can't be made.
std::vector<uint8_t> EncodePngRGBA(const uint8_t* rgba, int width, int height);

}  // namespace rexfabric::ui
