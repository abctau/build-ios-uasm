#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "pdf_core.h"

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

std::vector<uint8_t> RequireBytes(const emscripten::val& bytes) {
	if (bytes.isUndefined() || bytes.isNull()) {
		emscripten::val::global("Error").new_(std::string("expected a Uint8Array")).throw_();
		return std::vector<uint8_t>();
	}
	return emscripten::vecFromJSArray<uint8_t>(bytes);
}

std::vector<int> RequireInts(const emscripten::val& arr) {
	if (arr.isUndefined() || arr.isNull()) {
		emscripten::val::global("Error").new_(std::string("expected an array")).throw_();
		return std::vector<int>();
	}
	return emscripten::vecFromJSArray<int>(arr);
}

emscripten::val ImagesToPdf(emscripten::val packedVal, emscripten::val offsetsVal,
                            const std::string& pageSize, const std::string& orientation,
                            double margin) {
	const std::vector<uint8_t> packed = RequireBytes(packedVal);
	const std::vector<int> offsets = RequireInts(offsetsVal);
	if (offsets.size() < 2 || offsets.size() % 2 != 0) {
		emscripten::val::global("Error")
		    .new_(std::string("offsets must be flat [start, len, ...]"))
		    .throw_();
		return emscripten::val::undefined();
	}

	tui::PdfBuildOptions opts;
	if (pageSize == "a4" || pageSize == "A4") {
		opts.pageSize = tui::PdfPageSizeMode::A4;
	} else if (pageSize == "a5" || pageSize == "A5") {
		opts.pageSize = tui::PdfPageSizeMode::A5;
	} else if (pageSize == "letter" || pageSize == "LETTER") {
		opts.pageSize = tui::PdfPageSizeMode::LETTER;
	} else {
		opts.pageSize = tui::PdfPageSizeMode::FIT;
	}
	if (orientation == "portrait" || orientation == "PORTRAIT") {
		opts.orientation = tui::PdfOrientation::PORTRAIT;
	} else if (orientation == "landscape" || orientation == "LANDSCAPE") {
		opts.orientation = tui::PdfOrientation::LANDSCAPE;
	} else {
		opts.orientation = tui::PdfOrientation::AUTO;
	}
	opts.margin = static_cast<float>(margin);

	std::vector<tui::PdfImageBlob> blobs;
	const size_t pairCount = offsets.size() / 2;
	blobs.reserve(pairCount);
	for (size_t i = 0; i < pairCount; i++) {
		const size_t start = static_cast<size_t>(offsets[i * 2] < 0 ? 0 : offsets[i * 2]);
		const size_t len = static_cast<size_t>(offsets[i * 2 + 1] < 0 ? 0 : offsets[i * 2 + 1]);
		if (start > packed.size() || start + len > packed.size()) {
			emscripten::val::global("Error")
			    .new_(std::string("offsets out of range"))
			    .throw_();
			return emscripten::val::undefined();
		}
		blobs.push_back({packed.data() + start, len});
	}

	std::vector<uint8_t> out;
	if (!tui::ImagesToPdf(blobs, opts, out)) {
		emscripten::val::global("Error")
		    .new_(std::string("failed to build pdf (images must be jpeg or png)"))
		    .throw_();
		return emscripten::val::undefined();
	}
	return MakeBytes(out);
}

emscripten::val GetPdfInfo(emscripten::val bytes) {
	const std::vector<uint8_t> buffer = RequireBytes(bytes);
	tui::PdfInfoResult info;
	if (!tui::PdfInfoFromMemory(buffer.data(), buffer.size(), info)) {
		emscripten::val::global("Error").new_(std::string("failed to parse pdf")).throw_();
		return emscripten::val::undefined();
	}
	emscripten::val result = emscripten::val::object();
	result.set("version", std::string(info.version));
	result.set("pageCount", info.pageCount);
	result.set("encrypted", info.encrypted);
	return result;
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_pdf_uasm_module) {
	emscripten::function("imagesToPdf", &ImagesToPdf);
	emscripten::function("getPdfInfo", &GetPdfInfo);
}
