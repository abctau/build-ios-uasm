#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "svg_core.h"

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

	tui::SvgInfo svgInfo;
	if (!tui::SvgInfoFromMemory(bytes.data(), bytes.size(), svgInfo)) {
		napi_throw_type_error(env, nullptr, "failed to parse svg");
		return nullptr;
	}

	napi_value result = nullptr;
	if (napi_create_object(env, &result) != napi_ok) return nullptr;
	napi_value w = nullptr;
	napi_value h = nullptr;
	if (napi_create_int32(env, svgInfo.width, &w) != napi_ok ||
	    napi_create_int32(env, svgInfo.height, &h) != napi_ok ||
	    napi_set_named_property(env, result, "width", w) != napi_ok ||
	    napi_set_named_property(env, result, "height", h) != napi_ok) {
		return nullptr;
	}
	return result;
}

napi_value Render(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "render expects (bytes, scale?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double scale = 1.0;
	if (argc > 1 && args[1] != nullptr) {
		if (napi_get_value_double(env, args[1], &scale) != napi_ok) {
			napi_throw_type_error(env, nullptr, "scale must be a number");
			return nullptr;
		}
	}

	std::vector<uint8_t> out;
	if (!tui::RenderSvgToPng(bytes.data(), bytes.size(), scale, 0, 0, out)) {
		napi_throw_type_error(env, nullptr, "failed to render svg");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value RenderSize(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value args[4] = {nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "renderSize expects (bytes, width, height?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	double width = 0;
	double height = 0;
	if (napi_get_value_double(env, args[1], &width) != napi_ok) {
		napi_throw_type_error(env, nullptr, "width must be a number");
		return nullptr;
	}
	if (argc > 2 && args[2] != nullptr) {
		if (napi_get_value_double(env, args[2], &height) != napi_ok) {
			napi_throw_type_error(env, nullptr, "height must be a number");
			return nullptr;
		}
	}

	std::vector<uint8_t> out;
	if (!tui::RenderSvgToPng(bytes.data(), bytes.size(), 0.0,
	                         static_cast<int>(width), static_cast<int>(height), out)) {
		napi_throw_type_error(env, nullptr, "failed to render svg");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Init(napi_env env, napi_value exports) {
	const struct {
		const char* name;
		napi_value (*fn)(napi_env, napi_callback_info);
	} functions[] = {
	    {"getInfo", GetInfo},
	    {"render", Render},
	    {"renderSize", RenderSize},
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
