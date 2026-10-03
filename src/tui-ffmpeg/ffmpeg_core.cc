#include "ffmpeg_core.h"

#include <time.h>
#include <pthread.h>

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

namespace {

// 内存字节流的 AVIO 读取器
struct MemReader {
	const uint8_t* data;
	size_t size;
	size_t pos;
};

int ReadCb(void* opaque, uint8_t* buf, int bufSize) {
	MemReader* r = static_cast<MemReader*>(opaque);
	if (r->pos >= r->size) return AVERROR_EOF;
	const size_t n = static_cast<size_t>(bufSize) < (r->size - r->pos)
	                     ? static_cast<size_t>(bufSize)
	                     : (r->size - r->pos);
	std::memcpy(buf, r->data + r->pos, n);
	r->pos += n;
	return static_cast<int>(n);
}

int64_t SeekCb(void* opaque, int64_t offset, int whence) {
	MemReader* r = static_cast<MemReader*>(opaque);
	if (whence == AVSEEK_SIZE) return static_cast<int64_t>(r->size);
	int64_t newPos = -1;
	if (whence == SEEK_SET) newPos = offset;
	else if (whence == SEEK_CUR) newPos = static_cast<int64_t>(r->pos) + offset;
	else if (whence == SEEK_END) newPos = static_cast<int64_t>(r->size) + offset;
	if (newPos < 0) return -1;
	if (static_cast<size_t>(newPos) > r->size) newPos = static_cast<int64_t>(r->size);
	r->pos = static_cast<size_t>(newPos);
	return newPos;
}

std::string CodecName(const AVCodecParameters* par, AVMediaType type) {
	if (par->codec_id == AV_CODEC_ID_NONE) return "unknown";
	const char* name = avcodec_get_name(par->codec_id);
	return name != nullptr ? std::string(name) : "unknown";
}

void AppendNum(std::string& out, double v) {
	char buf[40];
	const auto res = std::to_chars(buf, buf + sizeof(buf), v);
	if (res.ec != std::errc()) {
		out += "0";
		return;
	}
	out.append(buf, res.ptr);
}

void AppendInt(std::string& out, int64_t v) {
	char buf[24];
	std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
	out += buf;
}

// 结构化打开：bytes → AVFormatContext（含内存 AVIO）
class FormatSession {
 public:
	FormatSession(const uint8_t* data, size_t size) : reader_{data, size, 0} {}
	~FormatSession() { Close(); }

	bool Open() {
		fmt_ = avformat_alloc_context();
		if (fmt_ == nullptr) return false;
		avioBuf_ = static_cast<uint8_t*>(av_malloc(64 * 1024));
		if (avioBuf_ == nullptr) return false;
		avio_ = avio_alloc_context(avioBuf_, 64 * 1024, 0, &reader_, ReadCb, nullptr,
		                           SeekCb);
		if (avio_ == nullptr) return false;
		fmt_->pb = avio_;
		if (avformat_open_input(&fmt_, "", nullptr, nullptr) < 0) return false;
		if (avformat_find_stream_info(fmt_, nullptr) < 0) return false;
		opened_ = true;
		return true;
	}

	AVFormatContext* get() const { return fmt_; }
	const MemReader* reader() const { return &reader_; }

	void Close() {
		if (fmt_ != nullptr) {
			avformat_close_input(&fmt_);
			fmt_ = nullptr;
		}
		if (avio_ != nullptr) {
			avio_context_free(&avio_);  // 内部 free avioBuf_
			avio_ = nullptr;
			avioBuf_ = nullptr;
		}
	}

 private:
	MemReader reader_;
	AVFormatContext* fmt_ = nullptr;
	AVIOContext* avio_ = nullptr;
	uint8_t* avioBuf_ = nullptr;
	bool opened_ = false;
};

struct PngSink {
	std::vector<uint8_t>* out;
};

void PngWriteCb(void* context, void* data, int size) {
	PngSink* sink = static_cast<PngSink*>(context);
	const uint8_t* p = static_cast<const uint8_t*>(data);
	sink->out->insert(sink->out->end(), p, p + size);
}

}  // namespace

