#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "image_core.h"

namespace {

bool GetBytes(napi_env env, napi_value value, const uint8_t** data, size_t* length) {
	napi_typedarray_type type;
	size_t byteOffset = 0;
	size_t arrayLength = 0;
	void* elementData = nullptr;
	napi_value buffer = nullptr;

	if (napi_get_typedarray_info(env, value, &type, &arrayLength, &elementData, &buffer,
	                             &byteOffset) == napi_ok) {
		if (type != napi_uint8_array) return false;
		*data = static_cast<const uint8_t*>(elementData);
		*length = arrayLength;
		return true;
	}

	size_t bufferLength = 0;
	void* bufferData = nullptr;
	bool isArrayBuffer = false;
	if (napi_is_arraybuffer(env, value, &isArrayBuffer) == napi_ok && isArrayBuffer &&
	    napi_get_arraybuffer_info(env, value, &bufferData, &bufferLength) == napi_ok) {
		*data = static_cast<const uint8_t*>(bufferData);
		*length = bufferLength;
		return true;
	}

	return false;
}

bool ParseBytesArg(napi_env env, napi_value value, std::vector<uint8_t>& out,
                   const char* argName) {
	const uint8_t* data = nullptr;
	size_t length = 0;
	if (!GetBytes(env, value, &data, &length)) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	out.assign(data, data + length);
	return true;
}

void FreeExternalBuffer(napi_env env, void* data, void* hint) {
	(void)env;
	(void)hint;
	free(data);
}

napi_value MakeBytes(napi_env env, const std::vector<uint8_t>& data) {
	napi_value arrayBuffer = nullptr;
	if (data.empty()) {
		if (napi_create_arraybuffer(env, 0, nullptr, &arrayBuffer) != napi_ok) {
			return nullptr;
		}
	} else {
		void* copy = malloc(data.size());
		if (copy == nullptr) return nullptr;
		std::memcpy(copy, data.data(), data.size());
		if (napi_create_external_arraybuffer(env, copy, data.size(), FreeExternalBuffer,
		                                     nullptr, &arrayBuffer) != napi_ok) {
			free(copy);
			return nullptr;
		}
	}

	napi_value typedArray = nullptr;
	if (napi_create_typedarray(env, napi_uint8_array, data.size(), arrayBuffer, 0,
	                           &typedArray) != napi_ok) {
		return nullptr;
	}
	return typedArray;
}

bool ParseStringArg(napi_env env, napi_value value, std::string& out, const char* argName) {
	size_t strLen = 0;
	if (napi_get_value_string_utf8(env, value, nullptr, 0, &strLen) != napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	out.resize(strLen);
	if (napi_get_value_string_utf8(env, value, out.data(), strLen + 1, &strLen) != napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	return true;
}

bool ParseNumberArg(napi_env env, napi_value value, double& out, const char* argName) {
	if (napi_get_value_double(env, value, &out) != napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	return true;
}

bool ParseBoolArg(napi_env env, napi_value value, bool& out, const char* argName) {
	if (napi_get_value_bool(env, value, &out) != napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	return true;
}

bool ParseHexColor(const std::string& text, int out[3]) {
	// accepts "#rrggbb" or "rrggbb"
	if (text.empty()) return false;
	const char* s = text.c_str();
	if (s[0] == '#') s++;
	if (std::strlen(s) != 6) return false;
	for (int i = 0; i < 3; i++) {
		char hex[3] = {s[i * 2], s[i * 2 + 1], '\0'};
		char* end = nullptr;
		const long v = std::strtol(hex, &end, 16);
		if (end == nullptr || *end != '\0' || v < 0 || v > 255) return false;
		out[i] = static_cast<int>(v);
	}
	return true;
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

// args[offset] = format string, args[offset+1] = quality (both optional)
bool ParseEncodeArgs(napi_env env, napi_value* args, size_t argc, size_t offset,
                     tui::EncodeOptions& opt) {
	if (argc > offset) {
		std::string format;
		if (!ParseStringArg(env, args[offset], format, "format must be a string")) return false;
		if (!ParseFormat(format, opt.format)) {
			napi_throw_type_error(env, nullptr, "format must be 'png' or 'jpg'");
			return false;
		}
	}
	if (argc > offset + 1) {
		double quality = 85;
		if (napi_get_value_double(env, args[offset + 1], &quality) != napi_ok) {
			napi_throw_type_error(env, nullptr, "quality must be a number");
			return false;
		}
		opt.quality = static_cast<int>(quality);
	}
	return true;
}

napi_value MakeInfo(napi_env env, const tui::ImageInfo& info) {
	napi_value result = nullptr;
	if (napi_create_object(env, &result) != napi_ok) return nullptr;

	napi_value w = nullptr;
	napi_value h = nullptr;
	napi_value c = nullptr;
	if (napi_create_int32(env, info.width, &w) != napi_ok ||
	    napi_create_int32(env, info.height, &h) != napi_ok ||
	    napi_create_int32(env, info.channels, &c) != napi_ok) {
		return nullptr;
	}
	if (napi_set_named_property(env, result, "width", w) != napi_ok ||
	    napi_set_named_property(env, result, "height", h) != napi_ok ||
	    napi_set_named_property(env, result, "channels", c) != napi_ok) {
		return nullptr;
	}
	return result;
}

napi_value MakeColor(napi_env env, const tui::RGB& color) {
	napi_value result = nullptr;
	if (napi_create_array_with_length(env, 3, &result) != napi_ok) return nullptr;
	const int v[3] = {color.r, color.g, color.b};
	for (int i = 0; i < 3; i++) {
		napi_value item = nullptr;
		if (napi_create_int32(env, v[i], &item) != napi_ok) return nullptr;
		if (napi_set_element(env, result, static_cast<uint32_t>(i), item) != napi_ok) {
			return nullptr;
		}
	}
	return result;
}

napi_value MakePalette(napi_env env, const tui::PaletteResult& palette) {
	napi_value result = nullptr;
	if (napi_create_object(env, &result) != napi_ok) return nullptr;

	napi_value w = nullptr;
	napi_value h = nullptr;
	if (napi_create_int32(env, palette.width, &w) != napi_ok ||
	    napi_create_int32(env, palette.height, &h) != napi_ok) {
		return nullptr;
	}
	if (napi_set_named_property(env, result, "width", w) != napi_ok ||
	    napi_set_named_property(env, result, "height", h) != napi_ok) {
		return nullptr;
	}

	napi_value dominant = MakeColor(env, palette.dominant);
	if (dominant == nullptr ||
	    napi_set_named_property(env, result, "dominant", dominant) != napi_ok) {
		return nullptr;
	}

	napi_value paletteArray = nullptr;
	if (napi_create_array_with_length(env, palette.palette.size(), &paletteArray) != napi_ok) {
		return nullptr;
	}
	for (size_t i = 0; i < palette.palette.size(); i++) {
		napi_value item = MakeColor(env, palette.palette[i]);
		if (item == nullptr ||
		    napi_set_element(env, paletteArray, static_cast<uint32_t>(i), item) != napi_ok) {
			return nullptr;
		}
	}
	if (napi_set_named_property(env, result, "palette", paletteArray) != napi_ok) {
		return nullptr;
	}

	napi_value edges = nullptr;
	if (napi_create_array_with_length(env, 4, &edges) != napi_ok) return nullptr;
	for (int e = 0; e < 4; e++) {
		napi_value item = MakeColor(env, palette.edges[e]);
		if (item == nullptr ||
		    napi_set_element(env, edges, static_cast<uint32_t>(e), item) != napi_ok) {
			return nullptr;
		}
	}
	if (napi_set_named_property(env, result, "edges", edges) != napi_ok) return nullptr;

	return result;
}

napi_value GetInfo(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "getInfo expects (bytes)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::ImageInfo imageInfo;
	if (!tui::ImageInfoFromMemory(bytes.data(), bytes.size(), imageInfo)) {
		napi_throw_type_error(env, nullptr, "failed to decode image");
		return nullptr;
	}
	return MakeInfo(env, imageInfo);
}

napi_value Resize(napi_env env, napi_callback_info info) {
	size_t argc = 5;
	napi_value args[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "resize expects (bytes, width, height, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double width = 0;
	double height = 0;
	if (napi_get_value_double(env, args[1], &width) != napi_ok ||
	    napi_get_value_double(env, args[2], &height) != napi_ok) {
		napi_throw_type_error(env, nullptr, "width and height must be numbers");
		return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 3, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::ResizeImage(bytes.data(), bytes.size(), static_cast<int>(width),
	                      static_cast<int>(height), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to resize image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Crop(napi_env env, napi_callback_info info) {
	size_t argc = 7;
	napi_value args[7] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 5) {
		napi_throw_type_error(env, nullptr, "crop expects (bytes, x, y, width, height, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double nums[4] = {0, 0, 0, 0};
	for (int i = 0; i < 4; i++) {
		if (napi_get_value_double(env, args[i + 1], &nums[i]) != napi_ok) {
			napi_throw_type_error(env, nullptr, "x, y, width, height must be numbers");
			return nullptr;
		}
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 5, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::CropImage(bytes.data(), bytes.size(), static_cast<int>(nums[0]),
	                    static_cast<int>(nums[1]), static_cast<int>(nums[2]),
	                    static_cast<int>(nums[3]), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to crop image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Rotate(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "rotate expects (bytes, degrees, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double degrees = 0;
	if (napi_get_value_double(env, args[1], &degrees) != napi_ok) {
		napi_throw_type_error(env, nullptr, "degrees must be a number");
		return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 2, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::RotateImage(bytes.data(), bytes.size(), static_cast<int>(degrees), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to rotate image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Convert(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3] = {nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "convert expects (bytes, format, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 1, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::ConvertImage(bytes.data(), bytes.size(), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to convert image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value GetPalette(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "getPalette expects (bytes, colorCount?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	double colorCount = 8;
	if (argc >= 2) {
		if (napi_get_value_double(env, args[1], &colorCount) != napi_ok) {
			napi_throw_type_error(env, nullptr, "colorCount must be a number");
			return nullptr;
		}
	}

	tui::PaletteResult palette;
	if (!tui::ExtractPaletteFromMemory(bytes.data(), bytes.size(),
	                                   static_cast<int>(colorCount), palette)) {
		napi_throw_type_error(env, nullptr, "failed to decode image");
		return nullptr;
	}
	return MakePalette(env, palette);
}

napi_value Flip(napi_env env, napi_callback_info info) {
	size_t argc = 5;
	napi_value args[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "flip expects (bytes, horizontal, vertical, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	bool horizontal = false;
	bool vertical = false;
	if (!ParseBoolArg(env, args[1], horizontal, "horizontal must be a boolean") ||
	    !ParseBoolArg(env, args[2], vertical, "vertical must be a boolean")) {
		return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 3, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::FlipImage(bytes.data(), bytes.size(), horizontal, vertical, opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to flip image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Blur(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "blur expects (bytes, radius, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double radius = 0;
	if (!ParseNumberArg(env, args[1], radius, "radius must be a number")) return nullptr;

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 2, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::BlurImage(bytes.data(), bytes.size(), static_cast<int>(radius), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to blur image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value RoundCorners(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "roundCorners expects (bytes, radius, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double radius = 0;
	if (!ParseNumberArg(env, args[1], radius, "radius must be a number")) return nullptr;

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 2, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::RoundCornersImage(bytes.data(), bytes.size(), static_cast<int>(radius), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to round corners");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value CircleClip(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value args[3] = {nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "circleClip expects (bytes, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 1, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::CircleClipImage(bytes.data(), bytes.size(), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to circle clip");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value ExtendFill(napi_env env, napi_callback_info info) {
	size_t argc = 9;
	napi_value args[9] = {};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr,
		                      "extendFill expects (bytes, targetWidth, targetHeight, direction?, "
		                      "centerRatio?, fill?, blurRadius?, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double targetWidth = 0;
	double targetHeight = 0;
	if (!ParseNumberArg(env, args[1], targetWidth, "targetWidth must be a number") ||
	    !ParseNumberArg(env, args[2], targetHeight, "targetHeight must be a number")) {
		return nullptr;
	}

	tui::ExtendFillOptions opts;
	opts.targetWidth = static_cast<int>(targetWidth);
	opts.targetHeight = static_cast<int>(targetHeight);

	if (argc > 3 && args[3] != nullptr) {
		std::string direction;
		if (!ParseStringArg(env, args[3], direction, "direction must be a string")) return nullptr;
		if (direction == "horizontal" || direction == "x") {
			opts.direction = 1;
		} else if (direction == "vertical" || direction == "y") {
			opts.direction = 0;
		} else {
			napi_throw_type_error(env, nullptr, "direction must be 'vertical' or 'horizontal'");
			return nullptr;
		}
	}
	if (argc > 4 && args[4] != nullptr) {
		double ratio = 0.5;
		if (!ParseNumberArg(env, args[4], ratio, "centerRatio must be a number")) return nullptr;
		opts.centerRatio = static_cast<float>(ratio);
	}
	if (argc > 5 && args[5] != nullptr) {
		std::string fill;
		if (!ParseStringArg(env, args[5], fill, "fill must be a string")) return nullptr;
		if (fill == "blur") {
			opts.mode = tui::ExtendFillMode::BLUR;
		} else if (fill == "edge") {
			opts.mode = tui::ExtendFillMode::EDGE;
		} else if (ParseHexColor(fill, opts.fillColor)) {
			opts.mode = tui::ExtendFillMode::COLOR;
		} else {
			napi_throw_type_error(env, nullptr, "fill must be 'blur', 'edge' or '#rrggbb'");
			return nullptr;
		}
	}
	if (argc > 6 && args[6] != nullptr) {
		double blurRadius = 0;
		if (!ParseNumberArg(env, args[6], blurRadius, "blurRadius must be a number")) return nullptr;
		opts.blurRadius = static_cast<int>(blurRadius);
	}
	if (!ParseEncodeArgs(env, args, argc, 7, opts.encode)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::ExtendFillImage(bytes.data(), bytes.size(), opts, out)) {
		napi_throw_type_error(env, nullptr, "failed to extend fill image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

bool ParseDirection2(napi_env env, napi_value value, int& out) {
	std::string direction;
	if (!ParseStringArg(env, value, direction, "direction must be a string")) return false;
	if (direction == "vertical" || direction == "y") {
		out = 0;
	} else if (direction == "horizontal" || direction == "x") {
		out = 1;
	} else {
		napi_throw_type_error(env, nullptr, "direction must be 'vertical' or 'horizontal'");
		return false;
	}
	return true;
}

napi_value EdgeBlur(napi_env env, napi_callback_info info) {
	size_t argc = 10;
	napi_value args[10] = {};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr,
		                      "edgeBlur expects (bytes, radius, direction?, regions?, "
		                      "transition?, overlay?, overlayOpacity?, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double radius = 0;
	if (!ParseNumberArg(env, args[1], radius, "radius must be a number")) return nullptr;

	tui::EdgeBlurOptions opts;
	opts.radius = static_cast<int>(radius);

	if (argc > 2 && args[2] != nullptr) {
		if (!ParseDirection2(env, args[2], opts.direction)) return nullptr;
	}
	if (argc > 3 && args[3] != nullptr) {
		bool isArray = false;
		if (napi_is_array(env, args[3], &isArray) != napi_ok || !isArray) {
			napi_throw_type_error(env, nullptr, "regions must be an array of 3 numbers");
			return nullptr;
		}
		for (uint32_t i = 0; i < 3; i++) {
			napi_value item = nullptr;
			double v = 0;
			if (napi_get_element(env, args[3], i, &item) != napi_ok ||
			    !ParseNumberArg(env, item, v, "regions must be an array of 3 numbers")) {
				return nullptr;
			}
			opts.regions[i] = static_cast<float>(v);
		}
	}
	if (argc > 4 && args[4] != nullptr) {
		double transition = 0;
		if (!ParseNumberArg(env, args[4], transition, "transition must be a number")) return nullptr;
		opts.transition = static_cast<float>(transition);
	}
	if (argc > 5 && args[5] != nullptr) {
		if (!ParseBoolArg(env, args[5], opts.overlay, "overlay must be a boolean")) return nullptr;
	}
	if (argc > 6 && args[6] != nullptr) {
		double opacity = 0;
		if (!ParseNumberArg(env, args[6], opacity, "overlayOpacity must be a number")) return nullptr;
		opts.overlayOpacity = static_cast<float>(opacity);
	}
	if (!ParseEncodeArgs(env, args, argc, 7, opts.encode)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::EdgeBlurImage(bytes.data(), bytes.size(), opts, out)) {
		napi_throw_type_error(env, nullptr, "failed to edge blur image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value ProgressiveBlur(napi_env env, napi_callback_info info) {
	size_t argc = 7;
	napi_value args[7] = {};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr,
		                      "progressiveBlur expects (bytes, direction, radius, offset?, "
		                      "interpolation?, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::ProgressiveBlurOptions opts;
	std::string direction;
	if (!ParseStringArg(env, args[1], direction, "direction must be a string")) return nullptr;
	if (direction == "down") {
		opts.direction = 0;
	} else if (direction == "up") {
		opts.direction = 1;
	} else if (direction == "right") {
		opts.direction = 2;
	} else if (direction == "left") {
		opts.direction = 3;
	} else {
		napi_throw_type_error(env, nullptr, "direction must be 'down'|'up'|'right'|'left'");
		return nullptr;
	}
	double radius = 0;
	if (!ParseNumberArg(env, args[2], radius, "radius must be a number")) return nullptr;
	opts.radius = static_cast<int>(radius);

	if (argc > 3 && args[3] != nullptr) {
		double offset = 0;
		if (!ParseNumberArg(env, args[3], offset, "offset must be a number")) return nullptr;
		opts.offset = static_cast<float>(offset);
	}
	if (argc > 4 && args[4] != nullptr) {
		double interp = 0;
		if (!ParseNumberArg(env, args[4], interp, "interpolation must be a number")) return nullptr;
		opts.interpolation = static_cast<float>(interp);
	}
	if (!ParseEncodeArgs(env, args, argc, 5, opts.encode)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::ProgressiveBlurImage(bytes.data(), bytes.size(), opts, out)) {
		napi_throw_type_error(env, nullptr, "failed to progressive blur image");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Composite(napi_env env, napi_callback_info info) {
	size_t argc = 7;
	napi_value args[7] = {};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 5) {
		napi_throw_type_error(env, nullptr,
		                      "composite expects (baseBytes, overlayBytes, x, y, alpha?, format?, quality?)");
		return nullptr;
	}

	std::vector<uint8_t> baseBytes;
	std::vector<uint8_t> overlayBytes;
	if (!ParseBytesArg(env, args[0], baseBytes, "baseBytes must be a Uint8Array or ArrayBuffer") ||
	    !ParseBytesArg(env, args[1], overlayBytes,
	                   "overlayBytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double x = 0;
	double y = 0;
	if (!ParseNumberArg(env, args[2], x, "x must be a number") ||
	    !ParseNumberArg(env, args[3], y, "y must be a number")) {
		return nullptr;
	}
	double alpha = 100;
	if (argc > 4 && args[4] != nullptr) {
		if (!ParseNumberArg(env, args[4], alpha, "alpha must be a number")) return nullptr;
	}

	tui::EncodeOptions opt;
	if (!ParseEncodeArgs(env, args, argc, 5, opt)) return nullptr;

	std::vector<uint8_t> out;
	if (!tui::CompositeImage(baseBytes.data(), baseBytes.size(), overlayBytes.data(),
	                         overlayBytes.size(), static_cast<int>(x), static_cast<int>(y),
	                         static_cast<int>(alpha), opt, out)) {
		napi_throw_type_error(env, nullptr, "failed to composite images");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Init(napi_env env, napi_value exports) {
	const struct {
		const char* name;
		napi_value (*fn)(napi_env, napi_callback_info);
	} functions[] = {
	    {"getInfo", GetInfo},         {"resize", Resize},
	    {"crop", Crop},               {"rotate", Rotate},
	    {"convert", Convert},         {"getPalette", GetPalette},
	    {"flip", Flip},               {"blur", Blur},
	    {"roundCorners", RoundCorners}, {"circleClip", CircleClip},
	    {"extendFill", ExtendFill},   {"edgeBlur", EdgeBlur},
	    {"progressiveBlur", ProgressiveBlur}, {"composite", Composite},
	};

	for (const auto& entry : functions) {
		napi_value fn = nullptr;
		if (napi_create_function(env, entry.name, NAPI_AUTO_LENGTH, entry.fn, nullptr, &fn) !=
		        napi_ok ||
		    napi_set_named_property(env, exports, entry.name, fn) != napi_ok) {
			return nullptr;
		}
	}
	return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
