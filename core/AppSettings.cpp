#include "AppSettings.h"
#include "../platform/Logger.h"
#include <fstream>
#include <sstream>
#include <format>
#include <cctype>

// ============================================================
// Minimal JSON reader/writer for flat string→string maps.
// Only handles the schema in AppSettings — not a general parser.
// ============================================================

static std::string Trim(std::string_view s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.remove_prefix(1);
    while (!s.empty() && std::isspace((unsigned char)s.back()))  s.remove_suffix(1);
    return std::string(s);
}

// Find the value for a top-level key in a JSON object.
// Handles strings, numbers (→string), booleans (→"true"/"false").
static std::string JsonExtract(std::string_view json, std::string_view key) {
    auto pos = json.find(std::format("\"{}\"", key));
    if (pos == std::string_view::npos) return {};
    pos = json.find(':', pos + key.size() + 2);
    if (pos == std::string_view::npos) return {};
    pos++; // skip ':'
    while (pos < json.size() && std::isspace((unsigned char)json[pos])) pos++;
    if (pos >= json.size()) return {};

    if (json[pos] == '"') {
        // Quoted string
        pos++; // skip opening quote
        std::string val;
        bool escaped = false;
        for (; pos < json.size(); pos++) {
            if (escaped) { val += json[pos]; escaped = false; continue; }
            if (json[pos] == '\\') { escaped = true; continue; }
            if (json[pos] == '"') break;
            val += json[pos];
        }
        return val;
    }

    if (json.substr(pos, 4) == "true")  return "true";
    if (json.substr(pos, 5) == "false") return "false";

    // Number: read until , } whitespace
    std::string num;
    for (; pos < json.size(); pos++) {
        if (json[pos] == ',' || json[pos] == '}' || std::isspace((unsigned char)json[pos])) break;
        if (json[pos] == '-' || json[pos] == '.' || std::isdigit((unsigned char)json[pos]))
            num += json[pos];
    }
    return num;
}

// Minimal JSON serialization for our schema.
static std::string JsonSerialize(const std::initializer_list<std::pair<std::string, std::string>>& fields) {
    std::string out = "{\n";
    bool first = true;
    for (auto& [k, v] : fields) {
        if (!first) out += ",\n";
        first = false;
        out += std::format("  \"{}\": \"{}\"", k, v);
    }
    out += "\n}\n";
    return out;
}

// ============================================================
// AppSettings implementation
// ============================================================

bool AppSettings::Load(const std::filesystem::path& exeDir) {
    auto path = exeDir / "config" / "settings.json";
    Logger::Instance().Info(std::format("SETTINGS: loading from {}", path.string()));

    std::ifstream f(path);
    if (!f.is_open()) {
        Logger::Instance().Info("SETTINGS: file not found, will create default");
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string json = ss.str();
    f.close();

    auto r = [&](std::string_view key, auto& out, auto conv) {
        auto v = JsonExtract(json, key);
        if (!v.empty()) out = conv(v);
    };

    r("outputDir",           outputDir,           [](std::string_view s) { return std::string(s); });
    r("container",           container,           [](std::string_view s) { return std::string(s); });
    r("fps",                fps,                  [](std::string_view s) { return std::stoi(std::string(s)); });
    r("bitrateKbps",        bitrateKbps,           [](std::string_view s) { return std::stoi(std::string(s)); });
    r("recordSystemAudio",  recordSystemAudio,     [](std::string_view s) { return s == "true"; });
    r("recordMicrophone",   recordMicrophone,      [](std::string_view s) { return s == "true"; });
    r("captureMode",        captureMode,           [](std::string_view s) { return std::string(s); });
    r("monitorIndex",       monitorIndex,          [](std::string_view s) { return std::stoi(std::string(s)); });
    r("lastWindowProcess",  lastWindowProcess,     [](std::string_view s) { return std::string(s); });
    r("minimizeToTray",     minimizeToTray,        [](std::string_view s) { return s == "true"; });
    r("showCompletionPrompt", showCompletionPrompt, [](std::string_view s) { return s == "true"; });
    r("savedOutputDir",     savedOutputDir,        [](std::string_view s) { return std::string(s); });

    Logger::Instance().Info(std::format("SETTINGS: loaded (fps={}, br={}, mode={}, audio={}/{})",
        fps, bitrateKbps, captureMode, (int)recordSystemAudio, (int)recordMicrophone));
    return true;
}

bool AppSettings::Save(const std::filesystem::path& exeDir) const {
    auto dir = exeDir / "config";
    std::filesystem::create_directories(dir);
    auto path = dir / "settings.json";

    // Atomic write: write to .tmp then rename
    auto tmpPath = dir / "settings.json.tmp";
    {
        std::ofstream f(tmpPath);
        if (!f.is_open()) {
            Logger::Instance().Error(std::format("SETTINGS: cannot write {}", tmpPath.string()));
            return false;
        }
        f << JsonSerialize({
            {"outputDir", outputDir},
            {"container", container},
            {"fps", std::to_string(fps)},
            {"bitrateKbps", std::to_string(bitrateKbps)},
            {"recordSystemAudio", recordSystemAudio ? "true" : "false"},
            {"recordMicrophone", recordMicrophone ? "true" : "false"},
            {"captureMode", captureMode},
            {"monitorIndex", std::to_string(monitorIndex)},
            {"lastWindowProcess", lastWindowProcess},
            {"minimizeToTray", minimizeToTray ? "true" : "false"},
            {"showCompletionPrompt", showCompletionPrompt ? "true" : "false"},
            {"savedOutputDir", savedOutputDir},
        });
    }
    std::error_code ec;
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        Logger::Instance().Error(std::format("SETTINGS: rename failed: {}", ec.message()));
        return false;
    }
    Logger::Instance().Info("SETTINGS: saved");
    return true;
}

bool AppSettings::LoadOrCreate(const std::filesystem::path& exeDir) {
    if (Load(exeDir)) return true;

    auto path = exeDir / "config" / "settings.json";
    auto badPath = exeDir / "config" / "settings.bad.json";

    // If file exists but failed to parse, back it up
    if (std::filesystem::exists(path)) {
        std::error_code ec;
        std::filesystem::rename(path, badPath, ec);
        if (!ec)
            Logger::Instance().Warning(std::format("SETTINGS: corrupt file backed up to {}", badPath.string()));
        else
            Logger::Instance().Warning("SETTINGS: corrupt file, overwriting");
    }

    // Create default
    *this = AppSettings{};
    if (!Save(exeDir)) {
        Logger::Instance().Error("SETTINGS: failed to create default");
        return false;
    }
    Logger::Instance().Info("SETTINGS: default config created");
    return true;
}
