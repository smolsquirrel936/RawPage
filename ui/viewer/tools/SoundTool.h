// ui/viewer/tools/SoundTool -- the audio comment tools: `annot.recordAudio` and `annot.attachAudio`. Stage 6 T14,
// decision D6-18 (`Stage6-Annotations-Plan.md` §3).
//
// A CLICK, as Acrobat's: where the page is clicked becomes the speaker icon's top-left corner, and then the sound is
// asked for -- RECORDED through the window's recorder (`ui/viewer/AudioRecorder`, Qt Multimedia's Media Foundation
// backend only), or read from a WAV file the window's chooser names (`core::soundFromWav`: PCM only, a codec refused by
// its tag). Either way ONE `AddAnnotationCommand` carries the samples (`AnnotationOverlay::recordSoundAt`,
// `attachSoundAt`), and nothing is made when the person stops short. A drag makes nothing.

#ifndef RAWPAGE_UI_VIEWER_TOOLS_SOUNDTOOL_H
#define RAWPAGE_UI_VIEWER_TOOLS_SOUNDTOOL_H

#include "ui/viewer/tools/AnnotationTool.h"
#include "ui/viewer/tools/Tool.h"

namespace rawpage::ui {

class SoundTool final : public Tool {
public:
    static bool handles(AnnotationToolKind kind) noexcept
    {
        return kind == AnnotationToolKind::RecordAudio || kind == AnnotationToolKind::AttachAudio;
    }

    ToolKind kind() const override { return ToolKind::Annotate; }
    Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

    AnnotationToolKind which() const noexcept { return which_; }
    void setWhich(AnnotationToolKind which) noexcept { which_ = which; }

    void press(ToolHost& host, QPoint pos) override;
    void move(ToolHost& host, QPoint pos) override;
    void release(ToolHost& host, QPoint pos) override;
    void cancel(ToolHost& host) override;
    bool isDragging() const override { return pressed_; }

private:
    AnnotationToolKind which_ = AnnotationToolKind::RecordAudio;
    bool pressed_ = false;
    QPoint pressedAt_;
};

} // namespace rawpage::ui

#endif // RAWPAGE_UI_VIEWER_TOOLS_SOUNDTOOL_H
