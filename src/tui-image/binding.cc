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
		napi_throw_type_error(env, nullptr, "failed to rotate image (degrees must be a multiple of 90)");
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

napi_value Init(napi_env env, napi_value exports) {
	const struct {
		const char* name;
		napi_value (*fn)(napi_env, napi_callback_info);
	} functions[] = {
	    {"getInfo", GetInfo},         {"resize", Resize},
	    {"crop", Crop},               {"rotate", Rotate},
	    {"convert", Convert},         {"getPalette", GetPalette},
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
