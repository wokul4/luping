#pragma once
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct AVPacket;

/// AAC encoder wrapping FFmpeg's native AAC encoder.
/// Input:  interleaved float PCM  (AV_SAMPLE_FMT_FLT)
/// Output: AAC packets via TakePacket()
class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    bool Initialize(int sampleRate, int channels, int bitrate);
    bool Encode(const float* pcm, size_t samples, int64_t pts);
    bool Flush();

    /// Get the next encoded AVPacket. Caller must av_packet_free().
    AVPacket* TakePacket();

    AVCodecContext* CodecCtx() const { return m_codecCtx; }
    int FrameSize() const { return m_frameSize; }
    int SubmittedFrames() const { return m_submittedFrames; }
    int QueuedPackets()   const { return m_queuedPackets; }

private:
    void FlushPackets();

    AVCodecContext* m_codecCtx   = nullptr;
    AVFrame*        m_audioFrame = nullptr;  // FLTP planar (reused)

    int  m_sampleRate = 0;
    int  m_channels   = 0;
    int  m_frameSize  = 0;
    int64_t m_ptsAcc = 0;

    // Stats
    int  m_submittedFrames = 0;
    int  m_queuedPackets   = 0;
    bool m_flushed         = false;

    // Packet queue
    static constexpr int kMaxQ = 64;
    AVPacket* m_pktQ[kMaxQ] = {};
    int m_qHead = 0;
    int m_qTail = 0;
};
