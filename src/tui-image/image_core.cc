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

// ---------------------------------------------------------------------------
// pixel primitives
// ---------------------------------------------------------------------------

// Bilinear sample; out-of-bounds contributes zero (transparent black).
void SampleBilinear(const ImageRGBA& img, float sx, float sy, uint8_t out[4]) {
	const int x0 = static_cast<int>(std::floor(sx));
	const int y0 = static_cast<int>(std::floor(sy));
	const float fx = sx - static_cast<float>(x0);
	const float fy = sy - static_cast<float>(y0);
	float acc[4] = {0.f, 0.f, 0.f, 0.f};
	const int xs[2] = {x0, x0 + 1};
	const int ys[2] = {y0, y0 + 1};
	const float wx[2] = {1.f - fx, fx};
	const float wy[2] = {1.f - fy, fy};
	for (int j = 0; j < 2; j++) {
		for (int i = 0; i < 2; i++) {
			const float wgt = wx[i] * wy[j];
			if (wgt <= 0.f) continue;
			const int cx = xs[i];
			const int cy = ys[j];
			if (cx < 0 || cy < 0 || cx >= img.width || cy >= img.height) continue;
			const uint8_t* p = img.pixels.data() +
			                   (static_cast<size_t>(cy) * img.width + cx) * 4;
			for (int ch = 0; ch < 4; ch++) acc[ch] += wgt * p[ch];
		}
	}
	for (int ch = 0; ch < 4; ch++) {
		out[ch] = static_cast<uint8_t>(
		    std::min(std::max(acc[ch] + 0.5f, 0.f), 255.f));
	}
}

// One sliding-window box-blur pass along one axis over all 4 channels
// (straight alpha). Edge pixels are clamped.
void BoxBlurPassAxis(uint8_t* img, int w, int h, int radius, bool horizontal) {
	if (radius <= 0) return;
	const int lineLen = horizontal ? w : h;
	const int lineCount = horizontal ? h : w;
	const int step = horizontal ? 4 : w * 4;
	const int lineStride = horizontal ? w * 4 : 4;
	const int win = radius * 2 + 1;

	std::vector<uint8_t> temp(static_cast<size_t>(lineLen) * 4);
	for (int l = 0; l < lineCount; l++) {
		uint8_t* base = img + static_cast<size_t>(l) * lineStride;
		std::memcpy(temp.data(), base, static_cast<size_t>(lineLen) * 4);

		auto px = [&](int idx) -> const uint8_t* {
			const int c = idx < 0 ? 0 : (idx >= lineLen ? lineLen - 1 : idx);
			return temp.data() + static_cast<size_t>(c) * 4;
		};

		int acc[4] = {0, 0, 0, 0};
		for (int i = -radius; i <= radius; i++) {
			const uint8_t* p = px(i);
			for (int ch = 0; ch < 4; ch++) acc[ch] += p[ch];
		}
		for (int i = 0; i < lineLen; i++) {
			uint8_t* q = base + static_cast<size_t>(i) * step;
			for (int ch = 0; ch < 4; ch++) {
				q[ch] = static_cast<uint8_t>((acc[ch] + win / 2) / win);
			}
			const uint8_t* rm = px(i - radius);
			const uint8_t* ad = px(i + radius + 1);
			for (int ch = 0; ch < 4; ch++) acc[ch] += ad[ch] - rm[ch];
		}
	}
}

