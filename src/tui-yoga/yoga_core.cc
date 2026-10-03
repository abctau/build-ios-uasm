// tui-yoga 共享核心：节点表、样式解析/应用、JSON 子集 parser。
// 由 binding.cc（napi）与 wasm_binding.cc（embind）共用，保持两端行为一致。

#include "yoga_core.h"

#include <yoga/Yoga.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace tui_yoga {

/* ============================ JSON 子集 parser ============================ */
/* 只需对象/字符串/数字/bool/null，无数组嵌套对象也可以，不抛异常。 */

namespace json {

struct Value;
using Member = std::pair<std::string, Value>;
using Object = std::vector<Member>;

struct Value {
	enum Type { STRING, NUMBER, BOOL, NUL, OBJ } type = NUL;
	std::string str;
	double num = 0;
	bool b = false;
	Object obj;
};

struct Parser {
	const char* p = nullptr;
	const char* end = nullptr;
	bool ok = true;

	void skipWs() {
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
	}

	bool parseString(std::string& out) {
		if (p >= end || *p != '"') { ok = false; return false; }
		++p;
		out.clear();
		while (p < end && *p != '"') {
			if (*p == '\\' && p + 1 < end) {
				++p;
				switch (*p) {
					case '"': out.push_back('"'); break;
					case '\\': out.push_back('\\'); break;
					case '/': out.push_back('/'); break;
					case 'n': out.push_back('\n'); break;
					case 't': out.push_back('\t'); break;
					case 'r': out.push_back('\r'); break;
					default: out.push_back(*p); break;
				}
				++p;
			} else {
				out.push_back(*p);
				++p;
			}
		}
		if (p >= end) { ok = false; return false; }
		++p;
		return true;
	}

	bool parseNumber(double& out) {
		char* endPtr = nullptr;
		const double v = strtod(p, &endPtr);
		if (endPtr == p) { ok = false; return false; }
		p = endPtr;
		out = v;
		return true;
	}

	Value parseValue() {
		Value v;
		skipWs();
		if (p >= end) { ok = false; return v; }
		if (*p == '"') {
			v.type = Value::STRING;
			parseString(v.str);
		} else if (*p == '{') {
			++p;
			v.type = Value::OBJ;
			skipWs();
			if (p < end && *p == '}') { ++p; return v; }
			while (p < end) {
				skipWs();
				std::string key;
				if (!parseString(key)) { ok = false; return v; }
				skipWs();
				if (p >= end || *p != ':') { ok = false; return v; }
				++p;
				v.obj.push_back({ key, parseValue() });
				skipWs();
				if (p < end && *p == ',') { ++p; continue; }
				if (p < end && *p == '}') { ++p; return v; }
				ok = false;
				return v;
			}
			ok = false;
		} else if (*p == 't') {
			if (end - p >= 4 && strncmp(p, "true", 4) == 0) { p += 4; v.type = Value::BOOL; v.b = true; }
			else ok = false;
		} else if (*p == 'f') {
			if (end - p >= 5 && strncmp(p, "false", 5) == 0) { p += 5; v.type = Value::BOOL; v.b = false; }
			else ok = false;
		} else if (*p == 'n') {
			if (end - p >= 4 && strncmp(p, "null", 4) == 0) { p += 4; }
			else ok = false;
		} else {
			v.type = Value::NUMBER;
			parseNumber(v.num);
		}
		return v;
	}
};

}  // namespace json

/* ============================ 值解析（点/百分比/auto/枚举） ============================ */

struct YgValue {
	float value = 0;
	YGUnit unit = YGUnitUndefined;
};

static bool ParseDim(const std::string& s, YgValue& out) {
	if (s == "auto") { out.unit = YGUnitAuto; return true; }
	if (s == "undefined" || s.empty()) { out.unit = YGUnitUndefined; return true; }
	if (!s.empty() && s.back() == '%') {
		out.value = static_cast<float>(atof(s.c_str()));
		out.unit = YGUnitPercent;
		return true;
	}
	std::string num = s;
	if (num.size() > 2 && num.compare(num.size() - 2, 2, "px") == 0) num.resize(num.size() - 2);
	char* endPtr = nullptr;
	const double v = strtod(num.c_str(), &endPtr);
	if (endPtr == num.c_str()) return false;
	out.value = static_cast<float>(v);
	out.unit = YGUnitPoint;
	return true;
}

