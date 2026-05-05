#pragma once
#include <cstddef>

/// Simple PCM mixer: adds two interleaved float stereo streams with per-stream gain,
/// hard-clamped to [-1, 1].
class AudioMixer {
public:
    /// Mix @p samples1 floats from @p input1 with @p samples2 floats from @p input2.
    /// All streams are stereo-interleaved (L R L R ...).
    /// @return number of float samples written to @p output (min of the two inputs).
    static size_t Mix(float*       output, size_t maxOut,
                      const float* input1, size_t samples1, float vol1,
                      const float* input2, size_t samples2, float vol2);
};
