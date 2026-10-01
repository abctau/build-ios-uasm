#include <node_api.h>

namespace {

napi_value Add(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value args[2];
  if (napi_get_cb_info(env, info, &argc, args, nullptr, nullptr) != napi_ok ||
      argc < 2) {
    napi_throw_type_error(env, nullptr, "add expects two numbers");
    return nullptr;
  }

  double a;
  double b;
  if (napi_get_value_double(env, args[0], &a) != napi_ok ||
      napi_get_value_double(env, args[1], &b) != napi_ok) {
    napi_throw_type_error(env, nullptr, "add expects two numbers");
    return nullptr;
  }

  napi_value result;
  if (napi_create_double(env, a + b, &result) != napi_ok) {
    return nullptr;
  }
  return result;
}

napi_value Init(napi_env env, napi_value exports) {
  napi_value add;
  if (napi_create_function(
          env,
          "add",
          NAPI_AUTO_LENGTH,
          Add,
          nullptr,
          &add) != napi_ok) {
    return nullptr;
  }
  if (napi_set_named_property(env, exports, "add", add) != napi_ok) {
    return nullptr;
  }
  return exports;
}

}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
