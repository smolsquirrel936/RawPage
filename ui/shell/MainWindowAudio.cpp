// ui/shell/MainWindowAudio -- the window's half of a sound comment: the recorder asked, the WAV asked, a sound played.
// Stage 6 T14, decision D6-18 (`Stage6-Annotations-Plan.md` §3). Defined apart from `MainWindow.cpp` as T13's
// `MainWindowExchange.cpp` is: one subject, one file.
//
// THE QUESTIONS ARE THE WINDOW'S, THE COMMAND THE OVERLAY'S. `AnnotationOverlay::recordSoundAt` and `attachSoundAt` ask
// through hooks every document's overlay is given (`MainWindow::adoptDocument`), so a tab torn off into another window
// asks there (convention 14); each answers nothing where nobody can be asked (convention 16) unless a case installed an
// answer (`setAudioAnswersForTesting`). PLAYING changes no byte: a sound's samples are read -- carried by the command
// that recorded it, or from its `/Sound` stream (`IDocumentBackend::soundSamples`) -- and handed to the one player.

#include "ui/shell/MainWindow.h"

#include "core/Log.h"
#include "core/doc/AnnotationStore.h"
#include "ui/shell/Catalog.h"
#include "ui/shell/DocumentWindow.h"
#include "ui/shell/StatusBar.h"
#include "ui/viewer/AnnotationOverlay.h"
#include "ui/viewer/AudioRecorder.h"

#include <QFileDialog>
#include <QGuiApplication>

#include <cmath>

namespace rawpage::ui {

using core::LogSubsystem;

namespace {

bool canPrompt()
{
    return QGuiApplication::platformName() != QLatin1String("offscreen");
}

MainWindow::AudioAnswers& installedAudioAnswers()
{
    static MainWindow::AudioAnswers answers;
    return answers;
}

QString duration(double seconds)
{
    const int whole = static_cast<int>(std::lround(std::max(0.0, seconds)));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

void MainWindow::setAudioAnswersForTesting(AudioAnswers answers)
{
    installedAudioAnswers() = std::move(answers);
}

std::optional<core::SoundData> MainWindow::askRecording()
{
    if (installedAudioAnswers().recording) return installedAudioAnswers().recording();
    if (!canPrompt()) return std::nullopt;
    AudioRecorderDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return std::nullopt;
    return dialog.recorded();
}

std::optional<QString> MainWindow::askSoundPath()
{
    if (installedAudioAnswers().soundPath) return installedAudioAnswers().soundPath();
    if (!canPrompt()) return std::nullopt;
    const QString chosen = QFileDialog::getOpenFileName(this, catalog::translate(catalog::key::AudioAttachTitle), QString(),
                                                        catalog::translate(catalog::key::AudioWavFilter));
    if (chosen.isEmpty()) return std::nullopt;
    return chosen;
}

core::Status MainWindow::playSound(const core::AnnotationId& id)
{
    DocumentWindow* document = currentDocument();
    const AnnotationOverlay* overlay = document ? document->annotations() : nullptr;
    const core::AnnotationStore* store = overlay ? overlay->store() : nullptr;
    const auto refuse = [this](const core::Error& error) -> core::Status {
        if (status_) status_->setMessage(catalog::translateError(QString(error.messageKey), error.context));
        return error;
    };
    const std::optional<std::size_t> at = store ? store->find(id) : std::nullopt;
    if (!at || store->all()[*at].subtype != core::AnnotationSubtype::Sound || !store->all()[*at].sound)
        return refuse(core::Error{core::ErrorCode::NotFound, QLatin1String("error.sound.noData"), id.toString()});
    core::SoundData sound = *store->all()[*at].sound;
    if (!sound.carried) {
        core::Result<QByteArray> samples = services_.backend.soundSamples(document->handle(), id);
        if (!samples) return refuse(samples.error());
        sound.samples = std::move(samples).value();
        sound.carried = true;
    }
    lastPlayed_ = sound;
    if (player_ == nullptr) player_ = new SoundPlayer(this);
    const core::Status played = player_->play(sound);
    if (!played) {
        RAWPAGE_LOG_INFO(LogSubsystem::Ui) << "audio: could not play" << id.toString() << "--" << QString(played.error().messageKey);
        return refuse(played.error());
    }
    if (status_) status_->setMessage(catalog::translate(catalog::key::AudioStatusPlaying).arg(duration(sound.seconds())));
    return core::Status{};
}

} // namespace rawpage::ui
