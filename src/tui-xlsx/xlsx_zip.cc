#include "xlsx_zip.h"

#include <cstdlib>
#include <cstring>

#include "vendor/zip.h"

// miniz 原语以 extern "C" 重新声明：miniz amalgamation 的实现只在
// vendor/zip.c 这一个编译单元里编译，这里不能 include miniz.h，
// 否则导出符号会重复定义。

namespace {

int ClampLevel(int level) {
	if (level < 0) return 6;
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
                  std::vector<uint8_t>& out, size_t maxUncompressed) {
	if (data == nullptr || size == 0) return false;

	zip_t* zip = zip_stream_open(reinterpret_cast<const char*>(data), size, 0, 'r');
	if (zip == nullptr) return false;

	bool ok = true;
	if (zip_entry_open(zip, name.c_str()) != 0) {
		ok = false;
	} else {
		if (zip_entry_isdir(zip) != 0) {
			out.clear();
		} else if (zip_entry_size(zip) > maxUncompressed) {
			ok = false;  // 未压缩大小超限，拒绝读取（解压炸弹防御）
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

}  // namespace tui
