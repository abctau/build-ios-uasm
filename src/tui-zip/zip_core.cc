#include "zip_core.h"

#include <cstdlib>
#include <cstring>

#include "vendor/zip.h"

// miniz 原语以 extern "C" 重新声明：miniz amalgamation 的实现只在
// vendor/zip.c 这一个编译单元里编译，这里不能 include miniz.h，
// 否则导出符号会重复定义。
extern "C" {

typedef unsigned long mz_ulong;

typedef void* (*mz_alloc_func_t)(void* opaque, size_t items, size_t size);
typedef void (*mz_free_func_t)(void* opaque, void* address);

typedef struct mz_internal_state_s mz_internal_state;

typedef struct {
	const unsigned char* next_in;
	unsigned int avail_in;
	mz_ulong total_in;

	unsigned char* next_out;
	unsigned int avail_out;
	mz_ulong total_out;

	char* msg;
	mz_internal_state* state;

	mz_alloc_func_t zalloc;
	mz_free_func_t zfree;
	void* opaque;

	int data_type;
	mz_ulong adler;
	mz_ulong reserved;
} mz_stream_shadow;

int mz_deflateInit2(mz_stream_shadow* pStream, int level, int method,
                    int window_bits, int mem_level, int strategy);
int mz_deflate(mz_stream_shadow* pStream, int flush);
int mz_deflateEnd(mz_stream_shadow* pStream);
int mz_inflateInit2(mz_stream_shadow* pStream, int window_bits);
int mz_inflate(mz_stream_shadow* pStream, int flush);
int mz_inflateEnd(mz_stream_shadow* pStream);
mz_ulong mz_crc32(mz_ulong crc, const unsigned char* ptr, size_t buf_len);
unsigned int mz_compressBound(unsigned int source_len);

}  // extern "C"

