// A hidden top-level window whose only job is to receive the OS's broadcast notifications.
// Stage 2 T12, shared by `WinDisplayTopology` and `WinPowerSession`.
//
// ---------------------------------------------------------------------------------------------
// WHY THIS FILE EXISTS AT ALL, WHEN THE PLAN'S MANIFEST NAMES ONLY TWO PAIRS.
//
// Both T12 interfaces are *notification* interfaces, and on Windows a notification is a window
// message: `WM_DISPLAYCHANGE`, `WM_DPICHANGED`, `WM_SETTINGCHANGE`, `WM_POWERBROADCAST`,
// `WM_WTSSESSION_CHANGE`. Every one of them needs an `HWND` to arrive at, and neither class has a
// window of its own -- `platform/` may not name a `QWidget`, and a window borrowed from `ui/` would
// make the OS layer depend on the presentation layer, which is the dependency direction this whole
// architecture exists to prevent.
//
// So the two classes each need the same forty lines of window-class registration, creation,
// `WNDPROC` trampolining and teardown. Written twice, the second copy is where the missing
// `UnregisterClass` lives. This is the same call T11 made for `core/render/DiskTileStore`: a file
// the manifest did not list, added with its own FileMap row rather than smuggled inside another.
//
// ---------------------------------------------------------------------------------------------
// TOP-LEVEL, NOT `HWND_MESSAGE`, AND THAT IS THE LOAD-BEARING DETAIL.
//
// A message-only window (`HWND_MESSAGE` as its parent) is cheaper and is the obvious choice, and it
// would silently break half of what this file is for: **message-only windows do not receive
// broadcast messages.** `WM_DISPLAYCHANGE` and `WM_SETTINGCHANGE` are broadcasts to top-level
// windows, so a display-topology watcher built on `HWND_MESSAGE` would register successfully, log
// nothing, and never fire -- the failure mode being an absence, which no test that only checks the
// happy path can see. The directed notifications (`WM_POWERBROADCAST` from
// `RegisterPowerSettingNotification`, `WM_WTSSESSION_CHANGE` from `WTSRegisterSessionNotification`)
// arrive at either kind, so the strictly-more-capable one is the right default.
//
// It is created with `WS_POPUP`, zero-sized, and never shown, so it takes no space on any monitor
// and appears in no taskbar.
//
// ---------------------------------------------------------------------------------------------
// THREADING. The window belongs to the thread that constructed it, and its handler is invoked from
// that thread's message pump -- which, in the product, is Qt's own event dispatcher: it pumps every
// message for the thread, not only those for `QWidget` windows. Construct and destroy it on one
// thread. A handler must therefore be as cheap as a signal emission, because it runs inside the
// event loop's `DispatchMessage`.

#ifndef RAWPAGE_PLATFORM_WIN_WINMESSAGEWINDOW_H
#define RAWPAGE_PLATFORM_WIN_WINMESSAGEWINDOW_H

#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

namespace rawpage::platform::win {

class WinMessageWindow {
public:
    // `message`, `wParam` and `lParam` verbatim. They are plain integers rather than `UINT`/
    // `WPARAM`/`LPARAM` so this header names no Win32 type and can be included by a test that has
    // no wish to pull in `windows.h`.
    //
    // Returning true means the handler consumed the message and `DefWindowProc` is not called.
    // `WM_POWERBROADCAST` is the reason the return value exists: a handler that wants to accept a
    // suspend must answer TRUE rather than let the default procedure answer for it.
    using MessageHandler = std::function<bool(unsigned int message, std::uint64_t wParam,
                                              std::int64_t lParam)>;

    // `classSuffix` distinguishes this window's registered class from every other one in the
    // process. Two `WinMessageWindow`s with the same suffix are fine -- the class is registered
    // once and reference-counted -- but a name that collides with an unrelated window class in the
    // process would fail to register, so the suffix names the owner.
    WinMessageWindow(QString classSuffix, MessageHandler handler);
    ~WinMessageWindow();

    WinMessageWindow(const WinMessageWindow&) = delete;
    WinMessageWindow& operator=(const WinMessageWindow&) = delete;

    // False when the window could not be created. A caller reports that as a `Status` failure
    // rather than pretending it subscribed: a subscription that silently never fires is the worst
    // of the three possible outcomes, because it looks exactly like "nothing has changed yet".
    bool isValid() const noexcept;

    // The `HWND` as an opaque integer, for `RegisterPowerSettingNotification` and friends -- and
    // for the suite, which posts a real message to it so the real `WNDPROC` runs. That is
    // deliberately not a test hook: the only synthetic part is where the message came from, and
    // everything downstream of `DispatchMessage` is the shipped path (convention 19).
    std::uintptr_t nativeHandle() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINMESSAGEWINDOW_H
