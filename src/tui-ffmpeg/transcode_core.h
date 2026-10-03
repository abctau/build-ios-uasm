#ifndef TUI_TRANSCODE_CORE_H_
#define TUI_TRANSCODE_CORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui {

struct JobInput {
	std::string path;  // napi：文件路径
	const uint8_t* bytes = nullptr;  // wasm：内存字节（二选一）
	size_t bytesSize = 0;
};

struct JobOptions {
	std::vector<JobInput> inputs;
	std::string outputPath;  // 空则输出到 outBytes
	std::string outputFormat;  // mp4/m4a/aac/png（outputPath 为空时必填）
	double startMs = 0;
	double durationMs = 0;  // >0 裁剪时长
	std::string videoCodec = "copy";  // copy/mpeg4/png/none
	std::string audioCodec = "copy";  // copy/aac/none
	std::string videoFilter;  // 滤镜串（逗号分隔的简单滤镜链）
	std::string audioFilter;
	int videoBitrateKbps = 0;
	int scaleWidth = 0;  // >0 前置等比缩放（force_divisible_by=2）
	int scaleHeight = 0;
	bool hasOverlay = false;  // PiP：input1 视频缩放后贴到 input0
	int overlayX = 0, overlayY = 0, overlayW = 0, overlayH = 0;
	bool mixAudio = false;  // amix：input0+input1 音频混音（audioMerge）
};

// JSON → JobOptions。失败返回 false 并给出原因。
bool ParseJobOptions(const std::string& json, JobOptions& opt, std::string& err);

// 执行转码。outputPath 非空时写文件，否则写入 outBytes。
bool TranscodeRun(const JobOptions& opt, std::vector<uint8_t>& outBytes,
                  std::string& err);

}  // namespace tui

#endif  // TUI_TRANSCODE_CORE_H_
