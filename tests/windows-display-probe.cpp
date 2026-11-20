#include "../src/display_observation_win32.h"
#include <cstdio>

struct Probe {
    unsigned checked = 0;
    unsigned failed = 0;
};

static BOOL CALLBACK ObserveMonitor(HMONITOR monitor, HDC, LPRECT bounds, LPARAM context) {
    auto& probe = *reinterpret_cast<Probe*>(context);
    ++probe.checked;
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", nullptr, WS_POPUP,
        bounds->left + 1, bounds->top + 1, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window) {
        ++probe.failed;
        std::printf("display=%u create_window_failed=%lu\n", probe.checked, GetLastError());
        return TRUE;
    }
    const auto observedMonitor = MonitorFromWindow(window, MONITOR_DEFAULTTONULL);
    const auto reading = highfps::refresh::ReadDisplay(window);
    DestroyWindow(window);
    const bool real = reading.source == highfps::refresh::DisplaySource::WindowsPath ||
                      reading.source == highfps::refresh::DisplaySource::WindowsMode;
    const bool matched = observedMonitor == monitor;
    std::printf("display=%u matched=%d source=%s numerator=%u denominator=%u hz=%.6f\n",
        probe.checked, matched, highfps::refresh::Name(reading.source), reading.display.mode.numerator,
        reading.display.mode.denominator, reading.display.mode.Hz());
    if (!matched || !real || !reading.display.mode.Valid()) ++probe.failed;
    return TRUE;
}

int main() {
    if (highfps::refresh::IsWine()) {
        std::puts("Run this probe in an interactive native Windows desktop session, not Wine.");
        return 2;
    }
    Probe probe;
    if (!EnumDisplayMonitors(nullptr, nullptr, &ObserveMonitor, reinterpret_cast<LPARAM>(&probe))) return 1;
    return !probe.checked || probe.failed ? 1 : 0;
}
