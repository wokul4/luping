#pragma once
#include <windows.h>
#include <vector>

struct HotkeyEntry {
    int  id;
    UINT modifiers; // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    UINT vkey;      // VK_F1, 'R', etc.
};

class HotkeyManager {
public:
    HotkeyManager();
    ~HotkeyManager();

    bool Register(HWND hwnd, int id, UINT modifiers, UINT vkey);
    void UnregisterAll(HWND hwnd);

    static constexpr UINT MOD_CTRL_ALT = MOD_CONTROL | MOD_ALT;
    static constexpr int  ID_START_STOP  = 1;
    static constexpr int  ID_PAUSE_RESUME = 2;

private:
    struct Entry { int id; UINT mod; UINT vkey; };
    std::vector<Entry> m_entries;
};
