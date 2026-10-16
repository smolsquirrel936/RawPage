// ui/viewer/AudioRecorder -- implementation. Stage 6 T14. See the header for the backend and what is recorded.

#include "ui/viewer/AudioRecorder.h"

#include "ui/shell/Catalog.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rawpage::ui {
namespace {

constexpr int kRecordRate = 44100;
constexpr int kStatusTickMs = 200;

QString minutesAndSeconds(double seconds)
{
    const int whole = static_cast<int>(std::floor(std::max(0.0, seconds)));
    return QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'));
}

void putBe16(QByteArray& out, std::int16_t v)
{
    const auto u = static_cast<std::uint16_t>(v);
    out.append(static_cast<char>((u >> 8) & 0xFF));
    out.append(static_cast<char>(u & 0xFF));
}

std::int16_t clamp16(double v)
{
    return static_cast<std::int16_t>(std::lround(std::clamp(v, -32768.0, 32767.0)));
}

// One sample of channel `c` of frame `f` as a value from -1 to 1.
double sampleAt(const QByteArray& in, const QAudioFormat& format, qsizetype f, int c)
{
    const int bytes = format.bytesPerSample();
    const char* p = in.constData() + (f * format.channelCount() + c) * bytes;
    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8: return (static_cast<unsigned char>(*p) - 128.0) / 128.0;
    case QAudioFormat::Int16: {
        std::int16_t v;
        std::memcpy(&v, p, 2);
        return v / 32768.0;
    }
    case QAudioFormat::Int32: {
        std::int32_t v;
        std::memcpy(&v, p, 4);
        return v / 2147483648.0;
    }
    case QAudioFormat::Float: {
        float v;
        std::memcpy(&v, p, 4);
        return std::isfinite(v) ? static_cast<double>(v) : 0.0;
    }
    default: return 0.0;
    }
}

} // namespace

void useMediaFoundation()
{
    if (qEnvironmentVariableIsEmpty("QT_MEDIA_BACKEND")) qputenv("QT_MEDIA_BACKEND", "windows");
}

core::Result<core::SoundData> soundFromCapture(const QByteArray& captured, const QAudioFormat& format)
{
    const int bytes = format.bytesPerSample();
    const int channels = format.channelCount();
    if (bytes <= 0 || channels <= 0 || format.sampleRate() < core::kMinSoundRate || format.sampleRate() > core::kMaxSoundRate)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.sound.unsupported"),
                           QStringLiteral("rate %1").arg(format.sampleRate())};
    const qsizetype frames = captured.size() / (static_cast<qsizetype>(bytes) * channels);
    if (frames == 0) return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.sound.noData"), QString()};

    core::SoundData sound;
    sound.rate = format.sampleRate();
    sound.carried = true;
    const int keep = channels <= 2 ? channels : 1;   // more than two are mixed to one
    sound.channels = keep;
    if (format.sampleFormat() == QAudioFormat::UInt8 && channels <= 2) {
        // 8-bit unsigned is ISO's Raw, kept as captured.
        sound.bits = 8;
        sound.encoding = core::SoundEncoding::Raw;
        sound.samples = captured.left(frames * channels);
    } else {
        sound.bits = 16;
        sound.encoding = core::SoundEncoding::Signed;
        sound.samples.reserve(frames * keep * 2);
        for (qsizetype f = 0; f < frames; ++f) {
            if (keep == channels) {
                for (int c = 0; c < channels; ++c) putBe16(sound.samples, clamp16(sampleAt(captured, format, f, c) * 32768.0));
            } else {
                double sum = 0.0;
                for (int c = 0; c < channels; ++c) sum += sampleAt(captured, format, f, c);
                putBe16(sound.samples, clamp16(sum / channels * 32768.0));
            }
        }
    }
    if (sound.samples.size() > core::kMaxSoundBytes)
        return core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.sound.tooLarge"), QString::number(sound.samples.size())};
    return sound;
}

// ---- SoundPlayer ----------------------------------------------------------------------------------------

SoundPlayer::SoundPlayer(QObject* parent) : QObject(parent)
{
    useMediaFoundation();
}

SoundPlayer::~SoundPlayer()
{
    stop();
}

core::Status SoundPlayer::play(const core::SoundData& sound)
{
    stop();
    core::Result<core::PlayablePcm> pcm = core::playablePcm(sound);
    if (!pcm) return pcm.error();
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull()) return core::Error{core::ErrorCode::Unavailable, QLatin1String("error.sound.noDevice"), QString()};
    QAudioFormat format;
    format.setSampleRate(pcm.value().rate);
    format.setChannelCount(pcm.value().channels);
    format.setSampleFormat(pcm.value().bits == 8 ? QAudioFormat::UInt8 : QAudioFormat::Int16);
    if (!device.isFormatSupported(format))
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.sound.unplayable"),
                           QStringLiteral("%1 Hz %2 bit").arg(format.sampleRate()).arg(pcm.value().bits)};
    buffer_ = std::make_unique<QBuffer>();
    buffer_->setData(pcm.value().samples);
    buffer_->open(QIODevice::ReadOnly);
    sink_ = std::make_unique<QAudioSink>(device, format);
    connect(sink_.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        // The buffer read to its end: done. A stop is not a finish anyone needs told twice.
        if (state == QAudio::IdleState) {
            stop();
            emit finished();
        }
    });
    sink_->start(buffer_.get());
    if (sink_->error() != QAudio::NoError) {
        stop();
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.sound.unplayable"), QString()};
    }
    return core::Status{};
}

void SoundPlayer::stop()
{
    if (sink_) {
        sink_->disconnect(this);
        sink_->stop();
        sink_.reset();
    }
    buffer_.reset();
}

