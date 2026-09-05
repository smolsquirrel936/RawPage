#include "tests/golden/GoldenSuite.h"

#include "core/doc/PageTree.h"
#include "core/doc/Revision.h"
#include "core/render/DpiPolicy.h"
#include "core/render/TileGrid.h"
#include "pdf/render/PdfiumDocument.h"
#include "pdf/render/PdfiumInit.h"
#include "pdf/render/PdfiumRasterizer.h"
#include "platform/win/WinFontResolver.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rawpage::golden {
namespace {

using core::RasterImage;
using core::render::RenderFlags;
using core::render::RenderRequest;
using core::render::Resolution;
using core::render::TileGrid;
using core::render::TileRect;

// THE GOLDEN FLAG SET, stated in one place because it is part of what a baseline means.
//
// `Annotations` because that is what `ui/viewer/PageView` draws by default, so a baseline is about
// the product rather than about a subset of it. `NoNativeText` because `RenderRequest.h` has said
// since T9 that it is required for reproducible goldens: without it PDFium may hand text to the
// OS renderer, and the baseline becomes a claim about this machine's ClearType and gamma settings.
//
// Antialiasing stays ON -- the product's default -- and is therefore part of what is being pinned.
// A baseline captured with smoothing off would be quieter and would stop testing the code path
// every user sees.
constexpr RenderFlags kGoldenFlags = RenderFlags::Annotations | RenderFlags::NoNativeText;

QString jsonString(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString();
}

std::vector<int> jsonIntArray(const QJsonValue& value)
{
    std::vector<int> out;
    for (const QJsonValue& entry : value.toArray()) {
        if (entry.isDouble()) out.push_back(entry.toInt());
    }
    return out;
}

// The BGRA byte at (x, y). `bytesPerLine` rather than `width * 4`, because a rasterizer may pad its
// rows and a reader that assumes otherwise shears the image into diagonal garbage -- the same trap
// `ui/viewer/TilePresenter` names on the other side of the boundary.
inline const unsigned char* pixelAt(const RasterImage& image, int x, int y)
{
    return reinterpret_cast<const unsigned char*>(image.pixels.constData())
           + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.bytesPerLine)
           + static_cast<std::size_t>(x) * 4u;
}

// Whether every pixel is a shade of grey -- checked exactly, over the pixels, because "this
// document looked monochrome" is not a fact about a raster. `Format_Grayscale8` is only lossless
// when it is true of every pixel, and a page with one coloured rule in a margin is not.
bool isGrayscale(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        const auto* row = reinterpret_cast<const unsigned char*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const unsigned char* pixel = row + x * 4;
            if (pixel[0] != pixel[1] || pixel[1] != pixel[2]) return false;
        }
    }
    return true;
}

} // namespace

// ===========================================================================================
// Manifest
// ===========================================================================================

int Manifest::imageCount() const
{
    int total = 0;
    for (const Case& item : cases)
        total += static_cast<int>(item.pages.size() * item.dpi.size());
    return total;
}

const Case* Manifest::find(const QString& file) const
{
    const auto at = std::find_if(cases.begin(), cases.end(),
                                 [&file](const Case& item) { return item.file == file; });
    return at == cases.end() ? nullptr : &*at;
}

