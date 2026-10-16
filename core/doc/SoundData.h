// core/doc/SoundData -- the samples a `Sound` annotation carries, and the WAV files they come from and go back to.
// Stage 6 T14, decision D6-18 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// A SOUND IS PCM, AS ISO 32000-1 §13.3 STORES IT
// ===========================================================================================
// A sound object is a stream of samples and four numbers -- `/R` samples a second, `/C` channels, `/B` bits a
// sample, `/E` their encoding (`Raw`, unsigned; `Signed`, two's complement; `muLaw`; `ALaw`). A sample wider than a
// byte is stored MOST SIGNIFICANT BYTE FIRST, and the channels are interleaved, the first channel first. RawPage
// writes only the two encodings a WAV file holds without a codec: 8-bit `Raw` -- WAV's own 8-bit samples are unsigned
// -- and 16-bit `Signed`, each 16-bit sample turned from WAV's little-endian into ISO's big-endian. Nothing is
// resampled, mixed or compressed, so a WAV taken in and written back out holds exactly the bytes it started with
// (guide case A-101).
//
// ===========================================================================================
// THE MODEL HOLDS THE SAMPLES ONLY FOR A SOUND RAWPAGE MADE
// ===========================================================================================
// As an attachment's bytes (`core::EmbeddedFile`): a sound RawPage records or attaches is CARRIED by the command
// that adds it, so a replay and an undo add the same sound; one READ from a document is the document's, and its
// samples are read from the file when they are asked for (`IDocumentBackend::soundSamples`) -- a five-minute
// recording nobody plays costs nothing. `carried` says which.
//
// ===========================================================================================
// WHAT IS REFUSED, AND BY WHAT NAME
// ===========================================================================================
// `soundFromWav` takes RIFF WAVE with an uncompressed PCM `fmt ` chunk -- format 1, or WAVE_FORMAT_EXTENSIBLE whose
// subformat is PCM -- one or two channels, 8 or 16 bits, 1,000 to 192,000 samples a second, and a `data` chunk of
// whole frames no larger than `kMaxSoundBytes`. ANY OTHER FORMAT is refused BY ITS TAG (`error.sound.notPcm`, the
// tag as context: `0x0003` is float, `0x0055` MP3), never converted: a codec is exactly what D6-18 keeps out.
//
// Pure: QtCore only, no engine, no display -- `tst_sounddata` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_DOC_SOUNDDATA_H
#define RAWPAGE_CORE_DOC_SOUNDDATA_H

#include "core/Result.h"

#include <QByteArray>
#include <QJsonObject>
#include <QLatin1String>
#include <QString>

#include <cstdint>
#include <optional>

namespace rawpage::core {

// ISO 32000-1 Table 294's `/E`.
enum class SoundEncoding : std::uint8_t { Raw, Signed, MuLaw, ALaw };

// `Raw`, `Signed`, `muLaw`, `ALaw` -- the names as a file writes them.
QLatin1String soundEncodingName(SoundEncoding encoding) noexcept;
std::optional<SoundEncoding> soundEncodingFromName(const QString& name) noexcept;

// THE BOUNDS OF A SOUND A COMMAND CARRIES -- the recovery journal's parser is a parser of hostile input. 64 MiB is
// more than six minutes of 16-bit stereo at 44.1 kHz, and a journal line past 64 KiB goes to a side file (D6-23).
inline constexpr qsizetype kMaxSoundBytes = qsizetype{64} * 1024 * 1024;
inline constexpr int kMinSoundRate = 1000;
inline constexpr int kMaxSoundRate = 192000;

struct SoundData {
    int rate = 8000;                            // /R: samples a second, each channel
    int channels = 1;                           // /C
    int bits = 8;                               // /B
    SoundEncoding encoding = SoundEncoding::Raw;   // /E
    QString compression;                        // /CO, read only: a sound this build cannot play when set
    // THE SAMPLES, as the stream holds them once its filters are undone: interleaved, big-endian -- see the
    // header. Held only for a sound RawPage made (`carried`); a plain array and a flag rather than an optional,
    // for the GCC 13.1 false positive `EmbeddedFile` met.
    QByteArray samples;
    bool carried = false;

    // Bytes a frame -- one sample of every channel -- and frames in `samples`.
    int frameBytes() const noexcept { return channels * ((bits + 7) / 8); }
    double seconds() const noexcept;

    // `{"rate", "channels", "bits", "encoding", "compression", "samples" (base64)}`, each only when it says
    // something beyond the defaults; `fromJson(toJson(s)) == s`. Refused: a rate, channel count or bit depth out of
    // the ranges `invalidSound` names, an encoding ISO does not, samples that are not base64.
    QJsonObject toJson() const;
    static Result<SoundData> fromJson(const QJsonObject& json);

    bool operator==(const SoundData&) const = default;
};

// What a sound a command CARRIES must be -- a sound RawPage made: one or two channels, 8 or 16 bits in the encoding
// RawPage writes for that depth, a rate in range, whole frames, at most `kMaxSoundBytes`, no compression. Nullopt
// when it is one; else the field that is not (`sound.rate` …). A sound READ from a file is the file's, and only its
// numbers are checked: positive, and a depth of at most 32 bits.
std::optional<QString> invalidSound(const SoundData& sound);

// A WAV file's PCM as a sound RawPage writes it -- see the header for what is accepted. `carried` is set.
Result<SoundData> soundFromWav(const QByteArray& wav);

// THE WAV FILE a sound plays as: a 44-byte RIFF header and the samples, in WAV's own order -- 8-bit unsigned, or
// 16-bit signed little-endian. `soundToWav(soundFromWav(w))`'s `data` chunk is `w`'s byte for byte. A `muLaw` or
// `ALaw` sound is expanded to 16-bit (ITU-T G.711); `error.sound.unplayable` for a depth other than 8 or 16 bits,
// more than two channels, compression, or samples that are not whole frames.
Result<QByteArray> soundToWav(const SoundData& sound);

// The samples a PLAYER takes -- what `soundToWav` puts after its header -- and the depth they are in (8 or 16).
struct PlayablePcm {
    QByteArray samples;   // 8-bit unsigned, or 16-bit signed little-endian, interleaved
    int bits = 16;
    int channels = 1;
    int rate = 8000;
};
Result<PlayablePcm> playablePcm(const SoundData& sound);

} // namespace rawpage::core

#endif // RAWPAGE_CORE_DOC_SOUNDDATA_H
