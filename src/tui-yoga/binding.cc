#include <node_api.h>

#include <cstdint>
#include <string>
#include <vector>

#include "yoga_core.h"

namespace {

napi_value MakeU32(napi_env env, uint32_t v) {
	napi_value out = nullptr;
	napi_create_uint32(env, v, &out);
	return out;
}

napi_value MakeBool(napi_env env, bool v) {
	napi_value out = nullptr;
	napi_get_boolean(env, v, &out);
	return out;
}

bool GetU32Arg(napi_env env, napi_value v, uint32_t& out, const char* name) {
	double d = 0;
	if (napi_get_value_double(env, v, &d) != napi_ok || d < 0 || d > 4294967295.0) {
		napi_throw_type_error(env, nullptr, name);
		return false;
	}
	out = static_cast<uint32_t>(d);
	return true;
}

bool GetStringArg(napi_env env, napi_value v, std::string& out, const char* name) {
	size_t len = 0;
	if (napi_get_value_string_utf8(env, v, nullptr, 0, &len) != napi_ok) {
		napi_throw_type_error(env, nullptr, name);
		return false;
	}
	out.resize(len);
	if (len > 0 &&
	    napi_get_value_string_utf8(env, v, out.data(), len + 1, &len) != napi_ok) {
		napi_throw_type_error(env, nullptr, name);
		return false;
	}
	return true;
}

napi_value CreateNode(napi_env env, napi_callback_info info) {
	(void)info;
	return MakeU32(env, tui_yoga::CreateNode());
}

napi_value FreeNode(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (nodeId)");
		return nullptr;
	}
	uint32_t id = 0;
	if (!GetU32Arg(env, argv[0], id, "nodeId must be a number")) return nullptr;
	tui_yoga::FreeNode(id);
	return MakeBool(env, true);
}

napi_value FreeTree(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (rootId)");
		return nullptr;
	}
	uint32_t id = 0;
	if (!GetU32Arg(env, argv[0], id, "rootId must be a number")) return nullptr;
	tui_yoga::FreeTree(id);
	return MakeBool(env, true);
}

napi_value InsertChild(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value argv[3];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "expected (parentId, childId, index?)");
		return nullptr;
	}
	uint32_t parentId = 0;
	uint32_t childId = 0;
	if (!GetU32Arg(env, argv[0], parentId, "parentId must be a number") ||
	    !GetU32Arg(env, argv[1], childId, "childId must be a number")) return nullptr;
	int32_t index = -1;
	if (argc >= 3) {
		double d = 0;
		if (napi_get_value_double(env, argv[2], &d) != napi_ok) {
			napi_throw_type_error(env, nullptr, "index must be a number");
			return nullptr;
		}
		index = static_cast<int32_t>(d);
	}
	return MakeBool(env, tui_yoga::InsertChild(parentId, childId, index));
}

napi_value RemoveChild(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value argv[2];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "expected (parentId, childId)");
		return nullptr;
	}
	uint32_t parentId = 0;
	uint32_t childId = 0;
	if (!GetU32Arg(env, argv[0], parentId, "parentId must be a number") ||
	    !GetU32Arg(env, argv[1], childId, "childId must be a number")) return nullptr;
	return MakeBool(env, tui_yoga::RemoveChild(parentId, childId));
}

napi_value RegisterStyle(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (propsJson)");
		return nullptr;
	}
	std::string props;
	if (!GetStringArg(env, argv[0], props, "propsJson must be a string")) return nullptr;
	std::string err;
	const uint32_t styleId = tui_yoga::RegisterStyle(props.data(), props.size(), err);
	if (styleId == 0) {
		napi_throw_type_error(env, nullptr, err.c_str());
		return nullptr;
	}
	return MakeU32(env, styleId);
}

napi_value ApplyStyle(napi_env env, napi_callback_info info) {
	size_t argc = 2;
	napi_value argv[2];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 2) {
		napi_throw_type_error(env, nullptr, "expected (nodeId, styleId)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	uint32_t styleId = 0;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number") ||
	    !GetU32Arg(env, argv[1], styleId, "styleId must be a number")) return nullptr;
	return MakeBool(env, tui_yoga::ApplyStyle(nodeId, styleId));
}

napi_value SetStyle(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value argv[3];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "expected (nodeId, key, value)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	std::string key;
	std::string value;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number") ||
	    !GetStringArg(env, argv[1], key, "key must be a string") ||
	    !GetStringArg(env, argv[2], value, "value must be a string")) return nullptr;
	return MakeBool(env, tui_yoga::SetStyle(nodeId, key.data(), key.size(), value.data(), value.size()));
}