Manifest loadManifest(const QString& path)
{
    Manifest manifest;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        manifest.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return manifest;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        manifest.error = QStringLiteral("%1: %2 at offset %3")
                             .arg(path, parseError.errorString())
                             .arg(parseError.offset);
        return manifest;
    }
    if (!document.isObject()) {
        manifest.error = QStringLiteral("%1: the top level is not an object").arg(path);
        return manifest;
    }

    const QJsonObject root = document.object();
    manifest.formatVersion = root.value(QLatin1String("formatVersion")).toInt();
    manifest.defaultDpi = jsonIntArray(root.value(QLatin1String("dpi")));

    const QJsonObject thresholds = root.value(QLatin1String("thresholds")).toObject();
    if (!thresholds.isEmpty()) {
        manifest.thresholds.channelTolerance =
            thresholds.value(QLatin1String("channelTolerance")).toInt(8);
        manifest.thresholds.differingFraction =
            thresholds.value(QLatin1String("differingFraction")).toDouble(0.0010);
        manifest.thresholds.grossChannelTolerance =
            thresholds.value(QLatin1String("grossChannelTolerance")).toInt(96);
        manifest.thresholds.grossFraction =
            thresholds.value(QLatin1String("grossFraction")).toDouble(0.0001);
    }

    for (const QJsonValue& entry : root.value(QLatin1String("cases")).toArray()) {
        const QJsonObject object = entry.toObject();
        Case item;
        item.file = jsonString(object, "file");
        item.pages = jsonIntArray(object.value(QLatin1String("pages")));
        item.dpi = jsonIntArray(object.value(QLatin1String("dpi")));
        if (item.dpi.empty()) item.dpi = manifest.defaultDpi;
        item.password = jsonString(object, "password");
        item.machineDependent = object.value(QLatin1String("machineDependent")).toBool(false);
        item.note = jsonString(object, "note");

        if (item.file.isEmpty() || item.pages.empty() || item.dpi.empty()) {
            manifest.error =
                QStringLiteral("%1: a case is missing its file, its pages or its dpi (%2)")
                    .arg(path, item.file.isEmpty() ? QStringLiteral("<unnamed>") : item.file);
            return manifest;
        }
        manifest.cases.push_back(std::move(item));
    }

    for (const QJsonValue& entry : root.value(QLatin1String("excluded")).toArray()) {
        const QJsonObject object = entry.toObject();
        Exclusion exclusion;
        exclusion.file = jsonString(object, "file");
        exclusion.reason = jsonString(object, "reason");
        // AN EXCLUSION WITHOUT A REASON IS A SILENT GAP, which is the whole point of listing them:
        // `corpus/PROVENANCE.md`'s rule, that a file with no row is a finding rather than a
        // default.
        if (exclusion.file.isEmpty() || exclusion.reason.isEmpty()) {
            manifest.error = QStringLiteral("%1: an exclusion has no file or no reason").arg(path);
            return manifest;
        }
        manifest.excluded.push_back(std::move(exclusion));
    }

    if (manifest.cases.empty()) {
        // Zero cases is a passing run over nothing, which is exactly the shape guide case A-27
        // refuses for the fuzz targets. Refuse it here for the same reason.
        manifest.error = QStringLiteral("%1: no cases declared").arg(path);
    }
    return manifest;
}

QString baselineRelativePath(const Case& item, int page, int dpi)
{
    QString stem = item.file;
    if (stem.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) stem.chop(4);
    return QStringLiteral("%1/%2/p%3.png")
        .arg(stem)
        .arg(dpi, 3, 10, QLatin1Char('0'))
        .arg(page, 3, 10, QLatin1Char('0'));
}

// ===========================================================================================
// The engine session
// ===========================================================================================

struct EngineSession::Impl {
    platform::win::WinFontResolver resolver;
    bool                           installed = false;
    QString                        error;
};

EngineSession::EngineSession()
    : impl_(std::make_unique<Impl>())
{
    const core::Status ready = pdf::render::ensure();
    if (!ready) {
        impl_->error = QStringLiteral("PDFium did not initialize (%1)")
                           .arg(QString(ready.error().messageKey));
        return;
    }

    pdf::render::FontResolverBridge::resetLog();
    const core::Status installed = pdf::render::FontResolverBridge::install(impl_->resolver);
    if (!installed) {
        impl_->error = QStringLiteral("the font bridge did not install (%1)")
                           .arg(QString(installed.error().messageKey));
        return;
    }
    impl_->installed = true;
}

EngineSession::~EngineSession()
{
    if (impl_->installed) pdf::render::FontResolverBridge::uninstall();
}

bool EngineSession::isReady() const { return impl_->installed; }

const QString& EngineSession::error() const { return impl_->error; }

// ===========================================================================================
// Rendering
// ===========================================================================================

