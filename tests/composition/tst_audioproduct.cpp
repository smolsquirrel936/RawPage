// Stage 6 T14 -- audio comments through the product's own composition (`Stage6-Annotations-Testing-Guide.md` §5.17, guide
// case A-101's product half; D6-18): `annot.attachAudio` and `annot.recordAudio` armed by their commands, a click on the
// page, the sound answered through the WINDOW'S hooks -- a WAV path, a recording -- ONE command each; saved, and a fresh
// engine's document gives back the samples byte for byte; a double click PLAYS one, the window reading its samples; a
// WAV that is not PCM refused by name, nothing made.
//
// WHAT THIS DOES NOT COVER (convention 19): the microphone and the speaker. Offscreen nobody can be asked, so the
// recorder is answered by `MainWindow::setAudioAnswersForTesting`, and the player is handed the samples whether or not
// the machine has an output device -- `lastPlayed()` is what it was given, not what was heard. A REAL recording is the
// `[A-101-record]` case below, which SKIPS unless `QT_QPA_PLATFORM=windows` and a microphone is present, and a person's
// ears are M-08's.

#include "tests/composition/AnnotProduct.h"

#include "core/cmd/ActionRegistry.h"
#include "core/doc/SoundData.h"
#include "pdf/DocumentBackendImpl.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AudioRecorder.h"
#include "ui/viewer/ToolController.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QThread>

#include <cstring>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <iostream>

using namespace rawpage;
using annot_product::Product;
using core::Annotation;
using core::AnnotationSubtype;

namespace {

// A QByteArray as Catch2 can print it: its hex when short, its SHA-256 when long -- printing two QByteArrays in a failed
// CHECK crashes the reporter (CLAUDE.md, *Traps*; S3 guide I-09).
std::string hexOf(const QByteArray& bytes)
{
    return bytes.size() <= 64 ? bytes.toHex().toStdString()
                              : "sha256:" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

void le16(QByteArray& out, std::uint16_t v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>(v >> 8));
}

void le32(QByteArray& out, std::uint32_t v)
{
    for (int s = 0; s < 32; s += 8) out.append(static_cast<char>((v >> s) & 0xFF));
}

QByteArray wav(std::uint16_t tag, int channels, int rate, int bits, const QByteArray& data)
{
    QByteArray body("WAVEfmt ", 8);
    le32(body, 16);
    le16(body, tag);
    le16(body, static_cast<std::uint16_t>(channels));
    le32(body, static_cast<std::uint32_t>(rate));
    le32(body, static_cast<std::uint32_t>(rate * channels * bits / 8));
    le16(body, static_cast<std::uint16_t>(channels * bits / 8));
    le16(body, static_cast<std::uint16_t>(bits));
    body.append("data", 4);
    le32(body, static_cast<std::uint32_t>(data.size()));
    body.append(data);
    QByteArray out("RIFF", 4);
    le32(out, static_cast<std::uint32_t>(body.size()));
    return out + body;
}

QByteArray tone(int frames, int channels)
{
    QByteArray out;
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c) le16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>((f % 50) * 600 - 15000 + c * 7)));
    return out;
}

QString write(Product& product, const QString& name, const QByteArray& bytes)
{
    const QString path = QDir(product.scratch.path()).filePath(name);
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    return path;
}

void click(ui::PageView& view, QPoint at, int clicks = 1)
{
    for (int i = 0; i < clicks; ++i) {
        const QEvent::Type press = i == 0 ? QEvent::MouseButtonPress : QEvent::MouseButtonDblClick;
        QMouseEvent down(press, QPointF(at), view.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton, Qt::LeftButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &down);
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(at), view.viewport()->mapToGlobal(QPointF(at)), Qt::LeftButton,
                       Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(view.viewport(), &up);
        QCoreApplication::processEvents();
    }
}

QPoint widgetAt(const ui::PageView& view, int page, core::PdfPoint point)
{
    const std::optional<core::DevicePoint> d = view.layoutEngine().layout().toContent(page, point);
    REQUIRE(d);
    const core::DeviceRect visible = view.controller().visibleRect();
    const double dpr = view.devicePixelRatioF();
    return QPoint(static_cast<int>(std::lround((d->x - visible.left()) / dpr)),
                  static_cast<int>(std::lround((d->y - visible.top()) / dpr)));
}

