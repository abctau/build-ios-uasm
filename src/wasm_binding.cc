#include <emscripten/bind.h>

namespace {

double Add(double a, double b) { return a + b; }

}  // namespace

EMSCRIPTEN_BINDINGS(uasm_test_uasm_module) {
  emscripten::function("add", &Add);
}
