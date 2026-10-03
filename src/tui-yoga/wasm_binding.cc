#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>
#include <vector>

#include "yoga_core.h"

namespace {

emscripten::val FramesToVal(const std::vector<double>& frames) {
	emscripten::val arr = emscripten::val::array();
	for (size_t i = 0; i < frames.size(); ++i) {
		arr.set(static_cast<uint32_t>(i), emscripten::val(frames[i]));
	}
	return arr;
}

uint32_t CreateNode() {
	return tui_yoga::CreateNode();
}

bool FreeNode(uint32_t id) {
	tui_yoga::FreeNode(id);
	return true;
}

bool FreeTree(uint32_t id) {
	tui_yoga::FreeTree(id);
	return true;
}

bool InsertChildWithIndex(uint32_t parentId, uint32_t childId, int32_t index) {
	return tui_yoga::InsertChild(parentId, childId, index);
}

bool InsertChildAtEnd(uint32_t parentId, uint32_t childId) {
	return tui_yoga::InsertChild(parentId, childId, -1);
}

bool RemoveChild(uint32_t parentId, uint32_t childId) {
	return tui_yoga::RemoveChild(parentId, childId);
}

uint32_t RegisterStyle(const std::string& propsJson) {
	std::string err;
	const uint32_t styleId = tui_yoga::RegisterStyle(propsJson.data(), propsJson.size(), err);
	if (styleId == 0) {
		emscripten::val::global("Error").new_(err).throw_();
		return 0;
	}
	return styleId;
}

bool ApplyStyle(uint32_t nodeId, uint32_t styleId) {
	return tui_yoga::ApplyStyle(nodeId, styleId);
}

bool SetStyle(uint32_t nodeId, const std::string& key, const std::string& value) {
	return tui_yoga::SetStyle(nodeId, key.data(), key.size(), value.data(), value.size());
}

bool SetStyleNum(uint32_t nodeId, const std::string& key, double value) {
	return tui_yoga::SetStyleNum(nodeId, key.data(), key.size(), value);
}

bool SetMeasuredSize(uint32_t nodeId, double width, double height) {
	return tui_yoga::SetMeasuredSize(nodeId, width, height);
}

bool CalculateLayout(uint32_t rootId, double availWidth, double availHeight, int32_t direction) {
	return tui_yoga::CalculateLayout(rootId, availWidth, availHeight, direction);
}

emscripten::val CollectFrames(uint32_t rootId) {
	std::vector<double> frames;
	if (!tui_yoga::CollectFrames(rootId, frames)) {
		emscripten::val::global("Error").new_(std::string("root not found")).throw_();
		return emscripten::val::undefined();
	}
	return FramesToVal(frames);
}

emscripten::val GetFrame(uint32_t nodeId) {
	double frame[5] = { 0, 0, 0, 0, 0 };
	if (!tui_yoga::GetFrame(nodeId, frame)) {
		emscripten::val::global("Error").new_(std::string("node not found")).throw_();
		return emscripten::val::undefined();
	}
	return FramesToVal(std::vector<double>(frame, frame + 5));
}

bool IsDirty(uint32_t nodeId) {
	bool dirty = false;
	if (!tui_yoga::IsDirty(nodeId, dirty)) {
		emscripten::val::global("Error").new_(std::string("node not found")).throw_();
		return false;
	}
	return dirty;
}

}  // namespace

EMSCRIPTEN_BINDINGS(tui_yoga_uasm) {
	emscripten::function("createNode", &CreateNode);
	emscripten::function("freeNode", &FreeNode);
	emscripten::function("freeTree", &FreeTree);
	emscripten::function("insertChild", &InsertChildAtEnd);
	emscripten::function("insertChildAt", &InsertChildWithIndex);
	emscripten::function("removeChild", &RemoveChild);
	emscripten::function("registerStyle", &RegisterStyle);
	emscripten::function("applyStyle", &ApplyStyle);
	emscripten::function("setStyle", &SetStyle);
	emscripten::function("setStyleNum", &SetStyleNum);
	emscripten::function("setMeasuredSize", &SetMeasuredSize);
	emscripten::function("calculateLayout", &CalculateLayout);
	emscripten::function("collectFrames", &CollectFrames);
	emscripten::function("getFrame", &GetFrame);
	emscripten::function("isDirty", &IsDirty);
}
