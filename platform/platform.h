// The OS boundary. Interfaces only -- no implementation, and no OS header.
//
// ---------------------------------------------------------------------------------------------
// WHAT IS AND IS NOT ALLOWED TO BE HERE, AND WHY IT IS MACHINE-CHECKED.
//
// This file is included by `core/`, `ui/` and `app/`. If a Win32 header ever reaches it, every one
// of those layers acquires `windows.h` transitively -- along with `min`/`max` macros, `interface`,
// `GetMessage` renaming itself to `GetMessageW`, and a compile dependency on the platform SDK in a
// layer whose entire contract is that it builds with no OS at all. It would not fail loudly; it
// would work on this machine and stop `portable-asan` from building `core/` on Linux, which is the
// mechanical proof that the domain layer is portable.
//
// So `tools/layer-lint/rules.json` declares this ONE FILE as its own layer with an OS-header deny
// list, and `tests/gates/` proves the rule fires. Prose in a header comment is not a rule; the gate
// is (Stage 0's standing lesson about gates versus review discipline).
//
// ---------------------------------------------------------------------------------------------
// WHY THE WHOLE SURFACE IS DECLARED NOW, WHEN STAGE 1 IMPLEMENTS TWO OF IT.
//
// The same reason `core/backend.h` declares all of its interfaces at Stage 2: a boundary added
// later is a boundary added under pressure, by whoever needed one method, in the shape that one
// call site wanted. Declaring the surface once, from the gap list in
// [`Tech-Stack-Plan.md` §4.3](../claude-plan/Tech-Stack-Plan.md#43-what-qt-gives-us-for-free-versus-what-needs-platform-code),
// means Stage 11 adds an implementation rather than an architecture.
//
// **An unimplemented interface is a NULL member of `Platform`, never a stub that silently does
// nothing.** `unimplementedInterfaces()` lists them by name, so "not built yet" is something a
// diagnostic can print rather than something a caller discovers by getting no result.
//
// ---------------------------------------------------------------------------------------------
// THE LIST IS DERIVED, NOT INVENTED. Every interface below corresponds to a row of §4.3's gap
// table: a thing Qt does not do cross-platform. If Qt already does it -- windows, dialogs, docking,
// file I/O, settings, network, printing of *ordinary* documents, UI text layout, DPI for widgets --
// it does NOT get an interface here, and adding one for it is a mistake this comment exists to
// prevent.

#ifndef RAWPAGE_PLATFORM_PLATFORM_H
#define RAWPAGE_PLATFORM_PLATFORM_H

#include "core/Result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <vector>

namespace rawpage::platform {

using core::Result;
using core::Status;

// =======================================================================================
// Value types crossing the boundary
// =======================================================================================
// Deliberately plain: no OS handle, no COM pointer, no Qt GUI type. What crosses this boundary is
// data, in both directions -- the same rule the PDFium boundary follows for the same reason.

// CORRECTED AT STAGE 2 T12, when the first implementation measured it (Stage 2 guide issue I-07).
// The geometry below was documented as *device-independent* pixels and is not: Windows lays the
// virtual desktop out in PHYSICAL pixels, so a 3840x2160 panel driven at 150 % occupies a
// 3840x2160 rectangle and not a 2560x1440 one. The DIP reading is what a DPI-UNAWARE process is
// handed -- virtualized figures that are self-consistent and wrong -- and describing the field that
// way would have made the virtualized answer look like the specified one. Guide case A-60 expects
// the physical figures, and `WinDisplayTopology::processIsPerMonitorAware()` is what lets a caller
// say whether it is being told them.
struct MonitorInfo {
    QString id;                  // stable across a session; the OS device name