bool dispatch(Product& product, core::ActionId id)
{
    return static_cast<bool>(product.root->services().actions.dispatch(id, core::KeyContext::Viewer));
}

std::vector<const Annotation*> sounds(Product& product)
{
    std::vector<const Annotation*> out;
    for (const Annotation& a : product.store().all())
        if (a.subtype == AnnotationSubtype::Sound) out.push_back(&a);
    return out;
}

struct AnswersReset {
    ~AnswersReset() { ui::MainWindow::setAudioAnswersForTesting({}); }
};

} // namespace

TEST_CASE("a WAV attached and a recording made through the tools are saved and played as their samples",
          "[audioproduct][A-101][D6-18]")
{
    Product product;
    AnswersReset reset;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    view->setZoom(core::render::Zoom{1.0});
    product.settle();

    const QByteArray stereo = tone(2205, 2);   // a tenth of a second at 22.05 kHz
    const QString wavPath = write(product, QStringLiteral("tone.wav"), wav(1, 2, 22050, 16, stereo));
    const core::Result<core::SoundData> recording = core::soundFromWav(wav(1, 1, 8000, 8, QByteArray(800, '\x90')));
    REQUIRE(recording);
    int asked = 0;
    ui::MainWindow::setAudioAnswersForTesting({[&]() -> std::optional<core::SoundData> {
                                                   ++asked;
                                                   return recording.value();
                                               },
                                               [&]() -> std::optional<QString> {
                                                   ++asked;
                                                   return wavPath;
                                               }});

    // ATTACH: the command arms the tool, a click places the speaker and asks for the file -- ONE command.
    view->revealPageRect(0, core::PdfRect{core::PdfPoint{400, 650}, core::PdfSize{150, 150}});
    product.settle();
    REQUIRE(dispatch(product, core::ActionId::AnnotAttachAudio));
    REQUIRE(view->tools().armedAnnotation() == ui::AnnotationToolKind::AttachAudio);
    click(*view, widgetAt(*view, 0, core::PdfPoint{450, 760}));
    product.settle();
    // RECORD: the same, through the recorder.
    REQUIRE(dispatch(product, core::ActionId::AnnotRecordAudio));
    click(*view, widgetAt(*view, 0, core::PdfPoint{450, 700}));
    product.settle();
    CHECK(asked == 2);
    CHECK(product.document()->journal()->position() == 2);
    REQUIRE(sounds(product).size() == 2);
    for (const Annotation* s : sounds(product)) {
        REQUIRE(s->sound);
        CHECK(s->sound->carried);
        CHECK(s->icon == QLatin1String("Speaker"));
        CHECK(s->rect.size.width == core::kSoundIconSize);
    }

    // PLAYED: a double click on the attached one hands the player its samples.
    const Annotation* attached = sounds(product)[0]->sound->channels == 2 ? sounds(product)[0] : sounds(product)[1];
    const core::AnnotationId attachedId = attached->id;
    const QByteArray attachedSamples = attached->sound->samples;
    REQUIRE(dispatch(product, core::ActionId::ToolSelect));
    click(*view, widgetAt(*view, 0, core::PdfPoint{attached->rect.left() + 10, attached->rect.top() - 10}), 2);
    product.settle();
    REQUIRE(product.window->lastPlayed().has_value());
    CHECK(hexOf(product.window->lastPlayed()->samples) == hexOf(attachedSamples));
    CHECK(product.window->lastPlayed()->channels == 2);

    // SAVED and reopened FRESH: the samples byte for byte, and the WAV the person chose is what a player gets back.
    REQUIRE(dispatch(product, core::ActionId::FileSave));
    product.settle();
    pdf::DocumentBackendImpl fresh;
    auto reopened = fresh.open(product.openedPath, nullptr);
    REQUIRE(reopened);
    const core::AnnotationStore* store = fresh.annotations(reopened.value());
    REQUIRE(store != nullptr);
    int found = 0;
    for (const Annotation& a : store->all()) {
        if (a.subtype != AnnotationSubtype::Sound || !a.sound) continue;
        ++found;
        const core::Result<QByteArray> samples = fresh.soundSamples(reopened.value(), a.id);
        REQUIRE(samples);
        core::SoundData heard = *a.sound;
        heard.samples = samples.value();
        heard.carried = true;
        const core::Result<QByteArray> back = core::soundToWav(heard);
        REQUIRE(back);
        if (a.sound->channels == 2) CHECK(hexOf(back.value().mid(44)) == hexOf(stereo));   // A-101, through the product
        else CHECK(hexOf(samples.value()) == hexOf(recording.value().samples));
    }
    CHECK(found == 2);
    CHECK(fresh.close(reopened.value()));

}

