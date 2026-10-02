#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "svg_core.h"

namespace {

emscripten::val MakeBytes(const std::vector<uint8_t>& data) {
	emscripten::val result = emscripten::val::global("Uint8Array").new_(data.size());
	if (!data.empty()) {
		emscripten::val view =
		    emscripten::val(emscripten::typed_memory_view(data.size(), data.data()));
		result.call<void>("set", view);
	}
	return result;
}

std::vector<uint8_t> RequireBytes(const emscripten::val& bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("expected a Uint8Array")).throw_();
		return std::vector<uint8_t>();
	}
	return emscripten::vecFromJSArray<uint8_t>(bytes);
}

emscripten::val GetInfo(emscripten::val bytes) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::SvgInfo info;
	if (!tui::SvgInfoFromMemory(buffer.data(), buffer.size(), info)) {
		emscripten::val::global("Error").new_(std::string("failed to parse svg")).throw_();
		return emscripten::val::undefined();
	}
	emscripten::val result = emscripten::val::object();
	result.set("width", info.width);
	result.set("height", info.height);
	return result;
}

emscripten::val Render(emscripten::val bytes, double scale) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	std::vector<uint8_t> out;
	if (!tui::RenderSvgToPng(buffer.data(), buffer.size(), scale, 0, 0, out)) {
		emscripten::val::global("Error").new_(std::string("failed to render svg")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val RenderSize(emscripten::val bytes, int width, int height) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	std::vector<uint8_t> out;
	if (!tui::RenderSvgToPng(buffer.data(), buffer.size(), 0.0, width, height, out)) {
		emscripten::val::global("Error").new_(std::string("failed to render svg")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_svg_uasm_module) {
	emscripten::function("getInfo", &GetInfo);
	emscripten::function("render", &Render);
	emscripten::function("renderSize", &RenderSize);
}