namespace {

core::PasswordCallback passwordCallback(const QString& password)
{
    if (password.isEmpty()) return nullptr;
    // The corpus passwords are fixed and public (`corpus/PROVENANCE.md`): a fixture whose password
    // is a secret is a fixture nobody can use. Answering only the FIRST attempt means a wrong
    // password here fails rather than looping.
    return [password](int attempt) -> std::optional<QString> {
        if (attempt > 1) return std::nullopt;
        return password;
    };
}

// The resolution a golden render asks for. Built directly rather than through `DpiPolicy::resolve`,
// and that is deliberate: `resolve()` answers *what DPI does this zoom on this monitor need*, and a
// golden baseline is not a zoom on a monitor -- it is a fixed resolution stated in the manifest. The
// cap `DpiPolicy::Limits::maxRenderDpi` guards (2400) is two orders above anything here.
Resolution resolutionFor(int dpi)
{
    Resolution resolution;
    resolution.dpi = static_cast<double>(dpi);
    resolution.devicePixelRatio = 1.0;
    resolution.upscale = 1.0;
    return resolution;
}

struct OpenedPage {
    std::unique_ptr<pdf::render::PdfiumDocument> document;
    const core::Page*                            page = nullptr;
    TileRect                                     bounds;
    QString                                      error;
};

OpenedPage openPage(const QString& corpusRoot, const Case& item, int page, int dpi)
{
    OpenedPage opened;

    const QString path = QDir(corpusRoot).filePath(item.file);
    auto document = pdf::render::PdfiumDocument::open(path, passwordCallback(item.password));
    if (!document) {
        opened.error = QStringLiteral("%1 did not open (%2)")
                           .arg(item.file, QString(document.error().messageKey));
        return opened;
    }
    opened.document = std::move(document).value();

    opened.page = opened.document->pages().at(page);
    if (opened.page == nullptr) {
        opened.error = QStringLiteral("%1 has no page %2 (it has %3)")
                           .arg(item.file)
                           .arg(page)
                           .arg(opened.document->pageCount());
        return opened;
    }

    opened.bounds = TileGrid::pageBounds(*opened.page, resolutionFor(dpi));
    if (opened.bounds.isEmpty()) {
        opened.error =
            QStringLiteral("%1 page %2 has an empty page rect at %3 dpi").arg(item.file).arg(page).arg(dpi);
    }
    return opened;
}

RenderRequest goldenRequest(int page, TileRect tile, int dpi, RenderFlags extraFlags)
{
    RenderRequest request;
    request.document = core::DocumentHandle{1};
    request.pageIndex = page;
    request.tile = tile;
    request.deviceScale = resolutionFor(dpi).rasterScale();
    request.revision = core::Revision::initial();
    request.flags = kGoldenFlags | extraFlags;
    return request;
}

} // namespace

RenderOutcome renderPage(const QString& corpusRoot, const Case& item, int page, int dpi,
                         RenderFlags extraFlags)
{
    RenderOutcome outcome;

    // SAMPLED BEFORE THE DOCUMENT IS OPENED, and the first version of this file sampled it after.
    // PDFium resolves some fonts while OPENING -- `ordinary/font-not-embedded.pdf`'s two absent
    // families are among them -- so a mark taken after `open()` returned put those rows on the
    // wrong side of the window and the capture recorded *no substitutions at all* for the one file
    // in the corpus whose whole purpose is to have them. The symptom was silent: the baseline was
    // still correct, and the machine-dependence guard it feeds was simply never armed.
    const std::size_t before = pdf::render::FontResolverBridge::substitutions().size();

    OpenedPage opened = openPage(corpusRoot, item, page, dpi);
    if (!opened.error.isEmpty()) {
        outcome.error = opened.error;
        return outcome;
    }

    pdf::render::PdfiumRasterizer rasterizer(*opened.document);
    core::Result<RasterImage> raster =
        rasterizer.raster(goldenRequest(page, opened.bounds, dpi, extraFlags));
    if (!raster) {
        outcome.error = QStringLiteral("%1 page %2 at %3 dpi did not rasterize (%4)")
                            .arg(item.file)
                            .arg(page)
                            .arg(dpi)
                            .arg(QString(raster.error().messageKey));
        return outcome;
    }

    outcome.image = std::move(raster).value();
    outcome.ok = true;

    // Only the rows THIS page produced. The bridge's log is process-wide and append-only, so a
    // caller that took all of it would attribute every earlier page's substitutions to this one.
    const auto all = pdf::render::FontResolverBridge::substitutions();
    for (std::size_t at = before; at < all.size(); ++at) outcome.substitutions.push_back(all[at]);
    return outcome;
}

int pageCount(const QString& corpusRoot, const Case& item)
{
    const QString path = QDir(corpusRoot).filePath(item.file);
    auto document = pdf::render::PdfiumDocument::open(path, passwordCallback(item.password));
    return document ? document.value()->pageCount() : -1;
}

