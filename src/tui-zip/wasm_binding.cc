#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "zip_core.h"

namespace {

emscripten::val MakeBytes(const std::vector<uint8_t>& data) {
	emscripten::val result = emscripten::val::global("Uint8Array").new_(data.size());
	if (!data.empty()) {
		emscripten::val view =
		    emscripten::val(emscripten::typed_memory_view(data.size(), data.data()));
		result.call<void>("set", view);
	}
	return result;
}

emscripten::val MakeStringArray(const std::vector<std::string>& values) {
	emscripten::val result = emscripten::val::array();
	for (size_t i = 0; i < values.size(); i++) {
		result.set(static_cast<int>(i), emscripten::val(values[i]));
	}
	return result;
}

std::vector<std::string> ParseStringArray(const emscripten::val& value) {
	std::vector<std::string> names;
	const unsigned int length = value["length"].as<unsigned int>();
	names.reserve(length);
	for (unsigned int i = 0; i < length; i++) {
		names.push_back(value[static_cast<int>(i)].as<std::string>());
	}
	return names;
}

std::vector<std::vector<uint8_t>> ParseBytesArray(const emscripten::val& value) {
	std::vector<std::vector<uint8_t>> contents;
	const unsigned int length = value["length"].as<unsigned int>();
	contents.reserve(length);
	for (unsigned int i = 0; i < length; i++) {
		contents.push_back(emscripten::vecFromJSArray<uint8_t>(value[static_cast<int>(i)]));
	}
	return contents;
}

emscripten::val ZipCreate(emscripten::val names, emscripten::val contents) {
	if (names.isUndefined() || names.isNull() || contents.isUndefined() ||
	    contents.isNull()) {
		emscripten::val::global("Error")
		    .new_(std::string("zipCreate expects (names, contents)"))
		    .throw_();
		return emscripten::val::undefined();
	}

	std::vector<uint8_t> out;
	if (!tui::ZipCreate(ParseStringArray(names), ParseBytesArray(contents), -1, out)) {
		emscripten::val::global("Error").new_(std::string("failed to create zip")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val ZipList(emscripten::val bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("zipList expects a Uint8Array")).throw_();
		return emscripten::val::undefined();
	}
	const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
	std::vector<std::string> names;
	if (!tui::ZipList(buffer.data(), buffer.size(), names)) {
		emscripten::val::global("Error").new_(std::string("failed to read zip")).throw_();
		return emscripten::val::undefined();
	}
	return MakeStringArray(names);
}

emscripten::val ZipReadEntry(emscripten::val bytes, const std::string& name) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("zipReadEntry expects a Uint8Array")).throw_();
		return emscripten::val::undefined();
	}
	const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
	std::vector<uint8_t> out;
	if (!tui::ZipReadEntry(buffer.data(), buffer.size(), name, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to read zip entry: ") + name)
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val ZipExtractAll(emscripten::val bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("zipExtractAll expects a Uint8Array")).throw_();
		return emscripten::val::undefined();
	}
	const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
	tui::ZipExtractResult result;
	if (!tui::ZipExtractAll(buffer.data(), buffer.size(), result)) {
		emscripten::val::global("Error").new_(std::string("failed to extract zip")).throw_();
		return emscripten::val::undefined();
	}

	emscripten::val names = emscripten::val::array();
	emscripten::val datas = emscripten::val::array();
	for (size_t i = 0; i < result.names.size(); i++) {
		names.set(static_cast<int>(i), emscripten::val(result.names[i]));
		datas.set(static_cast<int>(i), MakeBytes(result.datas[i]));
	}
	emscripten::val obj = emscripten::val::object();
	obj.set("names", names);
	obj.set("datas", datas);
	return obj;
}

emscripten::val GzipCompress(emscripten::val bytes, int level) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("gzipCompress expects a Uint8Array")).throw_();
		return emscripten::val::undefined();
	}
	const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
	std::vector<uint8_t> out;
	if (!tui::GzipCompress(buffer.data(), buffer.size(), level, out)) {
		emscripten::val::global("Error").new_(std::string("failed to gzip compress")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val GzipDecompress(emscripten::val bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("gzipDecompress expects a Uint8Array")).throw_();
		return emscripten::val::undefined();
	}
	const std::vector<uint8_t> buffer = emscripten::vecFromJSArray<uint8_t>(bytes);
	std::vector<uint8_t> out;
	if (!tui::GzipDecompress(buffer.data(), buffer.size(), out)) {
		emscripten::val::global("Error").new_(std::string("failed to gzip decompress")).throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_zip_uasm_module) {
	emscripten::function("zipCreate", &ZipCreate);
	emscripten::function("zipList", &ZipList);
	emscripten::function("zipReadEntry", &ZipReadEntry);
	emscripten::function("zipExtractAll", &ZipExtractAll);
	emscripten::function("gzipCompress", &GzipCompress);
	emscripten::function("gzipDecompress", &GzipDecompress);
}
