#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstring>
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
		    .new_(std::string("failed to rotate image"))
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

bool ParseHexColor(const std::string& text, int out[3]) {
	if (text.empty()) return false;
	const char* s = text.c_str();
	if (s[0] == '#') s++;
	if (std::strlen(s) != 6) return false;
	for (int i = 0; i < 3; i++) {
		int v = 0;
		for (int j = 0; j < 2; j++) {
			const char c = s[i * 2 + j];
			int d = 0;
			if (c >= '0' && c <= '9') d = c - '0';
			else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
			else return false;
			v = v * 16 + d;
		}
		out[i] = v;
	}
	return true;
}

int ParseDirection2(const std::string& direction) {
	if (direction == "horizontal" || direction == "x") return 1;
	return 0;  // vertical
}

int ParseDirection4(const std::string& direction) {
	if (direction == "up") return 1;
	if (direction == "right") return 2;
	if (direction == "left") return 3;
	return 0;  // down
}

emscripten::val Flip(emscripten::val bytes, bool horizontal, bool vertical,
                     emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::FlipImage(buffer.data(), buffer.size(), horizontal, vertical, opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to flip image")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val Blur(emscripten::val bytes, int radius, emscripten::val format,
                     emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::BlurImage(buffer.data(), buffer.size(), radius, opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to blur image")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val RoundCorners(emscripten::val bytes, int radius, emscripten::val format,
                             emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::RoundCornersImage(buffer.data(), buffer.size(), radius, opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to round corners")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val CircleClip(emscripten::val bytes, emscripten::val format,
                           emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::CircleClipImage(buffer.data(), buffer.size(), opt, out)) {
		emscripten::val::global("Error").new_(std::string("failed to circle clip")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val ExtendFill(emscripten::val bytes, int targetWidth, int targetHeight,
                           const std::string& direction, double centerRatio,
                           const std::string& fill, int blurRadius,
                           emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::ExtendFillOptions opts;
	opts.targetWidth = targetWidth;
	opts.targetHeight = targetHeight;
	opts.direction = ParseDirection2(direction);
	opts.centerRatio = static_cast<float>(centerRatio);
	opts.blurRadius = blurRadius;
	if (fill == "blur") {
		opts.mode = tui::ExtendFillMode::BLUR;
	} else if (fill == "edge") {
		opts.mode = tui::ExtendFillMode::EDGE;
	} else if (ParseHexColor(fill, opts.fillColor)) {
		opts.mode = tui::ExtendFillMode::COLOR;
	} else {
		emscripten::val::global("Error")
		    .new_(std::string("fill must be 'blur', 'edge' or '#rrggbb'"))
		    .throw_();
		return emscripten::val::undefined();
	}
	opts.encode = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::ExtendFillImage(buffer.data(), buffer.size(), opts, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to extend fill image"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val EdgeBlur(emscripten::val bytes, int radius,
                         const std::string& direction, double r1, double r2, double r3,
                         double transition, bool overlay, double overlayOpacity,
                         emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::EdgeBlurOptions opts;
	opts.radius = radius;
	opts.direction = ParseDirection2(direction);
	opts.regions[0] = static_cast<float>(r1);
	opts.regions[1] = static_cast<float>(r2);
	opts.regions[2] = static_cast<float>(r3);
	opts.transition = static_cast<float>(transition);
	opts.overlay = overlay;
	opts.overlayOpacity = static_cast<float>(overlayOpacity);
	opts.encode = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::EdgeBlurImage(buffer.data(), buffer.size(), opts, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to edge blur image"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val ProgressiveBlur(emscripten::val bytes, const std::string& direction,
                                int radius, double offset, double interpolation,
                                emscripten::val format, emscripten::val quality) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::ProgressiveBlurOptions opts;
	opts.direction = ParseDirection4(direction);
	opts.radius = radius;
	opts.offset = static_cast<float>(offset);
	opts.interpolation = static_cast<float>(interpolation);
	opts.encode = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::ProgressiveBlurImage(buffer.data(), buffer.size(), opts, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to progressive blur image"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val Composite(emscripten::val baseBytes, emscripten::val overlayBytes,
                          int x, int y, int alpha, emscripten::val format,
                          emscripten::val quality) {
	const std::vector<uint8_t> base = RequireBytes(baseBytes);
	const std::vector<uint8_t> overlay = RequireBytes(overlayBytes);
	tui::EncodeOptions opt = ParseEncodeArgs(format, quality);
	std::vector<uint8_t> out;
	if (!tui::CompositeImage(base.data(), base.size(), overlay.data(), overlay.size(),
	                         x, y, alpha, opt, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to composite images"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_image_uasm_module) {
	emscripten::function("getInfo", &GetInfo);
	emscripten::function("resize", &Resize);
	emscripten::function("crop", &Crop);
	emscripten::function("rotate", &Rotate);
	emscripten::function("convert", &Convert);
	emscripten::function("getPalette", &GetPalette);
	emscripten::function("flip", &Flip);
	emscripten::function("blur", &Blur);
	emscripten::function("roundCorners", &RoundCorners);
	emscripten::function("circleClip", &CircleClip);
	emscripten::function("extendFill", &ExtendFill);
	emscripten::function("edgeBlur", &EdgeBlur);
	emscripten::function("progressiveBlur", &ProgressiveBlur);
	emscripten::function("composite", &Composite);
}
