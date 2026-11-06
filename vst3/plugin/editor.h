#pragma once

#include "public.sdk/source/common/pluginview.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace RebirthBridge {

// UI-thread-only status view. It never claims a worker lease or drives audio.
class StatusView final : public Steinberg::CPluginView {
public:
    explicit StatusView(std::shared_ptr<std::atomic<std::int32_t>> frontendStatus = {});
    ~StatusView() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* size) override;
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* size) override;

private:
    static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void releaseWindow();
    void releaseMapping();
    void updateStatus();
    void paint();

    HWND window_ = nullptr;
    HANDLE mapping_ = nullptr;
    const void* shared_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT bodyFont_ = nullptr;
    std::shared_ptr<std::atomic<std::int32_t>> frontendStatus_;
    std::wstring status_;
    std::wstring details_;
    COLORREF statusColor_ = RGB(235, 190, 100);
};

} // namespace RebirthBridge
