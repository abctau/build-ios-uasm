#ifndef TUI_PDF_CORE_H
#define TUI_PDF_CORE_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tui {

struct PdfImageBlob {
	const uint8_t* data;
	size_t size;
};

enum class PdfPageSizeMode {
	FIT,      // page size = image pixel size (1px = 1pt)
	A4,
	A5,
	LETTER,
};

enum class PdfOrientation {
	AUTO,       // landscape swaps fixed pages; FIT ignores this
	PORTRAIT,
	LANDSCAPE,
};

struct PdfBuildOptions {
	PdfPageSizeMode pageSize = PdfPageSizeMode::FIT;
	PdfOrientation orientation = PdfOrientation::AUTO;
	float margin = 0.f;  // pt, only used with fixed page sizes
};

struct PdfInfoResult {
	char version[8];     // "1.7" etc, from the %PDF- header; empty if unknown
	int pageCount;       // heuristic: max(/Type /Page occurrences, /Count max)
	bool encrypted;      // /Encrypt found in trailer/xref context
};

// Build a PDF from images (JPEG embedded losslessly via DCTDecode; PNG and
// other formats decoded and embedded as FlateDecode DeviceRGB).
bool ImagesToPdf(const std::vector<PdfImageBlob>& images, const PdfBuildOptions& opts,
                 std::vector<uint8_t>& out);

// Heuristic PDF metadata scan: header version, page count, encryption flag.
bool PdfInfoFromMemory(const uint8_t* data, size_t size, PdfInfoResult& out);

}  // namespace tui

#endif  // TUI_PDF_CORE_H
