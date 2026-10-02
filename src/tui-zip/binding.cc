#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "zip_core.h"

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

napi_value MakeStringArray(napi_env env, const std::vector<std::string>& values) {
	napi_value result = nullptr;
	if (napi_create_array_with_length(env, values.size(), &result) != napi_ok) {
		return nullptr;
	}
	for (size_t i = 0; i < values.size(); i++) {
		napi_value item = nullptr;
		if (napi_create_string_utf8(env, values[i].c_str(), values[i].size(), &item) !=
		    napi_ok) {
			return nullptr;
		}
		if (napi_set_element(env, result, static_cast<uint32_t>(i), item) != napi_ok) {
			return nullptr;
		}
	}
	return result;
}

napi_value MakeExtractResult(napi_env env, const tui::ZipExtractResult& result) {
	napi_value obj = nullptr;
	if (napi_create_object(env, &obj) != napi_ok) return nullptr;

	napi_value names = MakeStringArray(env, result.names);
	if (names == nullptr || napi_set_named_property(env, obj, "names", names) != napi_ok) {
		return nullptr;
	}

	napi_value datas = nullptr;
	if (napi_create_array_with_length(env, result.datas.size(), &datas) != napi_ok) {
		return nullptr;
	}
	for (size_t i = 0; i < result.datas.size(); i++) {
		napi_value item = MakeBytes(env, result.datas[i]);
		if (item == nullptr ||
		    napi_set_element(env, datas, static_cast<uint32_t>(i), item) != napi_ok) {
			return nullptr;
		}
	}
	if (napi_set_named_property(env, obj, "datas", datas) != napi_ok) return nullptr;
	return obj;
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

napi_value ZipCreate(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "zipCreate expects (names: string[], contents: Uint8Array[])");
		return nullptr;
	}

	uint32_t nameCount = 0;
	if (napi_get_array_length(env, args[0], &nameCount) != napi_ok) {
		napi_throw_type_error(env, nullptr, "names must be an array");
		return nullptr;
	}
	uint32_t contentCount = 0;
	if (napi_get_array_length(env, args[1], &contentCount) != napi_ok) {
		napi_throw_type_error(env, nullptr, "contents must be an array");
		return nullptr;
	}
	if (nameCount != contentCount) {
		napi_throw_type_error(env, nullptr, "names.length must equal contents.length");
		return nullptr;
	}

	std::vector<std::string> names;
	names.reserve(nameCount);
	for (uint32_t i = 0; i < nameCount; i++) {
		napi_value item = nullptr;
		size_t strLen = 0;
		if (napi_get_element(env, args[0], i, &item) != napi_ok ||
		    napi_get_value_string_utf8(env, item, nullptr, 0, &strLen) != napi_ok) {
			napi_throw_type_error(env, nullptr, "names must contain strings");
			return nullptr;
		}
		std::string name(strLen, '\0');
		if (napi_get_value_string_utf8(env, item, name.data(), strLen + 1, &strLen) != napi_ok) {
			napi_throw_type_error(env, nullptr, "names must contain strings");
			return nullptr;
		}
		names.push_back(name);
	}

	std::vector<std::vector<uint8_t>> contents;
	contents.reserve(contentCount);
	for (uint32_t i = 0; i < contentCount; i++) {
		napi_value item = nullptr;
		if (napi_get_element(env, args[1], i, &item) != napi_ok) {
			napi_throw_type_error(env, nullptr, "contents must contain Uint8Array");
			return nullptr;
		}
		std::vector<uint8_t> bytes;
		if (!ParseBytesArg(env, item, bytes, "contents must contain Uint8Array or ArrayBuffer")) {
			return nullptr;
		}
		contents.push_back(std::move(bytes));
	}

	std::vector<uint8_t> out;
	if (!tui::ZipCreate(names, contents, -1, out)) {
		napi_throw_type_error(env, nullptr, "failed to create zip");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value ZipList(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "zipList expects (zipBytes)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "zipBytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	std::vector<std::string> names;
	if (!tui::ZipList(bytes.data(), bytes.size(), names)) {
		napi_throw_type_error(env, nullptr, "failed to read zip");
		return nullptr;
	}
	return MakeStringArray(env, names);
}

napi_value ZipReadEntry(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "zipReadEntry expects (zipBytes, name)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "zipBytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	size_t strLen = 0;
	if (napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen) != napi_ok) {
		napi_throw_type_error(env, nullptr, "name must be a string");
		return nullptr;
	}
	std::string name(strLen, '\0');
	if (napi_get_value_string_utf8(env, args[1], name.data(), strLen + 1, &strLen) != napi_ok) {
		napi_throw_type_error(env, nullptr, "name must be a string");
		return nullptr;
	}

	std::vector<uint8_t> out;
	if (!tui::ZipReadEntry(bytes.data(), bytes.size(), name, out)) {
		napi_throw_type_error(env, nullptr, "failed to read zip entry");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value ZipExtractAll(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "zipExtractAll expects (zipBytes)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "zipBytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::ZipExtractResult result;
	if (!tui::ZipExtractAll(bytes.data(), bytes.size(), result)) {
		napi_throw_type_error(env, nullptr, "failed to extract zip");
		return nullptr;
	}
	return MakeExtractResult(env, result);
}

napi_value GzipCompress(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value args[2] = {nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "gzipCompress expects (bytes, level?)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	double level = 6;
	if (argc >= 2) {
		if (napi_get_value_double(env, args[1], &level) != napi_ok) {
			napi_throw_type_error(env, nullptr, "level must be a number");
			return nullptr;
		}
	}

	std::vector<uint8_t> out;
	if (!tui::GzipCompress(bytes.data(), bytes.size(), static_cast<int>(level), out)) {
		napi_throw_type_error(env, nullptr, "failed to gzip compress");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value GzipDecompress(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "gzipDecompress expects (bytes)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	std::vector<uint8_t> out;
	if (!tui::GzipDecompress(bytes.data(), bytes.size(), out)) {
		napi_throw_type_error(env, nullptr, "failed to gzip decompress");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value Init(napi_env env, napi_value exports) {
	const struct {
		const char* name;
		napi_value (*fn)(napi_env, napi_callback_info);
	} functions[] = {
	    {"zipCreate", ZipCreate},         {"zipList", ZipList},
	    {"zipReadEntry", ZipReadEntry},   {"zipExtractAll", ZipExtractAll},
	    {"gzipCompress", GzipCompress},   {"gzipDecompress", GzipDecompress},
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