    // The MODEL name the display reports about itself -- `MAG 272URDF`, not `\\.\DISPLAY1`.
    //
    // IT EXISTS TO BE JOINED AGAINST A UI TOOLKIT'S IDEA OF A SCREEN, and that is the only reason
    // it is here: Qt 6 names a `QScreen` with exactly this string, so `id` -- which is the OS
    // device name and the right key for everything else -- can never match one. `ui/viewer/`
    // compared the two for the whole of Stage 2 and the join never once succeeded (**I-20**),
    // which cost nothing visible because the fallback answered plausibly and wrongly.
    //
    // MAY BE EMPTY, and a caller must tolerate that rather than treat it as the identity: it comes
    // from a different Windows API than the rest of this struct, it is not guaranteed unique
    // across two identical panels, and `id` remains the stable identifier.
    QString friendlyName;
    int     x = 0;               // virtual-desktop coordinates, in PHYSICAL device pixels
    int     y = 0;
    int     width = 0;           // the panel's real pixel width, not its logical one
    int     height = 0;
    double  scaleFactor = 1.0;   // device pixels per logical pixel: 1.5 at 150 %

    // The dots per inch of GLASS, which is a different question from the scale factor and is not
    // derivable from it -- see `platform/win/WinDisplayTopology.h`. It is what
    // `core/render/DpiPolicy` means by `PhysicalDpi`, and the reason that class takes three inputs
    // rather than two. Falls back to the effective DPI (`96 * scaleFactor`) when the panel reports
    // nothing believable, which is the approximation every caller used before T12.
    int     physicalDpi = 96;
    bool    primary = false;
};

struct FontMatch {
    QString family;
    QString filePath;      // empty when the face is not backed by a file we may read
    int     faceIndex = 0;
    bool    exact = false; // false means this is a substitute, which the caller may want to report
};

struct CertificateInfo {
    QString subject;
    QString issuer;
    QString serialHex;
    QString thumbprintHex;
    qint64  notBeforeUnix = 0;
    qint64  notAfterUnix = 0;
    bool    hasPrivateKey = false;   // i.e. it can sign, not merely validate
};

struct ScannerDevice {
    QString id;
    QString name;
    bool    hasDocumentFeeder = false;
    bool    supportsDuplex = false;
};

struct PrinterInfo {
    QString name;
    bool    isDefault = false;
    bool    supportsDuplex = false;
    bool    supportsColor = false;
    QStringList trays;
};

// =======================================================================================
// 1. IShellIntegration -- COM server registrations Qt has no notion of
// =======================================================================================
// Stage 1 implements FILE ASSOCIATION ONLY. Thumbnails, preview, the index filter, the jump list
// and taskbar progress are Stage 15, and each is a separate COM server registration.
class IShellIntegration {
public:
    virtual ~IShellIntegration() = default;

    struct AssociationStatus {
        bool    progIdRegistered = false;   // our ProgID exists and .pdf offers it in Open With
        bool    isDefaultHandler = false;   // the user has actually chosen us
        QString currentDefaultHandler;      // who has it now; empty when the OS reports none
    };

    virtual Result<AssociationStatus> pdfAssociation() const = 0;

    // Per-user registration. NEVER machine-wide: a machine-wide association needs elevation, and an
    // app that asks for elevation to open a file is an app people stop opening files with.
    virtual Status registerPdfAssociation() = 0;
    virtual Status unregisterPdfAssociation() = 0;

    // Asks the OS to show its own default-apps UI.
    //
    // THIS IS NOT A SETTER, AND IT CANNOT BE. Since Windows 8 an application cannot make itself the
    // default handler programmatically -- the association is protected by a hash the OS computes,
    // and writing the registry value directly is either ignored or actively reset. Every product
    // that appears to do it is opening this UI. Modelling it as `setDefaultHandler()` would create
    // an API whose contract cannot be honoured, and the caller would report the failure as our bug.
    virtual Status showDefaultAppsUi() = 0;
};

// =======================================================================================
// 2. IPrinterDriverService -- the virtual PDF printer (Stage 16)
// =======================================================================================
class IPrinterDriverService {
public:
    virtual ~IPrinterDriverService() = default;

    virtual bool   isInstalled() const = 0;
    virtual Status install() = 0;         // needs elevation; the installer calls this, not the app
    virtual Status uninstall() = 0;

