// tui-yoga 共享核心接口（binding.cc 与 wasm_binding.cc 共用）

#ifndef TUI_YOGA_CORE_H
#define TUI_YOGA_CORE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui_yoga {

bool Init();

uint32_t CreateNode();
void FreeNode(uint32_t id);
void FreeTree(uint32_t id);
bool InsertChild(uint32_t parentId, uint32_t childId, int32_t index);
bool RemoveChild(uint32_t parentId, uint32_t childId);

// propsJson: {"width":"100px","flex":1,...}；返回 styleId（0 = 失败，err 带原因）
uint32_t RegisterStyle(const char* propsJson, size_t len, std::string& err);
bool ApplyStyle(uint32_t nodeId, uint32_t styleId);
// value: "12px" | "50%" | "auto" | 枚举字符串（"center" 等）
bool SetStyle(uint32_t nodeId, const char* key, size_t keyLen, const char* value, size_t valueLen);
bool SetStyleNum(uint32_t nodeId, const char* key, size_t keyLen, double value);
// 文本预测量：叶子节点尺寸由 JS 侧测量后传入
bool SetMeasuredSize(uint32_t nodeId, double width, double height);

bool CalculateLayout(uint32_t rootId, double availWidth, double availHeight, int32_t direction);
// 前序遍历输出 [id, left, top, width, height, ...]，含根节点
bool CollectFrames(uint32_t rootId, std::vector<double>& out);
bool GetFrame(uint32_t nodeId, double out[5]);
bool IsDirty(uint32_t nodeId, bool& out);

}  // namespace tui_yoga

#endif  // TUI_YOGA_CORE_H
