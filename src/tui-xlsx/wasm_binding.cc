#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "xlsx_core.h"

namespace {

constexpr size_t kMaxUncompressed = 64u * 1024 * 1024;

emscripten::val MakeBytes(const std::vector<uint8_t>& data) {
	emscripten::val result = emscripten::val::global("Uint8Array").new_(data.size());
	if (!data.empty()) {
		emscripten::val view =
		    emscripten::val(emscripten::typed_memory_view(data.size(), data.data()));
		result.call<void>("set", view);
	}
	return result;
}

std::vector<uint8_t> RequireBytes(const emscripten::val& bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("expected a Uint8Array")).throw_();
		return std::vector<uint8_t>();
	}
	return emscripten::vecFromJSArray<uint8_t>(bytes);
}

emscripten::val WriteXlsx(const std::string& sheetsJson) {
	std::vector<uint8_t> out;
	if (!tui::WriteXlsx(sheetsJson, out)) {
		emscripten::val::global("Error")
		    .new_(std::string(
		        "failed to write xlsx (sheets must be [{name,rows}] with "
		        "string/number/bool/null cells)"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

std::string ReadXlsx(emscripten::val bytes) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	std::string out;
	if (!tui::ReadXlsx(buffer.data(), buffer.size(), out, kMaxUncompressed)) {
		emscripten::val::global("Error").new_(std::string("failed to parse xlsx")).throw_();
		return std::string();
	}
	return out;
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_xlsx_uasm_module) {
	emscripten::function("writeXlsx", &WriteXlsx);
	emscripten::function("readXlsx", &ReadXlsx);
}
