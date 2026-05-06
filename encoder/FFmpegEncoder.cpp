#include "FFmpegEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include "../platform/Logger.h"
#include <format>
#include <filesystem>
#include <cstring>

// ============================================================
// Helpers
// ============================================================
static bool LogAvErr(const char* ctx, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_make_error_string(buf, sizeof(buf), err);
    Logger::Instance().Error(std::format("{}: {}", ctx, buf));
    return false;
}

#define AV_CHECK(x, msg)                          \
    do {                                          \
        int _r = (x);                             \
        if (_r < 0) return LogAvErr(msg, _r);     \
    } while (0)

// ============================================================
// Ctor / Dtor
// ============================================================
FFmpegEncoder::FFmpegEncoder()  = default;
FFmpegEncoder::~FFmpegEncoder() { Finalize(); }

// ============================================================
// Initialize  –  sets up muxer + video only.  Header is
//               written lazily on the first frame (or via
//               BeginOutput / AddAudioStream).
// ============================================================
bool FFmpegEncoder::Initialize(const EncoderConfig& config) {
    if (m_initialized) return false;
    m_config    = config;
    m_startTime = std::chrono::steady_clock::now();
    m_framePts  = 0;

    // Validate dimensions (H.264/YUV420P requires even W/H)
    if (config.width < 2 || (config.width & 1) || config.height < 2 || (config.height & 1)) {
        Logger::Instance().Error(std::format(
            "ENC: odd frame size {}x{} — H.264/YUV420P requires even dimensions",
            config.width, config.height));
        return LogAvErr("InvalidFrameSize", AVERROR(EINVAL));
    }
    Logger::Instance().Info(std::format(
        "ENC: init video {}x{} fps={} br={}kbps codec=h264 pix_fmt=yuv420p",
        config.width, config.height, config.fps, config.bitrateKbps));

    // Alloc MKV format context
    AV_CHECK(avformat_alloc_output_context2(&m_fmtCtx, nullptr, "matroska",
                                            config.outputPath.c_str()),
             "avformat_alloc_output_context2");

    // Open video codec and create video stream
    if (!OpenVideoCodec()) return false;

    m_vidStream = avformat_new_stream(m_fmtCtx, nullptr);
    if (!m_vidStream) return LogAvErr("avformat_new_stream (video)", AVERROR_UNKNOWN);
    m_vidStream->id = m_fmtCtx->nb_streams - 1;
    m_vidStream->time_base = m_vidCodec->time_base;
    avcodec_parameters_from_context(m_vidStream->codecpar, m_vidCodec);

    // Alloc reusable YUV frame + SWS
    m_yuvFrame = av_frame_alloc();
    if (!m_yuvFrame) return LogAvErr("av_frame_alloc (yuv)", AVERROR(ENOMEM));
    m_yuvFrame->format = AV_PIX_FMT_YUV420P;
    m_yuvFrame->width  = config.width;
    m_yuvFrame->height = config.height;
    AV_CHECK(av_frame_get_buffer(m_yuvFrame, 64), "av_frame_get_buffer (yuv)");

    m_swsCtx = sws_getContext(
        config.width, config.height, AV_PIX_FMT_BGRA,
        config.width, config.height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) return LogAvErr("sws_getContext", AVERROR_UNKNOWN);

    m_initialized = true;
    Logger::Instance().Info(std::format(
        "Muxer+video ready: {}x{} @ {} fps, {} kbps, preset={}, output={}",
        config.width, config.height, config.fps, config.bitrateKbps,
        config.preset, config.outputPath));
    return true;
}

// ============================================================
// OpenVideoCodec  (was OpenCodec)
// ============================================================
bool FFmpegEncoder::OpenVideoCodec() {
    const AVCodec* codec = nullptr;
    if (!m_config.codecName.empty())
        codec = avcodec_find_encoder_by_name(m_config.codecName.c_str());
    if (!codec)
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) return LogAvErr("avcodec_find_encoder (H.264)", AVERROR_DECODER_NOT_FOUND);

    m_vidCodec = avcodec_alloc_context3(codec);
    if (!m_vidCodec) return LogAvErr("avcodec_alloc_context3", AVERROR(ENOMEM));

    m_vidCodec->width        = m_config.width;
    m_vidCodec->height       = m_config.height;
    m_vidCodec->bit_rate     = m_config.bitrateKbps * 1000LL;
    m_vidCodec->time_base    = AVRational{1, m_config.fps};
    m_vidCodec->framerate    = AVRational{m_config.fps, 1};
    m_vidCodec->gop_size     = m_config.fps * 2;
    m_vidCodec->max_b_frames = 2;
    m_vidCodec->pix_fmt      = AV_PIX_FMT_YUV420P;
    m_vidCodec->color_range  = AVCOL_RANGE_JPEG;
    m_vidCodec->color_primaries = AVCOL_PRI_BT709;
    m_vidCodec->color_trc       = AVCOL_TRC_BT709;
    m_vidCodec->colorspace      = AVCOL_SPC_BT709;

    av_opt_set(m_vidCodec->priv_data, "preset",  m_config.preset.c_str(), 0);
    av_opt_set(m_vidCodec->priv_data, "profile", "high", 0);
    av_opt_set(m_vidCodec->priv_data, "x264-params", "no-deblock=1:keyint=250", 0);
    m_vidCodec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AV_CHECK(avcodec_open2(m_vidCodec, codec, nullptr), "avcodec_open2 (video)");
    return true;
}

