#include "AudioEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include "../platform/Logger.h"
#include <format>
#include <cstring>
#include <cassert>

// ============================================================
// Ctor / Dtor
// ============================================================
AudioEncoder::AudioEncoder()  = default;

AudioEncoder::~AudioEncoder() {
    Flush();
    while (auto* pkt = TakePacket())
        av_packet_free(&pkt);
    av_frame_free(&m_inFrame);
    av_frame_free(&m_fltpFrame);
    swr_free(&m_swrCtx);
    avcodec_free_context(&m_codecCtx);
}

// ============================================================
// Initialize
// ============================================================
bool AudioEncoder::Initialize(int sampleRate, int channels, int bitrate) {
    m_sampleRate = sampleRate;
    m_channels   = channels;
    m_ptsAcc     = 0;

    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) codec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!codec) {
        Logger::Instance().Error("AudioEncoder: AAC codec not found");
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) return false;

    m_codecCtx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    m_codecCtx->sample_rate = sampleRate;
    m_codecCtx->bit_rate    = bitrate;
    av_channel_layout_default(&m_codecCtx->ch_layout, channels);

    m_codecCtx->time_base = AVRational{1, sampleRate};
    m_codecCtx->flags    |= AV_CODEC_FLAG_GLOBAL_HEADER;

    if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) {
        Logger::Instance().Error("AudioEncoder: avcodec_open2 failed");
        return false;
    }
    m_frameSize = m_codecCtx->frame_size; // typically 1024

    // ---- swresample: FLT → FLTP ----
    AVChannelLayout in_layout;
    av_channel_layout_default(&in_layout, channels);

    // swr_alloc_set_opts2 is the modern API (FFmpeg 7.x)
    swr_free(&m_swrCtx);
    int swrRet = swr_alloc_set_opts2(
        &m_swrCtx,
        &m_codecCtx->ch_layout, AV_SAMPLE_FMT_FLTP, sampleRate,   // out
        &in_layout,             AV_SAMPLE_FMT_FLT,  sampleRate,   // in
        0, nullptr);
    if (swrRet < 0 || !m_swrCtx || swr_init(m_swrCtx) < 0) {
        Logger::Instance().Error("AudioEncoder: swr_init failed");
        return false;
    }

    // ---- reusable input frame (FLT interleaved) ----
    m_inFrame = av_frame_alloc();
    if (!m_inFrame) return false;
    m_inFrame->format      = AV_SAMPLE_FMT_FLT;
    m_inFrame->sample_rate = sampleRate;
    av_channel_layout_copy(&m_inFrame->ch_layout, &m_codecCtx->ch_layout);

    // ---- reusable FLTP frame ----
    m_fltpFrame = av_frame_alloc();
    if (!m_fltpFrame) return false;
    m_fltpFrame->format      = AV_SAMPLE_FMT_FLTP;
    m_fltpFrame->sample_rate = sampleRate;
    av_channel_layout_copy(&m_fltpFrame->ch_layout, &m_codecCtx->ch_layout);

    Logger::Instance().Info(std::format(
        "AudioEncoder(AAC) ready: {} ch, {} Hz, {} bps, frame={} smpl",
        channels, sampleRate, bitrate, m_frameSize));
    return true;
}

// ============================================================
// Encode
// ============================================================
bool AudioEncoder::Encode(const float* pcm, size_t samples, int64_t pts) {
    if (!m_codecCtx) return false;

    size_t consumed = 0;
    while (consumed < samples) {
        int chSamples = m_frameSize;
        size_t remaining = (samples - consumed) / m_channels;
        if (remaining < (size_t)m_frameSize) {
            chSamples = (int)remaining;
            if (chSamples == 0) break;
        }

        // Fill input FLT frame
        av_frame_unref(m_inFrame);
        m_inFrame->nb_samples = chSamples;
        if (av_frame_get_buffer(m_inFrame, 0) < 0) return false;

        float* inData = reinterpret_cast<float*>(m_inFrame->data[0]);
        memcpy(inData, pcm + consumed, (size_t)chSamples * m_channels * sizeof(float));

        // Convert FLT → FLTP
        av_frame_unref(m_fltpFrame);
        m_fltpFrame->nb_samples = chSamples;
        if (av_frame_get_buffer(m_fltpFrame, 0) < 0) return false;

        int ret = swr_convert(m_swrCtx,
                              m_fltpFrame->data, chSamples,
                              (const uint8_t**)&m_inFrame->data, chSamples);
        if (ret < 0) return false;

        m_fltpFrame->pts = m_ptsAcc;

        if (!SendFrame(m_fltpFrame)) return false;

        consumed += (size_t)chSamples * m_channels;
        m_ptsAcc += chSamples;

        if (chSamples != m_frameSize) break;
    }
    return true;
}

// ============================================================
// Flush
// ============================================================
bool AudioEncoder::Flush() {
    if (!m_codecCtx) return true;
    return SendFrame(nullptr);
}

// ============================================================
// SendFrame / ReceivePackets
// ============================================================
bool AudioEncoder::SendFrame(AVFrame* frame) {
    int ret = avcodec_send_frame(m_codecCtx, frame);
    if (ret < 0 && ret != AVERROR_EOF) {
        Logger::Instance().Error(std::format("AAC send_frame error {}", ret));
        return false;
    }
    ReceivePackets();
    return true;
}

void AudioEncoder::ReceivePackets() {
    while (true) {
        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_receive_packet(m_codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        }
        if (ret < 0) {
            av_packet_free(&pkt);
            Logger::Instance().Error(std::format("AAC receive_packet error {}", ret));
            break;
        }
        int next = (m_qHead + 1) % kMaxQ;
        if (next != m_qTail) {
            m_pktQ[m_qHead] = pkt;
            m_qHead = next;
        } else {
            av_packet_free(&pkt);
            assert(!"AAC pkt queue overflow");
        }
    }
}

// ============================================================
// TakePacket
// ============================================================
AVPacket* AudioEncoder::TakePacket() {
    if (m_qTail == m_qHead) return nullptr;
    AVPacket* pkt = m_pktQ[m_qTail];
    m_pktQ[m_qTail] = nullptr;
    m_qTail = (m_qTail + 1) % kMaxQ;
    return pkt;
}