static bool ParseDimNum(double v, YgValue& out) {
	out.value = static_cast<float>(v);
	out.unit = YGUnitPoint;
	return true;
}

/* ============================ 属性 op 表 ============================ */
/* op 编码（显式数值）：
 *   0..9   margin   (op = kMarginBase + YGEdge)
 *   10..19 padding  (op = kPaddingBase + YGEdge)
 *   20..29 border   (op = kBorderBase + YGEdge)
 *   30..39 position (left/top/right/bottom)
 *   40+   其余数值/枚举属性
 */
enum PropOp : uint16_t {
	kMarginBase = 0,
	kPaddingBase = 10,
	kBorderBase = 20,
	kLeft = 30, kTop = 31, kRight = 32, kBottom = 33,
	kFlex = 40, kFlexGrow, kFlexShrink, kFlexBasis, kAspectRatio,
	kWidth, kHeight, kMinWidth, kMinHeight, kMaxWidth, kMaxHeight,
	kGapRow, kGapColumn,
	kDisplay = 60, kPositionType, kDirectionProp, kFlexDirection, kJustifyContent,
	kAlignItems, kAlignSelf, kAlignContent, kFlexWrap, kOverflow,
};

struct StyleEntry {
	uint16_t op;
	bool isEnum;
	int64_t enumVal;   // yoga 枚举
	YgValue dim;       // 点/百分比/auto 值
};

static bool ParseEdge(const std::string& key, const std::string& prefix, YGEdge& edge) {
	if (key == prefix) { edge = YGEdgeAll; return true; }
	std::string suffix = key.substr(prefix.size());
	if (suffix == "Top") { edge = YGEdgeTop; return true; }
	if (suffix == "Right") { edge = YGEdgeRight; return true; }
	if (suffix == "Bottom") { edge = YGEdgeBottom; return true; }
	if (suffix == "Left") { edge = YGEdgeLeft; return true; }
	if (suffix == "H") { edge = YGEdgeHorizontal; return true; }
	if (suffix == "V") { edge = YGEdgeVertical; return true; }
	return false;
}

static uint16_t ClassifyKey(const std::string& key, bool& ok, YGEdge& edge, bool& isEdge) {
	ok = true;
	isEdge = false;
	if (key == "display") return kDisplay;
	if (key == "position") return kPositionType;
	if (key == "direction") return kDirectionProp;
	if (key == "flexDirection") return kFlexDirection;
	if (key == "justifyContent") return kJustifyContent;
	if (key == "alignItems") return kAlignItems;
	if (key == "alignSelf") return kAlignSelf;
	if (key == "alignContent") return kAlignContent;
	if (key == "flexWrap") return kFlexWrap;
	if (key == "overflow") return kOverflow;
	if (key == "flex") return kFlex;
	if (key == "flexGrow") return kFlexGrow;
	if (key == "flexShrink") return kFlexShrink;
	if (key == "flexBasis") return kFlexBasis;
	if (key == "aspectRatio") return kAspectRatio;
	if (key == "width") return kWidth;
	if (key == "height") return kHeight;
	if (key == "minWidth") return kMinWidth;
	if (key == "minHeight") return kMinHeight;
	if (key == "maxWidth") return kMaxWidth;
	if (key == "maxHeight") return kMaxHeight;
	if (key == "left") return kLeft;
	if (key == "top") return kTop;
	if (key == "right") return kRight;
	if (key == "bottom") return kBottom;
	if (key.rfind("margin", 0) == 0) { if (ParseEdge(key, "margin", edge)) { isEdge = true; return kMarginBase; } }
	if (key.rfind("padding", 0) == 0) { if (ParseEdge(key, "padding", edge)) { isEdge = true; return kPaddingBase; } }
	if (key.rfind("border", 0) == 0) { if (ParseEdge(key, "border", edge)) { isEdge = true; return kBorderBase; } }
	if (key == "gapRow" || key == "rowGap") return kGapRow;
	if (key == "gapColumn" || key == "columnGap") return kGapColumn;
	ok = false;
	return 0;
}

