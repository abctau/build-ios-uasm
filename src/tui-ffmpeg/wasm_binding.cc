#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "ffmpeg_core.h"

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

int RequireInt(const emscripten::val& v, const char* name) {
	if (v.isUndefined() || v.isNull() || !v.isNumber() || !std::isfinite(v.as<double>())) {
		emscripten::val::global("Error").new_(std::string(name)).throw_();
		return 0;
	}
	return v.as<int>();
}

std::string GetMediaInfo(emscripten::val bytes) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	std::string out;
	if (!tui::GetMediaInfo(buffer.data(), buffer.size(), out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to parse media (unsupported container)"))
		    .throw_();
		return std::string();
	}
	return out;
}

emscripten::val ExtractFrame(emscripten::val bytes, int timeMs, int maxWidth) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	std::vector<uint8_t> png;
	if (!tui::ExtractFrame(buffer.data(), buffer.size(), timeMs, maxWidth, png)) {
		emscripten::val::global("Error").new_(std::string("failed to extract frame")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(png);
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_ffmpeg_uasm_module) {
	emscripten::function("getMediaInfo", &GetMediaInfo);
	emscripten::function("extractFrame", &ExtractFrame);
}