namespace {

constexpr int kMzDeflated = 8;
constexpr int kMzDefaultStrategy = 0;
constexpr int kMzDefaultWindowBits = 15;
constexpr int kMzNoFlush = 0;
constexpr int kMzFinish = 4;
constexpr int kMzOk = 0;
constexpr int kMzStreamEnd = 1;
constexpr int kMzDefaultLevel = 6;

uint32_t ReadLE32(const uint8_t* p) {
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void WriteLE32(uint8_t* p, uint32_t v) {
	p[0] = static_cast<uint8_t>(v & 0xFF);
	p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
	p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
	p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

int ClampLevel(int level) {
	if (level < 0) return kMzDefaultLevel;
	if (level > 9) return 9;
	return level;
}

}  // namespace

namespace tui {

bool ZipCreate(const std::vector<std::string>& names,
               const std::vector<std::vector<uint8_t>>& contents, int level,
               std::vector<uint8_t>& out) {
	if (names.size() != contents.size()) return false;

	zip_t* zip = zip_stream_open(nullptr, 0, ClampLevel(level), 'w');
	if (zip == nullptr) return false;

	bool ok = true;
	for (size_t i = 0; i < names.size(); i++) {
		if (zip_entry_open(zip, names[i].c_str()) != 0) {
			ok = false;
			break;
		}
		static const uint8_t kEmpty = 0;
		const void* buf = contents[i].empty() ? &kEmpty : contents[i].data();
		if (zip_entry_write(zip, buf, contents[i].size()) != 0 ||
		    zip_entry_close(zip) != 0) {
			ok = false;
			break;
		}
	}

	if (ok) {
		void* buf = nullptr;
		size_t bufsize = 0;
		if (zip_stream_copy(zip, &buf, &bufsize) < 0 || buf == nullptr) {
			ok = false;
		} else {
			out.assign(static_cast<uint8_t*>(buf),
			           static_cast<uint8_t*>(buf) + bufsize);
			free(buf);
		}
	}
	zip_stream_close(zip);
	return ok;
}

bool ZipList(const uint8_t* data, size_t size, std::vector<std::string>& out) {
	if (data == nullptr || size == 0) return false;

	zip_t* zip = zip_stream_open(reinterpret_cast<const char*>(data), size, 0, 'r');
	if (zip == nullptr) return false;

	const ssize_t total = zip_entries_total(zip);
	bool ok = total >= 0;
	for (ssize_t i = 0; ok && i < total; i++) {
		if (zip_entry_openbyindex(zip, static_cast<size_t>(i)) != 0) {
			ok = false;
			break;
		}
		const char* name = zip_entry_name(zip);
		if (zip_entry_isdir(zip) == 0 && name != nullptr) {
			out.push_back(std::string(name));
		}
		if (zip_entry_close(zip) != 0) ok = false;
	}
	zip_stream_close(zip);
	return ok;
}

bool ZipReadEntry(const uint8_t* data, size_t size, const std::string& name,
                  std::vector<uint8_t>& out) {
	if (data == nullptr || size == 0) return false;

	zip_t* zip = zip_stream_open(reinterpret_cast<const char*>(data), size, 0, 'r');
	if (zip == nullptr) return false;

	bool ok = true;
	if (zip_entry_open(zip, name.c_str()) != 0) {
		ok = false;
	} else {
		if (zip_entry_isdir(zip) != 0) {
			out.clear();  // 目录条目返回空内容
		} else {
			void* buf = nullptr;
			size_t bufsize = 0;
			if (zip_entry_read(zip, &buf, &bufsize) < 0 || buf == nullptr) {
				ok = false;
			} else {
				out.assign(static_cast<uint8_t*>(buf),
				           static_cast<uint8_t*>(buf) + bufsize);
				free(buf);
			}
		}
		if (zip_entry_close(zip) != 0) ok = false;
	}
	zip_stream_close(zip);
	return ok;
}

bool ZipExtractAll(const uint8_t* data, size_t size, ZipExtractResult& out) {
	if (data == nullptr || size == 0) return false;

	zip_t* zip = zip_stream_open(reinterpret_cast<const char*>(data), size, 0, 'r');
	if (zip == nullptr) return false;

	const ssize_t total = zip_entries_total(zip);
	bool ok = total >= 0;
	for (ssize_t i = 0; ok && i < total; i++) {
		if (zip_entry_openbyindex(zip, static_cast<size_t>(i)) != 0) {
			ok = false;
			break;
		}
		const char* name = zip_entry_name(zip);
		if (zip_entry_isdir(zip) == 0 && name != nullptr) {
			void* buf = nullptr;
			size_t bufsize = 0;
			if (zip_entry_read(zip, &buf, &bufsize) < 0 || buf == nullptr) {
				ok = false;
			} else {
				out.names.push_back(std::string(name));
				out.datas.emplace_back(static_cast<uint8_t*>(buf),
				                       static_cast<uint8_t*>(buf) + bufsize);
				free(buf);
			}
		}
		if (zip_entry_close(zip) != 0) ok = false;
	}
	zip_stream_close(zip);
	return ok;
}

bool GzipCompress(const uint8_t* data, size_t size, int level,
                  std::vector<uint8_t>& out) {
	if (data == nullptr && size > 0) return false;

	mz_stream_shadow s;
	std::memset(&s, 0, sizeof(s));
	if (mz_deflateInit2(&s, ClampLevel(level), kMzDeflated, -kMzDefaultWindowBits,
	                    9, kMzDefaultStrategy) != kMzOk) {
		return false;
	}

	const unsigned int bound = mz_compressBound(static_cast<unsigned int>(size));
	out.assign(10 + bound + 8, 0);
	static const uint8_t kGzipHeader[10] = {0x1F, 0x8B, 0x08, 0x00, 0x00,
	                                        0x00, 0x00, 0x00, 0x00, 0xFF};
	std::memcpy(out.data(), kGzipHeader, 10);

	s.next_in = data;
	s.avail_in = static_cast<unsigned int>(size);
	s.next_out = out.data() + 10;
	s.avail_out = bound;

	const int ret = mz_deflate(&s, kMzFinish);
	const mz_ulong produced = s.total_out;
	mz_deflateEnd(&s);
	if (ret != kMzStreamEnd) return false;

	const size_t bodyEnd = 10 + static_cast<size_t>(produced);
	const mz_ulong crc = mz_crc32(0, data, size);
	WriteLE32(out.data() + bodyEnd, static_cast<uint32_t>(crc));
	WriteLE32(out.data() + bodyEnd + 4, static_cast<uint32_t>(size));
	out.resize(bodyEnd + 8);
	return true;
}

bool GzipDecompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
	if (data == nullptr || size < 18) return false;
	if (data[0] != 0x1F || data[1] != 0x8B || data[2] != 0x08) return false;

	// RFC 1952：固定头 10 字节 + FLG 决定的可选字段
	const uint8_t flg = data[3];
	size_t pos = 10;
	if ((flg & 0x04) != 0) {  // FEXTRA
		if (pos + 2 > size) return false;
		const unsigned int xlen =
		    static_cast<unsigned int>(data[pos]) | (static_cast<unsigned int>(data[pos + 1]) << 8);
		pos += 2;
		if (pos + xlen > size) return false;
		pos += xlen;
	}
	if ((flg & 0x08) != 0) {  // FNAME
		while (pos < size && data[pos] != 0) pos++;
		if (pos >= size) return false;
		pos++;
	}
	if ((flg & 0x10) != 0) {  // FCOMMENT
		while (pos < size && data[pos] != 0) pos++;
		if (pos >= size) return false;
		pos++;
	}
	if ((flg & 0x02) != 0) {  // FHCRC
		if (pos + 2 > size) return false;
		pos += 2;
	}
	if (pos + 8 > size) return false;

	mz_stream_shadow s;
	std::memset(&s, 0, sizeof(s));
	if (mz_inflateInit2(&s, -kMzDefaultWindowBits) != kMzOk) return false;

	s.next_in = data + pos;
	s.avail_in = static_cast<unsigned int>(size - pos - 8);

	std::vector<uint8_t> result;
	uint8_t chunk[32768];
	bool ok = false;
	for (;;) {
		s.next_out = chunk;
		s.avail_out = sizeof(chunk);
		const int ret = mz_inflate(&s, kMzNoFlush);
		result.insert(result.end(), chunk, chunk + (sizeof(chunk) - s.avail_out));
		if (ret == kMzStreamEnd) {
			ok = true;
			break;
		}
		if (ret != kMzOk) break;
		if (s.avail_in == 0 && s.avail_out == sizeof(chunk)) break;  // 无进展，防死循环
	}
	mz_inflateEnd(&s);
	if (!ok) return false;

	// 校验尾部 CRC32 与 ISIZE
	const uint8_t* tail = data + (size - 8);
	const uint32_t expectCrc = ReadLE32(tail);
	const uint32_t expectSize = ReadLE32(tail + 4);
	const mz_ulong actualCrc =
	    mz_crc32(0, result.empty() ? nullptr : result.data(), result.size());
	if (static_cast<uint32_t>(actualCrc) != expectCrc) return false;
	if (static_cast<uint32_t>(result.size()) != expectSize) return false;

	out.swap(result);
	return true;
}

}  // namespace tui
