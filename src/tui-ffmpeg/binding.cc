#include <time.h>
#include <pthread.h>

#include <node_api.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ffmpeg_core.h"
#include "transcode_core.h"

namespace {

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

bool ParseIntArg(napi_env env, napi_value value, int& out, const char* argName) {
	double v = 0;
	if (napi_get_value_double(env, value, &v) != napi_ok || !std::isfinite(v) ||
	    v < -2147483648.0 || v > 2147483647.0) {
		napi_throw_type_error(env, nullptr, argName);
		return false;
	}
	out = static_cast<int>(v);
	return true;
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

napi_value GetMediaInfo(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok ||
	    argc < 1) {
		napi_throw_type_error(env, nullptr, "expected media bytes");
		return nullptr;
	}
	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, argv[0], bytes, "expected media bytes")) {
		return nullptr;
	}
	std::string json;
	if (!tui::GetMediaInfo(bytes.data(), bytes.size(), json)) {
		napi_throw_type_error(env, nullptr, "failed to parse media (unsupported container)");
		return nullptr;
	}
	napi_value result = nullptr;
	if (napi_create_string_utf8(env, json.c_str(), json.size(), &result) != napi_ok) {
		napi_throw_type_error(env, nullptr, "failed to create result string");
		return nullptr;
	}
	return result;
}

napi_value ExtractFrame(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value argv[3];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok ||
	    argc < 3) {
		napi_throw_type_error(env, nullptr, "expected (bytes, timeMs, maxWidth)");
		return nullptr;
	}
	std::vector<uint8_t> bytes;
	if (!ParseBytesArg(env, argv[0], bytes, "expected media bytes")) {
		return nullptr;
	}
	int timeMs = 0;
	int maxWidth = 0;
	if (!ParseIntArg(env, argv[1], timeMs, "timeMs must be a number") ||
	    !ParseIntArg(env, argv[2], maxWidth, "maxWidth must be a number")) {
		return nullptr;
	}
	std::vector<uint8_t> png;
	if (!tui::ExtractFrame(bytes.data(), bytes.size(), timeMs, maxWidth, png)) {
		napi_throw_type_error(env, nullptr, "failed to extract frame");
		return nullptr;
	}
	return MakeBytes(env, png);
}


// ---------- runJob（异步转码任务：JSON options → result JSON） ----------

struct JobCarrier {
	std::string optionsJson;
	tui::JobOptions opt;
	std::string err;
	bool ok = false;
	napi_async_work work = nullptr;
	napi_deferred deferred = nullptr;
};

void JobExecute(napi_env env, void* data) {
	auto* c = static_cast<JobCarrier*>(data);
	if (!tui::ParseJobOptions(c->optionsJson, c->opt, c->err)) return;
	std::vector<uint8_t> unused;
	c->ok = tui::TranscodeRun(c->opt, unused, c->err);
}

void JobComplete(napi_env env, napi_status, void* data) {
	auto* c = static_cast<JobCarrier*>(data);
	napi_value res;
	std::string out = c->ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"" + c->err + "\"}";
	napi_create_string_utf8(env, out.c_str(), out.size(), &res);
	if (c->ok) napi_resolve_deferred(env, c->deferred, res);
	else napi_reject_deferred(env, c->deferred, res);
	napi_delete_async_work(env, c->work);
	delete c;
}

napi_value RunJob(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr);
	if (argc < 1) {
		napi_throw_type_error(env, nullptr, "expected optionsJson string");
		return nullptr;
	}
	size_t len = 0;
	napi_get_value_string_utf8(env, argv[0], nullptr, 0, &len);
	std::string optionsJson(len, 0);
	napi_get_value_string_utf8(env, argv[0], optionsJson.data(), len + 1, &len);

	auto* c = new JobCarrier();
	c->optionsJson = optionsJson;
	napi_value name, promise;
	napi_create_string_utf8(env, "ffmpeg-job", NAPI_AUTO_LENGTH, &name);
	if (napi_create_promise(env, &c->deferred, &promise) != napi_ok) {
		delete c;
		napi_throw_type_error(env, nullptr, "create promise failed");
		return nullptr;
	}
	if (napi_create_async_work(env, nullptr, name, JobExecute, JobComplete, c,
	                           &c->work) != napi_ok) {
		delete c;
		napi_throw_type_error(env, nullptr, "create async work failed");
		return nullptr;
	}
	napi_queue_async_work(env, c->work);
	return promise;
}

napi_value Init(napi_env env, napi_value exports) {
	napi_property_descriptor props[] = {
	    {"getMediaInfo", nullptr, GetMediaInfo, nullptr, nullptr, nullptr,
	     napi_default, nullptr},
	    {"extractFrame", nullptr, ExtractFrame, nullptr, nullptr, nullptr,
	     napi_default, nullptr},
	    {"runJob", nullptr, RunJob, nullptr, nullptr, nullptr,
	     napi_default, nullptr},
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