RenderOutcome renderPageFromTiles(const QString& corpusRoot, const Case& item, int page, int dpi)
{
    RenderOutcome outcome;

    OpenedPage opened = openPage(corpusRoot, item, page, dpi);
    if (!opened.error.isEmpty()) {
        outcome.error = opened.error;
        return outcome;
    }

    // The whole page as the viewport, so `tilesFor` returns every tile the page covers.
    const TileGrid grid;
    const core::render::TileSet tiles =
        grid.tilesFor(*opened.page, resolutionFor(dpi), opened.bounds);
    if (tiles.visible.empty()) {
        outcome.error = QStringLiteral("%1 page %2 produced no tiles at %3 dpi")
                            .arg(item.file).arg(page).arg(dpi);
        return outcome;
    }

    RasterImage composed;
    composed.width = opened.bounds.width;
    composed.height = opened.bounds.height;
    composed.bytesPerLine = opened.bounds.width * 4;
    composed.format = core::PixelFormat::Bgrx8888;   // corrected from the first tile below
    composed.pixels = QByteArray(static_cast<qsizetype>(composed.bytesPerLine) * composed.height,
                                 char(0));

    pdf::render::PdfiumRasterizer rasterizer(*opened.document);
    bool formatTaken = false;
    for (const TileRect& tile : tiles.visible) {
        core::Result<RasterImage> raster =
            rasterizer.raster(goldenRequest(page, tile, dpi, RenderFlags::None));
        if (!raster) {
            outcome.error = QStringLiteral("%1 page %2 tile (%3,%4) did not rasterize (%5)")
                                .arg(item.file).arg(page).arg(tile.x).arg(tile.y)
                                .arg(QString(raster.error().messageKey));
            return outcome;
        }
        const RasterImage& piece = raster.value();
        if (!formatTaken) {
            composed.format = piece.format;
            formatTaken = true;
        }
        for (int y = 0; y < piece.height; ++y) {
            const int targetY = tile.y + y;
            if (targetY < 0 || targetY >= composed.height) continue;
            const int copyWidth = std::min(piece.width, composed.width - tile.x);
            if (copyWidth <= 0) continue;
            std::memcpy(composed.pixels.data()
                            + static_cast<std::size_t>(targetY) * composed.bytesPerLine
                            + static_cast<std::size_t>(tile.x) * 4u,
                        pixelAt(piece, 0, y), static_cast<std::size_t>(copyWidth) * 4u);
        }
    }

    outcome.image = std::move(composed);
    outcome.ok = true;
    return outcome;
}

// ===========================================================================================
// PNG
// ===========================================================================================

bool writePng(const QString& path, const RasterImage& image, QString* error)
{
    if (!image.isValid()) {
        if (error) *error = QStringLiteral("refusing to write an invalid raster to %1").arg(path);
        return false;
    }

    const QDir directory = QFileInfo(path).absoluteDir();
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        if (error) *error = QStringLiteral("cannot create %1").arg(directory.absolutePath());
        return false;
    }

    // The SAME format mapping `ui/viewer/TilePresenter` uses, and for the same reason: `Bgrx8888`
    // has no alpha channel -- its fourth byte is padding PDFium never filled -- so wrapping it as
    // `ARGB32` would write a PNG whose alpha is whatever that padding happened to be, and every
    // baseline would open as an invisible page in an image viewer.
    const QImage::Format wrapFormat = image.format == core::PixelFormat::Bgra8888
                                          ? QImage::Format_ARGB32
                                          : QImage::Format_RGB32;

    // Wrapped without a copy, then DEEP-COPIED before saving: `QImage` over borrowed bytes is only
    // valid while those bytes live, and `save()` on a shallow image whose buffer has moved is the
    // kind of failure that shows up as one corrupt baseline in a hundred.
    const QImage wrapped(reinterpret_cast<const uchar*>(image.pixels.constData()), image.width,
                         image.height, image.bytesPerLine, wrapFormat);
    QImage encodable = wrapped.copy();

    // GREY PAGES ARE STORED AS EIGHT BITS, and this is a size decision made in the one place it can
    // be made LOSSLESSLY. 171 of the 198 baselines this suite captured are black ink on white
    // paper -- every pixel R == G == B -- and storing them as 32-bit colour costs four bytes to say
    // the same thing three more times. Detected exactly rather than assumed: one pass over the
    // pixels, and a single coloured pixel anywhere puts the whole page back on the colour path.
    //
    // It is lossless in both directions -- `readPng` expands to RGB32 and the comparison reads the
    // three colour bytes -- which is what makes it a storage choice rather than a fidelity one.
    if (isGrayscale(encodable)) encodable = encodable.convertToFormat(QImage::Format_Grayscale8);

    // Qt's DEFAULT PNG compression, and the setting was tried and REVERTED rather than left out.
    // `QImageWriter::setCompression()` is documented as format-defined, and for PNG Qt reads it as a
    // ratio on its own scale rather than as a zlib level: passing 9 -- which reads like "maximum
    // zlib effort" and is what the first version of this file did -- took the committed tree from
    // 30 MB to **516 MB**. Measured, not reasoned about. The default is what the numbers below are.
    QImageWriter writer(path, "PNG");
    if (!writer.write(encodable)) {
        if (error)
            *error = QStringLiteral("cannot write %1: %2").arg(path, writer.errorString());
        return false;
    }
    return true;
}