static bool ParseEnumDim(uint16_t op, const std::string& s, int64_t& out) {
	switch (op) {
		case kDisplay:
			if (s == "flex") { out = YGDisplayFlex; return true; }
			if (s == "none") { out = YGDisplayNone; return true; }
			if (s == "contents") { out = YGDisplayContents; return true; }
			return false;
		case kPositionType:
			if (s == "static" || s == "relative") { out = YGPositionTypeStatic; return true; }
			if (s == "absolute") { out = YGPositionTypeAbsolute; return true; }
			return false;
		case kDirectionProp:
			if (s == "ltr") { out = YGDirectionLTR; return true; }
			if (s == "rtl") { out = YGDirectionRTL; return true; }
			if (s == "inherit") { out = YGDirectionInherit; return true; }
			return false;
		case kFlexDirection:
			if (s == "row") { out = YGFlexDirectionRow; return true; }
			if (s == "row-reverse") { out = YGFlexDirectionRowReverse; return true; }
			if (s == "column") { out = YGFlexDirectionColumn; return true; }
			if (s == "column-reverse") { out = YGFlexDirectionColumnReverse; return true; }
			return false;
		case kJustifyContent:
			if (s == "flex-start") { out = YGJustifyFlexStart; return true; }
			if (s == "center") { out = YGJustifyCenter; return true; }
			if (s == "flex-end") { out = YGJustifyFlexEnd; return true; }
			if (s == "space-between") { out = YGJustifySpaceBetween; return true; }
			if (s == "space-around") { out = YGJustifySpaceAround; return true; }
			if (s == "space-evenly") { out = YGJustifySpaceEvenly; return true; }
			return false;
		case kAlignItems:
		case kAlignSelf:
			if (s == "auto") { out = YGAlignAuto; return true; }
			if (s == "flex-start") { out = YGAlignFlexStart; return true; }
			if (s == "center") { out = YGAlignCenter; return true; }
			if (s == "flex-end") { out = YGAlignFlexEnd; return true; }
			if (s == "stretch") { out = YGAlignStretch; return true; }
			if (s == "baseline") { out = YGAlignBaseline; return true; }
			if (s == "space-between") { out = YGAlignSpaceBetween; return true; }
			if (s == "space-around") { out = YGAlignSpaceAround; return true; }
			return false;
		case kAlignContent:
			if (s == "flex-start") { out = YGAlignFlexStart; return true; }
			if (s == "center") { out = YGAlignCenter; return true; }
			if (s == "flex-end") { out = YGAlignFlexEnd; return true; }
			if (s == "stretch") { out = YGAlignStretch; return true; }
			if (s == "space-between") { out = YGAlignSpaceBetween; return true; }
			if (s == "space-around") { out = YGAlignSpaceAround; return true; }
			return false;
		case kFlexWrap:
			if (s == "nowrap") { out = YGWrapNoWrap; return true; }
			if (s == "wrap") { out = YGWrapWrap; return true; }
			if (s == "wrap-reverse") { out = YGWrapWrapReverse; return true; }
			return false;
		case kOverflow:
			if (s == "visible") { out = YGOverflowVisible; return true; }
			if (s == "hidden") { out = YGOverflowHidden; return true; }
			if (s == "scroll") { out = YGOverflowScroll; return true; }
			return false;
		default:
			return false;
	}
}

