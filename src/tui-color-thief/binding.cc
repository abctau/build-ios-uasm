#include <node_api.h>

#include <string>

#include "color_thief.h"

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

napi_value MakeColor(napi_env env, const tui::RGB& color) {
  napi_value result = nullptr;
  if (napi_create_array_with_length(env, 3, &result) != napi_ok) return nullptr;
  const int v[3] = {color.r, color.g, color.b};
  for (int i = 0; i < 3; i++) {
    napi_value item = nullptr;
    if (napi_create_int32(env, v[i], &item) != napi_ok) return nullptr;
    if (napi_set_element(env, result, static_cast<uint32_t>(i), item) != napi_ok) return nullptr;
  }
  return result;
}

napi_value MakeColorMatrix(napi_env env, const std::vector<tui::RGB>& colors) {
  napi_value result = nullptr;
  if (napi_create_array_with_length(env, colors.size(), &result) != napi_ok) return nullptr;
  for (size_t i = 0; i < colors.size(); i++) {
    napi_value item = MakeColor(env, colors[i]);
    if (item == nullptr) return nullptr;
    if (napi_set_element(env, result, static_cast<uint32_t>(i), item) != napi_ok) return nullptr;
  }
  return result;
}

napi_value MakeResult(napi_env env, const tui::PaletteResult& palette) {
  napi_value result = nullptr;
  if (napi_create_object(env, &result) != napi_ok) return nullptr;

  napi_value width = nullptr;
  napi_value height = nullptr;
  napi_value dominant = nullptr;
  napi_value paletteArray = nullptr;
  napi_value edges = nullptr;

  if (napi_create_int32(env, palette.width, &width) != napi_ok ||
      napi_set_named_property(env, result, "width", width) != napi_ok) {
    return nullptr;
  }
  if (napi_create_int32(env, palette.height, &height) != napi_ok ||
      napi_set_named_property(env, result, "height", height) != napi_ok) {
    return nullptr;
  }

  dominant = MakeColor(env, palette.dominant);
  if (dominant == nullptr ||
      napi_set_named_property(env, result, "dominant", dominant) != napi_ok) {
    return nullptr;
  }

  paletteArray = MakeColorMatrix(env, palette.palette);
  if (paletteArray == nullptr ||
      napi_set_named_property(env, result, "palette", paletteArray) != napi_ok) {
    return nullptr;
  }

  std::vector<tui::RGB> edgeList(palette.edges, palette.edges + 4);
  edges = MakeColorMatrix(env, edgeList);
  if (edges == nullptr || napi_set_named_property(env, result, "edges", edges) != napi_ok) {
    return nullptr;
  }

  return result;
}

napi_value GetPalette(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2] = {nullptr, nullptr};
  if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok || argc < 1) {
    napi_throw_type_error(env, nullptr, "getPalette expects (bytes, colorCount?)");
    return nullptr;
  }

  const uint8_t* data = nullptr;
  size_t length = 0;
  if (!GetBytes(env, args[0], &data, &length)) {
    napi_throw_type_error(env, nullptr, "first argument must be a Uint8Array or ArrayBuffer");
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
  if (!tui::ExtractPaletteFromMemory(data, static_cast<int>(length),
                                     static_cast<int>(colorCount), palette)) {
    napi_throw_type_error(env, nullptr, "failed to decode image");
    return nullptr;
  }

  return MakeResult(env, palette);
}

napi_value Init(napi_env env, napi_value exports) {
  napi_value fn = nullptr;
  if (napi_create_function(env, "getPalette", NAPI_AUTO_LENGTH, GetPalette, nullptr, &fn) !=
      napi_ok) {
    return nullptr;
  }
  if (napi_set_named_property(env, exports, "getPalette", fn) != napi_ok) {
    return nullptr;
  }
  return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
