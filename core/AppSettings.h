#pragma once
#include <string>
#include <filesystem>

// AppSettings manages a flat JSON config file stored at exeDir/config/settings.json.
// If the file is missing or corrupt, defaults are created.
struct AppSettings {
    std::string  outputDir           = "captures";
    std::string  container           = "mkv";
    int          fps                 = 30;
    int          bitrateKbps         = 10000;
    bool         recordSystemAudio   = true;
    bool         recordMicrophone    = true;
    std::string  captureMode         = "Monitor";
    int          monitorIndex        = 0;
    std::string  lastWindowProcess   = "";
    bool         minimizeToTray      = false;
    bool         showCompletionPrompt = true;
    std::string  savedOutputDir      = "";   // user-chosen output dir override

    // Load from exeDir/config/settings.json; creates default if missing.
    // Returns true on success (or default created), false if fatal.
    bool Load(const std::filesystem::path& exeDir);

    // Save current settings to exeDir/config/settings.json.
    bool Save(const std::filesystem::path& exeDir) const;

    // Short helper: exeDir/LoadOrCreate → Save if created.
    bool LoadOrCreate(const std::filesystem::path& exeDir);
};