TEST_CASE("a sound the file holds is played from its stream", "[audioproduct][A-101][D6-18]")
{
    // The corpus's quarter-second tone: the model holds its numbers, not its samples, so the window reads them from the
    // `/Sound` stream (`IDocumentBackend::soundSamples`) -- graded against the generator's label.
    Product product;
    ui::PageView* view = product.open("ordinary/annots-every-type.pdf");
    (void)view;
    REQUIRE(sounds(product).size() == 1);
    const Annotation* sound = sounds(product).front();
    REQUIRE(sound->sound);
    CHECK_FALSE(sound->sound->carried);
    (void)product.window->playSound(sound->id);
    REQUIRE(product.window->lastPlayed().has_value());
    const QByteArray& samples = product.window->lastPlayed()->samples;
    CHECK(samples.size() == 4000);   // 2000 16-bit samples, as the label says
    QFile labelFile(test_oracle::corpusPath("labels/annots-every-type.json"));
    REQUIRE(labelFile.open(QIODevice::ReadOnly));
    QString want;
    for (const QJsonValue page : QJsonDocument::fromJson(labelFile.readAll()).object().value(QStringLiteral("pages")).toArray())
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray())
            if (entry.toObject().contains(QStringLiteral("sound")))
                want = entry.toObject().value(QStringLiteral("sound")).toObject().value(QStringLiteral("sha256")).toString();
    REQUIRE_FALSE(want.isEmpty());
    CHECK(QString::fromLatin1(QCryptographicHash::hash(samples, QCryptographicHash::Sha256).toHex()) == want);
}

TEST_CASE("a WAV that is not PCM is refused by name and makes nothing", "[audioproduct][A-101][D6-18]")
{
    Product product;
    AnswersReset reset;
    ui::PageView* view = product.open("ordinary/text-a4.pdf");
    const QString floatWav = write(product, QStringLiteral("float.wav"), wav(3, 1, 8000, 32, QByteArray(400, '\0')));
    ui::MainWindow::setAudioAnswersForTesting({{}, [floatWav]() -> std::optional<QString> { return floatWav; }});
    REQUIRE(dispatch(product, core::ActionId::AnnotAttachAudio));
    click(*view, QPoint(view->viewport()->width() / 2, view->viewport()->height() / 2));
    product.settle();
    CHECK(sounds(product).empty());
    CHECK(product.document()->journal()->position() == 0);
    // The status bar names the format, in the person's words -- the refusal's own key, not "it failed".
    const QString said = product.window->statusBar()->message()->text();
    CHECK(said.contains(QStringLiteral("0x0003")));
    // And with nobody to ask and no answer installed, the tool asks nothing and makes nothing (convention 16).
    ui::MainWindow::setAudioAnswersForTesting({});
    REQUIRE(dispatch(product, core::ActionId::AnnotRecordAudio));
    click(*view, QPoint(view->viewport()->width() / 2, view->viewport()->height() / 2 + 40));
    product.settle();
    CHECK(sounds(product).empty());
}