bool readPng(const QString& path, RasterImage* out, QString* error)
{
    QImage loaded;
    if (!loaded.load(path, "PNG")) {
        if (error) *error = QStringLiteral("cannot read %1").arg(path);
        return false;
    }
    // NORMALIZED TO RGB32 ON THE WAY IN, which is what makes the comparison symmetric and what
    // makes the eight-bit grey baselines `writePng` produces invisible to everything downstream:
    // the golden flag set never asks for a transparent background, so both sides end up opaque
    // BGR-plus-padding, and `channelDelta` reads only the three colour bytes in any case.
    loaded = loaded.convertToFormat(QImage::Format_RGB32);

    out->width = loaded.width();
    out->height = loaded.height();
    out->bytesPerLine = static_cast<int>(loaded.bytesPerLine());
    out->format = core::PixelFormat::Bgrx8888;
    out->pixels = QByteArray(reinterpret_cast<const char*>(loaded.constBits()),
                             static_cast<qsizetype>(loaded.sizeInBytes()));
    return out->isValid();
}

// ===========================================================================================
// Comparison
// ===========================================================================================

double Comparison::differingRatio() const
{
    return totalPixels == 0 ? 0.0 : static_cast<double>(differingPixels) / static_cast<double>(totalPixels);
}

double Comparison::grossRatio() const
{
    return totalPixels == 0 ? 0.0 : static_cast<double>(grossPixels) / static_cast<double>(totalPixels);
}

bool Comparison::passes(const Thresholds& thresholds) const
{
    if (!sizeMatches) return false;
    if (totalPixels == 0) return false;
    return differingRatio() <= thresholds.differingFraction
           && grossRatio() <= thresholds.grossFraction;
}

QString Comparison::describe(const Thresholds& thresholds) const
{
    if (!sizeMatches) {
        return QStringLiteral("size mismatch: expected %1x%2, rendered %3x%4")
            .arg(expectedWidth).arg(expectedHeight).arg(actualWidth).arg(actualHeight);
    }
    if (passes(thresholds)) {
        return QStringLiteral("within threshold (%1 of %2 pixels differ, worst channel delta %3)")
            .arg(differingPixels).arg(totalPixels).arg(maxChannelDelta);
    }

    QString which;
    if (grossRatio() > thresholds.grossFraction && differingRatio() > thresholds.differingFraction)
        which = QStringLiteral("both thresholds");
    else if (grossRatio() > thresholds.grossFraction)
        which = QStringLiteral("the gross threshold");
    else
        which = QStringLiteral("the area threshold");

    return QStringLiteral(
               "%1 exceeded: %2 of %3 pixels differ by more than %4 (%5 %, limit %6 %), of which %7 "
               "differ by more than %8 (%9 %, limit %10 %); worst channel delta %11, first at "
               "(%12, %13)")
        .arg(which)
        .arg(differingPixels).arg(totalPixels).arg(thresholds.channelTolerance)
        .arg(differingRatio() * 100.0, 0, 'f', 4).arg(thresholds.differingFraction * 100.0, 0, 'f', 4)
        .arg(grossPixels).arg(thresholds.grossChannelTolerance)
        .arg(grossRatio() * 100.0, 0, 'f', 4).arg(thresholds.grossFraction * 100.0, 0, 'f', 4)
        .arg(maxChannelDelta).arg(firstDifferentX).arg(firstDifferentY);
}

