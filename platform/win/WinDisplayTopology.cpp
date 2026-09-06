#include "platform/win/WinDisplayTopology.h"

#include "core/Log.h"
#include "platform/win/WinMessageWindow.h"

#include <windows.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

// A raw DPI outside this range is not believed. EDID is user-supplied data in the sense that
// matters: a projector, a television, a KVM and a virtual display all report sizes ranging from
// absent to absurd, and a physical DPI of 4 or of 40,000 propagates into `DpiPolicy` as a
// rasterization resolution. Outside the range the effective DPI is used instead, which is the
// nominal answer every caller had before this file existed -- a known approximation rather than an
// unknown fiction.
constexpr double kMinBelievableRawDpi = 48.0;
constexpr double kMaxBelievableRawDpi = 1200.0;

QString monitorId(const MONITORINFOEXW& info)
{
    return QString::fromWCharArray(info.szDevice);
}

// `szDevice` -> the monitor's own model name, for every path DisplayConfig can see.
//
// WHY A SECOND API AT ALL. `GetMonitorInfoW` answers `\\.\DISPLAY1`, which is a name for a
// SLOT rather than for a panel: unplug the monitor and plug a different one into the same output
// and the string is unchanged. `DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME` answers what the
// display says about itself, and Qt 6 names a `QScreen` with exactly that -- which is the whole
// reason this function exists (**I-20**). Nothing in `platform/` needs it; `ui/viewer/` does, to
// find the row describing the monitor a widget is currently on.
//
// FAILURE IS EMPTINESS, NOT AN ERROR. Every call site treats a missing friendly name as "join on
// `id` instead", so a session where DisplayConfig is unavailable degrades to what this file did
// before rather than failing a query that has nothing to do with names.
std::unordered_map<std::wstring, QString> friendlyNamesByDevice()
{
    std::unordered_map<std::wstring, QString> out;

    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (::GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS)
        return out;

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (::QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount,
                             modes.data(), nullptr)
        != ERROR_SUCCESS)
        return out;

    paths.resize(pathCount);
    for (const DISPLAYCONFIG_PATH_INFO& path : paths) {
        // The SOURCE half names the adapter output -- this is the `\\.\DISPLAY1` that
        // `GetMonitorInfoW` also reports, and it is the key the two APIs share.
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (::DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;

        // The TARGET half names the panel.
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof(target);
        target.header.adapterId = path.targetInfo.adapterId;
        target.header.id = path.targetInfo.id;
        if (::DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;

        const QString friendly = QString::fromWCharArray(target.monitorFriendlyDeviceName);
        if (friendly.isEmpty()) continue;   // an unnamed panel joins by `id`, like any other
        out.emplace(std::wstring(source.viewGdiDeviceName), friendly);
    }

    return out;
}

struct EnumState {
    std::vector<MonitorInfo> monitors;
    std::unordered_map<std::wstring, QString> friendlyNames;
};

BOOL CALLBACK enumMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter)
{
    auto* state = reinterpret_cast<EnumState*>(parameter);

    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (::GetMonitorInfoW(monitor, &info) == 0) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "display: GetMonitorInfoW failed -- win32 error" << static_cast<int>(::GetLastError());
        return TRUE;   // skip this one, keep enumerating: one unreadable monitor is not a failure
    }

    MonitorInfo out;
    out.id = monitorId(info);
    if (const auto friendly = state->friendlyNames.find(std::wstring(info.szDevice));
        friendly != state->friendlyNames.end()) {
        out.friendlyName = friendly->second;
    }
    out.x = info.rcMonitor.left;
    out.y = info.rcMonitor.top;
    out.width = info.rcMonitor.right - info.rcMonitor.left;
    out.height = info.rcMonitor.bottom - info.rcMonitor.top;
    out.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // THE EFFECTIVE DPI IS THE SCALE FACTOR, AND THE RAW DPI IS THE GLASS. They are two different
    // questions and this is the one place in the product that may answer them from one call site.
    UINT effectiveX = 0;
    UINT effectiveY = 0;
    if (SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &effectiveX, &effectiveY))
        && effectiveX > 0) {
        out.scaleFactor = static_cast<double>(effectiveX) / 96.0;
        out.physicalDpi = static_cast<int>(effectiveX);
    }

    UINT rawX = 0;
    UINT rawY = 0;
    if (SUCCEEDED(::GetDpiForMonitor(monitor, MDT_RAW_DPI, &rawX, &rawY))) {
        const double raw = static_cast<double>(rawX);
        if (raw >= kMinBelievableRawDpi && raw <= kMaxBelievableRawDpi) {
            out.physicalDpi = static_cast<int>(rawX);
        } else if (raw > 0.0) {
            RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
                << "display:" << out.id << "reports a raw DPI of" << static_cast<int>(rawX)
                << "-- not believed; using the effective DPI instead";
        }
    }

    state->monitors.push_back(std::move(out));
    return TRUE;
}

} // namespace

bool sameTopology(const std::vector<MonitorInfo>& a, const std::vector<MonitorInfo>& b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const MonitorInfo& l = a[i];
        const MonitorInfo& r = b[i];
        if (l.id != r.id || l.x != r.x || l.y != r.y || l.width != r.width || l.height != r.height
            || l.scaleFactor != r.scaleFactor || l.physicalDpi != r.physicalDpi
            || l.primary != r.primary) {
            return false;
        }
    }
    return true;
}

bool WinDisplayTopology::processIsPerMonitorAware()
{
    const DPI_AWARENESS_CONTEXT context = ::GetThreadDpiAwarenessContext();
    return ::AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0
           || ::AreDpiAwarenessContextsEqual(context, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE) != 0;
}

