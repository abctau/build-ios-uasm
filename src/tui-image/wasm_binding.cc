#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "image_core.h"

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

emscripten::val MakeColor(const tui::RGB& color) {
	emscripten::val result = emscripten::val::array();
	result.set(0, color.r);
	result.set(1, color.g);
	result.set(2, color.b);
	return result;
}

std::vector<uint8_t> RequireBytes(const emscripten::val& bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("expected a Uint8Array")).throw_();
		return std::vector<uint8_t>();
	}
	return emscripten::vecFromJSArray<uint8_t>(bytes);
}

bool ParseFormat(const std::string& format, tui::OutFormat& out) {
	if (format == "png" || format == "PNG") {
		out = tui::OutFormat::PNG;
		return true;
	}
	if (format == "jpg" || format == "jpeg" || format == "JPG" || format == "JPEG") {
		out = tui::OutFormat::JPEG;
		return true;
	}
	return false;
}

// args: (bytes, ..., format?, quality?)
tui::EncodeOptions ParseEncodeArgs(const emscripten::val& formatVal,
                                   const emscripten::val& qualityVal) {
	tui::EncodeOptions opt;
	if (!formatVal.isUndefined() && !formatVal.isNull()) {
		const std::string format = formatVal.as<std::string>();
		if (!ParseFormat(format, opt.format)) {
			emscripten::val::global("Error")
			    .new_(std::string("format must be 'png' or 'jpg'"))
			    .throw_();
		}
	}
	if (!qualityVal.isUndefined() && !qualityVal.isNull()) {
		opt.quality = qualityVal.as<int>();
	}
	return opt;
}

emscripten::val GetInfo(emscripten::val bytes) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::ImageInfo info;
	if (!tui::ImageInfoFromMemory(buffer.data(), buffer.size(), info)) {
		emscripten::val::global("Error").new_(std::string("failed to decode image")).throw_();
		return emscripten::val::undefined();
	}
	emscripten::val result = emscripten::val::object();
	result.set("width", info.width);
	result.set("height", info.height);
	result.set("channels", info.channels);
	return result;
}

emscripten::val Resize(emscripten::val bytes, int width, int height,
                       emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::ResizeImage(buffer.data(), buffer.size(), width, height, opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to resize image")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val Crop(emscripten::val bytes, int x, int y, int w, int h,
                     emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::CropImage(buffer.data(), buffer.size(), x, y, w, h, opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to crop image")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val Rotate(emscripten::val bytes, int degrees, emscripten::val format,
                       emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::RotateImage(buffer.data(), buffer.size(), degrees, opt, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to rotate image (degrees must be a multiple of 90)"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val Convert(emscripten::val bytes, const std::string& format,
                        emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt;
	if (!ParseFormat(format, opt.format)) {
		emscripten::val::global("Error")
		    .new_(std::string("format must be 'png' or 'jpg'"))
		    .throw_();
		return emscripten::val::undefined();
	}
	if (!quality.isUndefined() && !quality.isNull()) {
		opt.quality = quality.as<int>();
	}
	std::vector<uint8_t> out;
	if (!tui::ConvertImage(buffer.data(), buffer.size(), opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to convert image")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val GetPalette(emscripten::val bytes, int colorCount) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::PaletteResult palette;
	if (!tui::ExtractPaletteFromMemory(buffer.data(), buffer.size(), colorCount, palette)) {
		emscripten::val::global("Error").new_(std::string("failed to decode image")).throw_();
		return emscripten::val::undefined();
	}

	emscripten::val result = emscripten::val::object();
	result.set("width", palette.width);
	result.set("height", palette.height);
	result.set("dominant", MakeColor(palette.dominant));

	emscripten::val paletteArray = emscripten::val::array();
	for (size_t i = 0; i < palette.palette.size(); i++) {
		paletteArray.set(static_cast<int>(i), MakeColor(palette.palette[i]));
	}
	result.set("palette", paletteArray);

	emscripten::val edges = emscripten::val::array();
	for (int e = 0; e < 4; e++) {
		edges.set(e, MakeColor(palette.edges[e]));
	}
	result.set("edges", edges);
	return result;
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_image_uasm_module) {
	emscripten::function("getInfo", &GetInfo);
	emscripten::function("resize", &Resize);
	emscripten::function("crop", &Crop);
	emscripten::function("rotate", &Rotate);
	emscripten::function("convert", &Convert);
	emscripten::function("getPalette", &GetPalette);
}