namespace {

// The per-pixel delta the whole comparison is built on: the largest absolute difference across the
// three colour channels. Alpha is deliberately excluded -- the ordinary golden render is onto
// opaque white, so an alpha channel that a PNG round trip normalizes would report a difference
// nobody can see.
inline int channelDelta(const unsigned char* a, const unsigned char* b)
{
    int worst = 0;
    for (int channel = 0; channel < 3; ++channel)
        worst = std::max(worst, std::abs(static_cast<int>(a[channel]) - static_cast<int>(b[channel])));
    return worst;
}

} // namespace

Comparison compare(const RasterImage& expected, const RasterImage& actual,
                   const Thresholds& thresholds)
{
    Comparison result;
    result.expectedWidth = expected.width;
    result.expectedHeight = expected.height;
    result.actualWidth = actual.width;
    result.actualHeight = actual.height;
    result.sizeMatches = expected.width == actual.width && expected.height == actual.height;
    if (!result.sizeMatches) return result;

    result.totalPixels = static_cast<long long>(expected.width) * expected.height;
    for (int y = 0; y < expected.height; ++y) {
        const unsigned char* expectedRow = pixelAt(expected, 0, y);
        const unsigned char* actualRow = pixelAt(actual, 0, y);
        for (int x = 0; x < expected.width; ++x) {
            const int delta = channelDelta(expectedRow + x * 4, actualRow + x * 4);
            if (delta > result.maxChannelDelta) result.maxChannelDelta = delta;
            if (delta > thresholds.channelTolerance) {
                if (result.firstDifferentX < 0) {
                    result.firstDifferentX = x;
                    result.firstDifferentY = y;
                }
                ++result.differingPixels;
                if (delta > thresholds.grossChannelTolerance) ++result.grossPixels;
            }
        }
    }
    return result;
}

bool writeFailureImages(const QString& stem, const RasterImage& expected, const RasterImage& actual,
                        const Thresholds& thresholds, QString* error)
{
    const QDir directory = QFileInfo(stem).absoluteDir();
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath())) {
        if (error) *error = QStringLiteral("cannot create %1").arg(directory.absolutePath());
        return false;
    }

    if (!writePng(stem + QStringLiteral("-expected.png"), expected, error)) return false;
    if (!writePng(stem + QStringLiteral("-actual.png"), actual, error)) return false;

    // THE DIFF IMAGE IS THE HALF THAT MAKES A FAILURE ACTIONABLE (D2-13). Matching pixels become a
    // faint grey ghost of what was rendered, so the page is still recognizable; differing pixels
    // become red at an intensity proportional to their delta, so the eye lands on the worst region
    // first. A diff that is a plain black-and-white mask loses the *where*.
    const int width = std::min(expected.width, actual.width);
    const int height = std::min(expected.height, actual.height);
    if (width <= 0 || height <= 0) {
        if (error) *error = QStringLiteral("nothing to diff: %1x%2").arg(width).arg(height);
        return false;
    }

    QImage diff(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<unsigned char*>(diff.scanLine(y));
        const unsigned char* expectedRow = pixelAt(expected, 0, y);
        const unsigned char* actualRow = pixelAt(actual, 0, y);
        for (int x = 0; x < width; ++x) {
            const unsigned char* a = actualRow + x * 4;
            const int delta = channelDelta(expectedRow + x * 4, a);
            unsigned char* target = row + x * 4;
            if (delta > thresholds.channelTolerance) {
                const int intensity = std::min(255, 96 + delta * 2);
                target[0] = 0;                                   // B
                target[1] = 0;                                   // G
                target[2] = static_cast<unsigned char>(intensity);  // R
            } else {
                const int ghost = 200 + (static_cast<int>(a[0]) + a[1] + a[2]) / 3 / 5;
                const auto value = static_cast<unsigned char>(std::min(255, ghost));
                target[0] = value;
                target[1] = value;
                target[2] = value;
            }
            target[3] = 255;
        }
    }
    if (!diff.save(stem + QStringLiteral("-diff.png"), "PNG")) {
        if (error) *error = QStringLiteral("cannot write %1-diff.png").arg(stem);
        return false;
    }
    return true;
}

// ===========================================================================================
// The capture log
// ===========================================================================================

const CaptureRecord* CaptureLog::find(const QString& file, int page, int dpi) const
{
    const auto at = std::find_if(records.begin(), records.end(),
                                 [&](const CaptureRecord& record) {
                                     return record.file == file && record.page == page
                                            && record.dpi == dpi;
                                 });
    return at == records.end() ? nullptr : &*at;
}

