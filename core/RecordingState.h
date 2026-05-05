#pragma once

enum class RecordingState {
    Idle,
    Starting,
    Recording,
    Paused,
    Stopping,
    Error,
};
