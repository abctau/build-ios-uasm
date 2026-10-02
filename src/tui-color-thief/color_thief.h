// tui-color-thief core: image decode (stb_image) + median-cut quantization + edge sampling.
// Shared by the N-API binding (App) and the embind binding (Web / mini programs).

#pragma once

#include <cstdint>
#include <vector>

namespace tui {

struct RGB {
  int r;
  int g;
  int b;
};

struct PaletteResult {
  int width;
  int height;
  RGB dominant;               // palette[0], the bucket with the most pixels
  std::vector<RGB> palette;   // dominant first, sorted by pixel count desc
  RGB edges[4];               // top, right, bottom, left
};

// Decode an image from memory (jpg/png/bmp/gif/webp/...) and extract the palette.
// colorCount: number of palette buckets, clamped to [2, 16]. Returns false on decode failure.
bool ExtractPaletteFromMemory(const uint8_t* data, int size, int colorCount, PaletteResult& out);

// Same as above but reads the image file at |path| (App platforms only, plain fopen).
bool ExtractPaletteFromFile(const char* path, int colorCount, PaletteResult& out);

}  // namespace tui
