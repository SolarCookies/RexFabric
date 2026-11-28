#include "image_decode.h"

#include <climits>

// Static, so it can sit beside another copy of stb_image in the same program.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#include <stb_image.h>

// The same for the writer, RexFabric's own copy (thirdparty/stb), which content/textures.cpp has a copy of too.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb/stb_image_write.h>

namespace rexfabric::ui {

std::vector<uint8_t> DecodeImageRGBA(const uint8_t* data, size_t size, int& width, int& height) {
  width = 0;
  height = 0;
  if (!data || size == 0 || size > static_cast<size_t>(INT_MAX)) return {};
  int channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
  if (!pixels) {
    width = 0;
    height = 0;
    return {};
  }
  std::vector<uint8_t> rgba(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  stbi_image_free(pixels);
  return rgba;
}

std::vector<uint8_t> EncodePngRGBA(const uint8_t* rgba, int width, int height) {
  if (!rgba || width <= 0 || height <= 0) return {};
  std::vector<uint8_t> png;
  const auto write = [](void* context, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(context);
    const auto* bytes = static_cast<const uint8_t*>(data);
    out->insert(out->end(), bytes, bytes + size);
  };
  if (stbi_write_png_to_func(write, &png, width, height, 4, rgba, width * 4) == 0) return {};
  return png;
}

}  // namespace rexfabric::ui
