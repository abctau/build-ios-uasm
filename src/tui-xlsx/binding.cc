#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "xlsx_core.h"

namespace {

constexpr size_t kMaxUncompressed = 64u * 1024 * 1024;

bool GetStringArg(napi_env env, napi_value value, std::string& out,
                  const char* argName) {
	size_t length = 0;
	if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	out.resize(length + 1);
	size_t copied = 0;
	if (napi_get_value_string_utf8(env, value, out.data(), out.size(), &copied) !=
	    napi_ok) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	out.resize(copied);
	return true;
}

bool GetBytes(napi_env env, napi_value value, const uint8_t** data, size_t* length) {
	napi_typedarray_type type;
	size_t byteOffset = 0;
	size_t arrayLength = 0;
	void* elementData = nullptr;
	napi_value buffer = nullptr;

	if (napi_get_typedarray_info(env, value, &type, &arrayLength, &elementData,
	                             &buffer, &byteOffset) == napi_ok) {
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

void FreeExternalBuffer(napi_env env, void* data, void* hint) {
	(void)env;
	(void)hint;
	free(data);
}

napi_value MakeBytes(napi_env env, const std::vector<uint8_t>& data) {
	uint8_t* buffer = static_cast<uint8_t*>(malloc(data.empty() ? 1 : data.size()));
	if (buffer == nullptr) {
		napi_throw_type_error(env, nullptr, "out of memory");
		return nullptr;
	}
	if (!data.empty()) std::memcpy(buffer, data.data(), data.size());

	napi_value arrayBuffer = nullptr;
	if (napi_create_external_arraybuffer(env, buffer, data.size(),
	                                     FreeExternalBuffer, nullptr,
	                                     &arrayBuffer) != napi_ok) {
		free(buffer);
		napi_throw_type_error(env, nullptr, "failed to create buffer");
		return nullptr;
	}
	napi_value result = nullptr;
	if (napi_create_typedarray(env, napi_uint8_array, data.size(), arrayBuffer, 0,
	                           &result) != napi_ok) {
		napi_throw_type_error(env, nullptr, "failed to create typed array");
		return nullptr;
	}
	return result;
}

napi_value WriteXlsx(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok ||
	    argc < 1) {
		napi_throw_type_error(env, nullptr, "expected sheets json string");
		return nullptr;
	}
	std::string sheetsJson;
	if (!GetStringArg(env, argv[0], sheetsJson, "expected sheets json string")) {
		return nullptr;
	}
	std::vector<uint8_t> bytes;
	if (!tui::WriteXlsx(sheetsJson, bytes)) {
		napi_throw_type_error(env, nullptr,
		                      "failed to write xlsx (sheets must be [{name,rows}] with string/number/bool/null cells)");
		return nullptr;
	}
	return MakeBytes(env, bytes);
}

napi_value ReadXlsx(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok ||
	    argc < 1) {
		napi_throw_type_error(env, nullptr, "expected xlsx bytes");
		return nullptr;
	}
	const uint8_t* data = nullptr;
	size_t length = 0;
	if (!GetBytes(env, argv[0], &data, &length)) {
		napi_throw_type_error(env, nullptr, "expected xlsx bytes");
		return nullptr;
	}
	std::string json;
	if (!tui::ReadXlsx(data, length, json, kMaxUncompressed)) {
		napi_throw_type_error(env, nullptr, "failed to parse xlsx");
		return nullptr;
	}
	napi_value result = nullptr;
	if (napi_create_string_utf8(env, json.c_str(), json.size(), &result) != napi_ok) {
		napi_throw_type_error(env, nullptr, "failed to create result string");
		return nullptr;
	}
	return result;
}

napi_value Init(napi_env env, napi_value exports) {
	napi_property_descriptor props[] = {
	    {"writeXlsx", nullptr, WriteXlsx, nullptr, nullptr, nullptr, napi_default,
	     nullptr},
	    {"readXlsx", nullptr, ReadXlsx, nullptr, nullptr, nullptr, napi_default,
	     nullptr},
	};
	if (napi_define_properties(env, exports,
	                           sizeof(props) / sizeof(props[0]),
	                           props) != napi_ok) {
		return nullptr;
	}
	return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)

}  // namespace