WinDisplayTopology::WinDisplayTopology() = default;

WinDisplayTopology::~WinDisplayTopology()
{
    // The window goes before the handler list it calls into. Destroying it runs WM_DESTROY
    // synchronously, and `WinMessageWindow`'s destructor drops its handler first for exactly this
    // reason -- but stating the order here as well is what makes the two files agree on purpose
    // rather than by luck.
    window_.reset();
    handlers_.clear();
}

Result<std::vector<MonitorInfo>> WinDisplayTopology::monitors() const
{
    if (!processIsPerMonitorAware()) {
        // A WARNING, NOT A REFUSAL. The figures are still the best this process can obtain, and a
        // diagnostic run that got nothing at all would be worse. What must not happen is a caller
        // believing 2560x1440-at-100 % of a 4K panel because nothing said otherwise.
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "display: this process is not per-monitor DPI aware -- the monitor geometry below is"
            << "virtualized by Windows and is not the real hardware";
    }

    EnumState state;
    state.friendlyNames = friendlyNamesByDevice();
    if (::EnumDisplayMonitors(nullptr, nullptr, &enumMonitor,
                              reinterpret_cast<LPARAM>(&state)) == 0) {
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.platform.displayQuery"),
                           QStringLiteral("EnumDisplayMonitors: win32 error %1")
                               .arg(static_cast<int>(::GetLastError()))};
    }

    if (state.monitors.empty()) {
        // Every desktop session has at least one. Zero means the query ran and told us nothing,
        // which is a failure wearing an empty list's clothes.
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.platform.displayQuery"),
                           QStringLiteral("EnumDisplayMonitors reported no monitors")};
    }

    // Primary first, then left to right, then top to bottom. A stable order is what makes
    // `sameTopology` a comparison of arrangements rather than of enumeration luck -- Windows does
    // not promise the callback order, and a reordering would otherwise read as a change.
    std::sort(state.monitors.begin(), state.monitors.end(),
              [](const MonitorInfo& a, const MonitorInfo& b) {
                  if (a.primary != b.primary) return a.primary;
                  if (a.x != b.x) return a.x < b.x;
                  if (a.y != b.y) return a.y < b.y;
                  return a.id < b.id;
              });

    return state.monitors;
}

Status WinDisplayTopology::subscribe(ChangedHandler handler)
{
    if (!handler) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.platform.displaySubscribe"),
                           QStringLiteral("null handler")};
    }

    if (!window_) {
        window_ = std::make_unique<WinMessageWindow>(
            QStringLiteral("DisplayTopology"),
            [this](unsigned int message, std::uint64_t wParam, std::int64_t lParam) {
                return onMessage(message, wParam, lParam);
            });

        if (!window_->isValid()) {
            window_.reset();
            return core::Error{core::ErrorCode::IoFailure,
                               QLatin1String("error.platform.displaySubscribe"),
                               QStringLiteral("could not create the notification window")};
        }

        // The baseline the first notification is compared against. Taken here rather than in the
        // constructor because a subscriber-less object never queries, and an object constructed
        // before the display was ready would hold a snapshot that makes the first real change look
        // like a change from nothing.
        if (const Result<std::vector<MonitorInfo>> current = monitors()) last_ = current.value();
    }

    handlers_.push_back(std::move(handler));
    return {};
}

std::uintptr_t WinDisplayTopology::notificationHandle() const noexcept
{
    return window_ ? window_->nativeHandle() : 0;
}

bool WinDisplayTopology::onMessage(unsigned int message, std::uint64_t, std::int64_t)
{
    switch (message) {
    case WM_DISPLAYCHANGE:      // resolution, colour depth, or a monitor arriving or leaving
    case WM_DPICHANGED:         // this window's monitor changed scale
    case WM_SETTINGCHANGE:      // the broadcast a Settings-app scale change rides on
    case WM_DEVICECHANGE:       // a display adapter or dock appearing
        reevaluate();
        break;
    default:
        break;
    }

    // Never consumed. Every message above has other legitimate recipients and meanings -- this
    // object is an observer, and a `WNDPROC` that swallowed `WM_SETTINGCHANGE` would be making a
    // decision on behalf of code it has never met.
    return false;
}

void WinDisplayTopology::reevaluate()
{
    const Result<std::vector<MonitorInfo>> current = monitors();
    if (!current) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "display: change notification could not be resolved --"
            << current.error().messageKey << current.error().context;
        return;
    }

    if (sameTopology(current.value(), last_)) {
        // THE SUPPRESSED CASE, and it is the common one: a single scale change in the Settings app
        // produces several of the messages above, and `WM_SETTINGCHANGE` arrives for things that
        // have nothing to do with displays at all.
        ++suppressed_;
        return;
    }

    last_ = current.value();
    ++changeCount_;

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "display: topology changed --" << static_cast<int>(last_.size()) << "monitors";
    for (const MonitorInfo& monitor : last_) {
        RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
            << "display:" << monitor.id << monitor.width << "x" << monitor.height << "at"
            << static_cast<int>(monitor.scaleFactor * 100.0) << "%, physical DPI"
            << monitor.physicalDpi << (monitor.primary ? "(primary)" : "");
    }

    // A COPY, iterated. A handler is entitled to subscribe another one -- the render policy does
    // exactly that when a document opens -- and iterating the live vector while it reallocates is
    // an invalidated iterator inside a message pump, which is the least debuggable place in the
    // program for one.
    const std::vector<ChangedHandler> handlers = handlers_;
    for (const ChangedHandler& handler : handlers) {
        if (handler) handler();
    }
}

} // namespace rawpage::platform::win
