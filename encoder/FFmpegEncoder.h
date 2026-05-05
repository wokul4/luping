#pragma once
#include "IVideoEncoder.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVCodecParameters;
struct AVStream;
struct SwsContext;
struct AVFrame;
struct AVPacket;

class FFmpegEncoder final : public IVideoEncoder {
public:
    FFmpegEncoder();
    ~FFmpegEncoder() override;

    FFmpegEncoder(const FFmpegEncoder&)            = delete;
    FFmpegEncoder& operator=(const FFmpegEncoder&) = delete;

    // ---- IVideoEncoder ----
    bool Initialize(const EncoderConfig& config) override;
    bool EncodeFrame(const void* rgbaData, int stride,
                     bool requestKeyframe = false) override;
    bool Finalize() override;
    EncoderStats GetStats() const override { return m_stats; }
    bool IsInitialized() const override { return m_initialized; }

    // ---- Audio stream support ----
    /// Add an audio stream from an already-initialised AVCodecContext.
    /// Must be called before the first EncodeFrame().
    /// @return stream index, or -1 on failure.
    int AddAudioStream(AVCodecContext* audioCodecCtx);

    /// Write a pre-encoded packet (e.g. audio) to the output.
    bool WriteExternalPacket(AVPacket* pkt, int streamIndex);

    // ---- Low-level access (for external encoders) ----
    AVFormatContext* FormatCtx() { return m_fmtCtx; }

private:
    bool BeginOutput();       // lazy: open file + write header once
    bool OpenVideoCodec();
    bool WriteVideoPacket(AVPacket* pkt);

    EncoderConfig  m_config;
    EncoderStats   m_stats;
    bool           m_initialized   = false;
    bool           m_headerWritten = false;
    bool           m_finalized     = false;

    // FFmpeg objects
    AVFormatContext* m_fmtCtx    = nullptr;
    AVCodecContext*  m_vidCodec  = nullptr;
    AVStream*        m_vidStream = nullptr;
    SwsContext*      m_swsCtx    = nullptr;

    AVFrame*         m_yuvFrame  = nullptr;
    int64_t          m_framePts  = 0;

    std::chrono::steady_clock::time_point m_startTime;
};
