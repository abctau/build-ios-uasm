#include "image_core.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#include "stb/stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb/stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb/stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace tui {

namespace {

// ---------------------------------------------------------------------------
// median-cut palette extraction (absorbed from tui-color-thief)
// ---------------------------------------------------------------------------

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

bool ExtractPalette(const uint8_t* data, size_t size, int colorCount, PaletteResult& out) {
	int width = 0;
	int height = 0;
	int comp = 0;
	uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height,
	                                        &comp, 4);
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

// ---------------------------------------------------------------------------
// encode helpers
// ---------------------------------------------------------------------------

void AppendToVector(void* context, void* data, int size) {
	std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	out->insert(out->end(), bytes, bytes + size);
}

// JPEG has no alpha channel: composite non-opaque pixels onto white.
void FlattenAlphaToWhite(ImageRGBA& image) {
	const size_t total =
	    static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4;
	for (size_t i = 0; i < total; i += 4) {
		const int a = image.pixels[i + 3];
		if (a < 255) {
			for (int c = 0; c < 3; c++) {
				const int v = image.pixels[i + c];
				image.pixels[i + c] =
				    static_cast<uint8_t>((v * a + 255 * (255 - a)) / 255);
			}
			image.pixels[i + 3] = 255;
		}
	}
}

bool Encode(const ImageRGBA& image, const EncodeOptions& opt, std::vector<uint8_t>& out) {
	if (image.width <= 0 || image.height <= 0 ||
	    image.pixels.size() !=
	        static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4) {
		return false;
	}
	out.clear();
	if (opt.format == OutFormat::JPEG) {
		ImageRGBA flat = image;
		FlattenAlphaToWhite(flat);
		return stbi_write_jpg_to_func(AppendToVector, &out, flat.width, flat.height, 4,
		                              flat.pixels.data(),
	                              std::min(std::max(opt.quality, 1), 100)) != 0;
	}
	return stbi_write_png_to_func(AppendToVector, &out, image.width, image.height, 4,
	                              image.pixels.data(), image.width * 4) != 0;
}

EncodeOptions NormalizeOptions(const EncodeOptions& opt) {
	EncodeOptions result = opt;
	result.quality = std::min(std::max(result.quality, 1), 100);
	return result;
}

}  // namespace

bool ImageInfoFromMemory(const uint8_t* data, size_t size, ImageInfo& out) {
	if (data == nullptr || size == 0) return false;
	int w = 0;
	int h = 0;
	int comp = 0;
	if (stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &comp) == 0) {
		return false;
	}
	out.width = w;
	out.height = h;
	out.channels = comp;
	return w > 0 && h > 0;
}

bool DecodeToRGBA(const uint8_t* data, size_t size, ImageRGBA& out) {
	if (data == nullptr || size == 0) return false;
	int w = 0;
	int h = 0;
	int comp = 0;
	uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 4);
	if (pixels == nullptr || w <= 0 || h <= 0) {
		if (pixels != nullptr) stbi_image_free(pixels);
		return false;
	}
	out.width = w;
	out.height = h;
	out.pixels.assign(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
	stbi_image_free(pixels);
	return true;
}

bool EncodeRGBA(const ImageRGBA& image, const EncodeOptions& opt, std::vector<uint8_t>& out) {
	return Encode(image, NormalizeOptions(opt), out);
}

bool ResizeImage(const uint8_t* data, size_t size, int targetWidth, int targetHeight,
                 const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	int tw = targetWidth;
	int th = targetHeight;
	if (tw <= 0 && th <= 0) return false;
	if (tw <= 0) {
		tw = static_cast<int>(static_cast<long long>(src.width) * th / src.height);
	} else if (th <= 0) {
		th = static_cast<int>(static_cast<long long>(src.height) * tw / src.width);
	}
	tw = std::max(tw, 1);
	th = std::max(th, 1);

	ImageRGBA dst;
	dst.width = tw;
	dst.height = th;
	dst.pixels.resize(static_cast<size_t>(tw) * static_cast<size_t>(th) * 4);
	if (stbir_resize_uint8_srgb(src.pixels.data(), src.width, src.height, src.width * 4,
	                            dst.pixels.data(), tw, th, tw * 4, STBIR_RGBA) == nullptr) {
		return false;
	}
	return Encode(dst, NormalizeOptions(opt), out);
}

bool CropImage(const uint8_t* data, size_t size, int x, int y, int w, int h,
               const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	w = std::min(std::max(w, 1), src.width);
	h = std::min(std::max(h, 1), src.height);
	x = std::min(std::max(x, 0), src.width - w);
	y = std::min(std::max(y, 0), src.height - h);

	ImageRGBA dst;
	dst.width = w;
	dst.height = h;
	dst.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
	for (int row = 0; row < h; row++) {
		const uint8_t* srcRow =
		    src.pixels.data() + (static_cast<size_t>(y + row) * src.width + x) * 4;
		std::memcpy(dst.pixels.data() + static_cast<size_t>(row) * w * 4, srcRow,
		            static_cast<size_t>(w) * 4);
	}
	return Encode(dst, NormalizeOptions(opt), out);
}

bool RotateImage(const uint8_t* data, size_t size, int degrees, const EncodeOptions& opt,
                 std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const int deg = ((degrees % 360) + 360) % 360;
	if (deg != 0 && deg != 90 && deg != 180 && deg != 270) return false;

	if (deg == 0) {
		return Encode(src, NormalizeOptions(opt), out);
	}

	const int w = src.width;
	const int h = src.height;
	ImageRGBA dst;
	dst.width = (deg == 180) ? w : h;
	dst.height = (deg == 180) ? h : w;
	dst.pixels.resize(static_cast<size_t>(dst.width) * static_cast<size_t>(dst.height) * 4);

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const uint8_t* p =
			    src.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
			int nx = 0;
			int ny = 0;
			if (deg == 90) {
				nx = h - 1 - y;
				ny = x;
			} else if (deg == 180) {
				nx = w - 1 - x;
				ny = h - 1 - y;
			} else {
				nx = y;
				ny = w - 1 - x;
			}
			uint8_t* q = dst.pixels.data() +
			             (static_cast<size_t>(ny) * dst.width + nx) * 4;
			q[0] = p[0];
			q[1] = p[1];
			q[2] = p[2];
			q[3] = p[3];
		}
	}
	return Encode(dst, NormalizeOptions(opt), out);
}

bool ConvertImage(const uint8_t* data, size_t size, const EncodeOptions& opt,
                  std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;
	return Encode(src, NormalizeOptions(opt), out);
}

bool ExtractPaletteFromMemory(const uint8_t* data, size_t size, int colorCount,
                              PaletteResult& out) {
	if (data == nullptr || size == 0) return false;
	if (colorCount < 2) colorCount = 2;
	if (colorCount > 16) colorCount = 16;
	return ExtractPalette(data, size, colorCount, out);
}

}  // namespace tui
