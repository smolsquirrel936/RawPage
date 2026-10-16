// core/doc/SoundData -- implementation. Stage 6 T14. See the header for what is accepted and how it is stored.

#include "core/doc/SoundData.h"

#include <QByteArrayView>
#include <QJsonValue>

#include <array>
#include <cmath>
#include <cstring>

namespace rawpage::core {
namespace {

Error soundError(ErrorCode code, const char* key, const QString& context = {})
{
    return Error{code, QLatin1String(key), context};
}

std::uint16_t le16(const char* p)
{
    return static_cast<std::uint16_t>(static_cast<unsigned char>(p[0]) | (static_cast<unsigned char>(p[1]) << 8));
}

std::uint32_t le32(const char* p)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(p[0])) | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[1])) << 8)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[2])) << 16)
           | (static_cast<std::uint32_t>(static_cast<unsigned char>(p[3])) << 24);
}

void putLe16(QByteArray& out, std::uint16_t v)
{
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

void putLe32(QByteArray& out, std::uint32_t v)
{
    for (int shift = 0; shift < 32; shift += 8) out.append(static_cast<char>((v >> shift) & 0xFF));
}

// Every second byte pair swapped: a 16-bit stream from one byte order to the other.
QByteArray swapped16(const QByteArray& in)
{
    QByteArray out(in.size(), Qt::Uninitialized);
    for (qsizetype i = 0; i + 1 < in.size(); i += 2) {
        out[i] = in[i + 1];
        out[i + 1] = in[i];
    }
    return out;
}

// ITU-T G.711, expanded to linear 16-bit.
std::int16_t muLawSample(std::uint8_t code)
{
    code = static_cast<std::uint8_t>(~code);
    const int sign = code & 0x80;
    const int exponent = (code >> 4) & 0x07;
    const int mantissa = code & 0x0F;
    int sample = ((mantissa << 3) + 0x84) << exponent;
    sample -= 0x84;
    return static_cast<std::int16_t>(sign != 0 ? -sample : sample);
}

std::int16_t aLawSample(std::uint8_t code)
{
    code ^= 0x55;
    const int sign = code & 0x80;
    const int exponent = (code >> 4) & 0x07;
    const int mantissa = code & 0x0F;
    int sample = exponent == 0 ? (mantissa << 4) + 8 : ((mantissa << 4) + 0x108) << (exponent - 1);
    return static_cast<std::int16_t>(sign != 0 ? sample : -sample);
}

// The WAVE_FORMAT_EXTENSIBLE subformat GUID of PCM, after its first two bytes (which are the format tag itself):
// `xxxx0000-0000-0010-8000-00aa00389b71`.
constexpr std::array<unsigned char, 14> kGuidTail = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                                                     0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

QString tagText(std::uint16_t tag)
{
    return QStringLiteral("0x%1").arg(tag, 4, 16, QLatin1Char('0'));
}

} // namespace

QLatin1String soundEncodingName(SoundEncoding encoding) noexcept
{
    switch (encoding) {
    case SoundEncoding::Raw: return QLatin1String("Raw");
    case SoundEncoding::Signed: return QLatin1String("Signed");
    case SoundEncoding::MuLaw: return QLatin1String("muLaw");
    case SoundEncoding::ALaw: return QLatin1String("ALaw");
    }
    return QLatin1String("Raw");
}

std::optional<SoundEncoding> soundEncodingFromName(const QString& name) noexcept
{
    if (name == QLatin1String("Raw")) return SoundEncoding::Raw;
    if (name == QLatin1String("Signed")) return SoundEncoding::Signed;
    if (name == QLatin1String("muLaw")) return SoundEncoding::MuLaw;
    if (name == QLatin1String("ALaw")) return SoundEncoding::ALaw;
    return std::nullopt;
}

double SoundData::seconds() const noexcept
{
    const int frame = frameBytes();
    if (frame <= 0 || rate <= 0) return 0.0;
    return static_cast<double>(samples.size() / frame) / static_cast<double>(rate);
}

QJsonObject SoundData::toJson() const
{
    QJsonObject json;
    json.insert(QStringLiteral("rate"), rate);
    json.insert(QStringLiteral("channels"), channels);
    json.insert(QStringLiteral("bits"), bits);
    json.insert(QStringLiteral("encoding"), QString(soundEncodingName(encoding)));
    if (!compression.isEmpty()) json.insert(QStringLiteral("compression"), compression);
    if (carried) json.insert(QStringLiteral("samples"), QString::fromLatin1(samples.toBase64()));
    return json;
}

