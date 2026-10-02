#include "color_thief.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#include "stb/stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tui {

namespace {

struct Sample {
  uint8_t r;
  uint8_t g;
  uint8_t b;
};

constexpr int kMaxSamples = 32768;

void ComputeBounds(const std::vector<Sample>& samples, size_t begin, size_t end,
                   int (&mins)[3], int (&maxs)[3]) {
  mins[0] = mins[1] = mins[2] = 255;
  maxs[0] = maxs[1] = maxs[2] = 0;
  for (size_t i = begin; i < end; i++) {
    const Sample& s = samples[i];
    const int v[3] = {s.r, s.g, s.b};
    for (int c = 0; c < 3; c++) {
      if (v[c] < mins[c]) mins[c] = v[c];
      if (v[c] > maxs[c]) maxs[c] = v[c];
    }
  }
}

RGB AverageColor(const std::vector<Sample>& samples, size_t begin, size_t end) {
  long long sum[3] = {0, 0, 0};
  const size_t n = end - begin;
  for (size_t i = begin; i < end; i++) {
    sum[0] += samples[i].r;
    sum[1] += samples[i].g;
    sum[2] += samples[i].b;
  }
  RGB c;
  c.r = n > 0 ? static_cast<int>(sum[0] / static_cast<long long>(n)) : 0;
  c.g = n > 0 ? static_cast<int>(sum[1] / static_cast<long long>(n)) : 0;
  c.b = n > 0 ? static_cast<int>(sum[2] / static_cast<long long>(n)) : 0;
  return c;
}

// One median-cut step: split the bucket with the largest pixel population along
// its widest color axis. Recursion depth ceil(log2(colorCount)) yields up to
// 2^depth buckets, matching the classic Leptonica median-cut used by color-thief.
void MedianCut(std::vector<Sample>& samples, size_t begin, size_t end,
               std::vector<std::pair<RGB, size_t>>& buckets, int depth) {
  if (end <= begin) return;

  int mins[3];
  int maxs[3];
  ComputeBounds(samples, begin, end, mins, maxs);

  const size_t count = end - begin;
  int axis = -1;
  int widest = -1;
  for (int c = 0; c < 3; c++) {
    const int range = maxs[c] - mins[c];
    if (range > widest) {
      widest = range;
      axis = c;
    }
  }

  if (depth <= 0 || count < 2 || widest <= 0) {
    buckets.push_back(std::make_pair(AverageColor(samples, begin, end), count));
    return;
  }

  std::sort(samples.begin() + static_cast<long>(begin),
            samples.begin() + static_cast<long>(end),
            [axis](const Sample& a, const Sample& b) {
              const uint8_t av[3] = {a.r, a.g, a.b};
              const uint8_t bv[3] = {b.r, b.g, b.b};
              return av[axis] < bv[axis];
            });

  size_t mid = begin + count / 2;
  MedianCut(samples, begin, mid, buckets, depth - 1);
  MedianCut(samples, mid, end, buckets, depth - 1);
}

RGB EdgeAverage(const uint8_t* pixels, int width, int height, int comp, int edge) {
  const int band = std::min(std::max(1, std::min(width, height) / 20),
                            std::min(std::max(width / 2, 1), std::max(height / 2, 1)));
  long long sum[3] = {0, 0, 0};
  long long n = 0;

  auto accumulate = [&](int x, int y) {
    const uint8_t* p = pixels + (static_cast<size_t>(y) * width + x) * comp;
    sum[0] += p[0];
    sum[1] += p[1];
    sum[2] += p[2];
    n++;
  };

  for (int i = 0; i < band; i++) {
    if (edge == 0) {  // top
      for (int x = 0; x < width; x++) accumulate(x, i);
    } else if (edge == 1) {  // right
      for (int y = 0; y < height; y++) accumulate(width - 1 - i, y);
    } else if (edge == 2) {  // bottom
      for (int x = 0; x < width; x++) accumulate(x, height - 1 - i);
    } else {  // left
      for (int y = 0; y < height; y++) accumulate(i, y);
    }
  }

  RGB c;
  c.r = n > 0 ? static_cast<int>(sum[0] / n) : 0;
  c.g = n > 0 ? static_cast<int>(sum[1] / n) : 0;
  c.b = n > 0 ? static_cast<int>(sum[2] / n) : 0;
  return c;
}

bool Extract(const uint8_t* data, int size, int colorCount, PaletteResult& out) {
  int width = 0;
  int height = 0;
  int comp = 0;
  // Force 4 channels so fully transparent pixels can be skipped instead of
  // dragging the palette toward black.
  uint8_t* pixels = stbi_load_from_memory(data, size, &width, &height, &comp, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    if (pixels != nullptr) stbi_image_free(pixels);
    return false;
  }

  std::vector<Sample> samples;
  const size_t total = static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t stride = total > kMaxSamples ? total / kMaxSamples + 1 : 1;
  samples.reserve(std::min(total, static_cast<size_t>(kMaxSamples)));
  for (size_t i = 0; i < total; i += stride) {
    const uint8_t* p = pixels + i * 4;
    if (p[3] == 0) continue;  // skip fully transparent pixels
    Sample s;
    s.r = p[0];
    s.g = p[1];
    s.b = p[2];
    samples.push_back(s);
  }

  if (samples.empty()) {
    stbi_image_free(pixels);
    return false;
  }

  int depth = 1;
  while ((1 << depth) < colorCount) depth++;

  std::vector<std::pair<RGB, size_t>> buckets;
  MedianCut(samples, 0, samples.size(), buckets, depth);

  std::sort(buckets.begin(), buckets.end(),
            [](const std::pair<RGB, size_t>& a, const std::pair<RGB, size_t>& b) {
              return a.second > b.second;
            });

  out.width = width;
  out.height = height;
  out.palette.clear();
  for (size_t i = 0; i < buckets.size(); i++) {
    out.palette.push_back(buckets[i].first);
  }
  out.dominant = out.palette.empty() ? RGB{0, 0, 0} : out.palette[0];
  for (int e = 0; e < 4; e++) {
    out.edges[e] = EdgeAverage(pixels, width, height, 4, e);
  }

  stbi_image_free(pixels);
  return true;
}

}  // namespace

bool ExtractPaletteFromMemory(const uint8_t* data, int size, int colorCount, PaletteResult& out) {
  if (data == nullptr || size <= 0) return false;
  if (colorCount < 2) colorCount = 2;
  if (colorCount > 16) colorCount = 16;
  return Extract(data, size, colorCount, out);
}

bool ExtractPaletteFromFile(const char* path, int colorCount, PaletteResult& out) {
  if (path == nullptr) return false;
  if (colorCount < 2) colorCount = 2;
  if (colorCount > 16) colorCount = 16;
  FILE* f = fopen(path, "rb");
  if (f == nullptr) return false;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  if (size <= 0) {
    fclose(f);
    return false;
  }
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> buffer(static_cast<size_t>(size));
  const size_t read = fread(buffer.data(), 1, buffer.size(), f);
  fclose(f);
  if (read <= 0) return false;
  return Extract(buffer.data(), static_cast<int>(read), colorCount, out);
}

}  // namespace tui
