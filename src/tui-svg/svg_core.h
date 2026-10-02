#ifndef TUI_SVG_CORE_H
#define TUI_SVG_CORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tui {

struct SvgInfo {
	int width;
	int height;
};

// Parse the SVG (UTF-8 bytes, null not required) and report its intrinsic
// size. Falls back to viewBox, then 300x150 per the SVG spec.
bool SvgInfoFromMemory(const uint8_t* data, size_t size, SvgInfo& out);

// Rasterize the SVG to RGBA and encode as PNG.
// - scale > 0: output = intrinsic size * scale
// - targetWidth/Height > 0: explicit size (height <= 0 keeps aspect ratio)
bool RenderSvgToPng(const uint8_t* data, size_t size, double scale,
                    int targetWidth, int targetHeight, std::vector<uint8_t>& out);

}  // namespace tui

#endif  // TUI_SVG_CORE_H