Result<SoundData> SoundData::fromJson(const QJsonObject& json)
{
    const auto malformed = [](const char* field) {
        return Error{ErrorCode::ParseFailure, QLatin1String("error.command.malformed"), QString::fromLatin1(field)};
    };
    SoundData s;
    // Whole numbers a double holds exactly, in a range a reader keeps -- a hostile line's 1e300 is not a rate.
    const auto whole = [&json](const char* key, int low, int high, int& out) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isUndefined()) return true;
        const double d = v.toDouble(-1.0);
        if (!v.isDouble() || d < low || d > high || d != std::floor(d)) return false;
        out = static_cast<int>(d);
        return true;
    };
    if (!whole("rate", 1, 10000000, s.rate)) return malformed("sound.rate");
    if (!whole("channels", 1, 64, s.channels)) return malformed("sound.channels");
    if (!whole("bits", 1, 32, s.bits)) return malformed("sound.bits");
    const std::optional<SoundEncoding> encoding =
        soundEncodingFromName(json.value(QStringLiteral("encoding")).toString(QStringLiteral("Raw")));
    if (!encoding) return malformed("sound.encoding");
    s.encoding = *encoding;
    s.compression = json.value(QStringLiteral("compression")).toString().left(64);
    if (const QJsonValue samples = json.value(QStringLiteral("samples")); !samples.isUndefined()) {
        if (!samples.isString()) return malformed("sound.samples");
        auto decoded = QByteArray::fromBase64Encoding(samples.toString().toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
        if (!decoded) return malformed("sound.samples");
        s.samples = std::move(*decoded);
        s.carried = true;
    }
    return s;
}

std::optional<QString> invalidSound(const SoundData& sound)
{
    if (sound.rate <= 0) return QStringLiteral("sound.rate");
    if (sound.channels <= 0) return QStringLiteral("sound.channels");
    if (sound.bits <= 0 || sound.bits > 32) return QStringLiteral("sound.bits");
    if (!sound.carried) return std::nullopt;
    if (sound.rate < kMinSoundRate || sound.rate > kMaxSoundRate) return QStringLiteral("sound.rate");
    if (sound.channels > 2) return QStringLiteral("sound.channels");
    const bool rawpageWrites = (sound.bits == 8 && sound.encoding == SoundEncoding::Raw)
                               || (sound.bits == 16 && sound.encoding == SoundEncoding::Signed);
    if (!rawpageWrites) return QStringLiteral("sound.encoding");
    if (!sound.compression.isEmpty()) return QStringLiteral("sound.compression");
    if (sound.samples.isEmpty() || sound.samples.size() > kMaxSoundBytes || sound.samples.size() % sound.frameBytes() != 0)
        return QStringLiteral("sound.samples");
    return std::nullopt;
}

Result<SoundData> soundFromWav(const QByteArray& wav)
{
    const char* const data = wav.constData();
    const qsizetype size = wav.size();
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        return soundError(ErrorCode::ParseFailure, "error.sound.notWav");

    std::optional<std::uint16_t> tag;
    std::uint16_t channels = 0, blockAlign = 0, bits = 0;
    std::uint32_t rate = 0;
    std::optional<QByteArray> samples;
    qsizetype at = 12;
    while (at + 8 <= size) {
        const char* chunk = data + at;
        const std::uint32_t length = le32(chunk + 4);
        const qsizetype body = at + 8;
        if (static_cast<qsizetype>(length) > size - body) {
            // A chunk that runs past the file: a recording cut short, or a header written before the length was known.
            return soundError(ErrorCode::ParseFailure, "error.sound.malformed", QString::fromLatin1(chunk, 4));
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (length < 16) return soundError(ErrorCode::ParseFailure, "error.sound.malformed", QStringLiteral("fmt "));
            const char* f = data + body;
            tag = le16(f);
            channels = le16(f + 2);
            rate = le32(f + 4);
            blockAlign = le16(f + 12);
            bits = le16(f + 14);
            if (*tag == 0xFFFE) {
                // WAVE_FORMAT_EXTENSIBLE: the real format is the subformat GUID's first two bytes.
                if (length < 40) return soundError(ErrorCode::ParseFailure, "error.sound.malformed", QStringLiteral("fmt "));
                const char* guid = f + 24;
                const std::uint16_t sub = le16(guid);
                const bool standard = std::memcmp(guid + 2, kGuidTail.data(), kGuidTail.size()) == 0;
                tag = standard ? sub : static_cast<std::uint16_t>(0xFFFE);
            }
        } else if (std::memcmp(chunk, "data", 4) == 0 && !samples) {
            if (static_cast<qsizetype>(length) > kMaxSoundBytes)
                return soundError(ErrorCode::InvalidArgument, "error.sound.tooLarge", QString::number(length));
            samples = QByteArray(data + body, static_cast<qsizetype>(length));
        }
        at = body + static_cast<qsizetype>(length) + (length & 1u);   // chunks are padded to an even length
    }
    if (!tag || !samples) return soundError(ErrorCode::ParseFailure, "error.sound.malformed", QString::fromLatin1(tag ? "data" : "fmt "));
    if (*tag != 1) return soundError(ErrorCode::Unsupported, "error.sound.notPcm", tagText(*tag));
    if (channels != 1 && channels != 2)
        return soundError(ErrorCode::Unsupported, "error.sound.unsupported", QStringLiteral("channels %1").arg(channels));
    if (bits != 8 && bits != 16)
        return soundError(ErrorCode::Unsupported, "error.sound.unsupported", QStringLiteral("bits %1").arg(bits));
    if (rate < static_cast<std::uint32_t>(kMinSoundRate) || rate > static_cast<std::uint32_t>(kMaxSoundRate))
        return soundError(ErrorCode::Unsupported, "error.sound.unsupported", QStringLiteral("rate %1").arg(rate));
    const int frame = channels * (bits / 8);
    if (blockAlign != frame || samples->isEmpty() || samples->size() % frame != 0)
        return soundError(ErrorCode::ParseFailure, "error.sound.malformed", QStringLiteral("data"));

    SoundData s;
    s.rate = static_cast<int>(rate);
    s.channels = channels;
    s.bits = bits;
    s.carried = true;
    if (bits == 8) {
        s.encoding = SoundEncoding::Raw;   // WAV's 8-bit samples are unsigned, which is ISO's Raw
        s.samples = std::move(*samples);
    } else {
        s.encoding = SoundEncoding::Signed;
        s.samples = swapped16(*samples);   // little-endian to ISO's most significant byte first
    }
    return s;
}