bool SoundPlayer::isPlaying() const
{
    return sink_ && sink_->state() == QAudio::ActiveState;
}

// ---- AudioRecorderDialog --------------------------------------------------------------------------------

AudioRecorderDialog::AudioRecorderDialog(QWidget* parent) : QDialog(parent)
{
    useMediaFoundation();
    setObjectName(QStringLiteral("audioRecorder"));
    setWindowTitle(catalog::translate(catalog::key::AudioDialogTitle));

    auto* column = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("audioRecorderStatus"));
    column->addWidget(status_);

    auto* buttons = new QHBoxLayout;
    record_ = new QPushButton(catalog::translate(catalog::key::AudioDialogRecord), this);
    record_->setObjectName(QStringLiteral("audioRecorderRecord"));
    stop_ = new QPushButton(catalog::translate(catalog::key::AudioDialogStop), this);
    stop_->setObjectName(QStringLiteral("audioRecorderStop"));
    play_ = new QPushButton(catalog::translate(catalog::key::AudioDialogPlay), this);
    play_->setObjectName(QStringLiteral("audioRecorderPlay"));
    for (QPushButton* b : {record_, stop_, play_}) buttons->addWidget(b);
    column->addLayout(buttons);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = box->button(QDialogButtonBox::Ok);
    column->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    player_ = new SoundPlayer(this);
    clock_ = new QTimer(this);
    clock_->setInterval(kStatusTickMs);
    connect(clock_, &QTimer::timeout, this, [this] {
        // Stopped before the samples would pass what one command carries.
        if (source_ && format_ && captured_.size() >= static_cast<qsizetype>(maximumSeconds() * format_->bytesForDuration(1000000)))
            stopRecording();
        updateStatus();
    });
    connect(record_, &QPushButton::clicked, this, [this] { startRecording(); });
    connect(stop_, &QPushButton::clicked, this, [this] {
        if (source_) stopRecording();
        else player_->stop();
        updateStatus();
    });
    connect(play_, &QPushButton::clicked, this, [this] {
        if (!recorded_) return;
        const core::Status played = player_->play(*recorded_);
        if (!played) status_->setText(catalog::translateError(QString(played.error().messageKey), played.error().context));
    });

    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    hasInput_ = !input.isNull();
    updateStatus();
}

AudioRecorderDialog::~AudioRecorderDialog()
{
    if (source_) source_->stop();
}

double AudioRecorderDialog::maximumSeconds() const
{
    // 16-bit samples of each channel, the format recording asks for or converts to.
    const int channels = format_ ? std::min(format_->channelCount(), 2) : 1;
    const int rate = format_ ? format_->sampleRate() : kRecordRate;
    return static_cast<double>(core::kMaxSoundBytes) / (2.0 * channels * rate) * 0.99;
}

void AudioRecorderDialog::startRecording()
{
    if (!hasInput_ || source_) return;
    player_->stop();
    recorded_.reset();
    const QAudioDevice input = QMediaDevices::defaultAudioInput();
    QAudioFormat format;
    format.setSampleRate(kRecordRate);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);
    if (!input.isFormatSupported(format)) format = input.preferredFormat();   // converted when it stops
    format_ = std::make_unique<QAudioFormat>(format);
    captured_.clear();
    source_ = std::make_unique<QAudioSource>(input, format);
    capture_ = source_->start();
    if (capture_ == nullptr || source_->error() != QAudio::NoError) {
        source_.reset();
        capture_ = nullptr;
        status_->setText(catalog::translateError(QStringLiteral("error.sound.recordFailed"), QString()));
        return;
    }
    connect(capture_, &QIODevice::readyRead, this, [this] {
        if (capture_ != nullptr) captured_.append(capture_->readAll());
    });
    clock_->start();
    updateStatus();
}

void AudioRecorderDialog::stopRecording()
{
    if (!source_) return;
    if (capture_ != nullptr) captured_.append(capture_->readAll());   // what arrived since the last readyRead
    source_->stop();
    clock_->stop();
    const QByteArray bytes = captured_;
    source_.reset();
    capture_ = nullptr;
    captured_.clear();
    core::Result<core::SoundData> sound = format_ ? soundFromCapture(bytes, *format_) : core::Result<core::SoundData>(
        core::Error{core::ErrorCode::InvalidArgument, QLatin1String("error.sound.noData"), QString()});
    if (sound) recorded_ = std::move(sound).value();
    else status_->setText(catalog::translateError(QString(sound.error().messageKey), sound.error().context));
    updateStatus();
}

void AudioRecorderDialog::updateStatus()
{
    const bool recording = source_ != nullptr;
    record_->setEnabled(hasInput_ && !recording);
    stop_->setEnabled(recording || player_->isPlaying());
    play_->setEnabled(!recording && recorded_.has_value());
    ok_->setEnabled(!recording && recorded_.has_value());
    if (!hasInput_) {
        status_->setText(catalog::translate(catalog::key::AudioDialogNoInput));
    } else if (recording) {
        const double seconds = format_ ? static_cast<double>(format_->durationForBytes(static_cast<qint32>(
                                             std::min<qsizetype>(captured_.size(), 0x7FFFFFFF)))) / 1e6
                                       : 0.0;
        status_->setText(catalog::translate(catalog::key::AudioDialogRecording).arg(minutesAndSeconds(seconds)));
    } else if (recorded_) {
        status_->setText(catalog::translate(catalog::key::AudioDialogRecorded).arg(minutesAndSeconds(recorded_->seconds())));
    } else {
        status_->setText(catalog::translate(catalog::key::AudioDialogReady));
    }
}

} // namespace rawpage::ui
