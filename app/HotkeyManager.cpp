#include "HotkeyManager.h"

HotkeyManager::HotkeyManager()  = default;
HotkeyManager::~HotkeyManager() = default;

bool HotkeyManager::Register(HWND hwnd, int id, UINT modifiers, UINT vkey) {
    if (!RegisterHotKey(hwnd, id, modifiers, vkey))
        return false;
    m_entries.push_back({id, modifiers, vkey});
    return true;
}

void HotkeyManager::UnregisterAll(HWND hwnd) {
    for (auto& e : m_entries)
        UnregisterHotKey(hwnd, e.id);
    m_entries.clear();
}
