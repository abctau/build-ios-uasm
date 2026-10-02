// tui-image core: decode (stb_image) + resize/rotate/crop/convert + encode
// (stb_image_write) + median-cut palette extraction (absorbed from tui-color-thief).
// Shared by the N-API binding (App) and the embind binding (Web / mini programs).

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tui {

enum class OutFormat {
	PNG,
	JPEG,
};

struct ImageInfo {
	int width;
	int height;
	int channels;  // components in the original file (1..4)
};

struct ImageRGBA {
	int width;
	int height;
	std::vector<uint8_t> pixels;  // RGBA8, width*height*4
};

struct EncodeOptions {
	OutFormat format = OutFormat::PNG;
	int quality = 85;  // jpeg only, 1..100
};

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

// Peek image dimensions without decoding pixels. Returns false on failure.
bool ImageInfoFromMemory(const uint8_t* data, size_t size, ImageInfo& out);

// Decode to raw RGBA8 pixels. Returns false on decode failure.
bool DecodeToRGBA(const uint8_t* data, size_t size, ImageRGBA& out);

// Encode RGBA pixels to PNG/JPEG. JPEG flattens alpha onto white.
bool EncodeRGBA(const ImageRGBA& image, const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> resize (height<=0 keeps aspect ratio; width<=0 with height>0 is symmetric)
// -> encode. targetWidth/Height clamped to >=1.
bool ResizeImage(const uint8_t* data, size_t size, int targetWidth, int targetHeight,
                 const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> crop rect (clamped into the image) -> encode.
bool CropImage(const uint8_t* data, size_t size, int x, int y, int w, int h,
               const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> rotate by arbitrary degrees clockwise (90-degree multiples use the
// fast pixel path; other angles use bilinear sampling on an expanded canvas).
// Deg 90 multiples: output canvas swaps dimensions. Other angles: canvas expands
// to fit, out-of-canvas areas stay transparent. Returns false for 0-size images.
bool RotateImage(const uint8_t* data, size_t size, int degrees,
                 const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> flip horizontally and/or vertically -> encode.
bool FlipImage(const uint8_t* data, size_t size, bool horizontal, bool vertical,
               const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> box blur (3 passes approximating gaussian, radius in px) -> encode.
// radius clamped to >=0; 0 returns the image re-encoded.
bool BlurImage(const uint8_t* data, size_t size, int radius,
               const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> round-corner alpha mask (anti-aliased edge) -> encode.
// JPEG output flattens the removed corners onto white automatically.
bool RoundCornersImage(const uint8_t* data, size_t size, int radius,
                       const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> center square crop -> inscribed circle alpha mask -> encode.
bool CircleClipImage(const uint8_t* data, size_t size,
                     const EncodeOptions& opt, std::vector<uint8_t>& out);

enum class ExtendFillMode {
	BLUR,     // scaled-cover copy of the source, blurred (poster background)
	EDGE,     // edge pixels stretched outward
	COLOR,    // solid fill color (fillColor RGB)
};

struct ExtendFillOptions {
	EncodeOptions encode;
	int targetWidth;
	int targetHeight;
	int direction;        // 0 = vertical (offset along Y), 1 = horizontal
	float centerRatio;    // 0..1 center position of the source along the
	                      // direction axis; clamped so the source stays inside
	ExtendFillMode mode = ExtendFillMode::BLUR;
	int fillColor[3] = {0, 0, 0};
	int blurRadius = 24;  // used by BLUR mode
};

// Decode -> build target-size canvas filled per mode, paste the source at its
// original size at centerRatio -> encode.
bool ExtendFillImage(const uint8_t* data, size_t size, const ExtendFillOptions& opts,
                     std::vector<uint8_t>& out);

struct EdgeBlurOptions {
	EncodeOptions encode;
	int radius;             // blur strength in px
	int direction;          // 0 = vertical (top/bottom bands), 1 = horizontal
	float regions[3];       // [blurBand, clearBand, blurBand] ratios, normalized
	float transition;       // 0..1 size of the smooth transition inside the bands
	bool overlay;           // tint the blurred bands with the image dominant color
	float overlayOpacity;   // 0..1
	int overlayColors[2][4] = {{-1, 0, 0, 0}, {-1, 0, 0, 0}};  // [R,G,B,A] per band;
	                            // A < 0 means "use dominant color auto-extraction"
};

// Decode -> full blur -> per-axis gradient mask blend (clear center, blurred
// bands with smooth transitions, optional dominant-color overlay) -> encode.
bool EdgeBlurImage(const uint8_t* data, size_t size, const EdgeBlurOptions& opts,
                   std::vector<uint8_t>& out);

struct ProgressiveBlurOptions {
	EncodeOptions encode;
	int radius;          // max blur radius at the far edge
	int direction;       // 0=down 1=up 2=right 3=left (blur grows toward that edge)
	float offset;        // 0..1 where the blur starts (0 = that edge, 1 = never)
	float interpolation; // 0..1 width of the ramp between offset and the edge
};

// Decode -> blur strength ramps from `offset` toward `direction` edge -> encode.
bool ProgressiveBlurImage(const uint8_t* data, size_t size, const ProgressiveBlurOptions& opts,
                          std::vector<uint8_t>& out);

// Decode base -> alpha-composite the overlay at (x, y) (may overhang; clipped)
// with global opacity 0..100 -> encode base-sized result.
bool CompositeImage(const uint8_t* baseData, size_t baseSize,
                    const uint8_t* overlayData, size_t overlaySize,
                    int x, int y, int alpha, const EncodeOptions& opt,
                    std::vector<uint8_t>& out);

// Decode -> re-encode in the target format/quality (format conversion / compression).
bool ConvertImage(const uint8_t* data, size_t size, const EncodeOptions& opt,
                  std::vector<uint8_t>& out);

// Decode -> median-cut quantization -> palette. colorCount clamped to [2, 16].
bool ExtractPaletteFromMemory(const uint8_t* data, size_t size, int colorCount,
                              PaletteResult& out);

}  // namespace tui
