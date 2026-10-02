#ifndef TUI_XLSX_CORE_H_
#define TUI_XLSX_CORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui {

// sheetsJson: [{"name":"Sheet1","rows":[["a",1,true,null],...]}]
// 值语义：string → 文本；number → 数字；bool → 布尔；null → 空单元格。
// 数组/对象类型的值会被拒绝。成功返回 xlsx 二进制（zip 容器）。
bool WriteXlsx(const std::string& sheetsJson, std::vector<uint8_t>& out);

// 解析 xlsx 二进制，返回 JSON：
// {"sheets":[{"name":"Sheet1","rows":[[...]]}]}
// 单元格：string/number/bool/null；日期与样式相关的格式化不解析（原始数字）。
// 单条目未压缩大小超过 maxUncompressed 时拒绝。
bool ReadXlsx(const uint8_t* data, size_t size, std::string& out,
              size_t maxUncompressed);

}  // namespace tui

#endif  // TUI_XLSX_CORE_H_
