// The Windows `IShellIntegration` -- Stage 1 implements FILE ASSOCIATION ONLY.
//
// ---------------------------------------------------------------------------------------------
// WHAT THIS CAN AND CANNOT DO, because the difference is the whole design.
//
// It CAN register a ProgID under `HKCU\Software\Classes` and add it to `.pdf`'s `OpenWithProgids`,
// which is what puts RawPage in the Open With list and makes "Always use this app" offer us.
//
// It CANNOT make RawPage the default handler. Since Windows 8 the `.pdf` → ProgID association in
// `HKCU\Software\Classes\.pdf\UserChoice` is protected by a hash the OS computes from the user's
// SID, the ProgID and a timestamp; a value written by anything but the shell is detected and the
// association is reset to the OS default. Products that appear to set it are opening the Settings
// UI, which is what `showDefaultAppsUi()` does. Modelling this as a setter would be an API whose
// contract cannot be honoured.
//
// PER-USER, NEVER MACHINE-WIDE. `HKLM` needs elevation, and an application that asks to be elevated
// in order to open a file is an application people stop opening files with. The installer may write
// `HKLM` at Stage 15; the running app never does.
//
// ---------------------------------------------------------------------------------------------
// THE ROOT IS INJECTABLE, AND THAT IS NOT A TEST HOOK BOLTED ON.
//
// `registryRoot` defaults to `Software\Classes`. A test passes a scratch subkey so the association
// path is exercised for real -- created, read back, removed -- without touching the association a
// person is actually using on this machine. A test that has to skip the write half is a test of the
// half that never breaks.

#ifndef RAWPAGE_PLATFORM_WIN_WINSHELLINTEGRATION_H
#define RAWPAGE_PLATFORM_WIN_WINSHELLINTEGRATION_H

#include "platform/platform.h"

#include <QString>

namespace rawpage::platform::win {

// The ProgID we register. A permanent public name in exactly the sense an `ActionId` string is: it
// is written into the user's registry and read back by later versions, and renaming it orphans
// every association a user has made.
inline constexpr char kPdfProgId[] = "RawPage.Document.1";

class WinShellIntegration final : public IShellIntegration {
public:
    // `executablePath` is written into the ProgID's shell/open command. Passed in rather than
    // resolved here so the composition root -- which already knows where it launched from -- stays
    // the single place that answers that question.
    explicit WinShellIntegration(QString executablePath,
                                 QString registryRoot = QStringLiteral("Software\\Classes"));

    Result<AssociationStatus> pdfAssociation() const override;
    Status registerPdfAssociation() override;
    Status unregisterPdfAssociation() override;
    Status showDefaultAppsUi() override;

    const QString& progIdKeyPath() const { return progIdKeyPath_; }

private:
    QString executablePath_;
    QString registryRoot_;
    QString progIdKeyPath_;
};

} // namespace rawpage::platform::win

#endif // RAWPAGE_PLATFORM_WIN_WINSHELLINTEGRATION_H
