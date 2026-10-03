#ifndef TUI_FFMPEG_CORE_H_
#define TUI_FFMPEG_CORE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tui {

// 解析媒体信息（内存字节流）。
// 输出 JSON：{"container":"mov,mp4","durationMs":10033,"bitrate":0,
//   "streams":[{"type":"video","codec":"h264","width":1280,"height":720,
//   "fps":15.0,"durationMs":10033,"bitrate":20271},
//   {"type":"audio","codec":"aac","sampleRate":44100,"channels":1,...}]}
bool GetMediaInfo(const uint8_t* data, size_t size, std::string& out);

// 解码 timeMs 附近（向后取关键帧）的视频帧并编码为 PNG。
// maxWidth > 0 时等比缩放到该宽度；maxWidth <= 0 时保持原始尺寸。
bool ExtractFrame(const uint8_t* data, size_t size, int timeMs, int maxWidth,
                  std::vector<uint8_t>& out);

}  // namespace tui

#endif  // TUI_FFMPEG_CORE_H_
