#include "svg_core.h"

#define NANOSVG_IMPLEMENTATION
#include "vendor/nanosvg.h"

#define NANOSVGRAST_IMPLEMENTATION
#include "vendor/nanosvgrast.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "vendor/stb_image_write.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace tui {

namespace {

void AppendToVector(void* context, void* data, int size) {
	std::vector<uint8_t>* out = static_cast<std::vector<uint8_t>*>(context);
	const uint8_t* bytes = static_cast<const uint8_t*>(data);
	out->insert(out->end(), bytes, bytes + size);
}

// nsvgParse parses in place and requires a null-terminated writable copy.
// Returns null if the document cannot be parsed or contains no shapes
// (nanosvg happily returns an empty image for garbage input).
NSVGimage* ParseSvgCopy(const uint8_t* data, size_t size) {
	if (data == nullptr || size == 0) return nullptr;
	char* buf = static_cast<char*>(malloc(size + 1));
	if (buf == nullptr) return nullptr;
	std::memcpy(buf, data, size);
	buf[size] = '\0';
	NSVGimage* image = nsvgParse(buf, "px", 96.0f);
	// nsvgParse owns/frees the buffer contents state; the buffer itself must
	// stay alive until parse finishes, which it has by now.
	free(buf);
	if (image != nullptr && image->shapes == nullptr) {
		nsvgDelete(image);
		return nullptr;
	}
	return image;
}

void IntrinsicSize(const NSVGimage* image, float& w, float& h) {
	// nsvgParse already falls back to viewBox, then content bounds, so these
	// are positive for any parseable document; the defaults are last-resort.
	w = image->width > 0.f ? image->width : 300.f;
	h = image->height > 0.f ? image->height : 150.f;
}

bool EncodePng(const uint8_t* pixels, int w, int h, std::vector<uint8_t>& out) {
	out.clear();
	return stbi_write_png_to_func(AppendToVector, &out, w, h, 4, pixels, w * 4) != 0;
}

}  // namespace

bool SvgInfoFromMemory(const uint8_t* data, size_t size, SvgInfo& out) {
	NSVGimage* image = ParseSvgCopy(data, size);
	if (image == nullptr) return false;
	float w = 0.f;
	float h = 0.f;
	IntrinsicSize(image, w, h);
	out.width = static_cast<int>(w + 0.5f);
	out.height = static_cast<int>(h + 0.5f);
	nsvgDelete(image);
	return out.width > 0 && out.height > 0;
}

bool RenderSvgToPng(const uint8_t* data, size_t size, double scale,
                    int targetWidth, int targetHeight, std::vector<uint8_t>& out) {
	NSVGimage* image = ParseSvgCopy(data, size);
	if (image == nullptr) return false;

	float iw = 0.f;
	float ih = 0.f;
	IntrinsicSize(image, iw, ih);

	int tw = 0;
	int th = 0;
	if (targetWidth > 0 || targetHeight > 0) {
		tw = targetWidth;
		th = targetHeight;
		if (tw <= 0) tw = static_cast<int>(static_cast<double>(iw) * th / ih + 0.5);
		if (th <= 0) th = static_cast<int>(static_cast<double>(ih) * tw / iw + 0.5);
	} else {
		const double s = scale > 0.0 ? scale : 1.0;
		tw = static_cast<int>(iw * s + 0.5);
		th = static_cast<int>(ih * s + 0.5);
	}
	tw = tw > 0 ? tw : 1;
	th = th > 0 ? th : 1;
	if (tw > 8192 || th > 8192) {
		nsvgDelete(image);
		return false;
	}

	const double tx = static_cast<double>(tw) / iw;
	const double ty = static_cast<double>(th) / ih;
	const double s = tx < ty ? tx : ty;  // uniform scale, centered implicit via 0 offset
	(void)tx;
	(void)ty;

	std::vector<uint8_t> pixels(static_cast<size_t>(tw) * static_cast<size_t>(th) * 4, 0);
	NSVGrasterizer* rast = nsvgCreateRasterizer();
	if (rast == nullptr) {
		nsvgDelete(image);
		return false;
	}
	nsvgRasterize(rast, image, 0.f, 0.f, static_cast<float>(s),
	              pixels.data(), tw, th, tw * 4);
	nsvgDeleteRasterizer(rast);
	nsvgDelete(image);

	return EncodePng(pixels.data(), tw, th, out);
}

}  // namespace tui
