#ifndef TUI_ZIP_CORE_H_
#define TUI_ZIP_CORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui {

struct ZipExtractResult {
	std::vector<std::string> names;
	std::vector<std::vector<uint8_t>> datas;
};

bool ZipCreate(const std::vector<std::string>& names,
               const std::vector<std::vector<uint8_t>>& contents, int level,
               std::vector<uint8_t>& out);

bool ZipList(const uint8_t* data, size_t size, std::vector<std::string>& out);

bool ZipReadEntry(const uint8_t* data, size_t size, const std::string& name,
                  std::vector<uint8_t>& out);

bool ZipExtractAll(const uint8_t* data, size_t size, ZipExtractResult& out);

bool GzipCompress(const uint8_t* data, size_t size, int level,
                  std::vector<uint8_t>& out);

bool GzipDecompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out);

}  // namespace tui

#endif  // TUI_ZIP_CORE_H_
