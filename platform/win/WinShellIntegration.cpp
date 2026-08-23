#include "platform/win/WinShellIntegration.h"

#include "core/Log.h"

#include <windows.h>
#include <shellapi.h>
// SHChangeNotify lives here, not in shellapi.h -- MinGW's headers split them the way the SDK does.
#include <shlobj.h>

#include <utility>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

std::wstring toWide(const QString& text)
{
    return std::wstring(reinterpret_cast<const wchar_t*>(text.utf16()), text.size());
}

QString fromWide(const wchar_t* text, DWORD lengthBytes)
{
    if (text == nullptr || lengthBytes < sizeof(wchar_t)) return {};
    // The OS counts bytes and includes the terminator when it feels like it; trimming here rather
    // than at every call site is what stops a stray NUL ending up inside a QString.
    int chars = static_cast<int>(lengthBytes / sizeof(wchar_t));
    while (chars > 0 && text[chars - 1] == L'\0') --chars;
    return QString::fromWCharArray(text, chars);
}

core::Error registryError(const QString& key, LONG code)
{
    return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.shell.registry"),
                       QStringLiteral("%1 (win32 error %2)").arg(key).arg(code)};
}

bool keyExists(const QString& path)
{
    HKEY key = nullptr;
    const std::wstring wide = toWide(path);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wide.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(key);
    return true;
}

QString readStringValue(HKEY root, const QString& path, const QString& valueName)
{
    HKEY key = nullptr;
    const std::wstring widePath = toWide(path);
    if (RegOpenKeyExW(root, widePath.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) return {};

    wchar_t buffer[512] = {};
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    const std::wstring wideValue = toWide(valueName);
    const LONG result =
        RegQueryValueExW(key, valueName.isEmpty() ? nullptr : wideValue.c_str(), nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return {};
    return fromWide(buffer, size);
}

LONG writeStringValue(const QString& path, const QString& valueName, const QString& value)
{
    HKEY key = nullptr;
    const std::wstring widePath = toWide(path);
    LONG result = RegCreateKeyExW(HKEY_CURRENT_USER, widePath.c_str(), 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return result;

    const std::wstring wideValue = toWide(value);
    const std::wstring wideName = toWide(valueName);
    result = RegSetValueExW(key, valueName.isEmpty() ? nullptr : wideName.c_str(), 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(wideValue.c_str()),
                            static_cast<DWORD>((wideValue.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return result;
}

} // namespace

WinShellIntegration::WinShellIntegration(QString executablePath, QString registryRoot)
    : executablePath_(std::move(executablePath)), registryRoot_(std::move(registryRoot))
{
    progIdKeyPath_ = registryRoot_ + QLatin1Char('\\') + QLatin1String(kPdfProgId);
}

Result<IShellIntegration::AssociationStatus> WinShellIntegration::pdfAssociation() const
{
    AssociationStatus status;
    status.progIdRegistered = keyExists(progIdKeyPath_);

    // Read-only, always. Nothing in this method writes, so a diagnostic that prints the association
    // cannot change it -- which matters because the obvious place to call this from is a Preferences
    // page that opens on every launch.
    const QString userChoice =
        readStringValue(HKEY_CURRENT_USER,
                        QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\"
                                       "FileExts\\.pdf\\UserChoice"),
                        QStringLiteral("ProgId"));

    if (!userChoice.isEmpty()) {
        status.currentDefaultHandler = userChoice;
    } else {
        // No UserChoice means the user has never chosen; the machine default under HKCR applies.
        status.currentDefaultHandler =
            readStringValue(HKEY_CLASSES_ROOT, QStringLiteral(".pdf"), QString());
    }

    status.isDefaultHandler = (status.currentDefaultHandler == QLatin1String(kPdfProgId));
    return status;
}

Status WinShellIntegration::registerPdfAssociation()
{
    if (executablePath_.isEmpty()) {
        return core::Error{core::ErrorCode::InvalidArgument,
                           QLatin1String("error.shell.noExecutablePath")};
    }

    // Backslashes, and quoted: an unquoted command whose path contains a space is the oldest
    // association bug there is, and `%1` unquoted breaks on every file name with a space in it.
    const QString command =
        QStringLiteral("\"%1\" \"%2\"").arg(QString(executablePath_).replace(QLatin1Char('/'),
                                                                            QLatin1Char('\\')),
                                            QStringLiteral("%1"));

    struct Write {
        QString key;
        QString value;
        QString data;
    };
    const QString progId = QLatin1String(kPdfProgId);
    const std::vector<Write> writes{
        {progIdKeyPath_, QString(), QStringLiteral("PDF Document")},
        {progIdKeyPath_ + QStringLiteral("\\DefaultIcon"), QString(),
         QStringLiteral("\"%1\",0").arg(QString(executablePath_).replace(QLatin1Char('/'),
                                                                        QLatin1Char('\\')))},
        {progIdKeyPath_ + QStringLiteral("\\shell\\open\\command"), QString(), command},
        // The entry that actually puts us in Open With. Registering the ProgID alone does nothing
        // visible; this is the association half.
        {registryRoot_ + QStringLiteral("\\.pdf\\OpenWithProgids"), progId, QString()},
    };

    for (const Write& write : writes) {
        const LONG result = writeStringValue(write.key, write.value, write.data);
        if (result != ERROR_SUCCESS) return registryError(write.key, result);
    }

    // Without this the shell keeps its cached association list until the next logon, and the user
    // reports that registering "did nothing".
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    RAWPAGE_LOG_INFO(LogSubsystem::Ui)
        << "shell: registered the PDF ProgID under HKCU" << progIdKeyPath_.toUtf8().constData();
    return {};
}

Status WinShellIntegration::unregisterPdfAssociation()
{
    // RegDeleteTreeW rather than RegDeleteKeyW: the ProgID has subkeys, and RegDeleteKeyW refuses a
    // key that has any -- which would leave a half-registration that reads as registered.
    const std::wstring wideProgId = toWide(progIdKeyPath_);
    const LONG progIdResult = RegDeleteTreeW(HKEY_CURRENT_USER, wideProgId.c_str());
    if (progIdResult != ERROR_SUCCESS && progIdResult != ERROR_FILE_NOT_FOUND) {
        return registryError(progIdKeyPath_, progIdResult);
    }

    // Only OUR value under OpenWithProgids. Deleting the key would remove every other application's
    // registration with it, which is the kind of collateral damage an uninstaller gets blamed for
    // years later.
    const QString openWith = registryRoot_ + QStringLiteral("\\.pdf\\OpenWithProgids");
    HKEY key = nullptr;
    const std::wstring wideOpenWith = toWide(openWith);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, wideOpenWith.c_str(), 0, KEY_SET_VALUE, &key)
        == ERROR_SUCCESS) {
        const std::wstring wideProgIdName = toWide(QLatin1String(kPdfProgId));
        RegDeleteValueW(key, wideProgIdName.c_str());
        RegCloseKey(key);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return {};
}

Status WinShellIntegration::showDefaultAppsUi()
{
    // The only honest implementation. See the header: the association cannot be set from here, and
    // this opens the page where the user can set it.
    const std::wstring target = toWide(QStringLiteral("ms-settings:defaultapps"));
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));

    // ShellExecuteW's success threshold is 32. Anything at or below it is an error code wearing a
    // handle's clothes.
    if (result <= 32) {
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.shell.defaultAppsUi"),
                           QStringLiteral("ShellExecute returned %1").arg(result)};
    }
    return {};
}

} // namespace rawpage::platform::win
