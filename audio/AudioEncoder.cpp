#include "AudioEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
}

#include "../platform/Logger.h"
#include <format>
#include <cstring>

static std::string AvStrError(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_make_error_string(buf, sizeof(buf), err);
    return buf;
}

AudioEncoder::AudioEncoder() = default;

AudioEncoder::~AudioEncoder() {
    Flush();
    while (auto* pkt = TakePacket()) av_packet_free(&pkt);
    av_frame_free(&m_audioFrame);
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
    if (!codec) { Logger::Instance().Error("AENC: AAC codec not found"); return false; }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) return false;

    m_codecCtx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    m_codecCtx->sample_rate = sampleRate;
    m_codecCtx->bit_rate    = bitrate;
    av_channel_layout_default(&m_codecCtx->ch_layout, channels);
    m_codecCtx->time_base = AVRational{1, sampleRate};
    m_codecCtx->flags    |= AV_CODEC_FLAG_GLOBAL_HEADER;
    m_codecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    int ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        Logger::Instance().Error(std::format("AENC: avcodec_open2 failed: {}", AvStrError(ret)));
        return false;
    }

    m_frameSize = m_codecCtx->frame_size; // typically 1024
    Logger::Instance().Info(std::format(
        "AENC: ready fmt={} sr={} ch={} frameSize={} tb=1/{}",
        av_get_sample_fmt_name(m_codecCtx->sample_fmt),
        m_codecCtx->sample_rate, m_codecCtx->ch_layout.nb_channels,
        m_frameSize, m_codecCtx->time_base.den));

    // Single reusable FLTP frame (no swr, manual packed→planar)
    m_audioFrame = av_frame_alloc();
    if (!m_audioFrame) return false;
    // Fields set per-call in Encode

    return true;
}

// ============================================================
// Encode — packed stereo float → manual FLTP → AAC
// Input:  L0 R0 L1 R1 ... (interleaved stereo float)
// Output: AAC packets queued internally
// ============================================================
bool AudioEncoder::Encode(const float* pcm, size_t samples, int64_t pts) {
    if (!m_codecCtx) return false;
    (void)pts; // internal PTS used instead

    size_t consumed = 0;
    while (consumed < samples) {
        int chSamples = m_frameSize;
        size_t remaining = (samples - consumed) / m_channels;
        if (remaining < (size_t)m_frameSize) {
            chSamples = (int)remaining;
            if (chSamples == 0) break;
        }

        // ---- Prepare FLTP frame ----
        av_frame_unref(m_audioFrame);
        m_audioFrame->nb_samples  = chSamples;
        m_audioFrame->format      = AV_SAMPLE_FMT_FLTP;
        m_audioFrame->sample_rate = m_sampleRate;
        av_channel_layout_copy(&m_audioFrame->ch_layout, &m_codecCtx->ch_layout);

        int ret = av_frame_get_buffer(m_audioFrame, 0);
        if (ret < 0) {
            Logger::Instance().Error(std::format(
                "AENC: get_buffer failed nb={} fmt={} sr={} ch={} err={}",
                chSamples, (int)AV_SAMPLE_FMT_FLTP, m_sampleRate,
                m_audioFrame->ch_layout.nb_channels, AvStrError(ret)));
            return false;
        }
        av_frame_make_writable(m_audioFrame);

        // ---- Manual packed → planar conversion ----
        // m_audioFrame->data[0] = left channel (chSamples floats)
        // m_audioFrame->data[1] = right channel (chSamples floats)
        float* left  = reinterpret_cast<float*>(m_audioFrame->data[0]);
        float* right = reinterpret_cast<float*>(m_audioFrame->data[1]);
        for (int i = 0; i < chSamples; ++i) {
            left[i]  = pcm[consumed / m_channels + i * 2];
            right[i] = pcm[consumed / m_channels + i * 2 + 1];
        }

        m_audioFrame->pts = m_ptsAcc;

        // ---- Send to AAC encoder ----
        if (m_submittedFrames < 3)
            Logger::Instance().Info(std::format("AENC: send_frame begin idx={}", m_submittedFrames + 1));
        ret = avcodec_send_frame(m_codecCtx, m_audioFrame);
        if (ret < 0) {
            Logger::Instance().Error(std::format("AENC: send_frame failed: {}", AvStrError(ret)));
            return false;
        }
        m_submittedFrames++;

        // ---- Receive packets ----
        if (m_submittedFrames < 4)
            Logger::Instance().Info(std::format("AENC: receive loop begin idx={}", m_submittedFrames));
        FlushPackets();
        if (m_submittedFrames < 4)
            Logger::Instance().Info(std::format("AENC: receive loop end idx={}", m_submittedFrames));

        consumed += (size_t)chSamples * m_channels;
        m_ptsAcc += chSamples;

        if (chSamples != m_frameSize) break;
    }
    return true;
}

// ============================================================
// FlushPackets
// ============================================================
void AudioEncoder::FlushPackets() {
    while (true) {
        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_receive_packet(m_codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            break;
        }
        if (ret < 0) {
            Logger::Instance().Error(std::format("AENC: receive_packet failed: {}", AvStrError(ret)));
            av_packet_free(&pkt);
            break;
        }
        if (m_queuedPackets < 3) {
            Logger::Instance().Info(std::format("AENC: packet received pts={} dts={} size={} dur={}",
                (int64_t)pkt->pts, (int64_t)pkt->dts, pkt->size, (int64_t)pkt->duration));
        }
        int next = (m_qHead + 1) % kMaxQ;
        if (next != m_qTail) {
            m_pktQ[m_qHead] = pkt;
            m_qHead = next;
            m_queuedPackets++;
        } else {
            Logger::Instance().Error("AENC: packet queue overflow");
            av_packet_free(&pkt);
        }
    }
}

// ============================================================
// Flush
// ============================================================
bool AudioEncoder::Flush() {
    if (!m_codecCtx) return true;
    if (m_flushed) { Logger::Instance().Info("AENC: flush already done"); return true; }
    m_flushed = true;
    Logger::Instance().Info("AENC: flushing...");
    int ret = avcodec_send_frame(m_codecCtx, nullptr);
    if (ret == AVERROR_EOF) {
        Logger::Instance().Info("AENC: flush already EOF");
        FlushPackets(); return true;
    }
    if (ret < 0) {
        Logger::Instance().Error(std::format("AENC: flush send_frame failed: {}", AvStrError(ret)));
        return false;
    }
    FlushPackets();
    Logger::Instance().Info(std::format("AENC: flush done submitted={} queued={}",
        m_submittedFrames, m_queuedPackets));
    return true;
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
