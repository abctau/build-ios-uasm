#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "pdf_core.h"

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

// offsets: flat [start0, len0, start1, len1, ...] as a JS array or Int32Array
bool ParseOffsets(napi_env env, napi_value value, std::vector<size_t>& out) {
	uint8_t* dummy = nullptr;
	(void)dummy;
	bool isArray = false;
	if (napi_is_array(env, value, &isArray) == napi_ok && isArray) {
		uint32_t len = 0;
		if (napi_get_array_length(env, value, &len) != napi_ok) {
			napi_throw_type_error(env, nullptr, "offsets must be a flat number array");
			return false;
		}
		for (uint32_t i = 0; i < len; i++) {
			napi_value item = nullptr;
			double v = 0;
			if (napi_get_element(env, value, i, &item) != napi_ok ||
			    napi_get_value_double(env, item, &v) != napi_ok) {
				napi_throw_type_error(env, nullptr, "offsets must be a flat number array");
				return false;
			}
			out.push_back(static_cast<size_t>(v));
		}
		return true;
	}

	napi_typedarray_type type;
	size_t byteOffset = 0;
	size_t arrayLength = 0;
	void* elementData = nullptr;
	napi_value buffer = nullptr;
	if (napi_get_typedarray_info(env, value, &type, &arrayLength, &elementData, &buffer,
	                             &byteOffset) == napi_ok &&
	    type == napi_int32_array) {
		const int32_t* nums = static_cast<const int32_t*>(elementData);
		for (size_t i = 0; i < arrayLength; i++) {
			out.push_back(static_cast<size_t>(nums[i] < 0 ? 0 : nums[i]));
		}
		return true;
	}

	napi_throw_type_error(env, nullptr, "offsets must be a flat number array or Int32Array");
	return false;
}

bool ParsePageSize(const std::string& s, tui::PdfPageSizeMode& out) {
	if (s == "fit" || s == "FIT") { out = tui::PdfPageSizeMode::FIT; return true; }
	if (s == "a4" || s == "A4") { out = tui::PdfPageSizeMode::A4; return true; }
	if (s == "a5" || s == "A5") { out = tui::PdfPageSizeMode::A5; return true; }
	if (s == "letter" || s == "LETTER") { out = tui::PdfPageSizeMode::LETTER; return true; }
	return false;
}

bool ParseOrientation(const std::string& s, tui::PdfOrientation& out) {
	if (s == "auto" || s == "AUTO") { out = tui::PdfOrientation::AUTO; return true; }
	if (s == "portrait" || s == "PORTRAIT") { out = tui::PdfOrientation::PORTRAIT; return true; }
	if (s == "landscape" || s == "LANDSCAPE") { out = tui::PdfOrientation::LANDSCAPE; return true; }
	return false;
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

napi_value ImagesToPdf(napi_env env, napi_callback_info info) {
	size_t argc = 5;
	napi_value args[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr,
		                      "imagesToPdf expects (bytes, offsets, pageSize?, orientation?, margin?)");
		return nullptr;
	}

	std::vector<uint8_t> packed;
	std::vector<size_t> offsets;
	if (!ParseBytesArg(env, args[0], packed, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}
	if (!ParseOffsets(env, args[1], offsets)) {
		return nullptr;
	}
	if (offsets.size() < 2 || offsets.size() % 2 != 0) {
		napi_throw_type_error(env, nullptr, "offsets must be flat [start, len, ...]");
		return nullptr;
	}

	tui::PdfBuildOptions opts;
	if (argc > 2 && args[2] != nullptr) {
		std::string pageSize;
		if (!ParseStringArg(env, args[2], pageSize, "pageSize must be a string")) return nullptr;
		if (!ParsePageSize(pageSize, opts.pageSize)) {
			napi_throw_type_error(env, nullptr, "pageSize must be 'fit'|'a4'|'a5'|'letter'");
			return nullptr;
		}
	}
	if (argc > 3 && args[3] != nullptr) {
		std::string orientation;
		if (!ParseStringArg(env, args[3], orientation, "orientation must be a string")) return nullptr;
		if (!ParseOrientation(orientation, opts.orientation)) {
			napi_throw_type_error(env, nullptr, "orientation must be 'auto'|'portrait'|'landscape'");
			return nullptr;
		}
	}
	if (argc > 4 && args[4] != nullptr) {
		double margin = 0;
		if (napi_get_value_double(env, args[4], &margin) != napi_ok) {
			napi_throw_type_error(env, nullptr, "margin must be a number");
			return nullptr;
		}
		opts.margin = static_cast<float>(margin);
	}

	std::vector<tui::PdfImageBlob> blobs;
	const size_t pairCount = offsets.size() / 2;
	blobs.reserve(pairCount);
	for (size_t i = 0; i < pairCount; i++) {
		const size_t start = offsets[i * 2];
		const size_t len = offsets[i * 2 + 1];
		if (start > packed.size() || start + len > packed.size()) {
			napi_throw_type_error(env, nullptr, "offsets out of range");
			return nullptr;
		}
		blobs.push_back({packed.data() + start, len});
	}

	std::vector<uint8_t> out;
	if (!tui::ImagesToPdf(blobs, opts, out)) {
		napi_throw_type_error(env, nullptr, "failed to build pdf (images must be jpeg or png)");
		return nullptr;
	}
	return MakeBytes(env, out);
}

napi_value GetPdfInfo(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value args[1] = {nullptr};
	if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "getPdfInfo expects (bytes)");
		return nullptr;
	}

	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, args[0], bytes, "bytes must be a Uint8Array or ArrayBuffer")) {
		return nullptr;
	}

	tui::PdfInfoResult pdfInfo;
	if (!tui::PdfInfoFromMemory(bytes.data(), bytes.size(), pdfInfo)) {
		napi_throw_type_error(env, nullptr, "failed to parse pdf");
		return nullptr;
	}

	napi_value result = nullptr;
	if (napi_create_object(env, &result) != napi_ok) return nullptr;
	napi_value version = nullptr;
	napi_value pageCount = nullptr;
	napi_value encrypted = nullptr;
	if (napi_create_string_utf8(env, pdfInfo.version, NAPI_AUTO_LENGTH, &version) != napi_ok ||
	    napi_create_int32(env, pdfInfo.pageCount, &pageCount) != napi_ok ||
	    napi_get_boolean(env, pdfInfo.encrypted, &encrypted) != napi_ok ||
	    napi_set_named_property(env, result, "version", version) != napi_ok ||
	    napi_set_named_property(env, result, "pageCount", pageCount) != napi_ok ||
	    napi_set_named_property(env, result, "encrypted", encrypted) != napi_ok) {
		return nullptr;
	}
	return result;
}

napi_value Init(napi_env env, napi_value exports) {
	const struct {
		const char* name;
		napi_value (*fn)(napi_env, napi_callback_info);
	} functions[] = {
	    {"imagesToPdf", ImagesToPdf},
	    {"getPdfInfo", GetPdfInfo},
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
