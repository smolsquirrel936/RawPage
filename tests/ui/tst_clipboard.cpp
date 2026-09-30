// Stage 5 T5 -- guide case A-50 (`Stage5-TextSearch-Testing-Guide.md` §5.7): a copy's HTML reaches
// Windows as CF_HTML. Decision D5-10: `platform::IRichClipboard` is not built, because Qt's Windows
// platform publishes `text/html` as the registered "HTML Format" itself -- and that was inferred at
// planning time from a STRING in `qwindows.dll`. This case replaces the inference with a measurement:
// the product's own `QMimeData` goes on the clipboard, and the clipboard is read back through Win32.
//
// IT SKIPS UNDER `ctest`, which runs this binary on the OFFSCREEN platform -- whose clipboard is Qt's
// in-process one and never reaches Windows -- and is run by hand, as Stage 4's A-85 and A-94 are:
//
//     $env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_ui_tests.exe "[D5-10]"
//
// with hands off the desktop: it owns the system clipboard for a moment, and a copy made elsewhere in
// that moment would be what it reads.
//
// WHAT IT DOES NOT COVER: how Word renders what it reads -- that is manual case M-02, P-1.

#include <catch2/catch_test_macros.hpp>

#include "core/text/PageTextModel.h"
#include "core/text/TextSelection.h"
#include "ui/viewer/SelectionOverlay.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QMimeData>
#include <QRegularExpression>
#include <QThread>

#include <memory>
#include <optional>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace rawpage;

namespace {

#ifdef _WIN32
// The registered format's bytes, or nothing. The clipboard is OPENED and CLOSED here and nowhere else,
// and retried briefly: another process holding it open is an ordinary Windows state, not a failure.
std::optional<QByteArray> readClipboardFormat(UINT format)
{
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (OpenClipboard(nullptr)) {
            std::optional<QByteArray> bytes;
            if (HANDLE handle = GetClipboardData(format)) {
                if (const void* data = GlobalLock(handle)) {
                    bytes = QByteArray(static_cast<const char*>(data), static_cast<qsizetype>(GlobalSize(handle)));
                    GlobalUnlock(handle);
                }
            }
            CloseClipboard();
            return bytes;
        }
        QThread::msleep(25);
    }
    return std::nullopt;
}

qsizetype headerOffset(const QByteArray& html, const char* name)
{
    const QRegularExpression field(QStringLiteral("%1:(\\d+)").arg(QLatin1String(name)));
    const QRegularExpressionMatch match = field.match(QString::fromLatin1(html.left(256)));
    return match.hasMatch() ? match.captured(1).toLongLong() : -1;
}
#endif

core::text::CopiedText twoParagraphs()
{
    core::text::PageTextModel model(0);
    const auto run = [&model](const std::u32string& text, double x, double baseline) {
        for (const char32_t cp : text) {
            core::text::TextChar c;
            c.codepoint = cp;
            c.fontSize = 10.0;
            c.origin = core::PdfPoint{x, baseline};
            c.box = core::PdfRect{core::PdfPoint{x, baseline - 2.1}, core::PdfSize{5.5, 11.1}};
            model.append(c);
            x += 5.5;
        }
    };
    run(U"The first paragraph.", 72, 700);
    run(U"The second, café and 中文.", 72, 640);
    core::text::DocumentText text(1);
    text.insert(core::text::PageReading::build(std::make_shared<const core::text::PageTextModel>(std::move(model))));
    return core::text::TextSelection::wholeDocument(1).copy(text);
}

} // namespace

TEST_CASE("a copy's HTML reaches Windows as CF_HTML with a consistent header", "[D5-10][clipboard]")
{
    if (QGuiApplication::platformName() != QLatin1String("windows"))
        SKIP("needs the real windows platform -- offscreen, the clipboard never reaches Windows (run by hand)");

#ifdef _WIN32
    const core::text::CopiedText copied = twoParagraphs();
    REQUIRE_FALSE(copied.html.isEmpty());

    // EXACTLY what the product publishes: `SelectionOverlay::mimeDataFor`, the one place a copy becomes
    // a `QMimeData`.
    QGuiApplication::clipboard()->setMimeData(ui::SelectionOverlay::mimeDataFor(copied));
    QCoreApplication::processEvents();

    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    REQUIRE(htmlFormat != 0);

    std::optional<QByteArray> html;
    QElapsedTimer waited;
    waited.start();
    while (waited.elapsed() < 2000) {
        html = readClipboardFormat(htmlFormat);
        if (html && !html->isEmpty()) break;
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
    REQUIRE(html.has_value());
    // GlobalSize may round up; the payload ends at its first NUL.
    if (const qsizetype nul = html->indexOf('\0'); nul >= 0) html->truncate(nul);

    // THE HEADER: a version and four offsets into the same bytes, each consistent with the others.
    CHECK(html->startsWith("Version:"));
    const qsizetype startHtml = headerOffset(*html, "StartHTML");
    const qsizetype endHtml = headerOffset(*html, "EndHTML");
    const qsizetype startFragment = headerOffset(*html, "StartFragment");
    const qsizetype endFragment = headerOffset(*html, "EndFragment");
    INFO(html->left(200).toStdString());
    REQUIRE(startHtml > 0);
    REQUIRE(startFragment >= startHtml);
    REQUIRE(endFragment > startFragment);
    REQUIRE(endHtml >= endFragment);
    REQUIRE(endHtml <= html->size());
    CHECK(html->mid(startHtml, 6).toLower() == "<html>");

    // THE FRAGMENT holds the two paragraphs -- as UTF-8, the CJK and the accent intact.
    const QString fragment = QString::fromUtf8(html->mid(startFragment, endFragment - startFragment));
    CHECK(fragment.count(QStringLiteral("<p>")) == 2);
    CHECK(fragment.contains(QStringLiteral("The first paragraph.")));
    CHECK(fragment.contains(QString::fromUcs4(U"The second, café")));
    CHECK(fragment.contains(QString::fromUcs4(U"中文")));

    // AND THE PLAIN FLAVOUR beside it, as Windows' own Unicode text.
    const std::optional<QByteArray> unicode = readClipboardFormat(CF_UNICODETEXT);
    REQUIRE(unicode.has_value());
    const QString plain = QString::fromWCharArray(reinterpret_cast<const wchar_t*>(unicode->constData()));
    // WINDOWS' LINE ENDINGS (measured on the first run, 2026-10-01): Qt publishes `text/plain` as
    // CF_UNICODETEXT with every `\n` written `\r\n`, which is what Notepad needs to show the lines at all.
    // So the text is compared with that one conversion undone, and the conversion itself is asserted.
    CHECK(plain.count(QStringLiteral("\r\n")) == copied.plain.count(QLatin1Char('\n')));
    CHECK(QString(plain).replace(QStringLiteral("\r\n"), QStringLiteral("\n")) == copied.plain);
#endif
}