// 3 alternating passes approximate a gaussian; radius clamped to half the
// smaller dimension so the window stays meaningful.
void BoxBlurRGBA(ImageRGBA& img, int radius) {
	if (radius <= 0) return;
	const int r = std::min(radius, std::max(std::min(img.width, img.height) / 2, 1));
	for (int i = 0; i < 3; i++) {
		BoxBlurPassAxis(img.pixels.data(), img.width, img.height, r, true);
		BoxBlurPassAxis(img.pixels.data(), img.width, img.height, r, false);
	}
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

	const int w = src.width;
	const int h = src.height;

	if (deg % 90 == 0) {
		if (deg == 0) {
			return Encode(src, NormalizeOptions(opt), out);
		}
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

	// Arbitrary angle: expand the canvas and inverse-map every destination
	// pixel through a clockwise rotation with bilinear sampling. The rotation
	// matrix for screen coordinates (y down, clockwise) is [c -s; s c].
	const double rad = deg * 3.14159265358979323846 / 180.0;
	const double c = std::cos(rad);
	const double s = std::sin(rad);
	int nw = static_cast<int>(std::ceil(w * std::fabs(c) + h * std::fabs(s)));
	int nh = static_cast<int>(std::ceil(w * std::fabs(s) + h * std::fabs(c)));
	nw = std::max(nw, 1);
	nh = std::max(nh, 1);

	ImageRGBA dst;
	dst.width = nw;
	dst.height = nh;
	dst.pixels.assign(static_cast<size_t>(nw) * static_cast<size_t>(nh) * 4, 0);

	const double cx = (w - 1) / 2.0;
	const double cy = (h - 1) / 2.0;
	const double ncx = (nw - 1) / 2.0;
	const double ncy = (nh - 1) / 2.0;

	for (int dy = 0; dy < nh; dy++) {
		for (int dx = 0; dx < nw; dx++) {
			const double ddx = dx - ncx;
			const double ddy = dy - ncy;
			const double sx = c * ddx + s * ddy + cx;
			const double sy = -s * ddx + c * ddy + cy;
			uint8_t q[4];
			SampleBilinear(src, static_cast<float>(sx), static_cast<float>(sy), q);
			uint8_t* p = dst.pixels.data() +
			             (static_cast<size_t>(dy) * nw + dx) * 4;
			p[0] = q[0];
			p[1] = q[1];
			p[2] = q[2];
			p[3] = q[3];
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

bool FlipImage(const uint8_t* data, size_t size, bool horizontal, bool vertical,
               const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const int w = src.width;
	const int h = src.height;
	ImageRGBA dst;
	dst.width = w;
	dst.height = h;
	dst.pixels.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			const int sx = horizontal ? w - 1 - x : x;
			const int sy = vertical ? h - 1 - y : y;
			const uint8_t* p =
			    src.pixels.data() + (static_cast<size_t>(sy) * w + sx) * 4;
			uint8_t* q = dst.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
			q[0] = p[0];
			q[1] = p[1];
			q[2] = p[2];
			q[3] = p[3];
		}
	}
	return Encode(dst, NormalizeOptions(opt), out);
}

bool BlurImage(const uint8_t* data, size_t size, int radius,
               const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;
	BoxBlurRGBA(src, radius);
	return Encode(src, NormalizeOptions(opt), out);
}

bool RoundCornersImage(const uint8_t* data, size_t size, int radius,
                       const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const int w = src.width;
	const int h = src.height;
	const float r = static_cast<float>(std::min(std::max(radius, 0), std::min(w, h) / 2));

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t* p = src.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
			if (p[3] == 0) continue;
			// Signed distance to the rounded rectangle: clamp the pixel center
			// into the inner rect; the distance to it is 0 inside, corner arc
			// radius outside.
			const float px = x + 0.5f;
			const float py = y + 0.5f;
			const float qx = std::min(std::max(px, r), static_cast<float>(w) - r);
			const float qy = std::min(std::max(py, r), static_cast<float>(h) - r);
			const float dx = px - qx;
			const float dy = py - qy;
			const float dist = std::sqrt(dx * dx + dy * dy);
			const float f = std::min(std::max(r - dist + 0.5f, 0.f), 1.f);
			if (f < 1.f) {
				p[3] = static_cast<uint8_t>(p[3] * f + 0.5f);
			}
		}
	}
	return Encode(src, NormalizeOptions(opt), out);
}