// ============================================================
// AddAudioStream  –  insert audio stream before first frame
// ============================================================
int FFmpegEncoder::AddAudioStream(AVCodecContext* audioCtx) {
    if (!m_fmtCtx || m_headerWritten) {
        Logger::Instance().Error("AddAudioStream: too late (header already written)");
        return -1;
    }
    AVStream* st = avformat_new_stream(m_fmtCtx, nullptr);
    if (!st) { LogAvErr("avformat_new_stream (audio)", AVERROR_UNKNOWN); return -1; }
    st->id = m_fmtCtx->nb_streams - 1;
    st->time_base = audioCtx->time_base;
    avcodec_parameters_from_context(st->codecpar, audioCtx);
    m_audioStream = st;
    m_audioTbNum = audioCtx->time_base.num;
    m_audioTbDen = audioCtx->time_base.den;
    Logger::Instance().Info(std::format("MUX: audio stream registered idx={} tb={}/{}",
        st->index, st->time_base.num, st->time_base.den));
    return st->index;
}

// ============================================================
// BeginOutput  –  write muxer header (must be called after all
//                 streams are registered, before any packet write)
// ============================================================
bool FFmpegEncoder::BeginOutput() {
    if (m_headerWritten) return true;
    std::lock_guard lock(m_muxMutex);
    return BeginOutputImpl();
}

bool FFmpegEncoder::BeginOutputImpl() {
    if (m_headerWritten) return true;
    if (!m_fmtCtx)       return false;

    Logger::Instance().Info(std::format("MUX: write header begin streams={} path={}",
        m_fmtCtx->nb_streams, m_config.outputPath));

    // Ensure parent directory exists and is writable
    std::error_code ec;
    auto parent = std::filesystem::path(m_config.outputPath).parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent)) {
        if (!std::filesystem::create_directories(parent, ec)) {
            Logger::Instance().Error(std::format("MUX: create_directories failed: {} path={}",
                ec.message(), parent.string()));
            return LogAvErr("avio_open - cannot create output directory", AVERROR(EACCES));
        }
        Logger::Instance().Info(std::format("MUX: created output dir {}", parent.string()));
    }
    if (!parent.empty() && std::filesystem::exists(parent)) {
        auto perms = std::filesystem::status(parent).permissions();
        bool writable = (perms & std::filesystem::perms::owner_write) != std::filesystem::perms::none;
        Logger::Instance().Info(std::format("MUX: output dir exists={} writable={}",
            std::filesystem::exists(parent), writable));
    }

    AV_CHECK(avio_open(&m_fmtCtx->pb, m_config.outputPath.c_str(), AVIO_FLAG_WRITE),
             "avio_open");

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "title", "ScreenRecorder", 0);
    int ret = avformat_write_header(m_fmtCtx, &opts);
    av_dict_free(&opts);
    if (ret < 0) return LogAvErr("avformat_write_header", ret);

    m_headerWritten = true;
    if (m_audioStream) {
        Logger::Instance().Info(std::format("MUX: audio stream tb after header={}/{}",
            m_audioStream->time_base.num, m_audioStream->time_base.den));
    }
    Logger::Instance().Info("MUX: write header done");
    return true;
}

