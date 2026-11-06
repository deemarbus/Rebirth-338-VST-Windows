#include "editor.h"
#include "../shared/bridge_ipc.h"
#include "pluginterfaces/gui/iplugview.h"

#include <commctrl.h>
#include <cstring>

namespace RebirthBridge {
namespace {
constexpr LONG viewWidth = 680;
constexpr LONG viewHeight = 330;
constexpr UINT_PTR subclassId = 0x338;
constexpr UINT_PTR statusTimer = 0x339;
constexpr COLORREF background = RGB(28, 31, 34);
constexpr COLORREF text = RGB(226, 231, 236);
constexpr COLORREF muted = RGB(172, 185, 195);
constexpr COLORREF warning = RGB(245, 192, 105);
constexpr COLORREF good = RGB(115, 221, 164);

// A FILE_MAP_READ view must never use rb_load: InterlockedCompareExchange
// would attempt a write. Aligned 32-bit volatile reads are atomic on x64;
// MemoryBarrier supplies ordering after the published magic/status read.
std::uint32_t read32(const volatile RBAtomic32& field) {
    const auto value = static_cast<std::uint32_t>(field);
    MemoryBarrier();
    return value;
}

bool validHeader(const RBShared* s) {
    return read32(s->magic) == RB_IPC_MAGIC &&
        s->version == RB_IPC_VERSION && s->bytes == sizeof(RBShared) &&
        s->frames == RB_BLOCK_FRAMES && s->sample_rate == RB_SAMPLE_RATE &&
        s->tempo_milli == RB_DEFAULT_TEMPO_MILLI && s->latency == RB_LATENCY;
}

const wchar_t* frontendText(std::uint32_t state) {
    switch (state) {
    case 3: return L"Live levert geen bruikbare transportinformatie. Stop en start opnieuw vanaf maat 1.";
    case 4: return L"Stel Live in op een tempo tussen 20 en 500 BPM.";
    case 5: return L"Spoelen of starten halverwege wordt nog niet ondersteund. Stop en start vanaf maat 1.";
    case 6: return L"Zet Loop in Live uit. Herhalen wordt in deze proef nog niet ondersteund.";
    case 7: return L"Audio kwam te laat aan. Er zijn ontbrekende samples; stop en probeer opnieuw.";
    case 8: return L"De koppeling kreeg ongeldige gegevens. Stop en herstart de brug na normaal afsluiten.";
    case 9: return L"Live-samplefrequentie niet ondersteund. Gebruik 44,1, 48, 88,2, 96, 176,4 of 192 kHz.";
    case 10: return L"Offline export werkt nog niet. Neem de uitvoer in realtime op een audiospoor op.";
    case 11: return L"Start eerst de aparte ReBirth-brug en laad daarna deze plug-in opnieuw.";
    case 12: return L"Een andere plug-ininstantie gebruikt ReBirth. Deze proef ondersteunt maar een instantie.";
    case 13: return L"De ReBirth-brug is gestopt of heeft een fout gemeld. Zie het consolevenster.";
    case 14: return L"Wachten op maat 1. Start het afspelen in Live vanaf het begin.";
    default: return nullptr;
    }
}

void draw(HDC dc, HFONT font, COLORREF color, const wchar_t* value, RECT area,
          UINT flags = DT_LEFT | DT_WORDBREAK | DT_NOPREFIX) {
    HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
    SetTextColor(dc, color);
    DrawTextW(dc, value, -1, &area, flags);
    SelectObject(dc, previous);
}
} // namespace

StatusView::StatusView(std::shared_ptr<std::atomic<std::int32_t>> frontendStatus)
    : frontendStatus_(std::move(frontendStatus)),
      status_(L"Start eerst de aparte ReBirth-brug."),
      details_(L"Nog geen verbinding met de 32-bits engine.") {
    rect = Steinberg::ViewRect(0, 0, viewWidth, viewHeight);
}

StatusView::~StatusView() {
    releaseWindow();
    releaseMapping();
    if (titleFont_) DeleteObject(titleFont_);
    if (bodyFont_) DeleteObject(bodyFont_);
}

Steinberg::tresult PLUGIN_API StatusView::isPlatformTypeSupported(Steinberg::FIDString type) {
    return type && std::strcmp(type, Steinberg::kPlatformTypeHWND) == 0
        ? Steinberg::kResultTrue : Steinberg::kResultFalse;
}

Steinberg::tresult PLUGIN_API StatusView::attached(void* parent, Steinberg::FIDString type) {
    if (!parent || window_ || isPlatformTypeSupported(type) != Steinberg::kResultTrue)
        return Steinberg::kInvalidArgument;
    auto parentWindow = static_cast<HWND>(parent);
    if (!IsWindow(parentWindow)) return Steinberg::kInvalidArgument;

    // Subclass our own built-in child window. No window class containing a
    // plug-in code pointer remains registered when the module is unloaded.
    window_ = CreateWindowExW(0, L"STATIC", L"ReBirth Bridge", WS_CHILD | WS_VISIBLE,
        0, 0, viewWidth, viewHeight, parentWindow, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!window_) return Steinberg::kResultFalse;
    if (!SetWindowSubclass(window_, windowProc, subclassId, reinterpret_cast<DWORD_PTR>(this))) {
        DestroyWindow(window_);
        window_ = nullptr;
        return Steinberg::kResultFalse;
    }
    if (!titleFont_)
        titleFont_ = CreateFontW(-23, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!bodyFont_)
        bodyFont_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (!SetTimer(window_, statusTimer, 250, nullptr)) {
        releaseWindow();
        return Steinberg::kResultFalse;
    }
    systemWindow = parent;
    updateStatus();
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API StatusView::removed() {
    releaseWindow();
    releaseMapping();
    systemWindow = nullptr;
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API StatusView::onSize(Steinberg::ViewRect* size) {
    if (!size || size->getWidth() != viewWidth || size->getHeight() != viewHeight)
        return Steinberg::kInvalidArgument;
    rect = *size;
    if (window_) SetWindowPos(window_, nullptr, 0, 0, viewWidth, viewHeight, SWP_NOZORDER);
    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API StatusView::checkSizeConstraint(Steinberg::ViewRect* size) {
    if (!size) return Steinberg::kInvalidArgument;
    size->right = size->left + viewWidth;
    size->bottom = size->top + viewHeight;
    return Steinberg::kResultTrue;
}

void StatusView::releaseWindow() {
    if (window_) {
        KillTimer(window_, statusTimer);
        DestroyWindow(window_); // WM_NCDESTROY removes subclass and clears window_.
        window_ = nullptr;
    }
}

void StatusView::releaseMapping() {
    if (shared_) UnmapViewOfFile(shared_);
    if (mapping_) CloseHandle(mapping_);
    shared_ = nullptr;
    mapping_ = nullptr;
}

void StatusView::updateStatus() {
    if (!shared_) {
        mapping_ = OpenFileMappingW(FILE_MAP_READ, FALSE, RB_IPC_NAME);
        if (mapping_) {
            shared_ = MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, sizeof(RBShared));
            if (!shared_) releaseMapping();
        }
    }
    statusColor_ = warning;
    status_ = L"Start eerst de aparte ReBirth-brug en laad de plug-in daarna opnieuw.";
    details_ = L"Geen verbinding. De plug-in start of sluit de originele ReBirth-app niet zelf.";
    const auto localStatus = frontendStatus_
        ? static_cast<std::uint32_t>(frontendStatus_->load(std::memory_order_relaxed)) : 0u;
    const auto localText = frontendText(localStatus);
    if (localText) status_ = localText;

    const auto* s = static_cast<const RBShared*>(shared_);
    if (s) {
        if (!validHeader(s)) {
            status_ = L"De brug gebruikt een andere versie. Gebruik de bij elkaar horende bestanden.";
            details_ = L"De ongeldige verbinding wordt niet gebruikt voor audio.";
            releaseMapping();
        } else if (static_cast<DWORD>(GetTickCount() - read32(s->heartbeat)) > 3000u) {
            status_ = L"De ReBirth-brug reageert niet meer. Zie het consolevenster en herlaad de plug-in.";
            details_ = L"Wachten op de engine is begrensd; er wordt geen oude audio herhaald.";
            releaseMapping();
        } else {
            const auto worker = read32(s->worker_state);
            const auto client = read32(s->client_status);
            const auto problem = localText ? localText : frontendText(client);
            switch (worker) {
            case RB_STARTING: status_ = L"De brug opent de originele engine. Wacht op het consolevenster."; break;
            case RB_NO_SONG: status_ = L"Open de originele ReBirth-app, laad een song en zet Loop uit."; break;
            case RB_READY:
                status_ = L"Gereed. Live-tempo wordt gevolgd. Start vanaf maat 1 met Play in Live.";
                statusColor_ = good;
                break;
            case RB_PLAYING:
                status_ = L"Live speelt de ReBirth-song af. De Play-knop in ReBirth bestuurt Live niet.";
                statusColor_ = good;
                break;
            case RB_LOOP_ON: status_ = L"Zet Loop in het originele ReBirth-paneel uit."; break;
            case RB_STOPPING: status_ = L"Sluit ReBirth normaal af via File > Quit. Laat de brug open."; break;
            case RB_FAILED: status_ = L"De brug meldt een enginefout. Lees de melding in het consolevenster."; break;
            case RB_STOPPED: status_ = L"De brug is afgesloten. Sluit ReBirth normaal af voordat je opnieuw begint."; break;
            case RB_REARM_REQUIRED:
                status_ = L"Stop Live, ga terug naar maat 1 en start opnieuw vanaf het begin.";
                break;
            default: status_ = L"Onbekende engine-status. Herstart de bij elkaar horende brugbestanden."; break;
            }
            if (problem && worker != RB_FAILED && worker != RB_STOPPING && worker != RB_STOPPED) {
                status_ = problem;
                statusColor_ = warning;
            }
            details_ = L"Engine-latentie: 4096 samples @ 44,1 kHz (92,9 ms); hostlatentie schaalt mee | ontbrekende blokken: " +
                std::to_wstring(read32(s->underruns)) + L" | enginefout: " +
                std::to_wstring(read32(s->error));
        }
    }
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void StatusView::paint() {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(window_, &ps);
    RECT area{};
    GetClientRect(window_, &area);
    HBRUSH brush = CreateSolidBrush(background);
    if (brush) { FillRect(dc, &area, brush); DeleteObject(brush); }
    SetBkMode(dc, TRANSPARENT);
    draw(dc, titleFont_, text, L"ReBirth Bridge - experimentele VST3", {20, 16, 660, 48});
    draw(dc, bodyFont_, muted, L"Engine 44,1 kHz  |  Live 44,1-192 kHz  |  tempo volgt Live  |  stereo", {20, 52, 660, 76});
    draw(dc, bodyFont_, statusColor_, status_.c_str(), {20, 94, 660, 145});
    draw(dc, bodyFont_, muted, details_.c_str(), {20, 154, 660, 195});
    draw(dc, bodyFont_, text,
        L"Gebruik de originele ReBirth-app om je song te laden en apart op te slaan.\n"
        L"Zet Loop uit in ReBirth en Live. Start Live vanaf maat 1.\n"
        L"Neem uitvoer in realtime op; offline export is nog niet ondersteund.",
        {20, 207, 660, 276});
    draw(dc, bodyFont_, muted,
        L"Nog geen MIDI-noten, automatisering of songopslag in het Live-project.",
        {20, 291, 660, 319});
    EndPaint(window_, &ps);
}

LRESULT CALLBACK StatusView::windowProc(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR id, DWORD_PTR reference) {
    auto* view = reinterpret_cast<StatusView*>(reference);
    if (view) {
        switch (message) {
        case WM_TIMER:
            if (wParam == statusTimer) { view->updateStatus(); return 0; }
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: view->paint(); return 0;
        case WM_NCDESTROY:
            KillTimer(window, statusTimer);
            RemoveWindowSubclass(window, windowProc, id);
            if (view->window_ == window) view->window_ = nullptr;
            break;
        default: break;
        }
    }
    return DefSubclassProc(window, message, wParam, lParam);
}

} // namespace RebirthBridge
