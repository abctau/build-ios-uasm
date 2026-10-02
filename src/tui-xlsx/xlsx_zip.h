#ifndef TUI_XLSX_ZIP_H_
#define TUI_XLSX_ZIP_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui {

// 复制自 tui-zip 的 zip_core（裁剪 gzip），供 xlsx 打包/解包 OOXML 使用。
// ReadEntry 增加 maxUncompressed 防御：条目未压缩大小超过该值时拒绝读取，
// 防止解压炸弹在 zip_entry_read 内部按声称的大小分配内存。

bool ZipCreate(const std::vector<std::string>& names,
               const std::vector<std::vector<uint8_t>>& contents, int level,
               std::vector<uint8_t>& out);

bool ZipList(const uint8_t* data, size_t size, std::vector<std::string>& out);

bool ZipReadEntry(const uint8_t* data, size_t size, const std::string& name,
                  std::vector<uint8_t>& out, size_t maxUncompressed);

}  // namespace tui

#endif  // TUI_XLSX_ZIP_H_
