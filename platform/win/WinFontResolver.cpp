#include "platform/win/WinFontResolver.h"

#include "core/Log.h"
#include "core/render/FontSubstitution.h"

#include <windows.h>
#include <dwrite.h>

#include <algorithm>
#include <string>

namespace rawpage::platform::win {
namespace {

using core::LogSubsystem;

// A COM pointer small enough to read in one glance and with no dependency beyond <windows.h>.
// `<wrl/client.h>` would do, and is not guaranteed present in a MinGW SDK -- and the toolchain pin
// (TA-1..TA-9) exists to keep the build from depending on which SDK a machine happens to carry.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { reset(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

    ComPtr& operator=(ComPtr&& other) noexcept
    {
        if (this != &other) {
            reset();
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
        }
        return *this;
    }

    T*  get() const noexcept { return ptr_; }
    T*  operator->() const noexcept { return ptr_; }
    T** put() noexcept { reset(); return &ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept
    {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

private:
    T* ptr_ = nullptr;
};

QString toQString(const std::wstring& text)
{
    return QString::fromWCharArray(text.data(), static_cast<qsizetype>(text.size()));
}

// The family's own name, in the locale we can actually compare against a PDF's /BaseFont.
//
// DirectWrite returns a LOCALIZED string set: on a Traditional-Chinese Windows, "Microsoft
// JhengHei" is also present as 微軟正黑體, and which one comes back first is a property of the
// user's language settings. A PDF names its fonts in the Latin/English form virtually without
// exception, so en-us is asked for first and index 0 is only the fallback. Getting this wrong
// produces a resolver that works on an English machine and silently stops matching on a Chinese
// one -- the exact class of defect this project's DPI work kept finding.
QString primaryName(IDWriteLocalizedStrings* names)
{
    if (names == nullptr || names->GetCount() == 0) return {};

    UINT32 index = 0;
    BOOL   exists = FALSE;
    names->FindLocaleName(L"en-us", &index, &exists);
    if (exists == FALSE) index = 0;

    UINT32 length = 0;
    if (FAILED(names->GetStringLength(index, &length))) return {};

    std::wstring buffer(static_cast<size_t>(length) + 1, L'\0');
    if (FAILED(names->GetString(index, buffer.data(), length + 1))) return {};
    buffer.resize(length);
    return toQString(buffer);
}

DWRITE_FONT_WEIGHT weightFor(int requested)
{
    // Clamped rather than mapped through a table: DirectWrite's enum is the same 1..999 scale PDF's
    // /FontWeight uses, and the collection matcher already picks the nearest available weight.
    const int clamped = std::clamp(requested, 1, 999);
    return static_cast<DWRITE_FONT_WEIGHT>(clamped);
}

} // namespace

// ===============================================================================================

struct WinFontResolver::Impl {
    ComPtr<IDWriteFactory>        factory;
    ComPtr<IDWriteFontCollection> collection;

    bool ensureFactory()
    {
        if (factory) return true;

        const HRESULT hr = ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                                 __uuidof(IDWriteFactory),
                                                 reinterpret_cast<IUnknown**>(factory.put()));
        if (FAILED(hr)) {
            RAWPAGE_LOG_ERROR(LogSubsystem::Render)
                << "font: DWriteCreateFactory failed -- hresult"
                << QString::number(static_cast<qulonglong>(hr), 16);
            return false;
        }
        return true;
    }

    // `checkForUpdates` is TRUE on purpose. A font installed while the application is running is
    // the ordinary case for anyone fixing a document -- they hit a missing face, install it, and
    // reopen the file -- and a collection cached at start-up would keep substituting for the rest
    // of the session with no way to tell the user why. The refresh is cheap when nothing changed.
    bool ensureCollection()
    {
        if (!ensureFactory()) return false;

        ComPtr<IDWriteFontCollection> fresh;
        const HRESULT hr = factory->GetSystemFontCollection(fresh.put(), TRUE);
        if (FAILED(hr) || !fresh) {
            RAWPAGE_LOG_ERROR(LogSubsystem::Render)
                << "font: GetSystemFontCollection failed -- hresult"
                << QString::number(static_cast<qulonglong>(hr), 16);
            return false;
        }
        collection = std::move(fresh);
        return true;
    }

    // Looks one family up and, if it is here, describes the face the request asks for. Returns
    // false for "not installed", which is not an error -- walking a candidate list means most
    // lookups miss.
    bool describe(const QString& family, const IFontResolver::Request& request, FontMatch& into)
    {
        UINT32 index = 0;
        BOOL   exists = FALSE;

        const std::wstring wanted = family.toStdWString();
        if (FAILED(collection->FindFamilyName(wanted.c_str(), &index, &exists)) || exists == FALSE) {
            return false;
        }

        ComPtr<IDWriteFontFamily> fontFamily;
        if (FAILED(collection->GetFontFamily(index, fontFamily.put()))) return false;

        ComPtr<IDWriteFont> font;
        if (FAILED(fontFamily->GetFirstMatchingFont(
                weightFor(request.weight), DWRITE_FONT_STRETCH_NORMAL,
                request.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                font.put()))) {
            return false;
        }

        ComPtr<IDWriteFontFace> face;
        if (FAILED(font->CreateFontFace(face.put()))) return false;

        // The family name is read back from the collection rather than echoed from the request,
        // because they differ in case and in spacing -- "times new roman" resolves and comes back
        // as "Times New Roman", and the name is what a report shows a person.
        ComPtr<IDWriteLocalizedStrings> names;
        if (SUCCEEDED(fontFamily->GetFamilyNames(names.put()))) {
            into.family = primaryName(names.get());
        }
        if (into.family.isEmpty()) into.family = family;

        into.faceIndex = static_cast<int>(face->GetIndex());
        into.filePath  = filePathOf(face.get());
        return true;
    }