TEST_CASE("a capture in any format a microphone gives becomes a sound RawPage writes", "[audioproduct][A-101][D6-18]")
{
    // 16-bit stereo: kept, turned big-endian.
    QAudioFormat format;
    format.setSampleRate(44100);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    const QByteArray captured = tone(100, 2);
    core::Result<core::SoundData> sound = ui::soundFromCapture(captured, format);
    REQUIRE(sound);
    CHECK(sound.value().channels == 2);
    CHECK(sound.value().bits == 16);
    CHECK_FALSE(core::invalidSound(sound.value()));
    CHECK(hexOf(core::playablePcm(sound.value()).value().samples) == hexOf(captured));
    // Float, four channels -- the shape a WASAPI mix format takes: mixed to one, scaled to 16 bits.
    format.setSampleFormat(QAudioFormat::Float);
    format.setChannelCount(4);
    format.setSampleRate(48000);
    QByteArray floats;
    for (const float v : {0.5f, 0.5f, 0.5f, 0.5f, -1.0f, -1.0f, -1.0f, -1.0f}) floats.append(reinterpret_cast<const char*>(&v), 4);
    sound = ui::soundFromCapture(floats, format);
    REQUIRE(sound);
    CHECK(sound.value().channels == 1);
    REQUIRE(sound.value().samples.size() == 4);
    CHECK(hexOf(sound.value().samples) == hexOf(QByteArray("\x40\x00\x80\x00", 4)));   // 16384 and -32768, big-endian
    CHECK_FALSE(core::invalidSound(sound.value()));
    // 8-bit: kept as Raw. Nothing captured: refused.
    format.setSampleFormat(QAudioFormat::UInt8);
    format.setChannelCount(1);
    format.setSampleRate(8000);
    sound = ui::soundFromCapture(QByteArray(10, '\x80'), format);
    REQUIRE(sound);
    CHECK(sound.value().encoding == core::SoundEncoding::Raw);
    CHECK_FALSE(ui::soundFromCapture(QByteArray(), format));
}

TEST_CASE("a real recording through the recorder dialog", "[audioproduct][A-101-record][D6-18]")
{
    // BY HAND, on the `windows` platform with a microphone -- like Stage 5's A-50 it SKIPS under ctest, offscreen:
    //   $env:QT_QPA_PLATFORM='windows'; .\build\x64-release\bin\rawpage_composition_tests.exe "[A-101-record]"
    // It records about a second from the default input -- the room, or whoever speaks -- and plays it back.
    if (QGuiApplication::platformName() != QLatin1String("windows")) SKIP("a real recording runs on the windows platform only");
    ui::AudioRecorderDialog dialog;
    if (!dialog.hasInput()) SKIP("no microphone on this machine");
    dialog.show();
    QCoreApplication::processEvents();
    REQUIRE(dialog.recordButton()->isEnabled());
    dialog.recordButton()->click();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1200) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    dialog.stopButton()->click();
    QCoreApplication::processEvents();
    REQUIRE(dialog.recorded().has_value());
    std::cout << "A-101 by hand: recorded " << dialog.recorded()->seconds() << " s at " << dialog.recorded()->rate << " Hz, "
              << dialog.recorded()->channels << " channel(s), " << dialog.recorded()->bits << " bits -- "
              << dialog.statusLabel()->text().toStdString() << "\n";
    CHECK(dialog.recorded()->seconds() > 0.8);
    CHECK_FALSE(core::invalidSound(*dialog.recorded()));
    REQUIRE(dialog.playButton()->isEnabled());
    dialog.playButton()->click();
    timer.restart();
    while (timer.elapsed() < 1500) QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
}

TEST_CASE("the backend this process loads is Media Foundation and never FFmpeg", "[audioproduct][A-07][D6-18]")
{
    // What A-07's script cannot see: which plugin a running process LOADS. A developer's PATH reaches the Qt prefix,
    // whose plugins/multimedia holds both backends, and Qt 6 prefers FFmpeg wherever it finds it -- so the product names
    // its backend before any multimedia object exists (`ui::useMediaFoundation`). After the first device query, the
    // Windows backend's DLL is in this process and the FFmpeg plugin's is not.
    ui::SoundPlayer player;
    (void)QMediaDevices::defaultAudioOutput();
    (void)QMediaDevices::audioInputs();
    CHECK(qgetenv("QT_MEDIA_BACKEND") == QByteArray("windows"));
    HMODULE windows = GetModuleHandleW(L"windowsmediaplugin.dll");
    HMODULE ffmpeg = GetModuleHandleW(L"ffmpegmediaplugin.dll");
    wchar_t where[MAX_PATH] = {};
    if (windows != nullptr) GetModuleFileNameW(windows, where, MAX_PATH);
    std::wcout << L"A-07 at run time: windowsmediaplugin.dll " << (windows != nullptr ? where : L"NOT loaded")
               << L"; ffmpegmediaplugin.dll " << (ffmpeg != nullptr ? L"LOADED" : L"not loaded") << L"\n";
    CHECK(windows != nullptr);
    CHECK(ffmpeg == nullptr);
    CHECK(GetModuleHandleW(L"avcodec-61.dll") == nullptr);
}