bool CircleClipImage(const uint8_t* data, size_t size,
                     const EncodeOptions& opt, std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const int s = std::min(src.width, src.height);
	const int x0 = (src.width - s) / 2;
	const int y0 = (src.height - s) / 2;
	const float half = s / 2.0f;

	ImageRGBA dst;
	dst.width = s;
	dst.height = s;
	dst.pixels.resize(static_cast<size_t>(s) * static_cast<size_t>(s) * 4);

	for (int y = 0; y < s; y++) {
		for (int x = 0; x < s; x++) {
			const uint8_t* p = src.pixels.data() +
			    (static_cast<size_t>(y0 + y) * src.width + x0 + x) * 4;
			uint8_t* q = dst.pixels.data() + (static_cast<size_t>(y) * s + x) * 4;
			const float dx = x + 0.5f - half;
			const float dy = y + 0.5f - half;
			const float dist = std::sqrt(dx * dx + dy * dy);
			const float f = std::min(std::max(half - dist + 0.5f, 0.f), 1.f);
			q[0] = p[0];
			q[1] = p[1];
			q[2] = p[2];
			q[3] = static_cast<uint8_t>(p[3] * f + 0.5f);
		}
	}
	return Encode(dst, NormalizeOptions(opt), out);
}

bool ExtendFillImage(const uint8_t* data, size_t size, const ExtendFillOptions& opts,
                     std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const int sw = src.width;
	const int sh = src.height;
	const int tw = std::max(opts.targetWidth, 1);
	const int th = std::max(opts.targetHeight, 1);

	// Source placement: centered on the fixed axis, centerRatio-driven on the
	// extend axis, clamped so the source stays fully inside the canvas.
	int ox;
	int oy;
	const auto placeAxis = [](int target, int srcLen, float ratio) {
		if (target <= srcLen) return 0;
		int pos = static_cast<int>(ratio * target - srcLen / 2.0f + 0.5f);
		return std::min(std::max(pos, 0), target - srcLen);
	};
	if (opts.direction == 1) {  // horizontal: offset along X
		ox = placeAxis(tw, sw, std::min(std::max(opts.centerRatio, 0.f), 1.f));
		oy = (th - sh) / 2;
	} else {  // vertical: offset along Y
		oy = placeAxis(th, sh, std::min(std::max(opts.centerRatio, 0.f), 1.f));
		ox = (tw - sw) / 2;
	}
	ox = std::min(std::max(ox, 0), std::max(tw - sw, 0));
	oy = std::min(std::max(oy, 0), std::max(th - sh, 0));

	ImageRGBA dst;
	dst.width = tw;
	dst.height = th;
	dst.pixels.assign(static_cast<size_t>(tw) * static_cast<size_t>(th) * 4, 0);

	if (opts.mode == ExtendFillMode::BLUR) {
		// Cover-fill the canvas with a scaled copy of the source, blurred.
		const float scale = std::max(static_cast<float>(tw) / sw,
		                             static_cast<float>(th) / sh);
		const int cw = std::max(static_cast<int>(sw * scale + 0.5f), tw);
		const int ch = std::max(static_cast<int>(sh * scale + 0.5f), th);
		ImageRGBA cover;
		cover.width = cw;
		cover.height = ch;
		cover.pixels.resize(static_cast<size_t>(cw) * static_cast<size_t>(ch) * 4);
		if (stbir_resize_uint8_srgb(src.pixels.data(), sw, sh, sw * 4,
		                            cover.pixels.data(), cw, ch, cw * 4,
		                            STBIR_RGBA) == nullptr) {
			return false;
		}
		const int cx0 = (cw - tw) / 2;
		const int cy0 = (ch - th) / 2;
		for (int y = 0; y < th; y++) {
			std::memcpy(dst.pixels.data() + static_cast<size_t>(y) * tw * 4,
			            cover.pixels.data() +
			                (static_cast<size_t>(cy0 + y) * cw + cx0) * 4,
			            static_cast<size_t>(tw) * 4);
		}
		BoxBlurRGBA(dst, std::min(std::max(opts.blurRadius, 0), std::min(tw, th) / 2));
	} else if (opts.mode == ExtendFillMode::COLOR) {
		uint8_t* px = dst.pixels.data();
		for (size_t i = 0; i < dst.pixels.size(); i += 4) {
			px[i] = static_cast<uint8_t>(std::min(std::max(opts.fillColor[0], 0), 255));
			px[i + 1] = static_cast<uint8_t>(std::min(std::max(opts.fillColor[1], 0), 255));
			px[i + 2] = static_cast<uint8_t>(std::min(std::max(opts.fillColor[2], 0), 255));
			px[i + 3] = 255;
		}
	}

	// Paste the source at its original size.
	for (int y = 0; y < sh; y++) {
		const int dy = oy + y;
		if (dy < 0 || dy >= th) continue;
		const int copyW = std::min(sw, tw - ox);
		if (copyW <= 0) continue;
		std::memcpy(dst.pixels.data() + (static_cast<size_t>(dy) * tw + ox) * 4,
		            src.pixels.data() + static_cast<size_t>(y) * sw * 4,
		            static_cast<size_t>(copyW) * 4);
	}

	// EDGE mode: stretch the pasted source's border pixels outward, first
	// horizontally per row, then vertically per column (corners resolve to the
	// source's corner pixel).
	if (opts.mode == ExtendFillMode::EDGE) {
		const int right = std::min(ox + sw, tw) - 1;
		const int bottom = std::min(oy + sh, th) - 1;
		for (int y = 0; y < th; y++) {
			uint8_t* row = dst.pixels.data() + static_cast<size_t>(y) * tw * 4;
			for (int x = 0; x < ox; x++) {
				std::memcpy(row + static_cast<size_t>(x) * 4, row + static_cast<size_t>(ox) * 4, 4);
			}
			for (int x = right + 1; x < tw; x++) {
				std::memcpy(row + static_cast<size_t>(x) * 4, row + static_cast<size_t>(right) * 4, 4);
			}
		}
		for (int x = 0; x < tw; x++) {
			for (int y = 0; y < oy; y++) {
				std::memcpy(dst.pixels.data() + (static_cast<size_t>(y) * tw + x) * 4,
				            dst.pixels.data() + (static_cast<size_t>(oy) * tw + x) * 4, 4);
			}
			for (int y = bottom + 1; y < th; y++) {
				std::memcpy(dst.pixels.data() + (static_cast<size_t>(y) * tw + x) * 4,
				            dst.pixels.data() + (static_cast<size_t>(bottom) * tw + x) * 4, 4);
			}
		}
	}

	return Encode(dst, NormalizeOptions(opts.encode), out);
}