    // A spooled job the virtual printer captured, handed over as a file path plus its originating
    // document title. Called on a worker thread.
    using JobHandler = std::function<void(const QString& spoolFilePath, const QString& jobTitle)>;
    virtual Status listenForJobs(JobHandler) = 0;
};

// =======================================================================================
// 3. IScanner -- imaging devices (Stage 16)
// =======================================================================================
class IScanner {
public:
    virtual ~IScanner() = default;

    struct AcquireOptions {
        QString deviceId;
        int     dpi = 300;
        bool    duplex = false;
        bool    useDocumentFeeder = false;
        bool    color = true;
    };

    virtual Result<std::vector<ScannerDevice>> devices() const = 0;

    // Returns the acquired pages as image FILES rather than as pixels: a 50-page duplex ADF run at
    // 600 dpi is gigabytes, and streaming it through a return value would put it all in memory at
    // once for no benefit.
    virtual Result<QStringList> acquire(const AcquireOptions&) = 0;
};

// =======================================================================================
// 4. ICertificateStore -- the OS certificate store (Stage 10 lookup, Stage 11 full)
// =======================================================================================
class ICertificateStore {
public:
    virtual ~ICertificateStore() = default;

    virtual Result<std::vector<CertificateInfo>> signingIdentities() const = 0;
    virtual Result<std::vector<CertificateInfo>> trustedRoots() const = 0;

    // The DER bytes of a certificate by thumbprint, for embedding in a signature dictionary.
    virtual Result<QByteArray> certificateDer(const QString& thumbprintHex) const = 0;

    // Chain building is the OS's job, not ours: it knows the store, the cross-certificates, and the
    // policy. `Result` carries why a chain could not be built, which a signature panel must show.
    virtual Result<std::vector<CertificateInfo>> buildChain(const QString& thumbprintHex) const = 0;
};

// =======================================================================================
// 5. ICryptoProvider -- signing through the OS, including smart cards and HSMs (Stage 11)
// =======================================================================================
class ICryptoProvider {
public:
    virtual ~ICryptoProvider() = default;

    enum class DigestAlgorithm { Sha256, Sha384, Sha512 };

    // Signs a HASH, never a document. The bytes never leave our process, the private key never
    // enters it, and a smart card's PIN prompt is the OS's to show.
    virtual Result<QByteArray> signHash(const QString& thumbprintHex, const QByteArray& digest,
                                        DigestAlgorithm) = 0;

    virtual Result<bool> verify(const QByteArray& signature, const QByteArray& digest,
                                const QByteArray& certificateDer, DigestAlgorithm) const = 0;

    // Revocation checking. Network work, so it is explicitly asynchronous-capable at the call site
    // rather than pretending to be instant.
    virtual Result<QByteArray> fetchOcsp(const QByteArray& certificateDer,
                                         const QByteArray& issuerDer) const = 0;
    virtual Result<QByteArray> fetchCrl(const QByteArray& certificateDer) const = 0;
};

// =======================================================================================
// 6. ISecretStore -- OS-key-protected storage (Stage 11)
// =======================================================================================
class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual Status              store(const QString& key, const QByteArray& secret) = 0;
    virtual Result<QByteArray>  retrieve(const QString& key) const = 0;
    virtual Status              erase(const QString& key) = 0;
};

// =======================================================================================
// 7. IFontResolver -- fallback for a MISSING EMBEDDED DOCUMENT font (Stage 2)
// =======================================================================================
// Not for UI text: Qt's font database handles that, cross-platform, and is not behind this
// boundary. This is the different and harder problem of substituting for a font a PDF names and
// does not embed, where the answer must match what the document's author saw.
class IFontResolver {
public:
    virtual ~IFontResolver() = default;

    struct Request {
        QString family;
        int     weight = 400;
        bool    italic = false;
        bool    serif = false;
        bool    fixedPitch = false;
        bool    symbolic = false;
        QString languageHint;   // BCP-47; decides Han disambiguation, which is not cosmetic
    };

