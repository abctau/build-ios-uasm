#include "pdf_core.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_FAILURE_STRINGS
#include "vendor/stb_image.h"

#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_ARCHIVE_APIS
#include "vendor/miniz.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>

namespace tui {

namespace {

// ---------------------------------------------------------------------------
// byte writer
// ---------------------------------------------------------------------------

struct ByteWriter {
	std::vector<uint8_t> buf;
	std::vector<size_t> objOffsets;  // index = objNum - 1

	void Raw(const char* s) {
		buf.insert(buf.end(), s, s + std::strlen(s));
	}

	void Bytes(const uint8_t* p, size_t n) {
		buf.insert(buf.end(), p, p + n);
	}

	void Int(long long v) {
		char tmp[24];
		const int n = std::snprintf(tmp, sizeof(tmp), "%lld", v);
		buf.insert(buf.end(), tmp, tmp + n);
	}

	void Float2(float v) {
		char tmp[32];
		const int n = std::snprintf(tmp, sizeof(tmp), "%.2f", v);
		buf.insert(buf.end(), tmp, tmp + n);
	}

	void BeginObj(int objNum) {
		objOffsets.resize(static_cast<size_t>(objNum > static_cast<int>(objOffsets.size()) ? objNum : static_cast<int>(objOffsets.size())));
		objOffsets[static_cast<size_t>(objNum - 1)] = buf.size();
		Int(objNum);
		Raw(" 0 obj\n");
	}