bool EdgeBlurImage(const uint8_t* data, size_t size, const EdgeBlurOptions& opts,
                   std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	float r1 = opts.regions[0];
	float r2 = opts.regions[1];
	float r3 = opts.regions[2];
	const float sum = r1 + r2 + r3;
	if (sum <= 0.f || !std::isfinite(sum)) {
		r1 = 0.2f;
		r2 = 0.6f;
		r3 = 0.2f;
	} else {
		r1 = r1 / sum;
		r2 = r2 / sum;
		r3 = r3 / sum;
	}
	const float transition = std::min(std::max(opts.transition, 0.f), 0.5f);
	const float opacity = std::min(std::max(opts.overlayOpacity, 0.f), 1.f);

	ImageRGBA blurred = src;
	BoxBlurRGBA(blurred, std::max(opts.radius, 0));

	// Band overlay colors: per-band custom RGBA, or auto-extracted edge
	// average tinted by overlayOpacity.
	uint8_t bandColor[2][4];
	const int edges[2] = {opts.direction == 1 ? 3 : 0, opts.direction == 1 ? 1 : 2};
	for (int b = 0; b < 2; b++) {
		if (opts.overlayColors[b][3] >= 0) {
			for (int ch = 0; ch < 4; ch++) {
				bandColor[b][ch] = static_cast<uint8_t>(
				    std::min(std::max(opts.overlayColors[b][ch], 0), 255));
			}
		} else {
			const RGB e = EdgeAverage(src.pixels.data(), src.width, src.height, 4, edges[b]);
			bandColor[b][0] = static_cast<uint8_t>(e.r);
			bandColor[b][1] = static_cast<uint8_t>(e.g);
			bandColor[b][2] = static_cast<uint8_t>(e.b);
			bandColor[b][3] = static_cast<uint8_t>(opacity * 255.f + 0.5f);
		}
	}

	const int w = src.width;
	const int h = src.height;
	const bool vertical = opts.direction != 1;
	const float clearEnd = 1.f - r3;

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t* p = src.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
			const float t = vertical ? (y + 0.5f) / h : (x + 0.5f) / w;
			float m;
			if (t < r1 || t >= clearEnd) {
				m = 1.f;
			} else if (transition > 0.f && t < r1 + transition) {
				m = 1.f - (t - r1) / transition;
			} else if (transition > 0.f && t > clearEnd - transition) {
				m = (t - (clearEnd - transition)) / transition;
			} else {
				m = 0.f;
			}
			m = std::min(std::max(m, 0.f), 1.f);
			if (m > 0.f) {
				const uint8_t* b =
				    blurred.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
				for (int ch = 0; ch < 4; ch++) {
					p[ch] = static_cast<uint8_t>(p[ch] * (1.f - m) + b[ch] * m + 0.5f);
				}
			}
			if (m > 0.f) {
				const uint8_t* col = bandColor[t < 0.5f ? 0 : 1];
				const float fa = (col[3] / 255.f) * m;
				if (fa > 0.f) {
					for (int ch = 0; ch < 3; ch++) {
						p[ch] = static_cast<uint8_t>(
						    p[ch] * (1.f - fa) + col[ch] * fa + 0.5f);
					}
				}
			}
		}
	}
	return Encode(src, NormalizeOptions(opts.encode), out);
}