namespace tui {

bool GetMediaInfo(const uint8_t* data, size_t size, std::string& out) {
	if (data == nullptr || size == 0) return false;

	FormatSession session(data, size);
	if (!session.Open()) return false;
	AVFormatContext* fmt = session.get();

	out = "{\"container\":\"";
	out += fmt->iformat->name != nullptr ? fmt->iformat->name : "unknown";
	out += "\",\"durationMs\":";
	AppendInt(out, fmt->duration > 0
	                   ? static_cast<int64_t>(fmt->duration *
	                                          1000 / AV_TIME_BASE)
	                   : 0);
	out += ",\"bitrate\":";
	AppendInt(out, fmt->bit_rate > 0 ? fmt->bit_rate : 0);
	out += ",\"streams\":[";

	bool firstStream = true;
	for (unsigned i = 0; i < fmt->nb_streams; i++) {
		const AVStream* st = fmt->streams[i];
		const AVCodecParameters* par = st->codecpar;
		if (par->codec_type != AVMEDIA_TYPE_VIDEO &&
		    par->codec_type != AVMEDIA_TYPE_AUDIO) {
			continue;
		}
		if (!firstStream) out += ",";
		firstStream = false;

		out += par->codec_type == AVMEDIA_TYPE_VIDEO
		           ? "{\"type\":\"video\""
		           : "{\"type\":\"audio\"";
		out += ",\"codec\":\"";
		out += CodecName(par, par->codec_type);
		out += "\",\"durationMs\":";
		const double stMs = st->duration > 0
		                        ? st->duration * av_q2d(st->time_base) * 1000.0
		                        : 0;
		AppendNum(out, stMs >= 0 ? stMs : 0);
		out += ",\"bitrate\":";
		AppendInt(out, par->bit_rate > 0 ? par->bit_rate : 0);
		if (par->codec_type == AVMEDIA_TYPE_VIDEO) {
			out += ",\"width\":";
			AppendInt(out, par->width);
			out += ",\"height\":";
			AppendInt(out, par->height);
			out += ",\"fps\":";
			AVRational fr = st->avg_frame_rate;
			if (fr.num <= 0 || fr.den <= 0) fr = st->r_frame_rate;
			const double fps = fr.num > 0 && fr.den > 0 ? av_q2d(fr) : 0;
			AppendNum(out, fps > 0 && std::isfinite(fps) ? fps : 0);
		} else {
			out += ",\"sampleRate\":";
			AppendInt(out, par->sample_rate);
			out += ",\"channels\":";
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 2, 100)
			AppendInt(out, par->ch_layout.nb_channels);
#else
			AppendInt(out, par->channels);
#endif
		}
		out += "}";
	}
	out += "]}";
	return true;
}

bool ExtractFrame(const uint8_t* data, size_t size, int timeMs, int maxWidth,
                  std::vector<uint8_t>& out) {
	if (data == nullptr || size == 0) return false;

	FormatSession session(data, size);
	if (!session.Open()) return false;
	AVFormatContext* fmt = session.get();

	int videoIndex = -1;
	for (unsigned i = 0; i < fmt->nb_streams; i++) {
		if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoIndex = static_cast<int>(i);
			break;
		}
	}
	if (videoIndex < 0) return false;
	AVStream* vs = fmt->streams[videoIndex];

	const AVCodec* codec = avcodec_find_decoder(vs->codecpar->codec_id);
	if (codec == nullptr) return false;
	AVCodecContext* ctx = avcodec_alloc_context3(codec);
	if (ctx == nullptr) return false;
	bool ok = false;
	do {
		if (avcodec_parameters_to_context(ctx, vs->codecpar) < 0) break;
		if (avcodec_open2(ctx, codec, nullptr) < 0) break;

		if (timeMs > 0) {
			const int64_t ts = static_cast<int64_t>(timeMs) * AV_TIME_BASE / 1000;
			if (av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) {
				avcodec_flush_buffers(ctx);
			}
		}

		AVPacket* pkt = av_packet_alloc();
		AVFrame* frame = av_frame_alloc();
		AVFrame* rgba = av_frame_alloc();
		bool decoded = false;
		if (pkt != nullptr && frame != nullptr && rgba != nullptr) {
			rgba->format = AV_PIX_FMT_RGBA;
			rgba->width = ctx->width;
			rgba->height = ctx->height;
			if (av_frame_get_buffer(rgba, 0) >= 0) {
				struct SwsContext* sws = nullptr;
				int outW = ctx->width;
				int outH = ctx->height;
				if (maxWidth > 0 && ctx->width > maxWidth) {
					outW = maxWidth;
					outH = (ctx->height * maxWidth + ctx->width / 2) / ctx->width;
					if (outH < 1) outH = 1;
				}
				while (!decoded && av_read_frame(fmt, pkt) >= 0) {
					if (pkt->stream_index != videoIndex) {
						av_packet_unref(pkt);
						continue;
					}
					if (avcodec_send_packet(ctx, pkt) >= 0) {
						while (avcodec_receive_frame(ctx, frame) >= 0) {
							if (sws == nullptr) {
								sws = sws_getContext(
								    frame->width, frame->height,
								    static_cast<AVPixelFormat>(frame->format), outW,
								    outH, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
								    nullptr, nullptr);
								if (sws == nullptr) break;
								rgba->width = outW;
								rgba->height = outH;
								av_frame_get_buffer(rgba, 0);
							}
							sws_scale(sws, frame->data, frame->linesize, 0,
							          frame->height, rgba->data, rgba->linesize);
							decoded = true;
							break;
						}
					}
					av_packet_unref(pkt);
				}
				if (sws != nullptr) sws_freeContext(sws);
			}
		}

		if (decoded) {
			PngSink sink{&out};
			if (!stbi_write_png_to_func(PngWriteCb, &sink, rgba->width,
			                            rgba->height, 4, rgba->data[0],
			                            rgba->linesize[0])) {
				out.clear();
			} else {
				ok = true;
			}
		}

		av_frame_free(&rgba);
		av_frame_free(&frame);
		av_packet_free(&pkt);
	} while (false);
	avcodec_free_context(&ctx);
	return ok;
}

}  // namespace tui