CaptureLog loadCaptureLog(const QString& path)
{
    CaptureLog log;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        log.error = QStringLiteral("cannot read %1: %2").arg(path, file.errorString());
        return log;
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        log.error = QStringLiteral("%1 is not readable JSON (%2)").arg(path, parseError.errorString());
        return log;
    }

    const QJsonObject root = document.object();
    log.pdfiumVersion = jsonString(root, "pdfiumVersion");
    log.capturedUtc = jsonString(root, "capturedUtc");
    log.reason = jsonString(root, "reason");

    for (const QJsonValue& entry : root.value(QLatin1String("baselines")).toArray()) {
        const QJsonObject object = entry.toObject();
        CaptureRecord record;
        record.file = jsonString(object, "file");
        record.page = object.value(QLatin1String("page")).toInt();
        record.dpi = object.value(QLatin1String("dpi")).toInt();
        record.width = object.value(QLatin1String("width")).toInt();
        record.height = object.value(QLatin1String("height")).toInt();
        for (const QJsonValue& row : object.value(QLatin1String("substitutions")).toArray())
            record.substitutions << row.toString();
        log.records.push_back(std::move(record));
    }
    return log;
}

bool writeCaptureLog(const QString& path, const CaptureLog& log, QString* error)
{
    QJsonObject root;
    root.insert(QLatin1String("$comment"),
                QStringLiteral("MEASURED, not declared -- written by tools/golden-update. The "
                               "declaration is golden/manifest.json. Do not hand-edit."));
    root.insert(QLatin1String("pdfiumVersion"), log.pdfiumVersion);
    root.insert(QLatin1String("capturedUtc"), log.capturedUtc);
    root.insert(QLatin1String("reason"), log.reason);

    QJsonArray baselines;
    for (const CaptureRecord& record : log.records) {
        QJsonObject object;
        object.insert(QLatin1String("file"), record.file);
        object.insert(QLatin1String("page"), record.page);
        object.insert(QLatin1String("dpi"), record.dpi);
        object.insert(QLatin1String("width"), record.width);
        object.insert(QLatin1String("height"), record.height);
        if (!record.substitutions.isEmpty()) {
            QJsonArray rows;
            for (const QString& row : record.substitutions) rows.append(row);
            object.insert(QLatin1String("substitutions"), rows);
        }
        baselines.append(object);
    }
    root.insert(QLatin1String("baselines"), baselines);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("cannot write %1: %2").arg(path, file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

QStringList describeSubstitutions(
    const std::vector<pdf::render::FontResolverBridge::Substitution>& rows)
{
    using Outcome = pdf::render::FontResolverBridge::Outcome;

    QStringList out;
    for (const auto& row : rows) {
        QLatin1String outcome("none-available");
        switch (row.outcome) {
        case Outcome::Exact:            outcome = QLatin1String("exact"); break;
        case Outcome::Substituted:      outcome = QLatin1String("substituted"); break;
        case Outcome::StandardDeclined: outcome = QLatin1String("standard-declined"); break;
        case Outcome::NoneAvailable:    outcome = QLatin1String("none-available"); break;
        }
        // The FAMILY, never the file path: a path carries a Windows directory and a drive letter,
        // and a baseline record that differs between two machines with the same fonts installed
        // would make every such case skip for a reason that is not about rendering.
        out << QStringLiteral("%1 -> %2 [%3]")
                   .arg(row.normalized,
                        row.chosen.isEmpty() ? QStringLiteral("(engine)") : row.chosen, outcome);
    }
    // DISTINCT rows, not one per call. What makes a baseline machine-dependent is *which faces the
    // page resolved to*, never how many times the engine happened to ask -- PDFium asks about the
    // same family twice on most of this corpus and once on `minimal.pdf`, and a count that leaked
    // into the comparison would turn a change in the engine's caching into a font finding.
    out.sort();
    out.removeDuplicates();
    return out;
}

QStringList corpusFiles(const QString& corpusRoot)
{
    QStringList found;
    const QDir root(corpusRoot);
    QDirIterator walk(corpusRoot, QStringList{QStringLiteral("*.pdf")}, QDir::Files,
                      QDirIterator::Subdirectories);
    while (walk.hasNext()) found << root.relativeFilePath(walk.next());
    found.sort();
    return found;
}

} // namespace rawpage::golden
