// ui/viewer/AudioRecorder -- recording a sound comment, and playing one back. Stage 6 T14, decision D6-18
// (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// QT MULTIMEDIA, MEDIA FOUNDATION ONLY (D6-18, Q5)
// ===========================================================================================
// Capture and playback are Qt Multimedia's `QAudioSource` and `QAudioSink` -- raw PCM in and out, no codec -- through
// its WINDOWS backend, Media Foundation and WASAPI, which are the operating system's. Qt 6 prefers its FFmpeg backend
// wherever the plugin can be found, and the Qt prefix a developer's PATH points at HAS it; so `useMediaFoundation()`
// sets `QT_MEDIA_BACKEND=windows` before the first multimedia object exists (unless the environment already says), and
// the deployment never copies the FFmpeg plugin or its five DLLs beside `rawpage.exe` (guide case A-07). This file is
// the only one that names Qt Multimedia, and only `rawpage_ui` links it -- never `core/`.
//
// ===========================================================================================
// WHAT IS RECORDED
// ===========================================================================================
// 16-bit mono at 44.1 kHz when the microphone takes it, else its own preferred format converted to 16 bits
// (`soundFromCapture`: float and 32-bit samples scaled, more than two channels mixed to one) -- and stored as ISO
// 32000-1 §13.3 stores a sound: big-endian `Signed` (`core/doc/SoundData`). A recording stops itself before its samples
// would pass what one command may carry (`core::kMaxSoundBytes`). THE DIALOG COLLECTS: it hands back the samples, and
// the overlay constructs the one command (`AnnotationOverlay::recordSoundAt`).
//
// Offscreen there is no microphone: the dialog says so and records nothing, and a case answers through the window's
// recorder hook instead. A real recording is a by-hand run on the `windows` platform (guide A-101), as Stage 5's A-50
// is for the system clipboard.

#ifndef RAWPAGE_UI_VIEWER_AUDIORECORDER_H
#define RAWPAGE_UI_VIEWER_AUDIORECORDER_H

#include "core/Result.h"
#include "core/doc/SoundData.h"

#include <QByteArray>
#include <QDialog>
#include <QObject>

#include <memory>
#include <optional>

class QAudioFormat;
class QAudioSink;
class QAudioSource;
class QBuffer;
class QIODevice;
class QLabel;
class QPushButton;
class QTimer;

namespace rawpage::ui {

// `QT_MEDIA_BACKEND=windows` unless the environment already names a backend -- see the header. Idempotent; called by
// every multimedia object here before it touches Qt Multimedia, and by `app/main.cpp` before the application exists.
void useMediaFoundation();

// Samples a `QAudioSource` captured in `format` as a sound RawPage writes: 8-bit unsigned kept as `Raw`, 16-bit kept,
// 32-bit integer and float scaled to 16-bit, all big-endian `Signed`; three or more channels mixed to one. Refused by
// key for a format with no samples (`error.sound.unsupported`) or a capture past `core::kMaxSoundBytes`.
core::Result<core::SoundData> soundFromCapture(const QByteArray& captured, const QAudioFormat& format);

// ONE SOUND PLAYED -- a double click on a sound comment, the recorder's preview. Playing changes no byte.
class SoundPlayer final : public QObject {
    Q_OBJECT

public:
    explicit SoundPlayer(QObject* parent = nullptr);
    ~SoundPlayer() override;

    // `sound` played from its start on the default output, stopping whatever was playing. Refused by key for a sound
    // this build cannot play (`core::playablePcm`), no output device (`error.sound.noDevice`), or a format the device
    // refuses (`error.sound.unplayable`).
    core::Status play(const core::SoundData& sound);
    void stop();
    bool isPlaying() const;

signals:
    void finished();

private:
    std::unique_ptr<QAudioSink> sink_;
    std::unique_ptr<QBuffer> buffer_;
};

class AudioRecorderDialog final : public QDialog {
    Q_OBJECT

public:
    explicit AudioRecorderDialog(QWidget* parent = nullptr);
    ~AudioRecorderDialog() override;

    // What was recorded, once the person stopped and chose OK; nothing before.
    std::optional<core::SoundData> recorded() const { return recorded_; }
    // Whether there is a microphone to record from at all.
    bool hasInput() const noexcept { return hasInput_; }

    // For the cases: the buttons, and the status line.
    QPushButton* recordButton() const noexcept { return record_; }
    QPushButton* stopButton() const noexcept { return stop_; }
    QPushButton* playButton() const noexcept { return play_; }
    QLabel* statusLabel() const noexcept { return status_; }

    // The longest recording, in seconds, at the format recording uses.
    double maximumSeconds() const;

private:
    void startRecording();
    void stopRecording();
    void updateStatus();

    bool hasInput_ = false;
    std::optional<core::SoundData> recorded_;
    std::unique_ptr<QAudioSource> source_;
    // What the source has delivered so far: read from Qt's own capture device as it arrives (pull mode), so nothing
    // here opens a device for writing -- a recording is memory until the command carries it.
    QByteArray captured_;
    QIODevice* capture_ = nullptr;
    std::unique_ptr<QAudioFormat> format_;
    SoundPlayer* player_ = nullptr;
    QTimer* clock_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* record_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* play_ = nullptr;
    QPushButton* ok_ = nullptr;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_AUDIORECORDER_H
