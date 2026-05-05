#pragma once
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVStream;
struct AVFrame;
struct SwrContext;
struct AVPacket;

/// AAC encoder wrapping FFmpeg's native AAC encoder.
/// Input:  interleaved float PCM  (AV_SAMPLE_FMT_FLT)
/// Output: AAC packets             (AV_SAMPLE_FMT_FLTP internally)
class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    AudioEncoder(const AudioEncoder&) = delete;
    AudioEncoder& operator=(const AudioEncoder&) = delete;

    /// @param sampleRate  e.g. 48000
    /// @param channels    e.g. 2
    /// @param bitrate     e.g. 128000
    bool Initialize(int sampleRate, int channels, int bitrate);

    /// Encode one block of interleaved float PCM.
    /// @param pcm     input samples (interleaved L R L R ...)
    /// @param samples number of float samples (not frames)
    /// @param pts     presentation timestamp in AV_TIME_BASE units (microseconds)
    /// @return true on success
    bool Encode(const float* pcm, size_t samples, int64_t pts);

    /// Flush any buffered frames.  Call after all input has been sent.
    /// @return true on success
    bool Flush();

    /// Get the next encoded AVPacket.  Call repeatedly after Encode/Flush until nullptr.
    /// The caller takes ownership and must av_packet_free().
    AVPacket* TakePacket();

    // -- Muxer integration --
    AVCodecContext* CodecCtx() const { return m_codecCtx; }

    int FrameSize() const { return m_frameSize; }  // AAC frames per encode call

private:
    bool SendFrame(AVFrame* frame);
    void ReceivePackets();

    AVCodecContext* m_codecCtx  = nullptr;
    SwrContext*     m_swrCtx    = nullptr;
    AVFrame*        m_inFrame   = nullptr;   // FLT  (interleaved, reused)
    AVFrame*        m_fltpFrame = nullptr;   // FLTP (planar,  reused)

    int  m_sampleRate = 0;
    int  m_channels   = 0;
    int  m_frameSize  = 0;          // samples per channel per encode call
    int64_t m_ptsAcc = 0;          // running PTS in sample-rate time_base

    // Packet queue (encoded results)
    static constexpr int kMaxQ = 64;
    AVPacket* m_pktQ[kMaxQ] = {};
    int       m_qHead = 0;
    int       m_qTail = 0;
};
