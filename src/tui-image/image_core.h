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

// Decode -> rotate 90/180/270 degrees clockwise -> encode.
bool RotateImage(const uint8_t* data, size_t size, int degrees,
                 const EncodeOptions& opt, std::vector<uint8_t>& out);

// Decode -> re-encode in the target format/quality (format conversion / compression).
bool ConvertImage(const uint8_t* data, size_t size, const EncodeOptions& opt,
                  std::vector<uint8_t>& out);

// Decode -> median-cut quantization -> palette. colorCount clamped to [2, 16].
bool ExtractPaletteFromMemory(const uint8_t* data, size_t size, int colorCount,
                              PaletteResult& out);

}  // namespace tui
