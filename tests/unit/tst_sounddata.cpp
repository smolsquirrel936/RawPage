// Stage 6 T14 -- a Sound's samples (`core/doc/SoundData`, D6-18): WAV in, ISO 32000-1 §13.3's big-endian PCM out, and
// back; written by the sink as a `/Sound` stream, saved, and read back from the file byte for byte (guide case A-101).
//
// THE ORACLE IS THE WAV FILE ITSELF: each case builds its WAV by hand -- a RIFF header and samples whose values it
// knows -- so `soundFromWav` is graded against the bytes a WAV holds, and `soundToWav` against the very bytes it was
// handed. And the corpus's sound against the GENERATOR's label (its rate, channels, depth and the SHA-256 of its
// samples), never against our own reader's earlier answer.
//
// WHAT IT DOES NOT COVER (convention 19): recording through a microphone and playing through a speaker -- Qt
// Multimedia's, in `ui/viewer/AudioRecorder` -- which `tst_audio` (ui) reaches through the window's hooks and a person
// reaches by hand (M-08); and whether ACROBAT plays the stream (M-08 again).

#include <catch2/catch_test_macros.hpp>

#include "SaveSession.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/doc/Fdf.h"
#include "core/doc/Redaction.h"
#include "core/doc/SoundData.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AppearanceStreamBuilder.h"
#include "pdf/ops/PageOps.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <string>

using namespace rawpage;
using core::Annotation;
using core::AnnotationSubtype;
using core::SoundData;
using core::SoundEncoding;