/* 把 StyleEntry 应用到节点（registerStyle/applyStyle/setStyle 共用），仅枚举型 */
static void ApplyEnumOp(YGNodeRef node, uint16_t op, int64_t enumVal) {
	switch (op) {
		case kDisplay: YGNodeStyleSetDisplay(node, static_cast<YGDisplay>(enumVal)); return;
		case kPositionType: YGNodeStyleSetPositionType(node, static_cast<YGPositionType>(enumVal)); return;
		case kDirectionProp: YGNodeStyleSetDirection(node, static_cast<YGDirection>(enumVal)); return;
		case kFlexDirection: YGNodeStyleSetFlexDirection(node, static_cast<YGFlexDirection>(enumVal)); return;
		case kJustifyContent: YGNodeStyleSetJustifyContent(node, static_cast<YGJustify>(enumVal)); return;
		case kAlignItems: YGNodeStyleSetAlignItems(node, static_cast<YGAlign>(enumVal)); return;
		case kAlignSelf: YGNodeStyleSetAlignSelf(node, static_cast<YGAlign>(enumVal)); return;
		case kAlignContent: YGNodeStyleSetAlignContent(node, static_cast<YGAlign>(enumVal)); return;
		case kFlexWrap: YGNodeStyleSetFlexWrap(node, static_cast<YGWrap>(enumVal)); return;
		case kOverflow: YGNodeStyleSetOverflow(node, static_cast<YGOverflow>(enumVal)); return;
	}
}