napi_value SetStyleNum(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value argv[3];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "expected (nodeId, key, value)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	std::string key;
	double value = 0;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number") ||
	    !GetStringArg(env, argv[1], key, "key must be a string") ||
	    napi_get_value_double(env, argv[2], &value) != napi_ok) {
		napi_throw_type_error(env, nullptr, "invalid args");
		return nullptr;
	}
	return MakeBool(env, tui_yoga::SetStyleNum(nodeId, key.data(), key.size(), value));
}

napi_value SetMeasuredSize(napi_env env, napi_callback_info info) {
	size_t argc = 3;
	napi_value argv[3];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "expected (nodeId, width, height)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	double w = 0;
	double h = 0;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number") ||
	    napi_get_value_double(env, argv[1], &w) != napi_ok ||
	    napi_get_value_double(env, argv[2], &h) != napi_ok) {
		napi_throw_type_error(env, nullptr, "invalid args");
		return nullptr;
	}
	return MakeBool(env, tui_yoga::SetMeasuredSize(nodeId, w, h));
}

napi_value CalculateLayout(napi_env env, napi_callback_info info) {
	size_t argc = 4;
	napi_value argv[4];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 3) {
		napi_throw_type_error(env, nullptr, "expected (rootId, availWidth, availHeight, direction?)");
		return nullptr;
	}
	uint32_t rootId = 0;
	double w = 0;
	double h = 0;
	int32_t direction = 0;
	if (!GetU32Arg(env, argv[0], rootId, "rootId must be a number") ||
	    napi_get_value_double(env, argv[1], &w) != napi_ok ||
	    napi_get_value_double(env, argv[2], &h) != napi_ok) {
		napi_throw_type_error(env, nullptr, "invalid args");
		return nullptr;
	}
	if (argc >= 4) {
		double d = 0;
		if (napi_get_value_double(env, argv[3], &d) != napi_ok) {
			napi_throw_type_error(env, nullptr, "direction must be a number");
			return nullptr;
		}
		direction = static_cast<int32_t>(d);
	}
	return MakeBool(env, tui_yoga::CalculateLayout(rootId, w, h, direction));
}

napi_value CollectFrames(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (rootId)");
		return nullptr;
	}
	uint32_t rootId = 0;
	if (!GetU32Arg(env, argv[0], rootId, "rootId must be a number")) return nullptr;
	std::vector<double> frames;
	if (!tui_yoga::CollectFrames(rootId, frames)) {
		napi_throw_type_error(env, nullptr, "root not found");
		return nullptr;
	}
	napi_value arr = nullptr;
	napi_create_array_with_length(env, frames.size(), &arr);
	for (size_t i = 0; i < frames.size(); ++i) {
		napi_value v = nullptr;
		napi_create_double(env, frames[i], &v);
		napi_set_element(env, arr, static_cast<uint32_t>(i), v);
	}
	return arr;
}

napi_value GetFrame(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (nodeId)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number")) return nullptr;
	double frame[5] = { 0, 0, 0, 0, 0 };
	if (!tui_yoga::GetFrame(nodeId, frame)) {
		napi_throw_type_error(env, nullptr, "node not found");
		return nullptr;
	}
	napi_value arr = nullptr;
	napi_create_array_with_length(env, 5, &arr);
	for (int i = 0; i < 5; ++i) {
		napi_value v = nullptr;
		napi_create_double(env, frame[i], &v);
		napi_set_element(env, arr, static_cast<uint32_t>(i), v);
	}
	return arr;
}

napi_value IsDirty(napi_env env, napi_callback_info info) {
	size_t argc = 1;
	napi_value argv[1];
	if (napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr) != napi_ok || argc < 1) {
		napi_throw_type_error(env, nullptr, "expected (nodeId)");
		return nullptr;
	}
	uint32_t nodeId = 0;
	if (!GetU32Arg(env, argv[0], nodeId, "nodeId must be a number")) return nullptr;
	bool dirty = false;
	if (!tui_yoga::IsDirty(nodeId, dirty)) {
		napi_throw_type_error(env, nullptr, "node not found");
		return nullptr;
	}
	return MakeBool(env, dirty);
}

napi_value Init(napi_env env, napi_value exports) {
	napi_property_descriptor props[] = {
		{"createNode", nullptr, CreateNode, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"freeNode", nullptr, FreeNode, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"freeTree", nullptr, FreeTree, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"insertChild", nullptr, InsertChild, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"removeChild", nullptr, RemoveChild, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"registerStyle", nullptr, RegisterStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"applyStyle", nullptr, ApplyStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setStyle", nullptr, SetStyle, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setStyleNum", nullptr, SetStyleNum, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"setMeasuredSize", nullptr, SetMeasuredSize, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"calculateLayout", nullptr, CalculateLayout, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"collectFrames", nullptr, CollectFrames, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"getFrame", nullptr, GetFrame, nullptr, nullptr, nullptr, napi_default, nullptr},
		{"isDirty", nullptr, IsDirty, nullptr, nullptr, nullptr, napi_default, nullptr},
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