    virtual Result<FontMatch> resolve(const Request&) const = 0;
    virtual Result<QStringList> installedFamilies() const = 0;
};

// =======================================================================================
// 8. IAcceleratedSurface -- presenting rendered tiles (Stage 2)
// =======================================================================================
class IAcceleratedSurface {
public:
    virtual ~IAcceleratedSurface() = default;

    // False means every caller falls back to software presentation. Not an error: a remote desktop
    // session, a stripped VM, or a driver reset are all normal, and a viewer that refuses to draw
    // because there is no GPU is a viewer nobody can use over RDP.
    virtual bool isAvailable() const = 0;

    // The window is identified by its native handle as an opaque integer, so this header stays free
    // of both `HWND` and `QWidget`.
    virtual Status attach(std::uintptr_t nativeWindowHandle) = 0;
    virtual void   detach() = 0;

    virtual Status present(const QByteArray& bgraPixels, int width, int height, int stride) = 0;
};

// =======================================================================================
// 9. IRichClipboard -- flavours beyond QClipboard's text and image (Stage 5)
// =======================================================================================
class IRichClipboard {
public:
    virtual ~IRichClipboard() = default;

    // A vector metafile, so a snapshot pasted into a word processor stays sharp when scaled.
    virtual Status setVectorMetafile(const QByteArray& emf) = 0;
    virtual Status setHtml(const QString& html, const QString& plainTextFallback) = 0;
    virtual Result<QByteArray> vectorMetafile() const = 0;
    virtual Result<QString>    html() const = 0;
};

// =======================================================================================
// 10. ISingleInstanceBus -- instance claim, file handoff, and foreground activation (Stage 1)
// =======================================================================================
// WHY THIS IS AN INTERFACE AND NOT A `QLocalServer` (D1-11): Windows refuses foreground activation
// to a process that has received no input, so a portable implementation forwards the file
// successfully and then FAILS TO RAISE THE WINDOW -- the visible symptom being "I double-clicked a
// PDF and nothing happened". The permission has to be granted by the process that *did* receive the
// input, before it exits, and that handshake is the whole reason this is OS code.
class ISingleInstanceBus {
public:
    virtual ~ISingleInstanceBus() = default;

    // Files forwarded by a secondary instance. See `listen()` for which thread this arrives on.
    using OpenFilesHandler = std::function<void(const QStringList& files)>;

    // Tries to become the primary instance for `scopeId`. True means we are it.
    //
    // `scopeId` exists so a test, a portable install and a shipped install do not fight over one
    // name: two builds of the same product on one desktop are a development reality, not an edge
    // case.
    virtual Result<bool> claim(const QString& scopeId) = 0;

    virtual bool isPrimary() const = 0;

    // Primary side. Starts accepting handoffs.
    virtual Status listen(OpenFilesHandler) = 0;

    // Secondary side. Sends the file list to the primary AND grants it the right to take the
    // foreground. Both halves, always: the grant is worthless after this process exits, and the
    // handoff without it is the failure this interface exists to prevent.
    virtual Status forward(const QStringList& files) = 0;

    // Stops listening and releases the claim. Idempotent, and safe to call on a bus that never
    // claimed anything.
    virtual void release() = 0;
};

// =======================================================================================
// 11. IOfficeBridge -- COM automation of the installed office suite (Stages 8, 12, 16)
// =======================================================================================
class IOfficeBridge {
public:
    virtual ~IOfficeBridge() = default;

    enum class Application { Word, Excel, PowerPoint, Visio };

    virtual bool isAvailable(Application) const = 0;
    virtual Result<QString> convertToPdf(const QString& sourcePath, Application) = 0;
    virtual Result<QString> convertFromPdf(const QString& pdfPath, Application,
                                           const QString& targetPath) = 0;
};

// =======================================================================================
// 12. IBrowserBridge -- the native-messaging host registration (Stage 16)
// =======================================================================================
class IBrowserBridge {
public:
    virtual ~IBrowserBridge() = default;