namespace {

// A QByteArray as Catch2 can print it: its hex when short, its SHA-256 when long -- printing two QByteArrays in a failed
// CHECK crashes the reporter (CLAUDE.md, *Traps*; S3 guide I-09).
std::string hexOf(const QByteArray& bytes)
{
    return bytes.size() <= 64 ? bytes.toHex().toStdString()
                              : "sha256:" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex().toStdString();
}

constexpr std::int64_t kNow = 1792108800;   // 2026-10-16T00:00:00Z
const QString kAuthor = QStringLiteral("Amara Nwosu");

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
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

// A WAV file: `fmt ` with `tag`, `channels`, `rate`, `bits`, an optional `LIST` chunk before `data`, then `data`.
QByteArray wav(std::uint16_t tag, int channels, int rate, int bits, const QByteArray& data, bool extraChunk = false)
{
    QByteArray body;
    body.append("WAVE", 4);
    body.append("fmt ", 4);
    le32(body, 16);
    le16(body, tag);
    le16(body, static_cast<std::uint16_t>(channels));
    le32(body, static_cast<std::uint32_t>(rate));
    le32(body, static_cast<std::uint32_t>(rate * channels * bits / 8));
    le16(body, static_cast<std::uint16_t>(channels * bits / 8));
    le16(body, static_cast<std::uint16_t>(bits));
    if (extraChunk) {
        body.append("LIST", 4);
        le32(body, 5);
        body.append("INFOx", 5);
        body.append('\0');   // padded to an even length
    }
    body.append("data", 4);
    le32(body, static_cast<std::uint32_t>(data.size()));
    body.append(data);
    if (data.size() % 2 != 0) body.append('\0');
    QByteArray out("RIFF", 4);
    le32(out, static_cast<std::uint32_t>(body.size()));
    out.append(body);
    return out;
}

// Samples whose every byte is known: a ramp of `frames` frames, each channel offset, as WAV stores them.
QByteArray ramp(int frames, int channels, int bits)
{
    QByteArray out;
    for (int f = 0; f < frames; ++f) {
        for (int c = 0; c < channels; ++c) {
            if (bits == 8) {
                out.append(static_cast<char>((f * 3 + c * 50) & 0xFF));
            } else {
                const auto v = static_cast<std::int16_t>(f * 97 - 20000 + c * 1234);
                le16(out, static_cast<std::uint16_t>(v));
            }
        }
    }
    return out;
}

// The `data` chunk of a WAV file this code wrote: the bytes after its 44-byte header.
QByteArray dataChunkOf(const QByteArray& file)
{
    REQUIRE(file.size() >= 44);
    REQUIRE(file.mid(36, 4) == QByteArray("data"));
    const auto length = static_cast<qsizetype>(static_cast<unsigned char>(file[40]) | (static_cast<unsigned char>(file[41]) << 8)
                                               | (static_cast<unsigned char>(file[42]) << 16)
                                               | (static_cast<unsigned char>(file[43]) << 24));
    return file.mid(44, length);
}

QJsonObject everyTypeSound()
{
    QFile file(tests::corpusFile("labels/annots-every-type.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonObject label = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue page : label.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue entry : page.toObject().value(QStringLiteral("annots")).toArray()) {
            if (entry.toObject().value(QStringLiteral("subtype")).toString() == QLatin1String("Sound")) return entry.toObject();
        }
    }
    FAIL("annots-every-type.json holds no Sound");
    return {};
}

QString sha256Hex(const QByteArray& bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

} // namespace

TEST_CASE("a PCM WAV of every depth and channel count round-trips its samples exactly", "[sounddata][A-101][D6-18]")
{
    struct Shape {
        int channels;
        int bits;
        int rate;
    };
    for (const Shape shape : {Shape{1, 8, 8000}, Shape{2, 8, 11025}, Shape{1, 16, 22050}, Shape{2, 16, 44100}}) {
        INFO(shape.channels << " channel(s), " << shape.bits << " bits, " << shape.rate << " Hz");
        const QByteArray samples = ramp(shape.bits == 8 ? 401 : 400, shape.channels, shape.bits);   // an odd 8-bit length
        for (const bool extra : {false, true}) {
            const core::Result<SoundData> sound = core::soundFromWav(wav(1, shape.channels, shape.rate, shape.bits, samples, extra));
            REQUIRE(sound);
            CHECK(sound.value().rate == shape.rate);
            CHECK(sound.value().channels == shape.channels);
            CHECK(sound.value().bits == shape.bits);
            CHECK(sound.value().carried);
            CHECK(sound.value().encoding == (shape.bits == 8 ? SoundEncoding::Raw : SoundEncoding::Signed));
            CHECK_FALSE(core::invalidSound(sound.value()));
            if (shape.bits == 16) {
                // ISO's order: the most significant byte FIRST -- every pair the WAV's reversed.
                REQUIRE(sound.value().samples.size() == samples.size());
                for (qsizetype i = 0; i + 1 < samples.size(); i += 2) {
                    CHECK(sound.value().samples[i] == samples[i + 1]);
                    CHECK(sound.value().samples[i + 1] == samples[i]);
                }
            } else {
                CHECK(hexOf(sound.value().samples) == hexOf(samples));   // 8-bit unsigned is ISO's Raw as it is
            }
            const core::Result<QByteArray> back = core::soundToWav(sound.value());
            REQUIRE(back);
            CHECK(hexOf(dataChunkOf(back.value())) == hexOf(samples));   // BYTE-IDENTICAL
            const core::Result<SoundData> again = core::soundFromWav(back.value());
            REQUIRE(again);
            CHECK(again.value() == sound.value());
        }
    }
}

TEST_CASE("a WAV that is not PCM is refused by its format tag and never converted", "[sounddata][A-101][D6-18]")
{
    const QByteArray samples = ramp(100, 1, 16);
    struct Refusal {
        std::uint16_t tag;
        const char* named;
    };
    for (const Refusal r : {Refusal{3, "0x0003"}, Refusal{6, "0x0006"}, Refusal{7, "0x0007"}, Refusal{0x55, "0x0055"},
                            Refusal{0x11, "0x0011"}}) {
        INFO("format tag " << r.named);
        const core::Result<SoundData> sound = core::soundFromWav(wav(r.tag, 1, 8000, 16, samples));
        REQUIRE_FALSE(sound);
        CHECK(keyOf(sound.error()) == "error.sound.notPcm");
        CHECK(sound.error().context == QLatin1String(r.named));
    }
    // WAVE_FORMAT_EXTENSIBLE: its SUBFORMAT decides -- float refused by its own tag, PCM taken.
    const auto extensible = [&samples](std::uint16_t sub) {
        QByteArray body("WAVE", 4);
        body.append("fmt ", 4);
        le32(body, 40);
        le16(body, 0xFFFE);
        le16(body, 1);
        le32(body, 8000);
        le32(body, 16000);
        le16(body, 2);
        le16(body, 16);
        le16(body, 22);
        le16(body, 16);
        le32(body, 4);
        le16(body, sub);
        const unsigned char tail[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
        body.append(reinterpret_cast<const char*>(tail), 14);
        body.append("data", 4);
        le32(body, static_cast<std::uint32_t>(samples.size()));
        body.append(samples);
        QByteArray out("RIFF", 4);
        le32(out, static_cast<std::uint32_t>(body.size()));
        return out + body;
    };
    const core::Result<SoundData> fl = core::soundFromWav(extensible(3));
    REQUIRE_FALSE(fl);
    CHECK(keyOf(fl.error()) == "error.sound.notPcm");
    CHECK(fl.error().context == QLatin1String("0x0003"));
    REQUIRE(core::soundFromWav(extensible(1)));

    // Not a WAV at all; three channels; 24 bits; a rate out of range; a header cut short; samples that are not frames.
    CHECK(keyOf(core::soundFromWav(QByteArray("OggS....")).error()) == "error.sound.notWav");
    CHECK(keyOf(core::soundFromWav(wav(1, 3, 8000, 16, ramp(10, 3, 16))).error()) == "error.sound.unsupported");
    CHECK(keyOf(core::soundFromWav(wav(1, 1, 8000, 24, QByteArray(300, '\x01'))).error()) == "error.sound.unsupported");
    CHECK(keyOf(core::soundFromWav(wav(1, 1, 500, 16, samples)).error()) == "error.sound.unsupported");
    QByteArray cut = wav(1, 1, 8000, 16, samples);
    cut.chop(50);
    CHECK(keyOf(core::soundFromWav(cut).error()) == "error.sound.malformed");
    CHECK(keyOf(core::soundFromWav(wav(1, 1, 8000, 16, QByteArray(301, '\x02'))).error()) == "error.sound.malformed");
}

TEST_CASE("what a player takes is every encoding ISO names turned into WAV's order", "[sounddata][D6-18]")
{
    SoundData s;
    s.rate = 8000;
    s.carried = true;
    // Raw 16-bit (unsigned) to signed little-endian: 0x8000 is silence.
    s.bits = 16;
    s.encoding = SoundEncoding::Raw;
    s.samples = QByteArray("\x80\x00\xFF\xFF", 4);
    core::Result<core::PlayablePcm> pcm = core::playablePcm(s);
    REQUIRE(pcm);
    CHECK(hexOf(pcm.value().samples) == hexOf(QByteArray("\x00\x00\xFF\x7F", 4)));
    // Signed 8-bit to unsigned: 0 is 128.
    s.bits = 8;
    s.encoding = SoundEncoding::Signed;
    s.samples = QByteArray("\x00\x7F\x80", 3);
    pcm = core::playablePcm(s);
    REQUIRE(pcm);
    CHECK(hexOf(pcm.value().samples) == hexOf(QByteArray("\x80\xFF\x00", 3)));
    // G.711: mu-law 0xFF and A-law 0xD5 are both silence, mu-law 0x00 its loudest negative.
    s.encoding = SoundEncoding::MuLaw;
    s.samples = QByteArray("\xFF\x00", 2);
    pcm = core::playablePcm(s);
    REQUIRE(pcm);
    REQUIRE(pcm.value().bits == 16);
    CHECK(hexOf(pcm.value().samples.left(2)) == hexOf(QByteArray("\x00\x00", 2)));
    const auto v = static_cast<std::int16_t>(static_cast<unsigned char>(pcm.value().samples[2])
                                             | (static_cast<unsigned char>(pcm.value().samples[3]) << 8));
    CHECK(v == -32124);
    s.encoding = SoundEncoding::ALaw;
    s.samples = QByteArray("\xD5", 1);
    pcm = core::playablePcm(s);
    REQUIRE(pcm);
    const auto a = static_cast<std::int16_t>(static_cast<unsigned char>(pcm.value().samples[0])
                                             | (static_cast<unsigned char>(pcm.value().samples[1]) << 8));
    CHECK(std::abs(a) <= 8);
    // Compressed, or a depth no player takes: refused by key.
    s.encoding = SoundEncoding::Signed;
    s.bits = 12;
    CHECK(keyOf(core::playablePcm(s).error()) == "error.sound.unplayable");
    s.bits = 16;
    s.compression = QStringLiteral("Foo");
    CHECK(keyOf(core::playablePcm(s).error()) == "error.sound.unplayable");
}

TEST_CASE("a sound's JSON round-trips and a hostile one is refused", "[sounddata][D6-18]")
{
    const core::Result<SoundData> sound = core::soundFromWav(wav(1, 2, 44100, 16, ramp(64, 2, 16)));
    REQUIRE(sound);
    const core::Result<SoundData> back = SoundData::fromJson(sound.value().toJson());
    REQUIRE(back);
    CHECK(back.value() == sound.value());
    for (const char* hostile : {R"({"rate": 1e300})", R"({"rate": 8000.5})", R"({"channels": 0})", R"({"bits": 33})",
                                R"({"encoding": "Opus"})", R"({"samples": "not base64!"})", R"({"samples": 7})"}) {
        INFO(hostile);
        CHECK_FALSE(SoundData::fromJson(QJsonDocument::fromJson(hostile).object()));
    }
    // What a COMMAND carries is narrower than what a file may say: 24 bits read from a file is the file's; carried, refused.
    SoundData read;
    read.bits = 24;
    read.rate = 96000;
    CHECK_FALSE(core::invalidSound(read));
    read.carried = true;
    read.samples = QByteArray(6, '\0');
    REQUIRE(core::invalidSound(read));
    CHECK(*core::invalidSound(read) == QLatin1String("sound.encoding"));
}

TEST_CASE("a sound comment is saved as a /Sound stream and read back byte for byte", "[sounddata][A-101][D6-18]")
{
    auto session = tests::openCopy("ordinary/text-a4.pdf");
    REQUIRE(session);
    const QByteArray wavSamples = ramp(3001, 1, 16);   // 6002 bytes: an odd frame count
    const core::Result<SoundData> sound = core::soundFromWav(wav(1, 1, 22050, 16, wavSamples));
    REQUIRE(sound);
    Annotation a = core::newSoundComment(0, core::PdfPoint{500, 780}, sound.value(), {1.0, 0.85, 0.3}, kAuthor, kNow);
    const QString nm = a.nm;
    REQUIRE(session->journal->run(std::make_unique<core::AddAnnotationCommand>(std::vector<Annotation>{a})));

    // Before the save, the samples are the command's.
    const core::Result<QByteArray> live = session->backend.soundSamples(session->handle, a.id);
    REQUIRE(live);
    CHECK(hexOf(live.value()) == hexOf(sound.value().samples));

    auto saved = session->coordinator->save(*session->journal, session->path);
    REQUIRE(saved);
    const QString keep = QDir(QStringLiteral(RAWPAGE_WRITTEN_DIR)).filePath(QStringLiteral("sounddata-wav.pdf"));
    REQUIRE(QDir().mkpath(QStringLiteral(RAWPAGE_WRITTEN_DIR)));
    QFile::remove(keep);
    REQUIRE(QFile::copy(session->path, keep));   // kept for `qpdf --check` (A-31)

    // READ BACK from the bytes on disk, first-party: the four numbers the WAV said, and the samples ISO's way round.
    const QByteArray bytes = tests::readAllBytes(session->path);
    auto opened = pdf::cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    REQUIRE(opened);
    auto walked = pdf::ops::PageOps::walk(opened.value().table);
    REQUIRE(walked);
    const core::AnnotationStore store =
        core::AnnotationStore::fromReading(pdf::ops::AnnotationReader::read(opened.value().table, walked.value()));
    const Annotation* read = nullptr;
    for (const Annotation& x : store.all())
        if (x.nm == nm) read = &x;
    REQUIRE(read != nullptr);
    REQUIRE(read->sound);
    CHECK(read->sound->rate == 22050);
    CHECK(read->sound->channels == 1);
    CHECK(read->sound->bits == 16);
    CHECK(read->sound->encoding == SoundEncoding::Signed);
    CHECK_FALSE(read->sound->carried);   // the file's: its samples are read when asked for
    CHECK(read->icon == QLatin1String("Speaker"));
    CHECK(read->hasAppearance);

    // And from a document opened on the saved file, through the backend: the WAV's samples, exactly.
    tests::CosBackend fresh;
    auto handle = fresh.open(session->path, nullptr);
    REQUIRE(handle);
    const core::Result<QByteArray> samples = fresh.soundSamples(handle.value(), read->id);
    REQUIRE(samples);
    CHECK(hexOf(samples.value()) == hexOf(sound.value().samples));
    SoundData heard = *read->sound;
    heard.samples = samples.value();
    heard.carried = true;
    const core::Result<QByteArray> wavBack = core::soundToWav(heard);
    REQUIRE(wavBack);
    CHECK(hexOf(dataChunkOf(wavBack.value())) == hexOf(wavSamples));   // A-101: the samples BYTE-IDENTICAL, WAV to PDF to WAV
    (void)fresh.close(handle.value());
}

TEST_CASE("the corpus's sound is read as the generator labelled it", "[sounddata][A-101][D6-18]")
{
    const QJsonObject want = everyTypeSound().value(QStringLiteral("sound")).toObject();
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    const Annotation* sound = nullptr;
    for (const Annotation& a : store.all())
        if (a.subtype == AnnotationSubtype::Sound) sound = &a;
    REQUIRE(sound != nullptr);
    REQUIRE(sound->sound);
    CHECK(sound->sound->rate == want.value(QStringLiteral("rate")).toInt());
    CHECK(sound->sound->channels == want.value(QStringLiteral("channels")).toInt());
    CHECK(sound->sound->bits == want.value(QStringLiteral("bits")).toInt());
    CHECK(QString(core::soundEncodingName(sound->sound->encoding)) == want.value(QStringLiteral("encoding")).toString());
    const core::Result<QByteArray> samples = session->backend.soundSamples(session->handle, sound->id);
    REQUIRE(samples);
    CHECK(sha256Hex(samples.value()) == want.value(QStringLiteral("sha256")).toString());
    CHECK(samples.value().size() * 8 / (sound->sound->bits * sound->sound->channels) == want.value(QStringLiteral("samples")).toInt());

    // RECOLOURED: since T14 a sound is drawn by our builder -- a Speaker in its new colour, the stream untouched.
    core::AnnotationAppearance blue;
    blue.color = std::vector<double>{0.0, 0.0, 1.0};
    REQUIRE(session->journal->run(std::make_unique<core::SetAnnotationAppearanceCommand>(std::vector<core::AnnotationId>{sound->id},
                                                                                         blue, kNow)));
    const pdf::ops::MutationSinkImpl::Edit* edit = session->sink()->editOf(sound->id);
    REQUIRE(edit != nullptr);
    CHECK(edit->redraw);
    const core::Result<QByteArray> still = session->backend.soundSamples(session->handle, sound->id);
    REQUIRE(still);
    CHECK(hexOf(still.value()) == hexOf(samples.value()));
}

TEST_CASE("a sound with nothing to play is not written and a hostile journal line is refused", "[sounddata][D6-18]")
{
    // A Sound made with no samples: the sink refuses it by key before any command lists it.
    Annotation empty = core::newAnnotation(AnnotationSubtype::Sound, 0, kAuthor, kNow);
    empty.rect = core::PdfRect{core::PdfPoint{10, 10}, core::PdfSize{20, 20}};
    const core::Status refused = pdf::ops::MutationSinkImpl::canCreate(empty);
    REQUIRE_FALSE(refused);
    CHECK(keyOf(refused.error()) == "error.sound.noData");
    // The speaker and the microphone are drawn; any other icon is refused by name.
    empty.icon = QStringLiteral("Mic");
    CHECK(pdf::ops::AppearanceStreamBuilder::normal(empty));
    empty.icon = QStringLiteral("Horn");
    const auto horn = pdf::ops::AppearanceStreamBuilder::normal(empty);
    REQUIRE_FALSE(horn);
    CHECK(keyOf(horn.error()) == "error.pdf.appearanceUnsupported");

    // A journal line carrying a sound a command would not carry: refused by the command's parser.
    const core::Result<SoundData> sound = core::soundFromWav(wav(1, 1, 8000, 8, ramp(50, 1, 8)));
    REQUIRE(sound);
    Annotation a = core::newSoundComment(0, core::PdfPoint{100, 700}, sound.value(), {1.0, 0.85, 0.3}, kAuthor, kNow);
    const QJsonObject good = core::AddAnnotationCommand(std::vector<Annotation>{a}).serialize();
    REQUIRE(core::CommandRegistry::deserialize(good));
    for (const char* field : {"rate", "bits", "encoding"}) {
        INFO(field);
        QJsonObject line = good;
        QJsonArray annotations = line.value(QStringLiteral("annotations")).toArray();
        QJsonObject first = annotations[0].toObject();
        QJsonObject s = first.value(QStringLiteral("sound")).toObject();
        if (std::string(field) == "rate") s.insert(QStringLiteral("rate"), 500);
        if (std::string(field) == "bits") s.insert(QStringLiteral("bits"), 24);
        if (std::string(field) == "encoding") s.insert(QStringLiteral("encoding"), QStringLiteral("muLaw"));
        first.insert(QStringLiteral("sound"), s);
        annotations[0] = first;
        line.insert(QStringLiteral("annotations"), annotations);
        CHECK_FALSE(core::CommandRegistry::deserialize(line));
    }
}

TEST_CASE("an export carries a sound's samples and an import brings them back", "[sounddata][A-101][D6-18][D6-20]")
{
    // D6-40's revisit, paid at T14: the Comments pane's export writes a Sound WITH its samples -- read from the document
    // by the writer, as an attached file's bytes are -- in both formats, and the file read back holds them exactly.
    const QJsonObject want = everyTypeSound().value(QStringLiteral("sound")).toObject();
    auto session = tests::openCopy("ordinary/annots-every-type.pdf");
    REQUIRE(session);
    const core::AnnotationStore& store = session->journal->document().annotations();
    std::vector<std::size_t> all;
    for (std::size_t i = 0; i < store.all().size(); ++i) all.push_back(i);
    const core::Fdf exported = store.exportFdf(all);
    for (const core::ExchangeFormat format : {core::ExchangeFormat::Xfdf, core::ExchangeFormat::Fdf}) {
        const QString path = session->directory.filePath(format == core::ExchangeFormat::Xfdf ? QStringLiteral("out.xfdf")
                                                                                               : QStringLiteral("out.fdf"));
        REQUIRE(session->backend.writeAnnotationExchange(session->handle, exported, format, path, true));
        const core::Result<core::Fdf> read = session->backend.readAnnotationExchange(path);
        REQUIRE(read);
        int sounds = 0;
        for (const Annotation& a : read.value().annotations) {
            if (a.subtype != AnnotationSubtype::Sound) continue;
            ++sounds;
            REQUIRE(a.sound);
            CHECK(a.sound->carried);
            CHECK(a.sound->rate == want.value(QStringLiteral("rate")).toInt());
            CHECK(sha256Hex(a.sound->samples) == want.value(QStringLiteral("sha256")).toString());
        }
        CHECK(sounds == 1);
    }
    for (const core::ExchangeNote& note : exported.notes) CHECK(note.kind != core::ExchangeNoteKind::NotExported);
}