bool ProgressiveBlurImage(const uint8_t* data, size_t size, const ProgressiveBlurOptions& opts,
                          std::vector<uint8_t>& out) {
	ImageRGBA src;
	if (!DecodeToRGBA(data, size, src)) return false;

	const float offset = std::min(std::max(opts.offset, 0.f), 1.f);
	const float interp = std::min(std::max(opts.interpolation, 0.001f), 1.f);

	ImageRGBA blurred = src;
	BoxBlurRGBA(blurred, std::max(opts.radius, 0));

	const int w = src.width;
	const int h = src.height;
	const bool vertical = opts.direction <= 1;
	const bool towardEnd = (opts.direction == 0 || opts.direction == 2);  // down/right

	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			uint8_t* p = src.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
			float t = vertical ? (y + 0.5f) / h : (x + 0.5f) / w;
			if (!towardEnd) t = 1.f - t;
			const float m = std::min(std::max((t - offset) / interp, 0.f), 1.f);
			if (m > 0.f) {
				const uint8_t* b =
				    blurred.pixels.data() + (static_cast<size_t>(y) * w + x) * 4;
				for (int ch = 0; ch < 4; ch++) {
					p[ch] = static_cast<uint8_t>(p[ch] * (1.f - m) + b[ch] * m + 0.5f);
				}
			}
		}
	}
	return Encode(src, NormalizeOptions(opts.encode), out);
}

bool CompositeImage(const uint8_t* baseData, size_t baseSize,
                    const uint8_t* overlayData, size_t overlaySize,
                    int x, int y, int alpha, const EncodeOptions& opt,
                    std::vector<uint8_t>& out) {
	ImageRGBA base;
	ImageRGBA overlay;
	if (!DecodeToRGBA(baseData, baseSize, base)) return false;
	if (!DecodeToRGBA(overlayData, overlaySize, overlay)) return false;

	const float fa0 = std::min(std::max(alpha, 0), 100) / 100.0f;
	const int bw = base.width;
	const int bh = base.height;
	const int ow = overlay.width;
	const int oh = overlay.height;

	for (int oy = 0; oy < oh; oy++) {
		const int by = y + oy;
		if (by < 0 || by >= bh) continue;
		for (int ox = 0; ox < ow; ox++) {
			const int bx = x + ox;
			if (bx < 0 || bx >= bw) continue;
			const uint8_t* p =
			    overlay.pixels.data() + (static_cast<size_t>(oy) * ow + ox) * 4;
			uint8_t* q = base.pixels.data() + (static_cast<size_t>(by) * bw + bx) * 4;
			const float fa = (p[3] / 255.f) * fa0;
			for (int ch = 0; ch < 3; ch++) {
				q[ch] = static_cast<uint8_t>(q[ch] * (1.f - fa) + p[ch] * fa + 0.5f);
			}
			const float outA = p[3] / 255.f * fa0 + (q[3] / 255.f) * (1.f - fa);
			q[3] = static_cast<uint8_t>(outA * 255.f + 0.5f);
		}
	}
	return Encode(base, NormalizeOptions(opt), out);
}

}  // namespace tui