    enum class Browser { Chrome, Edge, Firefox };

    virtual bool   isRegistered(Browser) const = 0;
    virtual Status registerHost(Browser) = 0;
    virtual Status unregisterHost(Browser) = 0;
};

// =======================================================================================
// 13. IRightsManagement -- enterprise information-protection labels (Stage 16)
// =======================================================================================
class IRightsManagement {
public:
    virtual ~IRightsManagement() = default;

    struct Label {
        QString id;
        QString name;
        bool    encrypts = false;
    };

    virtual bool isAvailable() const = 0;
    virtual Result<std::vector<Label>> labels() const = 0;
    virtual Result<Label> labelOf(const QString& filePath) const = 0;
    virtual Status apply(const QString& filePath, const QString& labelId) = 0;
};

// =======================================================================================
// 14. IProcessSandbox -- a restricted child process for protected view (Stage 10)
// =======================================================================================
class IProcessSandbox {
public:
    virtual ~IProcessSandbox() = default;

    struct Limits {
        bool denyNetwork = true;
        bool denyFilesystemWrite = true;
        int  memoryCapMb = 512;
        int  wallClockSeconds = 60;
    };

    virtual bool isAvailable() const = 0;
    virtual Result<int> runRestricted(const QString& executable, const QStringList& arguments,
                                      const Limits&) = 0;
};

// =======================================================================================
// 15. IPowerSession -- sleep, battery, and session lock (Stage 2)
// =======================================================================================
class IPowerSession {
public:
    virtual ~IPowerSession() = default;

    enum class Event { Suspending, Resumed, OnBattery, OnMains, SessionLocked, SessionUnlocked };

    using EventHandler = std::function<void(Event)>;

    virtual bool isOnBattery() const = 0;
    virtual Status subscribe(EventHandler) = 0;

    // Keeps the display awake during a presentation, and only then. A viewer that holds this
    // permanently is a laptop that never sleeps, which is a support ticket about battery life.
    virtual Status setPresentationModeActive(bool) = 0;
};

// =======================================================================================
// 16. IDisplayTopology -- monitors, per-monitor DPI, and change notification (Stage 2)
// =======================================================================================
class IDisplayTopology {
public:
    virtual ~IDisplayTopology() = default;

    using ChangedHandler = std::function<void()>;

    virtual Result<std::vector<MonitorInfo>> monitors() const = 0;
    virtual Status subscribe(ChangedHandler) = 0;
};

// =======================================================================================
// 17. IPrinting -- printer capabilities beyond QPrinter (Stage 8)
// =======================================================================================
// Added at Stage 1 T7. §4.3's gap table did not name it, but `platform/win/WinPrinting` has a row
// in [`Architecture-FileMap.md` §3.12](../claude-plan/Architecture-FileMap.md#312-platform--the-os-abstraction)
// and `platform/win/` may hold nothing that does not implement a declared interface -- so either
// this exists or that file is a layering violation on the day it is written. Declaring it now is
// what T7 is for; §4.3 has been given its row.
//
// Ordinary printing stays with `QPrinter` and is NOT behind this interface. What Qt cannot express
// is tray selection by name, the printer's real duplex and colour capabilities, and submitting a
// job with a page range the driver understands rather than one we rasterize ourselves.
class IPrinting {
public:
    virtual ~IPrinting() = default;

    struct JobOptions {
        QString printerName;
        QString trayName;
        bool    duplex = false;
        bool    color = true;
        int     copies = 1;
    };

    virtual Result<std::vector<PrinterInfo>> printers() const = 0;
    virtual Result<int> submit(const QString& filePath, const JobOptions&) = 0;
};