	size_t Pos() const { return buf.size(); }
};

// ---------------------------------------------------------------------------
// image classification
// ---------------------------------------------------------------------------

bool IsJpeg(const uint8_t* d, size_t n) {
	return n > 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
}

bool IsPng(const uint8_t* d, size_t n) {
	return n > 8 && d[0] == 0x89 && d[1] == 0x50 && d[2] == 0x4E && d[3] == 0x47;
}

struct EmbeddedImage {
	std::vector<uint8_t> stream;  // raw stream payload
	int width;
	int height;
	int components;  // 1 (gray) or 3 (rgb)
	bool dctDecode;  // true = JPEG passed through
};

// Decode any raster format to RGB and zlib-compress for FlateDecode.
bool MakeFlateImage(const uint8_t* data, size_t size, EmbeddedImage& out) {
	int w = 0;
	int h = 0;
	int comp = 0;
	uint8_t* pixels = stbi_load_from_memory(data, static_cast<int>(size), &w, &h, &comp, 3);
	if (pixels == nullptr || w <= 0 || h <= 0) {
		if (pixels != nullptr) stbi_image_free(pixels);
		return false;
	}
	const size_t rgbLen = static_cast<size_t>(w) * static_cast<size_t>(h) * 3;
	mz_ulong destLen = mz_compressBound(static_cast<mz_ulong>(rgbLen));
	std::vector<uint8_t> dest(static_cast<size_t>(destLen));
	if (mz_compress2(dest.data(), &destLen, pixels, static_cast<mz_ulong>(rgbLen), 6) != MZ_OK) {
		stbi_image_free(pixels);
		return false;
	}
	stbi_image_free(pixels);
	out.stream.assign(dest.data(), dest.data() + destLen);
	out.width = w;
	out.height = h;
	out.components = 3;
	out.dctDecode = false;
	return true;
}

// Validate JPEG and extract dimensions/components via stb_info (no decode).
bool MakeDctImage(const uint8_t* data, size_t size, EmbeddedImage& out) {
	int w = 0;
	int h = 0;
	int comp = 0;
	if (stbi_info_from_memory(data, static_cast<int>(size), &w, &h, &comp) == 0) {
		return false;
	}
	if (w <= 0 || h <= 0) return false;
	if (comp != 1 && comp != 3) return false;  // CMYK JPEGs go through the flate path
	out.stream.assign(data, data + size);
	out.width = w;
	out.height = h;
	out.components = comp;
	out.dctDecode = true;
	return true;
}

bool BuildEmbeddedImage(const uint8_t* data, size_t size, EmbeddedImage& out) {
	if (IsJpeg(data, size)) {
		// CMYK JPEGs cannot be embedded directly with DeviceRGB; decode them.
		if (MakeDctImage(data, size, out)) return true;
		return MakeFlateImage(data, size, out);
	}
	return MakeFlateImage(data, size, out);
}

void PageBox(PdfPageSizeMode mode, PdfOrientation orient, float margin, float iw, float ih,
             float& pw, float& ph, float& ix, float& iy, float& sw, float& sh) {
	if (mode == PdfPageSizeMode::FIT) {
		pw = iw;
		ph = ih;
		ix = 0.f;
		iy = 0.f;
		sw = iw;
		sh = ih;
		return;
	}
	// fixed sizes in pt
	switch (mode) {
		case PdfPageSizeMode::A4:
			pw = 595.28f;
			ph = 841.89f;
			break;
		case PdfPageSizeMode::A5:
			pw = 419.53f;
			ph = 595.28f;
			break;
		default:
			pw = 612.f;   // letter
			ph = 792.f;
			break;
	}
	if (orient == PdfOrientation::LANDSCAPE) {
		std::swap(pw, ph);
	} else if (orient == PdfOrientation::AUTO && iw > ih) {
		std::swap(pw, ph);
	}
	float mw = margin;
	float mh = margin;
	if (mw < 0.f) mw = 0.f;
	if (mh < 0.f) mh = 0.f;
	const float availW = std::max(pw - mw * 2.f, 1.f);
	const float availH = std::max(ph - mh * 2.f, 1.f);
	const float scale = std::min(availW / iw, availH / ih);
	sw = iw * scale;
	sh = ih * scale;
	ix = (pw - sw) / 2.f;
	iy = (ph - sh) / 2.f;
}

}  // namespace

bool ImagesToPdf(const std::vector<PdfImageBlob>& images, const PdfBuildOptions& opts,
                 std::vector<uint8_t>& out) {
	if (images.empty()) return false;

	std::vector<EmbeddedImage> embedded;
	embedded.reserve(images.size());
	for (const PdfImageBlob& blob : images) {
		if (blob.data == nullptr || blob.size == 0) return false;
		EmbeddedImage img;
		if (!BuildEmbeddedImage(blob.data, blob.size, img)) return false;
		embedded.push_back(std::move(img));
	}

	ByteWriter w;
	w.Raw("%PDF-1.5\n%\xE2\xE3\xCF\xD3\n");

	// Object layout: 1 catalog, 2 pages, then per page i (0-based):
	//   3 + i*3 = page, 4 + i*3 = contents, 5 + i*3 = image
	const int pageCount = static_cast<int>(embedded.size());

	w.BeginObj(1);
	w.Raw("<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

	w.BeginObj(2);
	w.Raw("<< /Type /Pages /Kids [");
	for (int i = 0; i < pageCount; i++) {
		if (i > 0) w.Raw(" ");
		w.Int(3 + static_cast<long long>(i) * 3);
		w.Raw(" 0 R");
	}
	w.Raw("] /Count ");
	w.Int(pageCount);
	w.Raw(" >>\nendobj\n");

	for (int i = 0; i < pageCount; i++) {
		const EmbeddedImage& img = embedded[static_cast<size_t>(i)];
		const int pageObj = 3 + i * 3;
		const int contentObj = 4 + i * 3;
		const int imageObj = 5 + i * 3;

		float pw, ph, ix, iy, sw, sh;
		PageBox(opts.pageSize, opts.orientation, opts.margin,
		        static_cast<float>(img.width), static_cast<float>(img.height),
		        pw, ph, ix, iy, sw, sh);

		// content stream
		std::string content;
		{
			char tmp[128];
			std::snprintf(tmp, sizeof(tmp), "q\n%.2f 0 0 %.2f %.2f %.2f cm\n/Im Do\nQ\n",
			              sw, sh, ix, iy);
			content = tmp;
		}

		w.BeginObj(pageObj);
		w.Raw("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ");
		w.Float2(pw);
		w.Raw(" ");
		w.Float2(ph);
		w.Raw("] /Resources << /XObject << /Im ");
		w.Int(imageObj);
		w.Raw(" 0 R >> >> /Contents ");
		w.Int(contentObj);
		w.Raw(" 0 R >>\nendobj\n");

		w.BeginObj(contentObj);
		w.Raw("<< /Length ");
		w.Int(static_cast<long long>(content.size()));
		w.Raw(" >>\nstream\n");
		w.Bytes(reinterpret_cast<const uint8_t*>(content.data()), content.size());
		w.Raw("\nendstream\nendobj\n");

		w.BeginObj(imageObj);
		w.Raw("<< /Type /XObject /Subtype /Image /Width ");
		w.Int(img.width);
		w.Raw(" /Height ");
		w.Int(img.height);
		w.Raw(" /ColorSpace /Device");
		if (img.components == 1) {
			w.Raw("Gray");
		} else {
			w.Raw("RGB");
		}
		w.Raw(" /BitsPerComponent 8 /Filter ");
		if (img.dctDecode) {
			w.Raw("/DCTDecode");
		} else {
			w.Raw("/FlateDecode");
		}
		w.Raw(" /Length ");
		w.Int(static_cast<long long>(img.stream.size()));
		w.Raw(" >>\nstream\n");
		w.Bytes(img.stream.data(), img.stream.size());
		w.Raw("\nendstream\nendobj\n");
	}

	// xref table
	const int totalObjs = 2 + pageCount * 3;
	const size_t xrefPos = w.Pos();
	w.Raw("xref\n0 ");
	w.Int(static_cast<long long>(totalObjs) + 1);
	w.Raw("\n0000000000 65535 f \n");
	for (int i = 1; i <= totalObjs; i++) {
		char tmp[32];
		std::snprintf(tmp, sizeof(tmp), "%010lld 00000 n \n",
		              static_cast<long long>(w.objOffsets[static_cast<size_t>(i - 1)]));
		w.Raw(tmp);
	}
	w.Raw("trailer\n<< /Size ");
	w.Int(static_cast<long long>(totalObjs) + 1);
	w.Raw(" /Root 1 0 R >>\nstartxref\n");
	w.Int(static_cast<long long>(xrefPos));
	w.Raw("\n%%EOF\n");

	out = std::move(w.buf);
	return true;
}

// ---------------------------------------------------------------------------
// heuristic info scan
// ---------------------------------------------------------------------------

namespace {

// Binary-safe search for an ASCII needle.
const uint8_t* FindAscii(const uint8_t* hay, size_t len, const char* needle, size_t from = 0) {
	const size_t nLen = std::strlen(needle);
	if (nLen == 0 || len < nLen) return nullptr;
	for (size_t i = from; i + nLen <= len; i++) {
		if (hay[i] != static_cast<uint8_t>(needle[0])) continue;
		if (std::memcmp(hay + i, needle, nLen) == 0) return hay + i;
	}
	return nullptr;
}

int CountPattern(const uint8_t* data, size_t len, const char* needle,
                 const char* rejectSuffix = nullptr) {
	int count = 0;
	size_t from = 0;
	while (true) {
		const uint8_t* hit = FindAscii(data, len, needle, from);
		if (hit == nullptr) break;
		const size_t at = static_cast<size_t>(hit - data);
		if (rejectSuffix != nullptr) {
			const size_t rLen = std::strlen(rejectSuffix);
			if (at + std::strlen(needle) + rLen <= len &&
			    std::memcmp(hit + std::strlen(needle), rejectSuffix, rLen) == 0) {
				from = at + 1;
				continue;
			}
		}
		count++;
		from = at + 1;
	}
	return count;
}

int FindMaxCount(const uint8_t* data, size_t len) {
	int maxCount = 0;
	size_t from = 0;
	const char* pat = "/Count ";
	while (true) {
		const uint8_t* hit = FindAscii(data, len, pat, from);
		if (hit == nullptr) break;
		const size_t at = static_cast<size_t>(hit - data) + std::strlen(pat);
		int v = 0;
		bool any = false;
		size_t i = at;
		while (i < len && data[i] >= '0' && data[i] <= '9' && i - at < 9) {
			v = v * 10 + (data[i] - '0');
			i++;
			any = true;
		}
		if (any && v > maxCount) maxCount = v;
		from = at;
	}
	return maxCount;
}

}  // namespace

bool PdfInfoFromMemory(const uint8_t* data, size_t size, PdfInfoResult& out) {
	if (data == nullptr || size < 8) return false;
	if (!(data[0] == '%' && data[1] == 'P' && data[2] == 'D' && data[3] == 'F' && data[4] == '-')) {
		return false;
	}

	std::memset(&out, 0, sizeof(out));

	// version: parse "x.y" right after %PDF-
	{
		size_t i = 5;
		size_t v = 0;
		while (i < size && v + 1 < sizeof(out.version) && data[i] >= '0' && data[i] <= '9') {
			out.version[v++] = static_cast<char>(data[i]);
			i++;
		}
		if (i < size && data[i] == '.') {
			out.version[v++] = '.';
			i++;
			while (i < size && v + 1 < sizeof(out.version) && data[i] >= '0' && data[i] <= '9') {
				out.version[v++] = static_cast<char>(data[i]);
				i++;
			}
		}
		out.version[v] = '\0';
	}

	// page count: /Type /Page not followed by "s"; fall back to max /Count
	const int byType = CountPattern(data, size, "/Type /Page", "s")
	                   + CountPattern(data, size, "/Type/Page", "s");
	const int byCount = FindMaxCount(data, size);
	out.pageCount = std::max(byType, byCount);

	out.encrypted = FindAscii(data, size, "/Encrypt") != nullptr;
	return true;
}

}  // namespace tui