// ============================================================
// EncodeFrame  (video)
// ============================================================
bool FFmpegEncoder::EncodeFrame(const void* rgbaData, int stride, bool requestKeyframe) {
    if (!m_initialized) return false;

    // Header should already be written via explicit BeginOutput call
    if (!m_headerWritten) {
        Logger::Instance().Error("MUX: header not written before video frame");
        return false;
    }

    // BGRA → YUV420P
    m_yuvFrame->pts = m_framePts;
    if (requestKeyframe) m_yuvFrame->pict_type = AV_PICTURE_TYPE_I;

    const uint8_t* srcPlane[4] = {
        static_cast<const uint8_t*>(rgbaData), nullptr, nullptr, nullptr
    };
    int srcStride[4] = { stride, 0, 0, 0 };
    sws_scale(m_swsCtx, srcPlane, srcStride, 0, m_config.height,
              m_yuvFrame->data, m_yuvFrame->linesize);

    // Send
    int ret = avcodec_send_frame(m_vidCodec, m_yuvFrame);
    if (ret < 0 && ret != AVERROR_EOF)
        return LogAvErr("avcodec_send_frame (video)", ret);
    ++m_framePts;
    ++m_stats.framesInput;

    // Drain
    AVPacket* pkt = av_packet_alloc();
    while (true) {
        ret = avcodec_receive_packet(m_vidCodec, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
        if (ret < 0) { av_packet_free(&pkt); return LogAvErr("avcodec_receive_packet (video)", ret); }
        WriteVideoPacket(pkt);
        ++m_stats.framesOutput;
    }
    av_packet_free(&pkt);
    return true;
}

// ============================================================
// WriteVideoPacket
// ============================================================
bool FFmpegEncoder::WriteVideoPacket(AVPacket* pkt) {
    pkt->stream_index = m_vidStream->index;
    av_packet_rescale_ts(pkt, m_vidCodec->time_base, m_vidStream->time_base);
    return WriteExternalPacket(pkt, pkt->stream_index);
}

// ============================================================
// WriteExternalPacket  –  for audio or other external streams
// ============================================================
bool FFmpegEncoder::WriteExternalPacket(AVPacket* pkt, int streamIndex) {
    if (m_audioPacketsLogged < 3) {
        Logger::Instance().Info(std::format(
            "MUX: WriteExternalPacket ENTER this={} pkt={} fmtCtx={} pktSize={} streamIdx={}",
            (void*)this, (void*)pkt, (void*)m_fmtCtx,
            pkt ? pkt->size : -1, streamIndex));
    }

    if (!pkt) { Logger::Instance().Error("MUX: null pkt"); return false; }
    if (!m_fmtCtx) { Logger::Instance().Error("MUX: null fmtCtx"); return false; }

    // Ensure header is written before any packet (safety net)
    if (!m_headerWritten) {
        if (!BeginOutput()) {
            Logger::Instance().Error("MUX: header not written, dropping packet");
            return false;
        }
    }

    pkt->stream_index = streamIndex;

    // Rescale audio packets from codec time_base to stream time_base
    if (m_audioStream && streamIndex == m_audioStream->index) {
        if (m_audioPacketsLogged < 3) {
            std::string log = std::format(
                "MUX: audio before rescale pts={} dts={} dur={} size={}",
                (long long)pkt->pts, (long long)pkt->dts, (long long)pkt->duration, pkt->size);
            Logger::Instance().Info(log);
        }
        AVRational audioCodecTb = {m_audioTbNum, m_audioTbDen};
        av_packet_rescale_ts(pkt, audioCodecTb, m_audioStream->time_base);
        if (m_audioPacketsLogged < 3) {
            std::string log = std::format(
                "MUX: audio after  rescale pts={} dts={} dur={}",
                (long long)pkt->pts, (long long)pkt->dts, (long long)pkt->duration);
        }
        m_audioPacketsLogged++;
    }

    // Throttle logging for non-audio or beyond-first-few audio
    bool logWrite = (m_audioPacketsLogged <= 5);

    int64_t sz = pkt->size;
    std::lock_guard lock(m_muxMutex);
    if (logWrite) Logger::Instance().Info("MUX: av_interleaved_write_frame begin");
    int ret = av_interleaved_write_frame(m_fmtCtx, pkt);
    if (logWrite) Logger::Instance().Info(std::format("MUX: av_interleaved_write_frame done ret={}", ret));
    if (ret < 0) {
        LogAvErr("av_interleaved_write_frame", ret);
        return false;
    }
    m_stats.totalBytes += sz;
    return true;
}

// ============================================================
// Finalize
// ============================================================
bool FFmpegEncoder::Finalize() {
    if (!m_initialized || m_finalized) return true;
    m_finalized = true;

    Logger::Instance().Info("Finalizing encoder ...");

    // Flush video codec
    if (m_vidCodec && m_headerWritten) {
        avcodec_send_frame(m_vidCodec, nullptr);
        AVPacket* pkt = av_packet_alloc();
        while (true) {
            int ret = avcodec_receive_packet(m_vidCodec, pkt);
            if (ret == AVERROR_EOF) break;
            if (ret < 0) break;
            WriteVideoPacket(pkt);
            ++m_stats.framesOutput;
        }
        av_packet_free(&pkt);
    }

    // Write trailer (with mutex)
    if (m_fmtCtx && m_headerWritten) {
        std::lock_guard lock(m_muxMutex);
        av_write_trailer(m_fmtCtx);
    }

    // Elapsed
    auto elapsed = std::chrono::steady_clock::now() - m_startTime;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    if (ms > 0) m_stats.actualFps = m_stats.framesInput * 1000.0 / ms;

    Logger::Instance().Info(std::format(
        "Encoder done: {} video frames, {} bytes ({:.1f} MiB), {:.1f} fps",
        m_stats.framesOutput, m_stats.totalBytes,
        m_stats.totalBytes / (1024.0 * 1024.0),
        m_stats.actualFps));

    // Cleanup
    sws_freeContext(m_swsCtx);        m_swsCtx   = nullptr;
    av_frame_free(&m_yuvFrame);
    avcodec_free_context(&m_vidCodec);
    if (m_fmtCtx) {
        avformat_free_context(m_fmtCtx);
        m_fmtCtx   = nullptr;
    }
    m_vidStream   = nullptr;
    m_initialized = false;
    return true;
}