// =======================================================================================
// The bundle
// =======================================================================================
// Owning, and NULL for anything this build does not implement. A null member is the honest state:
// a stub that silently succeeds would let a caller believe a signature was created or a scan
// happened, and that failure mode surfaces stages later as data that was never produced.
//
// The order matches the numbering above, which matches §4.3's table, so a reader can check the two
// against each other without a search.
struct Platform {
    std::unique_ptr<IShellIntegration>     shellIntegration;
    std::unique_ptr<IPrinterDriverService> printerDriverService;
    std::unique_ptr<IScanner>              scanner;
    std::unique_ptr<ICertificateStore>     certificateStore;
    std::unique_ptr<ICryptoProvider>       cryptoProvider;
    std::unique_ptr<ISecretStore>          secretStore;
    std::unique_ptr<IFontResolver>         fontResolver;
    std::unique_ptr<IAcceleratedSurface>   acceleratedSurface;
    std::unique_ptr<IRichClipboard>        richClipboard;
    std::unique_ptr<ISingleInstanceBus>    singleInstanceBus;
    std::unique_ptr<IOfficeBridge>         officeBridge;
    std::unique_ptr<IBrowserBridge>        browserBridge;
    std::unique_ptr<IRightsManagement>     rightsManagement;
    std::unique_ptr<IProcessSandbox>       processSandbox;
    std::unique_ptr<IPowerSession>         powerSession;
    std::unique_ptr<IDisplayTopology>      displayTopology;
    std::unique_ptr<IPrinting>             printing;
};

// Every interface this boundary declares, in the order of the `Platform` members. The census is
// what makes "declared but not implemented" a countable, printable fact rather than a thing you
// learn by dereferencing null.
inline constexpr std::size_t kPlatformInterfaceCount = 17;

inline const std::array<std::string_view, kPlatformInterfaceCount>& platformInterfaceNames()
{
    static constexpr std::array<std::string_view, kPlatformInterfaceCount> kNames{
        "IShellIntegration", "IPrinterDriverService", "IScanner", "ICertificateStore",
        "ICryptoProvider", "ISecretStore", "IFontResolver", "IAcceleratedSurface",
        "IRichClipboard", "ISingleInstanceBus", "IOfficeBridge", "IBrowserBridge",
        "IRightsManagement", "IProcessSandbox", "IPowerSession", "IDisplayTopology",
        "IPrinting",
    };
    return kNames;
}

// Which slots the bundle leaves null, as booleans in the same order as the names above. Written as
// one list rather than seventeen `if`s at each call site, and kept beside the names so that adding
// an interface without extending both is a size mismatch the compiler reports.
inline std::array<bool, kPlatformInterfaceCount> platformInterfacePresence(const Platform& platform)
{
    return {
        platform.shellIntegration != nullptr,     platform.printerDriverService != nullptr,
        platform.scanner != nullptr,              platform.certificateStore != nullptr,
        platform.cryptoProvider != nullptr,       platform.secretStore != nullptr,
        platform.fontResolver != nullptr,         platform.acceleratedSurface != nullptr,
        platform.richClipboard != nullptr,        platform.singleInstanceBus != nullptr,
        platform.officeBridge != nullptr,         platform.browserBridge != nullptr,
        platform.rightsManagement != nullptr,     platform.processSandbox != nullptr,
        platform.powerSession != nullptr,         platform.displayTopology != nullptr,
        platform.printing != nullptr,
    };
}

// The names of the interfaces `platform` leaves null. Empty means every interface is implemented,
// which will not be true until Stage 16.
inline std::vector<std::string_view> unimplementedInterfaces(const Platform& platform)
{
    const std::array<bool, kPlatformInterfaceCount> present = platformInterfacePresence(platform);
    std::vector<std::string_view> missing;
    for (std::size_t i = 0; i < kPlatformInterfaceCount; ++i) {
        if (!present[i]) missing.push_back(platformInterfaceNames()[i]);
    }
    return missing;
}

// THE ONLY FUNCTION THIS LAYER EXPORTS. `app/` is the sole caller -- `tools/layer-lint` does not
// enforce that today, but the composition-root rule in CLAUDE.md does, and a second caller means
// something outside the composition root is choosing an OS implementation.
std::unique_ptr<Platform> createPlatform();

} // namespace rawpage::platform

#endif // RAWPAGE_PLATFORM_PLATFORM_H
