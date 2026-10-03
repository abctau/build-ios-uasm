#include "transcode_core.h"

#include <time.h>
#include <pthread.h>

#include <cstdio>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include "vendor/xlsx_json.h"

namespace tui {
namespace {

int g_bitrateKbps = 0;

std::string AveStr(int e) {
	char buf[128];
	av_strerror(e, buf, sizeof(buf));
	return buf;
}

// ---------- JSON 解析 ----------

const JsonValue* FindObj(const JsonValue& obj, const char* key) {
	if (!obj.IsObject()) return nullptr;
	return obj.Find(key);
}

double GetNum(const JsonValue& obj, const char* key, double def) {
	const JsonValue* v = FindObj(obj, key);
	if (v == nullptr || v->type != JsonValue::Type::Number) return def;
	return v->numVal;
}

std::string GetStr(const JsonValue& obj, const char* key, const std::string& def = "") {
	const JsonValue* v = FindObj(obj, key);
	if (v == nullptr || v->type != JsonValue::Type::String) return def;
	return v->strVal;
}

// ---------- 输出内存 AVIO ----------

struct VecWriter {
	std::vector<uint8_t>* out;
	size_t logicalPos = 0;
};

int WriteVecCb(void* opaque, const uint8_t* buf, int bufSize) {
	VecWriter* w = static_cast<VecWriter*>(opaque);
	const size_t n = static_cast<size_t>(bufSize);
	if (w->logicalPos + n > w->out->size()) w->out->resize(w->logicalPos + n, 0);
	std::memcpy(w->out->data() + w->logicalPos, buf, n);
	w->logicalPos += n;
	return bufSize;
}

int64_t SeekVecCb(void* opaque, int64_t offset, int whence) {
	VecWriter* w = static_cast<VecWriter*>(opaque);
	int64_t newPos = -1;
	if (whence == SEEK_SET) newPos = offset;
	else if (whence == SEEK_CUR) newPos = static_cast<int64_t>(w->logicalPos) + offset;
	else if (whence == SEEK_END) newPos = static_cast<int64_t>(w->out->size()) + offset;
	if (newPos < 0) return -1;
	w->logicalPos = static_cast<size_t>(newPos);
	return newPos;
}

// ---------- 输入源统一打开 ----------

struct MemReader {
	const uint8_t* data;
	size_t size;
	size_t pos;
};

int ReadCb(void* opaque, uint8_t* buf, int bufSize) {
	MemReader* r = static_cast<MemReader*>(opaque);
	if (r->pos >= r->size) return AVERROR_EOF;
	size_t n = static_cast<size_t>(bufSize) < (r->size - r->pos)
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

struct InputCtx {
	AVFormatContext* ic = nullptr;
	AVIOContext* avio = nullptr;
	uint8_t* avioBuf = nullptr;
	MemReader reader{};
};

bool OpenInput(const JobInput& in, InputCtx& ctx, std::string& err) {
	if (in.bytes != nullptr) {
		ctx.reader = {in.bytes, in.bytesSize, 0};
		ctx.ic = avformat_alloc_context();
		if (ctx.ic == nullptr) { err = "alloc input failed"; return false; }
		ctx.avioBuf = static_cast<uint8_t*>(av_malloc(64 * 1024));
		ctx.avio = avio_alloc_context(ctx.avioBuf, 64 * 1024, 0, &ctx.reader, ReadCb,
		                              nullptr, SeekCb);
		if (ctx.avio == nullptr) { err = "alloc avio failed"; return false; }
		ctx.ic->pb = ctx.avio;
		int e = avformat_open_input(&ctx.ic, "", nullptr, nullptr);
		if (e < 0) { err = "open input: " + AveStr(e); return false; }
	} else {
		int e = avformat_open_input(&ctx.ic, in.path.c_str(), nullptr, nullptr);
		if (e < 0) { err = "open input: " + AveStr(e); return false; }
	}
	if (avformat_find_stream_info(ctx.ic, nullptr) < 0) {
		err = "find stream info failed";
		return false;
	}
	return true;
}

void CloseInput(InputCtx& ctx) {
	if (ctx.ic != nullptr) avformat_close_input(&ctx.ic);
	if (ctx.avio != nullptr) {
		avio_context_free(&ctx.avio);
		ctx.avio = nullptr;
		ctx.avioBuf = nullptr;
	}
}

// ---------- 流规划 ----------

enum class Mode { COPY, ENCODE, SKIP, FILTER_ONLY };

struct StreamPlan {
	int inputIdx = -1;
	int streamIdx = -1;
	AVMediaType type = AVMEDIA_TYPE_UNKNOWN;
	Mode mode = Mode::SKIP;
	std::string codec;
	int outIndex = -1;
	AVCodecContext* dec = nullptr;
	AVCodecContext* enc = nullptr;
	AVFilterGraph* graph = nullptr;
	AVFilterContext* src = nullptr;    // buffersrc（主输入）
	AVFilterContext* src2 = nullptr;   // buffersrc（PiP overlay 输入）
	AVFilterContext* sink = nullptr;
	AVAudioFifo* fifo = nullptr;       // aac 编码重采样 FIFO
	AVCodecContext* dec2 = nullptr;    // overlay/amix 第二输入解码器
	AVPacket* pkt2 = nullptr;          // 第二输入读包游标
	int auxStreamIdx = -1;             // 第二输入目标流
	int64_t startTs = AV_NOPTS_VALUE;
	int64_t nextPts = 0;               // 编码输出累计 pts（enc time_base）
	bool done = false;
	bool src2Eof = false;
};

const AVCodec* FindEncoder(const std::string& name) {
	if (name == "mpeg4") return avcodec_find_encoder(AV_CODEC_ID_MPEG4);
	if (name == "aac") return avcodec_find_encoder(AV_CODEC_ID_AAC);
	if (name == "png") return avcodec_find_encoder(AV_CODEC_ID_PNG);
	return nullptr;
}

// 简单滤镜链按逗号拆分（不支持滤镜参数内的逗号）
std::vector<std::string> SplitFilters(const std::string& s) {
	std::vector<std::string> parts;
	std::string cur;
	for (char c : s) {
		if (c == ',') {
			if (!cur.empty()) parts.push_back(cur);
			cur.clear();
		} else {
			cur.push_back(c);
		}
	}
	if (!cur.empty()) parts.push_back(cur);
	return parts;
}

// 单滤镜挂载：spec 形如 "crop=640:480:0:0" 或 "hue"
bool AppendFilter(AVFilterGraph* graph, AVFilterContext** cur,
                  const std::string& spec, const std::string& tag,
                  std::string& err) {
	size_t eq = spec.find('=');
	std::string name = eq == std::string::npos ? spec : spec.substr(0, eq);
	std::string args = eq == std::string::npos ? "" : spec.substr(eq + 1);
	const AVFilter* fdef = avfilter_get_by_name(name.c_str());
	if (fdef == nullptr) { err = "unknown filter '" + name + "'"; return false; }
	AVFilterContext* f = nullptr;
	if (avfilter_graph_create_filter(&f, fdef, (tag + name).c_str(),
	                                 args.empty() ? nullptr : args.c_str(), nullptr,
	                                 graph) < 0) {
		err = "create filter '" + name + "' failed";
		return false;
	}
	if (avfilter_link(*cur, 0, f, 0) < 0) { err = "link filter '" + name + "' failed"; return false; }
	*cur = f;
	return true;
}

bool BuildVideoChain(StreamPlan& plan, const InputCtx& in, const InputCtx* inOv,
                     const JobOptions& opt, std::string& err) {
	const AVStream* st = in.ic->streams[plan.streamIdx];
	const AVCodecParameters* par = st->codecpar;

	plan.graph = avfilter_graph_alloc();
	if (plan.graph == nullptr) { err = "alloc filter graph failed"; return false; }

	char args[512];
	std::snprintf(args, sizeof(args),
	              "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
	              par->width, par->height, static_cast<int>(par->format),
	              st->time_base.num, st->time_base.den,
	              par->sample_aspect_ratio.num ? par->sample_aspect_ratio.num : 1,
	              par->sample_aspect_ratio.den ? par->sample_aspect_ratio.den : 1);
	if (avfilter_graph_create_filter(&plan.src, avfilter_get_by_name("buffer"), "src",
	                                 args, nullptr, plan.graph) < 0) {
		err = "create buffer src failed";
		return false;
	}
	AVFilterContext* cur = plan.src;

	// PiP：第二输入缩放后 overlay 到主输入
	if (opt.hasOverlay && plan.inputIdx == 0 && inOv != nullptr) {
		int vIdx = -1;
		for (unsigned i = 0; i < inOv->ic->nb_streams; ++i) {
			if (inOv->ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
				vIdx = static_cast<int>(i);
				break;
			}
		}
		if (vIdx < 0) { err = "overlay input has no video stream"; return false; }
		const AVStream* st2 = inOv->ic->streams[vIdx];
		const AVCodecParameters* par2 = st2->codecpar;
		char args2[512];
		std::snprintf(args2, sizeof(args2),
		              "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
		              par2->width, par2->height, static_cast<int>(par2->format),
		              st2->time_base.num, st2->time_base.den, 1, 1);
		if (avfilter_graph_create_filter(&plan.src2, avfilter_get_by_name("buffer"),
		                                 "src2", args2, nullptr, plan.graph) < 0) {
			err = "create overlay buffer src failed";
			return false;
		}
		char ovScale[128];
		std::snprintf(ovScale, sizeof(ovScale), "%d:%d",
		              opt.overlayW > 0 ? opt.overlayW : par2->width,
		              opt.overlayH > 0 ? opt.overlayH : par2->height);
		AVFilterContext* sc = nullptr;
		if (avfilter_graph_create_filter(&sc, avfilter_get_by_name("scale"), "ovscale",
		                                 ovScale, nullptr, plan.graph) < 0) {
			err = "create overlay scale failed";
			return false;
		}
		if (avfilter_link(plan.src2, 0, sc, 0) < 0) { err = "link overlay scale failed"; return false; }
		char ovXY[64];
		std::snprintf(ovXY, sizeof(ovXY), "x=%d:y=%d", opt.overlayX, opt.overlayY);
		AVFilterContext* ov = nullptr;
		if (avfilter_graph_create_filter(&ov, avfilter_get_by_name("overlay"), "ov",
		                                 ovXY, nullptr, plan.graph) < 0) {
			err = "create overlay failed";
			return false;
		}
		if (avfilter_link(cur, 0, ov, 0) < 0) { err = "link overlay main failed"; return false; }
		if (avfilter_link(sc, 0, ov, 1) < 0) { err = "link overlay second failed"; return false; }
		cur = ov;
	}

	// 前置缩放
	if (opt.scaleWidth > 0 && opt.scaleHeight > 0) {
		char sa[160];
		std::snprintf(sa, sizeof(sa),
		              "scale=%d:%d:force_original_aspect_ratio=decrease:force_divisible_by=2",
		              opt.scaleWidth, opt.scaleHeight);
		AVFilterContext* f = nullptr;
		if (avfilter_graph_create_filter(&f, avfilter_get_by_name("scale"), "pre_scale",
		                                 sa + 6, nullptr, plan.graph) < 0) {
			err = "create scale failed";
			return false;
		}
		if (avfilter_link(cur, 0, f, 0) < 0) { err = "link scale failed"; return false; }
		cur = f;
	}

	// 用户滤镜串
	for (const std::string& spec : SplitFilters(opt.videoFilter)) {
		if (!AppendFilter(plan.graph, &cur, spec, "v", err)) return false;
	}

	// 目标像素格式
	AVFilterContext* fmtF = nullptr;
	if (avfilter_graph_create_filter(&fmtF, avfilter_get_by_name("format"), "fmt",
	                                 plan.codec == "png" ? "pix_fmts=rgba" : "pix_fmts=yuv420p",
	                                 nullptr, plan.graph) < 0) {
		err = "create format failed";
		return false;
	}
	if (avfilter_link(cur, 0, fmtF, 0) < 0) { err = "link format failed"; return false; }

	if (avfilter_graph_create_filter(&plan.sink, avfilter_get_by_name("buffersink"),
	                                 "sink", nullptr, nullptr, plan.graph) < 0) {
		err = "create buffersink failed";
		return false;
	}
	const enum AVPixelFormat pixFmts[] = {
	    plan.codec == "png" ? AV_PIX_FMT_RGBA : AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
	if (av_opt_set_int_list(plan.sink, "pix_fmts", pixFmts, AV_PIX_FMT_NONE,
	                        AV_OPT_SEARCH_CHILDREN) < 0) {
		err = "set sink pix fmts failed";
		return false;
	}
	if (avfilter_link(fmtF, 0, plan.sink, 0) < 0) { err = "link sink failed"; return false; }
	if (avfilter_graph_config(plan.graph, nullptr) < 0) {
		err = "config video filter graph failed";
		return false;
	}
	return true;
}

bool BuildAudioChain(StreamPlan& plan, const InputCtx& in,
                     const InputCtx* inMix, const JobOptions& opt,
                     std::string& err) {
	const AVStream* st = in.ic->streams[plan.streamIdx];
	const AVCodecParameters* par = st->codecpar;
	int64_t layout = par->ch_layout.u.mask
	                     ? static_cast<int64_t>(par->ch_layout.u.mask)
	                     : (1ULL << par->ch_layout.nb_channels) - 1;

	plan.graph = avfilter_graph_alloc();
	if (plan.graph == nullptr) { err = "alloc graph failed"; return false; }

	char args[256];
	std::snprintf(args, sizeof(args),
	              "sample_rate=%d:sample_fmt=%d:channel_layout=%lld", par->sample_rate,
	              static_cast<int>(par->format), static_cast<long long>(layout));
	if (avfilter_graph_create_filter(&plan.src, avfilter_get_by_name("abuffer"), "src",
	                                 args, nullptr, plan.graph) < 0) {
		err = "create abuffer failed";
		return false;
	}
	AVFilterContext* cur = plan.src;

	// amix：第二输入音频
	if (opt.mixAudio && inMix != nullptr) {
		int aIdx = -1;
		for (unsigned i = 0; i < inMix->ic->nb_streams; ++i) {
			if (inMix->ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
				aIdx = static_cast<int>(i);
				break;
			}
		}
		if (aIdx < 0) { err = "mix input has no audio stream"; return false; }
		const AVCodecParameters* par2 = inMix->ic->streams[aIdx]->codecpar;
		int64_t layout2 = par2->ch_layout.u.mask
		                      ? static_cast<int64_t>(par2->ch_layout.u.mask)
		                      : (1ULL << par2->ch_layout.nb_channels) - 1;
		char args2[256];
		std::snprintf(args2, sizeof(args2),
		              "sample_rate=%d:sample_fmt=%d:channel_layout=%lld",
		              par2->sample_rate, static_cast<int>(par2->format),
		              static_cast<long long>(layout2));
		if (avfilter_graph_create_filter(&plan.src2, avfilter_get_by_name("abuffer"),
		                                 "src2", args2, nullptr, plan.graph) < 0) {
			err = "create mix abuffer failed";
			return false;
		}
		AVFilterContext* mix = nullptr;
		if (avfilter_graph_create_filter(&mix, avfilter_get_by_name("amix"), "mix",
		                                 "inputs=2:duration=longest", nullptr,
		                                 plan.graph) < 0) {
			err = "create amix failed";
			return false;
		}
		if (avfilter_link(plan.src, 0, mix, 0) < 0) { err = "link amix main failed"; return false; }
		if (avfilter_link(plan.src2, 0, mix, 1) < 0) { err = "link amix second failed"; return false; }
		cur = mix;
	}

	for (const std::string& spec : SplitFilters(opt.audioFilter)) {
		if (!AppendFilter(plan.graph, &cur, spec, "a", err)) return false;
	}

	AVFilterContext* fmtF = nullptr;
	if (avfilter_graph_create_filter(&fmtF, avfilter_get_by_name("aformat"), "afmt",
	                                 "sample_fmts=fltp", nullptr, plan.graph) < 0) {
		err = "create aformat failed";
		return false;
	}
	if (avfilter_link(cur, 0, fmtF, 0) < 0) { err = "link aformat failed"; return false; }

	if (avfilter_graph_create_filter(&plan.sink, avfilter_get_by_name("abuffersink"),
	                                 "sink", nullptr, nullptr, plan.graph) < 0) {
		err = "create abuffersink failed";
		return false;
	}
	if (avfilter_link(fmtF, 0, plan.sink, 0) < 0) { err = "link sink failed"; return false; }
	if (avfilter_graph_config(plan.graph, nullptr) < 0) {
		err = "config audio filter graph failed";
		return false;
	}
	return true;
}

bool OpenDecoder(StreamPlan& plan, const AVCodecParameters* par, std::string& err) {
	const AVCodec* dec = avcodec_find_decoder(par->codec_id);
	if (dec == nullptr) { err = "decoder not found"; return false; }
	plan.dec = avcodec_alloc_context3(dec);
	if (plan.dec == nullptr) { err = "alloc decoder failed"; return false; }
	if (avcodec_parameters_to_context(plan.dec, par) < 0) {
		err = "decoder params failed";
		return false;
	}
	if (avcodec_open2(plan.dec, dec, nullptr) < 0) { err = "open decoder failed"; return false; }
	return true;
}

bool OpenEncoder(StreamPlan& plan, AVStream* outSt, std::string& err) {
	const AVCodec* enc = FindEncoder(plan.codec);
	if (enc == nullptr) { err = "encoder '" + plan.codec + "' not found"; return false; }
	plan.enc = avcodec_alloc_context3(enc);
	if (plan.enc == nullptr) { err = "alloc encoder failed"; return false; }

	if (plan.type == AVMEDIA_TYPE_VIDEO) {
		// 从 buffersink 探测实际输出格式
		plan.enc->pix_fmt = static_cast<AVPixelFormat>(
		    av_buffersink_get_format(plan.sink));
		plan.enc->width = av_buffersink_get_w(plan.sink);
		plan.enc->height = av_buffersink_get_h(plan.sink);
		AVRational fr = av_buffersink_get_frame_rate(plan.sink);
		if (fr.num > 0 && fr.den > 0) {
			plan.enc->time_base = AVRational{fr.den, fr.num};
			plan.enc->framerate = fr;
			plan.enc->gop_size =
			    static_cast<int>(static_cast<double>(fr.num) / fr.den * 12) + 1;
		} else {
			plan.enc->time_base = av_buffersink_get_time_base(plan.sink);
			plan.enc->framerate = AVRational{0, 1};
			plan.enc->gop_size = 250;
		}
		if (plan.codec != "png") {
			plan.enc->bit_rate = g_bitrateKbps > 0
			                         ? static_cast<int64_t>(g_bitrateKbps) * 1000
			                         : 2500000;
		}
	} else {
		plan.enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
		plan.enc->sample_rate = av_buffersink_get_sample_rate(plan.sink);
		AVChannelLayout inLayout;
		av_buffersink_get_ch_layout(plan.sink, &inLayout);
		av_channel_layout_copy(&plan.enc->ch_layout, &inLayout);
		plan.enc->time_base = AVRational{1, plan.enc->sample_rate};
		plan.enc->bit_rate = 128000;
	}

	AVDictionary* encOpts = nullptr;
	if (avcodec_open2(plan.enc, enc, &encOpts) < 0) {
		av_dict_free(&encOpts);
		err = "open encoder '" + plan.codec + "' failed";
		return false;
	}
	av_dict_free(&encOpts);

	if (avcodec_parameters_from_context(outSt->codecpar, plan.enc) < 0) {
		err = "encoder params to stream failed";
		return false;
	}
	outSt->time_base = plan.enc->time_base;
	return true;
}

void DrainFilter(StreamPlan& plan, AVFormatContext* ofmt, AVRational encTb,
                 std::string& err, bool& ok) {
	ok = true;
	if (plan.type == AVMEDIA_TYPE_AUDIO && plan.fifo == nullptr) {
		plan.fifo = av_audio_fifo_alloc(plan.enc->sample_fmt,
		                                plan.enc->ch_layout.nb_channels, 1);
	}
	// decoder EOF 已送入 filter；drain filter → encoder
	if (plan.fifo == nullptr) {
		while (true) {
			AVFrame* f = av_frame_alloc();
			int e = av_buffersink_get_frame(plan.sink, f);
			if (e < 0) { av_frame_free(&f); break; }
			// 送编码
			AVFrame* ef = av_frame_clone(f);
			ef->pts = av_rescale_q(f->pts, av_buffersink_get_time_base(plan.sink), encTb);
			if (avcodec_send_frame(plan.enc, ef) < 0) {
				av_frame_free(&ef);
				av_frame_free(&f);
				err = "encode frame failed";
				ok = false;
				return;
			}
			av_frame_free(&ef);
			av_frame_free(&f);
			AVPacket* pkt = av_packet_alloc();
			while (avcodec_receive_packet(plan.enc, pkt) >= 0) {
				av_packet_rescale_ts(pkt, encTb, ofmt->streams[plan.outIndex]->time_base);
				pkt->stream_index = plan.outIndex;
				av_interleaved_write_frame(ofmt, pkt);
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
		}
		return;
	}
	// 音频 FIFO 路径
	const int frameSize = plan.enc->frame_size > 0 ? plan.enc->frame_size : 1024;
	auto encodeFifo = [&](bool flush) {
		while (av_audio_fifo_size(plan.fifo) >= frameSize ||
		       (flush && av_audio_fifo_size(plan.fifo) > 0)) {
			AVFrame* ef = av_frame_alloc();
			ef->nb_samples = flush ? av_audio_fifo_size(plan.fifo) : frameSize;
			ef->format = plan.enc->sample_fmt;
			av_channel_layout_copy(&ef->ch_layout, &plan.enc->ch_layout);
			ef->sample_rate = plan.enc->sample_rate;
			ef->pts = plan.nextPts;
			if (av_frame_get_buffer(ef, 0) < 0) { av_frame_free(&ef); err = "fifo frame alloc failed"; ok = false; return; }
			av_audio_fifo_read(plan.fifo, reinterpret_cast<void**>(ef->data), ef->nb_samples);
			plan.nextPts += ef->nb_samples;
			if (avcodec_send_frame(plan.enc, ef) < 0) {
				av_frame_free(&ef);
				err = "encode audio frame failed";
				ok = false;
				return;
			}
			av_frame_free(&ef);
			AVPacket* pkt = av_packet_alloc();
			while (avcodec_receive_packet(plan.enc, pkt) >= 0) {
				av_packet_rescale_ts(pkt, encTb, ofmt->streams[plan.outIndex]->time_base);
				pkt->stream_index = plan.outIndex;
				av_interleaved_write_frame(ofmt, pkt);
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
		}
	};
	while (true) {
		AVFrame* f = av_frame_alloc();
		int e = av_buffersink_get_frame(plan.sink, f);
		if (e < 0) { av_frame_free(&f); break; }
		if (plan.fifo == nullptr) {
			plan.fifo = av_audio_fifo_alloc(plan.enc->sample_fmt,
			                                plan.enc->ch_layout.nb_channels, 1);
		}
		av_audio_fifo_realloc(plan.fifo, av_audio_fifo_size(plan.fifo) + f->nb_samples);
		av_audio_fifo_write(plan.fifo, reinterpret_cast<void**>(f->data), f->nb_samples);
		av_frame_free(&f);
		encodeFifo(false);
		if (!ok) return;
	}
	encodeFifo(true);
}

void FeedFrameToEncoder(StreamPlan& plan, AVFrame* f, AVFormatContext* ofmt,
                        std::string& err, bool& ok) {
	ok = true;
	const AVRational encTb = plan.enc->time_base;
	if (plan.type == AVMEDIA_TYPE_AUDIO && plan.fifo == nullptr) {
		plan.fifo = av_audio_fifo_alloc(plan.enc->sample_fmt,
		                                plan.enc->ch_layout.nb_channels, 1);
	}
	if (plan.fifo == nullptr) {
		AVFrame* ef = av_frame_clone(f);
		ef->pts = av_rescale_q(f->pts, av_buffersink_get_time_base(plan.sink), encTb);
		if (avcodec_send_frame(plan.enc, ef) < 0) {
			av_frame_free(&ef);
			err = "encode frame failed";
			ok = false;
			return;
		}
		av_frame_free(&ef);
		AVPacket* pkt = av_packet_alloc();
		while (avcodec_receive_packet(plan.enc, pkt) >= 0) {
			av_packet_rescale_ts(pkt, encTb, ofmt->streams[plan.outIndex]->time_base);
			pkt->stream_index = plan.outIndex;
			av_interleaved_write_frame(ofmt, pkt);
			av_packet_unref(pkt);
		}
		av_packet_free(&pkt);
		return;
	}
	av_audio_fifo_realloc(plan.fifo, av_audio_fifo_size(plan.fifo) + f->nb_samples);
	av_audio_fifo_write(plan.fifo, reinterpret_cast<void**>(f->data), f->nb_samples);
	const int frameSize = plan.enc->frame_size > 0 ? plan.enc->frame_size : 1024;
	while (av_audio_fifo_size(plan.fifo) >= frameSize) {
		AVFrame* ef = av_frame_alloc();
		ef->nb_samples = frameSize;
		ef->format = plan.enc->sample_fmt;
		av_channel_layout_copy(&ef->ch_layout, &plan.enc->ch_layout);
		ef->sample_rate = plan.enc->sample_rate;
		ef->pts = plan.nextPts;
		if (av_frame_get_buffer(ef, 0) < 0) { av_frame_free(&ef); err = "fifo frame alloc failed"; ok = false; return; }
		av_audio_fifo_read(plan.fifo, reinterpret_cast<void**>(ef->data), frameSize);
		plan.nextPts += frameSize;
		if (avcodec_send_frame(plan.enc, ef) < 0) {
			av_frame_free(&ef);
			err = "encode audio frame failed";
			ok = false;
			return;
		}
		av_frame_free(&ef);
		AVPacket* pkt = av_packet_alloc();
		while (avcodec_receive_packet(plan.enc, pkt) >= 0) {
			av_packet_rescale_ts(pkt, encTb, ofmt->streams[plan.outIndex]->time_base);
			pkt->stream_index = plan.outIndex;
			av_interleaved_write_frame(ofmt, pkt);
			av_packet_unref(pkt);
		}
		av_packet_free(&pkt);
	}
}

bool FeedAuxFrame(StreamPlan& plan, const InputCtx& aux, int auxStreamIdx,
                  std::string& err) {
	if (plan.src2 == nullptr || plan.src2Eof || plan.dec2 == nullptr) return true;
	AVPacket* pkt = plan.pkt2;
	AVFrame* frame = av_frame_alloc();
	while (true) {
		int e = av_read_frame(aux.ic, pkt);
		if (e < 0) {
			av_buffersrc_add_frame_flags(plan.src2, nullptr,
			                             AV_BUFFERSRC_FLAG_KEEP_REF);
			plan.src2Eof = true;
			av_frame_free(&frame);
			return true;
		}
		if (pkt->stream_index != auxStreamIdx) {
			av_packet_unref(pkt);
			continue;
		}
		if (avcodec_send_packet(plan.dec2, pkt) < 0) {
			av_packet_unref(pkt);
			continue;
		}
		av_packet_unref(pkt);
		if (avcodec_receive_frame(plan.dec2, frame) >= 0) {
			if (av_buffersrc_add_frame_flags(plan.src2, frame,
			                                 AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
				err = "filter src2 add frame failed";
				av_frame_free(&frame);
				return false;
			}
			av_frame_free(&frame);
			return true;
		}
	}
}

bool HandlePacket(StreamPlan& plan, const InputCtx& in, const InputCtx* auxIn,
                  int auxStreamIdx, AVPacket* pkt, AVFormatContext* ofmt,
                  const JobOptions& opt, std::string& err) {
	const AVStream* st = in.ic->streams[plan.streamIdx];
	const int64_t startUs =
	    opt.startMs > 0 ? static_cast<int64_t>(opt.startMs * 1000) : -1;
	const int64_t durationUs =
	    opt.durationMs > 0 ? static_cast<int64_t>(opt.durationMs * 1000) : -1;

	const int64_t ptsUs =
	    pkt->pts != AV_NOPTS_VALUE
	        ? av_rescale_q(pkt->pts, st->time_base, AVRational{1, 1000000})
	        : AV_NOPTS_VALUE;
	if (startUs > 0 && ptsUs != AV_NOPTS_VALUE && ptsUs < startUs) return true;
	if (durationUs > 0 && ptsUs != AV_NOPTS_VALUE) {
		if (plan.startTs == AV_NOPTS_VALUE) plan.startTs = ptsUs;
		if (ptsUs - plan.startTs >= durationUs) {
			plan.done = true;
			if (plan.dec != nullptr) avcodec_flush_buffers(plan.dec);
			return true;
		}
	}

	if (plan.mode == Mode::COPY) {
		av_packet_rescale_ts(pkt, st->time_base,
		                     ofmt->streams[plan.outIndex]->time_base);
		pkt->stream_index = plan.outIndex;
		if (av_interleaved_write_frame(ofmt, pkt) < 0) {
			err = "write copy packet failed";
			return false;
		}
		return true;
	}

	// ENCODE：解码 → 喂主路 src → 同步喂 src2（overlay/amix）→ 收 sink → 编码
	if (avcodec_send_packet(plan.dec, pkt) < 0) return true;
	AVFrame* frame = av_frame_alloc();
	bool ok = true;
	while (avcodec_receive_frame(plan.dec, frame) >= 0) {
		if (av_buffersrc_add_frame_flags(plan.src, frame,
		                                 AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
			err = "filter src add frame failed";
			ok = false;
			break;
		}
		if (plan.dec2 != nullptr && auxIn != nullptr) {
			if (!FeedAuxFrame(plan, *auxIn, auxStreamIdx, err)) {
				ok = false;
				break;
			}
		}
		while (true) {
			AVFrame* of = av_frame_alloc();
			int ge = av_buffersink_get_frame(plan.sink, of);
			if (ge < 0) { av_frame_free(&of); break; }
			FeedFrameToEncoder(plan, of, ofmt, err, ok);
			av_frame_free(&of);
			if (!ok) break;
		}
		av_frame_unref(frame);
		if (!ok) break;
	}
	av_frame_free(&frame);
	return ok;
}

void DrainPlan(StreamPlan& p, AVFormatContext* ofmt, std::string& err,
               bool& ok) {
	if (p.mode != Mode::ENCODE) return;
	if (!p.done && p.dec != nullptr) avcodec_send_packet(p.dec, nullptr);
	if (p.dec2 != nullptr) avcodec_send_packet(p.dec2, nullptr);
	if (!p.src2Eof && p.src2 != nullptr) {
		av_buffersrc_add_frame_flags(p.src2, nullptr, AV_BUFFERSRC_FLAG_KEEP_REF);
		p.src2Eof = true;
	}
	if (p.dec != nullptr) {
		AVFrame* frame = av_frame_alloc();
		while (avcodec_receive_frame(p.dec, frame) >= 0) {
			av_buffersrc_add_frame_flags(p.src, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
			av_frame_unref(frame);
		}
		av_frame_free(&frame);
	}
	av_buffersrc_add_frame_flags(p.src, nullptr, AV_BUFFERSRC_FLAG_KEEP_REF);
	DrainFilter(p, ofmt, p.enc->time_base, err, ok);
	if (!ok) return;
	avcodec_send_frame(p.enc, nullptr);
	AVPacket* pkt = av_packet_alloc();
	while (avcodec_receive_packet(p.enc, pkt) >= 0) {
		av_packet_rescale_ts(pkt, p.enc->time_base,
		                     ofmt->streams[p.outIndex]->time_base);
		pkt->stream_index = p.outIndex;
		av_interleaved_write_frame(ofmt, pkt);
		av_packet_unref(pkt);
	}
	av_packet_free(&pkt);
}

const char* OutputFormatName(const JobOptions& opt) {
	std::string fmt = opt.outputFormat;
	if (!opt.outputPath.empty()) {
		size_t dot = opt.outputPath.rfind('.');
		if (dot != std::string::npos) fmt = opt.outputPath.substr(dot + 1);
		for (auto& c : fmt) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
	}
	if (fmt == "m4a") return "ipod";
	if (fmt == "aac") return "adts";
	return "mp4";
}

}  // namespace

bool ParseJobOptions(const std::string& json, JobOptions& opt, std::string& err) {
	tui::JsonValue root;
	if (!tui::JsonParse(json, root) || !root.IsObject()) {
		err = "invalid options json";
		return false;
	}
	const tui::JsonValue* inputs = FindObj(root, "inputs");
	if (inputs == nullptr || !inputs->IsArray() || inputs->items.empty() ||
	    inputs->items.size() > 2) {
		err = "inputs must be 1..2 entries";
		return false;
	}
	for (const auto& item : inputs->items) {
		JobInput in;
		if (item.IsString()) {
			in.path = item.strVal;
		} else {
			err = "inputs entries must be strings";
			return false;
		}
		opt.inputs.push_back(std::move(in));
	}
	opt.outputPath = GetStr(root, "output");
	opt.outputFormat = GetStr(root, "outputFormat");
	opt.startMs = GetNum(root, "startMs", 0);
	opt.durationMs = GetNum(root, "durationMs", 0);
	opt.videoCodec = GetStr(root, "videoCodec", "copy");
	opt.audioCodec = GetStr(root, "audioCodec", "copy");
	opt.videoFilter = GetStr(root, "videoFilter");
	opt.audioFilter = GetStr(root, "audioFilter");
	opt.videoBitrateKbps = static_cast<int>(GetNum(root, "videoBitrateKbps", 0));
	opt.scaleWidth = static_cast<int>(GetNum(root, "scaleWidth", 0));
	opt.scaleHeight = static_cast<int>(GetNum(root, "scaleHeight", 0));
	const tui::JsonValue* overlay = FindObj(root, "overlay");
	if (overlay != nullptr && overlay->IsObject()) {
		opt.hasOverlay = true;
		opt.overlayX = static_cast<int>(GetNum(*overlay, "x", 0));
		opt.overlayY = static_cast<int>(GetNum(*overlay, "y", 0));
		opt.overlayW = static_cast<int>(GetNum(*overlay, "width", 0));
		opt.overlayH = static_cast<int>(GetNum(*overlay, "height", 0));
	}
	const tui::JsonValue* mix = FindObj(root, "mixAudio");
	if (mix != nullptr && mix->IsBool()) opt.mixAudio = mix->boolVal;
	return true;
}

bool TranscodeRun(const JobOptions& opt, std::vector<uint8_t>& outBytes,
                  std::string& err) {
	g_bitrateKbps = opt.videoBitrateKbps;
	const int nInputs = static_cast<int>(opt.inputs.size());
	std::vector<InputCtx> inputs(nInputs);
	std::vector<StreamPlan> plans;
	AVFormatContext* ofmt = nullptr;
	AVIOContext* outAvio = nullptr;
	uint8_t* outAvioBuf = nullptr;
	VecWriter vecWriter{&outBytes};
	bool ok = false;

	auto cleanup = [&]() {
		for (auto& p : plans) {
			if (p.dec != nullptr) avcodec_free_context(&p.dec);
			if (p.enc != nullptr) avcodec_free_context(&p.enc);
			if (p.dec2 != nullptr) avcodec_free_context(&p.dec2);
			if (p.pkt2 != nullptr) av_packet_free(&p.pkt2);
			if (p.graph != nullptr) avfilter_graph_free(&p.graph);
			if (p.fifo != nullptr) av_audio_fifo_free(p.fifo);
		}
		if (ofmt != nullptr) {
			avformat_free_context(ofmt);
		}
		if (outAvio != nullptr) {
			avio_flush(outAvio);
			if (outAvio->buffer != nullptr) av_freep(&outAvio->buffer);
			avio_context_free(&outAvio);
			outAvio = nullptr;
		}
		for (auto& c : inputs) CloseInput(c);
		g_bitrateKbps = 0;
	};

	// 1. 打开输入
	for (int i = 0; i < nInputs; ++i) {
		if (!OpenInput(opt.inputs[i], inputs[i], err)) return false;
	}

	// 2. seek（时长裁剪起点）
	if (opt.startMs > 0) {
		const int64_t startUs = static_cast<int64_t>(opt.startMs * 1000);
		for (auto& in : inputs) {
			av_seek_frame(in.ic, -1, startUs, AVSEEK_FLAG_BACKWARD);
		}
	}

	// 3. 规划流
	auto findStream = [](const InputCtx& in, AVMediaType type) -> int {
		for (unsigned i = 0; i < in.ic->nb_streams; ++i) {
			if (in.ic->streams[i]->codecpar->codec_type == type) return static_cast<int>(i);
		}
		return -1;
	};

	// 主输入视频
	const int vIdx = findStream(inputs[0], AVMEDIA_TYPE_VIDEO);
	const bool videoEncode = opt.videoCodec != "copy" && opt.videoCodec != "none";
	StreamPlan vp;
	if (vIdx >= 0 && opt.videoCodec != "none") {
		vp.inputIdx = 0;
		vp.streamIdx = vIdx;
		vp.type = AVMEDIA_TYPE_VIDEO;
		vp.mode = videoEncode ? Mode::ENCODE : Mode::COPY;
		vp.codec = videoEncode ? opt.videoCodec : "";
		plans.push_back(vp);
	}

	// 音频：mixAudio 时 amix；否则主输入 + （videoMerge）第二输入
	StreamPlan ap;
	if (opt.audioCodec != "none") {
		const int aIdx = findStream(inputs[0], AVMEDIA_TYPE_AUDIO);
		if (aIdx >= 0) {
			ap.inputIdx = 0;
			ap.streamIdx = aIdx;
			ap.type = AVMEDIA_TYPE_AUDIO;
			ap.mode = opt.mixAudio ? Mode::ENCODE
			          : (opt.audioCodec == "copy" ? Mode::COPY : Mode::ENCODE);
			ap.codec = opt.audioCodec == "copy" ? "" : opt.audioCodec;
			plans.push_back(ap);
		}
		// videoMerge：第二输入音频
		if (!opt.mixAudio && nInputs == 2 && !opt.hasOverlay) {
			const int a2 = findStream(inputs[1], AVMEDIA_TYPE_AUDIO);
			if (a2 >= 0) {
				StreamPlan ap2 = ap;
				ap2.inputIdx = 1;
				ap2.streamIdx = a2;
				ap2.mode = opt.audioCodec == "copy" ? Mode::COPY : Mode::ENCODE;
				ap2.graph = nullptr;
				ap2.dec = nullptr;
				ap2.enc = nullptr;
				ap2.src = nullptr;
				ap2.sink = nullptr;
				ap2.fifo = nullptr;
				ap2.outIndex = -1;
				ap2.startTs = AV_NOPTS_VALUE;
				ap2.nextPts = 0;
				ap2.done = false;
				plans.push_back(ap2);
			}
		}
	}

	// overlay 的第二输入视频：FILTER_ONLY（不出流）
	const InputCtx* overlayInput = nullptr;
	int overlayStreamIdx = -1;
	if (opt.hasOverlay && nInputs == 2) {
		overlayStreamIdx = findStream(inputs[1], AVMEDIA_TYPE_VIDEO);
		if (overlayStreamIdx < 0) { err = "overlay input has no video stream"; cleanup(); return false; }
		overlayInput = &inputs[1];
	}

	if (plans.empty()) { err = "no output streams"; cleanup(); return false; }

	// 4. 打开输出
	const char* fmtName = OutputFormatName(opt);
	if (avformat_alloc_output_context2(&ofmt, nullptr, fmtName,
	                                   opt.outputPath.empty() ? "out" : opt.outputPath.c_str()) < 0 ||
	    ofmt == nullptr) {
		err = "alloc output failed";
		cleanup();
		return false;
	}
	for (auto& p : plans) {
		AVStream* os = avformat_new_stream(ofmt, nullptr);
		if (os == nullptr) { err = "new output stream failed"; cleanup(); return false; }
		p.outIndex = static_cast<int>(ofmt->nb_streams - 1);
		if (p.mode == Mode::COPY) {
			if (avcodec_parameters_copy(os->codecpar,
			                            inputs[p.inputIdx].ic->streams[p.streamIdx]->codecpar) < 0) {
				err = "copy codecpar failed";
				cleanup();
				return false;
			}
			os->codecpar->codec_tag = 0;
			os->time_base = inputs[p.inputIdx].ic->streams[p.streamIdx]->time_base;
		}
	}

	// 5. 编码流：decoder + filter chain + encoder
	for (auto& p : plans) {
		if (p.mode != Mode::ENCODE) continue;
		const AVCodecParameters* par = inputs[p.inputIdx].ic->streams[p.streamIdx]->codecpar;
		if (!OpenDecoder(p, par, err)) { cleanup(); return false; }
		if (p.type == AVMEDIA_TYPE_VIDEO) {
			if (!BuildVideoChain(p, inputs[0], overlayInput, opt, err)) { cleanup(); return false; }
		} else {
			if (!BuildAudioChain(p, inputs[0], opt.mixAudio ? &inputs[1] : nullptr, opt, err)) {
				cleanup();
				return false;
			}
		}
		if (!OpenEncoder(p, ofmt->streams[p.outIndex], err)) { cleanup(); return false; }
	}

	// 6. 打开输出 IO + 写头
	if (opt.outputPath.empty()) {
		outAvioBuf = static_cast<uint8_t*>(av_malloc(64 * 1024));
		outAvio = avio_alloc_context(outAvioBuf, 64 * 1024, 1, &vecWriter, nullptr,
		                             WriteVecCb, SeekVecCb);
		if (outAvio == nullptr) { err = "alloc out avio failed"; cleanup(); return false; }
		ofmt->pb = outAvio;
		ofmt->flags |= AVFMT_FLAG_CUSTOM_IO;
	} else {
		int e = avio_open(&ofmt->pb, opt.outputPath.c_str(), AVIO_FLAG_WRITE);
		if (e < 0) { err = "open output: " + AveStr(e); cleanup(); return false; }
	}
	const int eHeader = avformat_write_header(ofmt, nullptr);
	if (eHeader < 0) {
		err = "write header failed: " + AveStr(eHeader);
		cleanup();
		return false;
	}

	// 7. 主循环：按输入统一 demux 分发（多流共享读指针）
	{
		// 第二输入辅助流定位（overlay 视频 / mix 音频）
		for (auto& p : plans) {
			if (p.mode != Mode::ENCODE) continue;
			if (p.src2 == nullptr || nInputs < 2) continue;
			const int auxIdx = p.inputIdx == 0 ? 1 : 0;
			AVMediaType want = p.type == AVMEDIA_TYPE_VIDEO
			                       ? AVMEDIA_TYPE_VIDEO
			                       : AVMEDIA_TYPE_AUDIO;
			for (unsigned s = 0; s < inputs[auxIdx].ic->nb_streams; ++s) {
				if (inputs[auxIdx].ic->streams[s]->codecpar->codec_type == want) {
					p.auxStreamIdx = static_cast<int>(s);
					break;
				}
			}
			const AVCodecParameters* apar =
			    p.auxStreamIdx >= 0
			        ? inputs[auxIdx].ic->streams[p.auxStreamIdx]->codecpar
			        : nullptr;
			if (apar == nullptr) continue;
			const AVCodec* dec = avcodec_find_decoder(apar->codec_id);
			if (dec == nullptr) continue;
			p.dec2 = avcodec_alloc_context3(dec);
			if (p.dec2 == nullptr) continue;
			avcodec_parameters_to_context(p.dec2, apar);
			avcodec_open2(p.dec2, dec, nullptr);
			p.pkt2 = av_packet_alloc();
		}
		for (int i = 0; i < nInputs; ++i) {
			AVPacket* pkt = av_packet_alloc();
			while (true) {
				bool allDone = true;
				for (auto& p : plans) {
					if (p.inputIdx == i && !p.done) allDone = false;
				}
				if (allDone) break;
				int e = av_read_frame(inputs[i].ic, pkt);
				if (e < 0) {
					for (auto& p : plans) {
						if (p.inputIdx == i) p.done = true;
					}
					break;
				}
				StreamPlan* target = nullptr;
				for (auto& p : plans) {
					if (p.inputIdx == i && p.streamIdx == pkt->stream_index) {
						target = &p;
						break;
					}
				}
				if (target == nullptr || target->done) {
					av_packet_unref(pkt);
					continue;
				}
				const InputCtx* auxIn =
				    nInputs > 1 ? &inputs[target->inputIdx == 0 ? 1 : 0] : nullptr;
				if (!HandlePacket(*target, inputs[i], auxIn, target->auxStreamIdx,
				                  pkt, ofmt, opt, err)) {
					av_packet_free(&pkt);
					cleanup();
					return false;
				}
				av_packet_unref(pkt);
			}
			av_packet_free(&pkt);
		}
	}

	// 8. flush：decoder → filter → encoder
	for (auto& p : plans) {
		bool okDrain = true;
		DrainPlan(p, ofmt, err, okDrain);
		if (!okDrain) { cleanup(); return false; }
	}

	// 9. trailer
	if (av_write_trailer(ofmt) < 0) { err = "write trailer failed"; cleanup(); return false; }
	ok = true;
	cleanup();
	return ok;
}

}  // namespace tui
