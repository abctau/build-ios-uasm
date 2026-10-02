#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <vector>

#include "color_thief.h"

namespace {

emscripten::val MakeColor(const tui::RGB& color) {
  emscripten::val result = emscripten::val::array();
  result.set(0, color.r);
  result.set(1, color.g);
  result.set(2, color.b);
  return result;
}

emscripten::val MakeColorMatrix(const std::vector<tui::RGB>& colors) {
  emscripten::val result = emscripten::val::array();
  for (size_t i = 0; i < colors.size(); i++) {
    result.set(static_cast<int>(i), MakeColor(colors[i]));
  }
  return result;
}

emscripten::val MakeResult(const tui::PaletteResult& palette) {
  emscripten::val result = emscripten::val::object();
  result.set("width", palette.width);
  result.set("height", palette.height);
  result.set("dominant", MakeColor(palette.dominant));
  result.set("palette", MakeColorMatrix(palette.palette));
  result.set("edges",
             MakeColorMatrix(std::vector<tui::RGB>(palette.edges, palette.edges + 4)));
  return result;
}

emscripten::val GetPalette(emscripten::val bytes, int colorCount) {
  if (bytes.isUndefined() || bytes.isNull()) {
    emscripten::val::global("Error")
        .new_(std::string("getPalette expects a Uint8Array"))
        .throw_();
    return emscripten::val::undefined();
  }

  // vecFromJSArray copies element-by-element; fine for the sizes involved here
  // (a palette extraction decodes a full image anyway, which dwarfs the copy).
  const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
  tui::PaletteResult palette;
  if (!tui::ExtractPaletteFromMemory(buffer.data(), static_cast<int>(buffer.size()),
                                     colorCount, palette)) {
    static const char* kHex = "0123456789ABCDEF";
    std::string info = "failed to decode image (bytes=" + std::to_string(buffer.size()) + " head=";
    const size_t n = buffer.size() < 8 ? buffer.size() : 8;
    for (size_t i = 0; i < n; i++) {
      info += kHex[buffer[i] >> 4];
      info += kHex[buffer[i] & 0xF];
      if (i + 1 < n) info += ',';
    }
    info += ")";
    emscripten::val::global("Error").new_(info).throw_();
    return emscripten::val::undefined();
  }
  return MakeResult(palette);
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_color_thief_uasm_module) {
  emscripten::function("getPalette", &GetPalette);
}
