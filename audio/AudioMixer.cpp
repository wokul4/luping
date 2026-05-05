#include "AudioMixer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

/* static */
size_t AudioMixer::Mix(float*       output, size_t maxOut,
                        const float* input1, size_t samples1, float vol1,
                        const float* input2, size_t samples2, float vol2) {
    // If one input has no data, use the other alone (fix: do not min to 0)
    if (samples1 == 0 && samples2 == 0) {
        if (maxOut > 0) std::memset(output, 0, maxOut * sizeof(float));
        return 0;
    }
    if (samples1 == 0) {
        size_t n = std::min(samples2, maxOut);
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(input2[i] * vol2, -1.0f, 1.0f);
        return n;
    }
    if (samples2 == 0) {
        size_t n = std::min(samples1, maxOut);
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(input1[i] * vol1, -1.0f, 1.0f);
        return n;
    }

    size_t n = std::min({samples1, samples2, maxOut});

    if (vol1 == 0.0f && vol2 == 0.0f) {
        std::memset(output, 0, n * sizeof(float));
    } else if (vol1 == 0.0f) {
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(input2[i] * vol2, -1.0f, 1.0f);
    } else if (vol2 == 0.0f) {
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(input1[i] * vol1, -1.0f, 1.0f);
    } else {
        for (size_t i = 0; i < n; ++i)
            output[i] = std::clamp(input1[i] * vol1 + input2[i] * vol2,
                                   -1.0f, 1.0f);
    }
    return n;
}