static void ApplyValueOp(YGNodeRef node, uint16_t op, const YgValue& v) {
	if (op < kPaddingBase) {
		// margin：op = kMarginBase + YGEdge
		const YGEdge edge = static_cast<YGEdge>(op - kMarginBase);
		if (v.unit == YGUnitPercent) YGNodeStyleSetMarginPercent(node, edge, v.value);
		else if (v.unit == YGUnitPoint) YGNodeStyleSetMargin(node, edge, v.value);
		else YGNodeStyleSetMargin(node, edge, YGUndefined);
		return;
	}
	if (op < kBorderBase) {
		// padding
		const YGEdge edge = static_cast<YGEdge>(op - kPaddingBase);
		if (v.unit == YGUnitPercent) YGNodeStyleSetPaddingPercent(node, edge, v.value);
		else if (v.unit == YGUnitPoint) YGNodeStyleSetPadding(node, edge, v.value);
		else YGNodeStyleSetPadding(node, edge, YGUndefined);
		return;
	}
	if (op < kLeft) {
		// border
		const YGEdge edge = static_cast<YGEdge>(op - kBorderBase);
		if (v.unit == YGUnitPoint) YGNodeStyleSetBorder(node, edge, v.value);
		else YGNodeStyleSetBorder(node, edge, YGUndefined);
		return;
	}
	switch (op) {
		case kFlex: YGNodeStyleSetFlex(node, v.value); return;
		case kFlexGrow: YGNodeStyleSetFlexGrow(node, v.value); return;
		case kFlexShrink: YGNodeStyleSetFlexShrink(node, v.value); return;
		case kAspectRatio: YGNodeStyleSetAspectRatio(node, v.value); return;
		case kFlexBasis:
			if (v.unit == YGUnitAuto) YGNodeStyleSetFlexBasisAuto(node);
			else if (v.unit == YGUnitPercent) YGNodeStyleSetFlexBasisPercent(node, v.value);
			else if (v.unit == YGUnitPoint) YGNodeStyleSetFlexBasis(node, v.value);
			return;
		case kWidth:
			if (v.unit == YGUnitAuto) YGNodeStyleSetWidthAuto(node);
			else if (v.unit == YGUnitPercent) YGNodeStyleSetWidthPercent(node, v.value);
			else if (v.unit == YGUnitPoint) YGNodeStyleSetWidth(node, v.value);
			return;
		case kHeight:
			if (v.unit == YGUnitAuto) YGNodeStyleSetHeightAuto(node);
			else if (v.unit == YGUnitPercent) YGNodeStyleSetHeightPercent(node, v.value);
			else if (v.unit == YGUnitPoint) YGNodeStyleSetHeight(node, v.value);
			return;
		case kMinWidth: if (v.unit == YGUnitPercent) YGNodeStyleSetMinWidthPercent(node, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetMinWidth(node, v.value); return;
		case kMinHeight: if (v.unit == YGUnitPercent) YGNodeStyleSetMinHeightPercent(node, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetMinHeight(node, v.value); return;
		case kMaxWidth: if (v.unit == YGUnitPercent) YGNodeStyleSetMaxWidthPercent(node, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetMaxWidth(node, v.value); return;
		case kMaxHeight: if (v.unit == YGUnitPercent) YGNodeStyleSetMaxHeightPercent(node, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetMaxHeight(node, v.value); return;
		case kGapRow: YGNodeStyleSetGap(node, YGGutterRow, v.value); return;
		case kGapColumn: YGNodeStyleSetGap(node, YGGutterColumn, v.value); return;
		case kLeft: if (v.unit == YGUnitPercent) YGNodeStyleSetPositionPercent(node, YGEdgeLeft, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetPosition(node, YGEdgeLeft, v.value); return;
		case kTop: if (v.unit == YGUnitPercent) YGNodeStyleSetPositionPercent(node, YGEdgeTop, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetPosition(node, YGEdgeTop, v.value); return;
		case kRight: if (v.unit == YGUnitPercent) YGNodeStyleSetPositionPercent(node, YGEdgeRight, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetPosition(node, YGEdgeRight, v.value); return;
		case kBottom: if (v.unit == YGUnitPercent) YGNodeStyleSetPositionPercent(node, YGEdgeBottom, v.value); else if (v.unit == YGUnitPoint) YGNodeStyleSetPosition(node, YGEdgeBottom, v.value); return;
		default: return;
	}
}

static const uint16_t kEdgeOffsetStep = 10;
static uint16_t EdgeOp(uint16_t baseOp, YGEdge edge) {
	return static_cast<uint16_t>(baseOp + static_cast<int>(edge));
}

static void ApplyEdgeEntry(YGNodeRef node, uint16_t op, const YgValue& v) {
	ApplyValueOp(node, op, v);
}

static void ApplyPositionEntry(YGNodeRef node, uint16_t op, const YgValue& v) {
	ApplyValueOp(node, op, v);
}


/* ============================ 节点表与样式表 ============================ */

static YGConfigRef g_config = nullptr;
static uint32_t g_nextNodeId = 1;
static uint32_t g_nextStyleId = 1;
static std::unordered_map<uint32_t, YGNodeRef> g_nodes;
static std::unordered_map<uint32_t, std::vector<StyleEntry>> g_styles;
static std::unordered_map<uint32_t, std::pair<float, float>> g_measured;  // id -> (w, h)

struct NodeCtx {
	uint32_t id;
};

static YGConfigRef GetConfig() {
	if (g_config == nullptr) {
		g_config = YGConfigNew();
		// web defaults：flexShrink 默认 1 等，与 CSS 心智一致
		YGConfigSetUseWebDefaults(g_config, true);
	}
	return g_config;
}

/* 文本预测量：measure 回调直接返回 JS 侧预先量好的尺寸 */
static YGSize MeasureCallback(YGNodeConstRef node, float width, YGMeasureMode widthMode, float height, YGMeasureMode heightMode) {
	(void)width;
	(void)widthMode;
	(void)height;
	(void)heightMode;
	const NodeCtx* ctx = static_cast<const NodeCtx*>(YGNodeGetContext(node));
	if (ctx == nullptr) return YGSize{ 0, 0 };
	auto it = g_measured.find(ctx->id);
	if (it == g_measured.end()) return YGSize{ 0, 0 };
	return YGSize{ it->second.first, it->second.second };
}

static void DestroyNodeRec(uint32_t id, YGNodeRef node) {
	const size_t count = YGNodeGetChildCount(node);
	for (size_t i = 0; i < count; ++i) {
		YGNodeRef child = YGNodeGetChild(node, i);
		const NodeCtx* ctx = static_cast<const NodeCtx*>(YGNodeGetContext(child));
		if (ctx != nullptr) DestroyNodeRec(ctx->id, child);
	}
	auto it = g_measured.find(id);
	if (it != g_measured.end()) g_measured.erase(it);
	YGNodeFree(node);
}

/* ============================ 公共实现 ============================ */

static void PushDim(std::vector<StyleEntry>& entries, uint16_t op, const YgValue& dim, bool isEdge, YGEdge edge) {
	if (isEdge) {
		entries.push_back(StyleEntry{ EdgeOp(op, edge), false, 0, dim });
	} else {
		entries.push_back(StyleEntry{ op, false, 0, dim });
	}
}

bool Init() {
	return true;
}

uint32_t CreateNode() {
	YGNodeRef node = YGNodeNewWithConfig(GetConfig());
	if (node == nullptr) return 0;
	const uint32_t id = g_nextNodeId++;
	NodeCtx* ctx = new NodeCtx{ id };
	YGNodeSetContext(node, ctx);
	g_nodes[id] = node;
	return id;
}

static YGNodeRef GetNode(uint32_t id) {
	auto it = g_nodes.find(id);
	if (it == g_nodes.end()) return nullptr;
	return it->second;
}

void FreeNode(uint32_t id) {
	YGNodeRef node = GetNode(id);
	if (node == nullptr) return;
	auto ctx = static_cast<NodeCtx*>(YGNodeGetContext(node));
	g_nodes.erase(id);
	if (ctx != nullptr) {
		delete ctx;
		YGNodeSetContext(node, nullptr);
	}
	YGNodeFree(node);
}

void FreeTree(uint32_t id) {
	YGNodeRef node = GetNode(id);
	if (node == nullptr) return;
	DestroyNodeRec(id, node);
	g_nodes.erase(id);
}

bool InsertChild(uint32_t parentId, uint32_t childId, int32_t index) {
	YGNodeRef parent = GetNode(parentId);
	YGNodeRef child = GetNode(childId);
	if (parent == nullptr || child == nullptr) return false;
	const size_t count = YGNodeGetChildCount(parent);
	size_t idx = index < 0 ? count : static_cast<size_t>(index);
	if (idx > count) idx = count;
	YGNodeInsertChild(parent, child, idx);
	return true;
}

bool RemoveChild(uint32_t parentId, uint32_t childId) {
	YGNodeRef parent = GetNode(parentId);
	YGNodeRef child = GetNode(childId);
	if (parent == nullptr || child == nullptr) return false;
	YGNodeRemoveChild(parent, child);
	return true;
}

uint32_t RegisterStyle(const char* propsJson, size_t len, std::string& err) {
	json::Parser parser{ propsJson, propsJson + len };
	json::Value root = parser.parseValue();
	if (!parser.ok || root.type != json::Value::OBJ) {
		err = "invalid style json";
		return 0;
	}
	std::vector<StyleEntry> entries;
	for (const auto& m : root.obj) {
		bool ok = false;
		bool isEdge = false;
		YGEdge edge = YGEdgeAll;
		uint16_t op = ClassifyKey(m.first, ok, edge, isEdge);
		if (!ok) {
			err = "unknown style key: " + m.first;
			return 0;
		}
		if (m.second.type == json::Value::STRING) {
			int64_t enumVal = 0;
			if (ParseEnumDim(op, m.second.str, enumVal)) {
				entries.push_back(StyleEntry{ op, true, enumVal, YgValue{} });
				continue;
			}
			YgValue dim;
			if (!ParseDim(m.second.str, dim)) {
				err = "invalid value for key: " + m.first;
				return 0;
			}
			PushDim(entries, op, dim, isEdge, edge);
		} else if (m.second.type == json::Value::NUMBER) {
			YgValue dim;
			if (!ParseDimNum(m.second.num, dim)) {
				err = "invalid number for key: " + m.first;
				return 0;
			}
			PushDim(entries, op, dim, isEdge, edge);
		} else {
			// bool/null：null = 重置为 undefined
			YgValue dim;
			dim.unit = YGUnitUndefined;
			PushDim(entries, op, dim, isEdge, edge);
		}
	}
	const uint32_t id = g_nextStyleId++;
	g_styles[id] = std::move(entries);
	return id;
}

bool ApplyStyle(uint32_t nodeId, uint32_t styleId) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	auto it = g_styles.find(styleId);
	if (it == g_styles.end()) return false;
	for (const StyleEntry& e : it->second) {
		if (e.isEnum) {
			ApplyEnumOp(node, e.op, e.enumVal);
		} else {
			ApplyValueOp(node, e.op, e.dim);
		}
	}
	return true;
}

bool SetStyle(uint32_t nodeId, const char* key, size_t keyLen, const char* value, size_t valueLen) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	std::string k(key, keyLen);
	std::string s(value, valueLen);
	bool ok = false;
	bool isEdge = false;
	YGEdge edge = YGEdgeAll;
	uint16_t op = ClassifyKey(k, ok, edge, isEdge);
	if (!ok) return false;
	int64_t enumVal = 0;
	if (ParseEnumDim(op, s, enumVal)) {
		ApplyEnumOp(node, op, enumVal);
		return true;
	}
	YgValue dim;
	if (!ParseDim(s, dim)) return false;
	ApplyValueOp(node, op, dim);
	return true;
}

bool SetStyleNum(uint32_t nodeId, const char* key, size_t keyLen, double value) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	std::string k(key, keyLen);
	bool ok = false;
	bool isEdge = false;
	YGEdge edge = YGEdgeAll;
	uint16_t op = ClassifyKey(k, ok, edge, isEdge);
	if (!ok) return false;
	YgValue dim;
	if (!ParseDimNum(value, dim)) return false;
	ApplyValueOp(node, op, dim);
	return true;
}

bool SetMeasuredSize(uint32_t nodeId, double width, double height) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	if (!YGNodeHasMeasureFunc(node)) {
		YGNodeSetMeasureFunc(node, &MeasureCallback);
	}
	auto& slot = g_measured[nodeId];
	const bool changed = slot.first != static_cast<float>(width) || slot.second != static_cast<float>(height);
	slot.first = static_cast<float>(width);
	slot.second = static_cast<float>(height);
	if (changed) YGNodeMarkDirty(node);
	return true;
}

bool CalculateLayout(uint32_t rootId, double availWidth, double availHeight, int32_t direction) {
	YGNodeRef node = GetNode(rootId);
	if (node == nullptr) return false;
	YGDirection dir = direction == 1 ? YGDirectionRTL : (direction == 2 ? YGDirectionInherit : YGDirectionLTR);
	YGNodeCalculateLayout(node, static_cast<float>(availWidth), static_cast<float>(availHeight), dir);
	return true;
}

/* 前序遍历收集 [id, left, top, width, height]，根节点也在内 */
static void CollectRec(YGNodeRef node, std::vector<double>& out) {
	const NodeCtx* ctx = static_cast<const NodeCtx*>(YGNodeGetContext(node));
	if (ctx != nullptr) {
		out.push_back(static_cast<double>(ctx->id));
		out.push_back(YGNodeLayoutGetLeft(node));
		out.push_back(YGNodeLayoutGetTop(node));
		out.push_back(YGNodeLayoutGetWidth(node));
		out.push_back(YGNodeLayoutGetHeight(node));
	}
	const size_t count = YGNodeGetChildCount(node);
	for (size_t i = 0; i < count; ++i) CollectRec(YGNodeGetChild(node, i), out);
}

bool CollectFrames(uint32_t rootId, std::vector<double>& out) {
	YGNodeRef node = GetNode(rootId);
	if (node == nullptr) return false;
	CollectRec(node, out);
	return true;
}

bool GetFrame(uint32_t nodeId, double out[5]) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	out[0] = static_cast<double>(nodeId);
	out[1] = YGNodeLayoutGetLeft(node);
	out[2] = YGNodeLayoutGetTop(node);
	out[3] = YGNodeLayoutGetWidth(node);
	out[4] = YGNodeLayoutGetHeight(node);
	return true;
}

bool IsDirty(uint32_t nodeId, bool& out) {
	YGNodeRef node = GetNode(nodeId);
	if (node == nullptr) return false;
	out = YGNodeIsDirty(node);
	return true;
}

}  // namespace tui_yoga