    // Empty when the face is not backed by a file we may read -- a memory-loaded face, or a remote
    // loader. `FontMatch::filePath` documents that value, and it is a real state rather than a
    // failure: DirectWrite composes fonts that have no path at all.
    static QString filePathOf(IDWriteFontFace* face)
    {
        UINT32 fileCount = 0;
        if (FAILED(face->GetFiles(&fileCount, nullptr)) || fileCount == 0) return {};

        // One file is the whole of what a caller can use: a face spread over several files cannot
        // be handed to a rasterizer as one buffer, so reporting the first would be a lie rather
        // than an approximation.
        if (fileCount != 1) return {};

        ComPtr<IDWriteFontFile> file;
        if (FAILED(face->GetFiles(&fileCount, file.put())) || !file) return {};

        const void* key = nullptr;
        UINT32      keySize = 0;
        if (FAILED(file->GetReferenceKey(&key, &keySize))) return {};

        ComPtr<IDWriteFontFileLoader> loader;
        if (FAILED(file->GetLoader(loader.put())) || !loader) return {};

        ComPtr<IDWriteLocalFontFileLoader> local;
        if (FAILED(loader->QueryInterface(__uuidof(IDWriteLocalFontFileLoader),
                                          reinterpret_cast<void**>(local.put())))) {
            return {};
        }

        UINT32 length = 0;
        if (FAILED(local->GetFilePathLengthFromKey(key, keySize, &length))) return {};

        std::wstring path(static_cast<size_t>(length) + 1, L'\0');
        if (FAILED(local->GetFilePathFromKey(key, keySize, path.data(), length + 1))) return {};
        path.resize(length);
        return toQString(path);
    }
};

WinFontResolver::WinFontResolver() : impl_(std::make_unique<Impl>()) {}

WinFontResolver::~WinFontResolver() = default;

Result<FontMatch> WinFontResolver::resolve(const Request& request) const
{
    const std::lock_guard<std::mutex> guard(mutex_);

    if (!impl_->ensureCollection()) {
        return core::Error{core::ErrorCode::Unavailable,
                           QLatin1String("error.font.collectionUnavailable")};
    }

    FontMatch match;

    // The requested family first, and it is the ONLY lookup that may report `exact`. Normalizing
    // the name is `core/`'s job -- a /BaseFont carries a subset tag and PostScript suffixes that no
    // font database has ever heard of -- and doing it here would put that judgement in the layer
    // that is not allowed to make one.
    if (!request.family.isEmpty() && impl_->describe(request.family, request, match)) {
        match.exact = true;
        return match;
    }

    const QStringList candidates = core::render::substitutionCandidates(request);
    for (const QString& candidate : candidates) {
        FontMatch substitute;
        if (!impl_->describe(candidate, request, substitute)) continue;

        substitute.exact = false;
        RAWPAGE_LOG_INFO(LogSubsystem::Render)
            << "font: substituted" << substitute.family << "for" << request.family
            << "-- file" << (substitute.filePath.isEmpty() ? QString(QLatin1String("<none>"))
                                                           : substitute.filePath);
        return substitute;
    }

    // Every candidate absent means the last resort is absent, which on Windows means Arial is not
    // installed. Reported rather than swallowed: the caller's fallback is the engine's own internal
    // face, and "we chose nothing" and "we chose the engine's default" are different facts.
    RAWPAGE_LOG_WARNING(LogSubsystem::Render)
        << "font: no substitute installed for" << request.family
        << "-- tried" << static_cast<int>(candidates.size()) << "candidates:"
        << candidates.join(QLatin1String(", "));
    return core::Error{core::ErrorCode::NotFound, QLatin1String("error.font.noSubstitute"),
                       request.family};
}

Result<QStringList> WinFontResolver::installedFamilies() const
{
    const std::lock_guard<std::mutex> guard(mutex_);

    if (!impl_->ensureCollection()) {
        return core::Error{core::ErrorCode::Unavailable,
                           QLatin1String("error.font.collectionUnavailable")};
    }

    QStringList families;
    const UINT32 count = impl_->collection->GetFontFamilyCount();
    families.reserve(static_cast<qsizetype>(count));

    for (UINT32 i = 0; i < count; ++i) {
        ComPtr<IDWriteFontFamily> family;
        if (FAILED(impl_->collection->GetFontFamily(i, family.put()))) continue;

        ComPtr<IDWriteLocalizedStrings> names;
        if (FAILED(family->GetFamilyNames(names.put()))) continue;

        const QString name = primaryName(names.get());
        if (!name.isEmpty()) families.append(name);
    }

    families.sort(Qt::CaseInsensitive);
    return families;
}

} // namespace rawpage::platform::win