Result<PlayablePcm> playablePcm(const SoundData& sound)
{
    const auto unplayable = [](const QString& why) {
        return soundError(ErrorCode::Unsupported, "error.sound.unplayable", why);
    };
    if (!sound.compression.isEmpty()) return unplayable(QStringLiteral("compression ") + sound.compression);
    if (sound.channels < 1 || sound.channels > 2) return unplayable(QStringLiteral("channels %1").arg(sound.channels));
    if (sound.rate <= 0) return unplayable(QStringLiteral("rate %1").arg(sound.rate));
    PlayablePcm out;
    out.channels = sound.channels;
    out.rate = sound.rate;
    const QByteArray& in = sound.samples;
    if (sound.encoding == SoundEncoding::MuLaw || sound.encoding == SoundEncoding::ALaw) {
        if (sound.bits != 8) return unplayable(QStringLiteral("bits %1").arg(sound.bits));
        out.bits = 16;
        out.samples.reserve(in.size() * 2);
        for (const char c : in) {
            const auto code = static_cast<std::uint8_t>(c);
            const std::int16_t v = sound.encoding == SoundEncoding::MuLaw ? muLawSample(code) : aLawSample(code);
            putLe16(out.samples, static_cast<std::uint16_t>(v));
        }
        return out;
    }
    if (sound.bits == 8) {
        out.bits = 8;
        out.samples = in;
        if (sound.encoding == SoundEncoding::Signed) {
            for (char& c : out.samples) c = static_cast<char>(static_cast<unsigned char>(c) ^ 0x80u);   // to unsigned
        }
        return out;
    }
    if (sound.bits == 16) {
        if (in.size() % (2 * sound.channels) != 0) return unplayable(QStringLiteral("frames"));
        out.bits = 16;
        out.samples = swapped16(in);   // ISO's big-endian to WAV's little-endian
        if (sound.encoding == SoundEncoding::Raw) {
            for (qsizetype i = 1; i < out.samples.size(); i += 2)   // unsigned to signed: the high byte's top bit
                out.samples[i] = static_cast<char>(static_cast<unsigned char>(out.samples[i]) ^ 0x80u);
        }
        return out;
    }
    return unplayable(QStringLiteral("bits %1").arg(sound.bits));
}

Result<QByteArray> soundToWav(const SoundData& sound)
{
    Result<PlayablePcm> pcm = playablePcm(sound);
    if (!pcm) return pcm.error();
    const PlayablePcm& p = pcm.value();
    const std::uint32_t frame = static_cast<std::uint32_t>(p.channels * (p.bits / 8));
    const auto length = static_cast<std::uint32_t>(p.samples.size());
    QByteArray out;
    out.reserve(44 + p.samples.size() + 1);
    out.append("RIFF", 4);
    putLe32(out, 36 + length + (length & 1u));
    out.append("WAVEfmt ", 8);
    putLe32(out, 16);
    putLe16(out, 1);   // PCM
    putLe16(out, static_cast<std::uint16_t>(p.channels));
    putLe32(out, static_cast<std::uint32_t>(p.rate));
    putLe32(out, static_cast<std::uint32_t>(p.rate) * frame);
    putLe16(out, static_cast<std::uint16_t>(frame));
    putLe16(out, static_cast<std::uint16_t>(p.bits));
    out.append("data", 4);
    putLe32(out, length);
    out.append(p.samples);
    if ((length & 1u) != 0) out.append('\0');
    return out;
}

} // namespace rawpage::core
