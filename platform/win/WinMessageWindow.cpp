#include "platform/win/WinMessageWindow.h"

#include "core/Log.h"

#include <windows.h>

#include <string>
#include <utility>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

std::wstring toWide(const QString& text)
{
    return std::wstring(reinterpret_cast<const wchar_t*>(text.utf16()),
                        static_cast<std::size_t>(text.size()));
}

// The property the trampoline reads to find the handler. `SetWindowLongPtrW(GWLP_USERDATA)` would
// be one pointer cheaper and is not used, because `GWLP_USERDATA` is a single slot that any other
// code touching this HWND would also claim -- and this window's whole purpose is to be handed to OS
// notification APIs written by other people.
//
// It points at the HANDLER rather than at the object: `Impl` is a private nested type and a free
// `WNDPROC` cannot name it, while `MessageHandler` is the public vocabulary this class is built on.
// The trampoline needs nothing else.
constexpr wchar_t kHandlerProperty[] = L"RawPage.MessageWindow.Handler";

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

} // namespace

struct WinMessageWindow::Impl {
    MessageHandler handler;
    std::wstring className;
    HINSTANCE instance = nullptr;
    ATOM classAtom = 0;
    HWND hwnd = nullptr;
};

namespace {

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    auto* handler =
        static_cast<WinMessageWindow::MessageHandler*>(::GetPropW(hwnd, kHandlerProperty));

    // Null between `CreateWindowExW` starting and the property being set -- Windows sends
    // WM_GETMINMAXINFO, WM_NCCREATE and WM_CREATE before the call returns. Falling through to the
    // default procedure is correct for all of them; dereferencing would be a null crash at startup
    // that only ever reproduces on a machine slow enough to notice.
    if (handler != nullptr && *handler) {
        if ((*handler)(static_cast<unsigned int>(message), static_cast<std::uint64_t>(wParam),
                       static_cast<std::int64_t>(lParam))) {
            return TRUE;
        }
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

} // namespace

WinMessageWindow::WinMessageWindow(QString classSuffix, MessageHandler handler)
    : impl_(std::make_unique<Impl>())
{
    impl_->handler = std::move(handler);
    impl_->instance = ::GetModuleHandleW(nullptr);
    impl_->className = toWide(QStringLiteral("RawPage.MessageWindow.") + classSuffix);

    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &windowProc;
    description.hInstance = impl_->instance;
    description.lpszClassName = impl_->className.c_str();

    impl_->classAtom = ::RegisterClassExW(&description);
    if (impl_->classAtom == 0) {
        const DWORD error = ::GetLastError();

        // ERROR_CLASS_ALREADY_EXISTS is not a failure: two objects with the same suffix share one
        // registration, and `CreateWindowExW` below works from the name either way. Anything else
        // is, and there is no point attempting the create.
        if (error != ERROR_CLASS_ALREADY_EXISTS) {
            RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
                << "platform: could not register notification window class --" << classSuffix
                << "win32 error" << static_cast<int>(error);
            return;
        }
    }

    // WS_POPUP and never shown. A zero-sized popup that is never given WS_VISIBLE occupies no
    // monitor, takes no taskbar button and steals no focus, while remaining a TOP-LEVEL window --
    // which is the property `WM_DISPLAYCHANGE` and `WM_SETTINGCHANGE` are delivered on. See the
    // header on why `HWND_MESSAGE` is the wrong answer here despite being the obvious one.
    impl_->hwnd = ::CreateWindowExW(0, impl_->className.c_str(), L"", WS_POPUP, 0, 0, 0, 0, nullptr,
                                    nullptr, impl_->instance, nullptr);
    if (impl_->hwnd == nullptr) {
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "platform: could not create notification window --" << classSuffix << "win32 error"
            << static_cast<int>(::GetLastError());
        return;
    }

    ::SetPropW(impl_->hwnd, kHandlerProperty, &impl_->handler);
}

WinMessageWindow::~WinMessageWindow()
{
    if (impl_->hwnd != nullptr) {
        // The property goes FIRST. `DestroyWindow` sends WM_DESTROY and WM_NCDESTROY synchronously,
        // and a handler entered while this destructor is unwinding would be a call into a
        // half-destroyed owner -- convention 10's rule, in its Win32 spelling.
        ::RemovePropW(impl_->hwnd, kHandlerProperty);
        impl_->handler = nullptr;
        ::DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }

    // The class is left registered on purpose. `UnregisterClassW` fails while any window of the
    // class still exists, and this process may hold a second `WinMessageWindow` with the same
    // suffix; a per-process class registration is a few dozen bytes that the process exit reclaims,
    // and racing it is how a second subscriber fails to create its window.
}

bool WinMessageWindow::isValid() const noexcept
{
    return impl_->hwnd != nullptr;
}

std::uintptr_t WinMessageWindow::nativeHandle() const noexcept
{
    return reinterpret_cast<std::uintptr_t>(impl_->hwnd);
}

} // namespace rawpage::platform::win
