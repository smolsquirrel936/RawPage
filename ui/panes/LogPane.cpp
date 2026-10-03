#include "ui/panes/LogPane.h"

#include "ui/shell/Catalog.h"
#include "ui/shell/DockEdge.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStringList>
#include <QTextCursor>
#include <QVBoxLayout>

#include <algorithm>
#include <cstddef>
#include <limits>

namespace rawpage::ui {
namespace {

using core::LogEntry;
using core::LogLevel;
using core::LogSubsystem;
using core::LogSubsystemMask;

// The token every choice carries, resolved back through the SAME name tables the settings codecs
// use. A second spelling of "warning" here would be a filter that silently matched nothing.
QString levelLabel(LogLevel level)
{
    return catalog::translate(QStringLiteral("prefs.choice.logLevel.")
                              + QLatin1String(core::logLevelName(level)));
}

QString subsystemLabel(LogSubsystem subsystem)
{
    return catalog::translate(QStringLiteral("prefs.choice.logSubsystem.")
                              + QLatin1String(core::logSubsystemName(subsystem)));
}

} // namespace

QString LogPane::formatEntry(const LogEntry& entry)
{
    return QStringLiteral("%1 [%2][%3] %4")
        .arg(entry.when.toString(Qt::ISODateWithMs),
             QLatin1String(core::logLevelName(entry.level)),
             QLatin1String(core::logSubsystemName(entry.subsystem)),
             entry.message);
}

LogPane::LogPane(core::LogBuffer& buffer, QWidget* parent)
    : QDockWidget(parent), buffer_(buffer)
{
    setObjectName(QStringLiteral("logPane"));
    setWindowTitle(catalog::translate(catalog::key::PrefsLogTitle));
    // ALL FOUR AREAS, and the default placement does the opinionating instead. The window docks it
    // along the bottom because log lines are wide and that is where every editor puts them; a user
    // who wants it down one side is not making a mistake, and `setAllowedAreas` would have been a
    // restriction dressed up as a layout preference. Manual case M-04 walks exactly that.

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);

    // --- The filter row --------------------------------------------------------------------
    auto* filters = new QHBoxLayout;
    filters->addWidget(new QLabel(catalog::translate(catalog::key::PrefsLogLevel), body));

    levelBox_ = new QComboBox(body);
    levelBox_->setObjectName(QStringLiteral("logLevelFilter"));
    for (const LogLevel level : {LogLevel::Error, LogLevel::Warning, LogLevel::Info,
                                 LogLevel::Debug, LogLevel::Trace})
        levelBox_->addItem(levelLabel(level), static_cast<int>(level));
    levelBox_->setCurrentIndex(levelBox_->findData(static_cast<int>(levelFilter_)));
    filters->addWidget(levelBox_);

    filters->addWidget(new QLabel(catalog::translate(catalog::key::PrefsLogSubsystem), body));

    subsystemBox_ = new QComboBox(body);
    subsystemBox_->setObjectName(QStringLiteral("logSubsystemFilter"));
    // -1 is "all of them", which is not a subsystem and must not be spelled as one. The seven real
    // values carry their own enum value; the item that means everything carries a value no
    // subsystem can have.
    subsystemBox_->addItem(catalog::translate(catalog::key::PrefsLogAllSubsystems), -1);
    for (std::size_t i = 0; i < core::kLogSubsystemCount; ++i) {
        const auto subsystem = static_cast<LogSubsystem>(i);
        subsystemBox_->addItem(subsystemLabel(subsystem), static_cast<int>(i));
    }
    filters->addWidget(subsystemBox_);
    filters->addStretch(1);

    auto* copy = new QPushButton(catalog::translate(catalog::key::PrefsLogCopy), body);
    copy->setObjectName(QStringLiteral("logCopy"));
    filters->addWidget(copy);

    auto* clearButton = new QPushButton(catalog::translate(catalog::key::PrefsLogClear), body);
    clearButton->setObjectName(QStringLiteral("logClear"));
    filters->addWidget(clearButton);
    column->addLayout(filters);

    // --- The view --------------------------------------------------------------------------
    view_ = new QPlainTextEdit(body);
    view_->setObjectName(QStringLiteral("logView"));
    view_->setReadOnly(true);
    // A monospaced view, because every line starts with a timestamp of a fixed width and a column
    // that does not line up is a column nobody scans.
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    // NO MORE LINES THAN THE BUFFER HOLDS. `rebuild()` reads the capped buffer, but the live path
    // appends, and until Stage 2 issue I-35 nothing trimmed it: a pane nobody had opened kept every
    // line the process ever logged, measured at ~1.1 MB of live heap per round of nine documents
    // with no plateau. The view drops its oldest line as the buffer drops its oldest record.
    // Under a narrowing filter the view can hold lines older than the buffer's oldest -- still at
    // most `capacity()` of them, so still bounded, and the next `rebuild()` realigns it.
    view_->setMaximumBlockCount(
        static_cast<int>(std::min<std::size_t>(buffer_.capacity(), std::numeric_limits<int>::max())));
    column->addWidget(view_, 1);

    // --- The note ----------------------------------------------------------------------------
    note_ = new QLabel(body);
    note_->setObjectName(QStringLiteral("logThresholdNote"));
    note_->setWordWrap(true);
    column->addWidget(note_);

    setWidget(body);
    // THE EDGE THAT FACES THE PAGE, drawn (`ui/shell/DockEdge`, the user's request, S5 guide A-124).
    new DockEdge(this);

    connect(levelBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        setLevelFilter(static_cast<LogLevel>(levelBox_->itemData(index).toInt()));
    });
    connect(subsystemBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) return;
        const int value = subsystemBox_->itemData(index).toInt();
        if (value < 0) {
            setSubsystemFilter(LogSubsystemMask::all());
            return;
        }
        LogSubsystemMask mask = LogSubsystemMask::none();
        mask.add(static_cast<LogSubsystem>(value));
        setSubsystemFilter(mask);
    });
    connect(copy, &QPushButton::clicked, this, [this] { copyToClipboard(); });
    connect(clearButton, &QPushButton::clicked, this, [this] { clear(); });

    rebuild();

    // MARSHALLED TO THIS THREAD. The handler runs on whoever logged -- a render worker from Stage 2
    // -- and touching a widget from there is a crash that only starts happening once there are
    // worker threads. `invokeMethod` with `this` as the context object posts to this object's
    // thread; `~QObject` removes its own posted events, so a record in flight when the pane is
    // destroyed is dropped rather than delivered.
    subscription_ = buffer_.subscribe([this](const LogEntry& entry) {
        QMetaObject::invokeMethod(
            this, [this, entry] { appendIfVisible(entry); }, Qt::QueuedConnection);
    });
}

LogPane::~LogPane()
{
    // FIRST, and before any child is destroyed. The handler holds `this`, the buffer outlives every
    // window, and a notification arriving after this destructor started is issue I-12 wearing its
    // fifth disguise.
    if (subscription_ != 0) buffer_.unsubscribe(subscription_);
}

bool LogPane::passesFilter(const LogEntry& entry) const
{
    if (static_cast<int>(entry.level) > static_cast<int>(levelFilter_)) return false;
    return subsystemFilter_.contains(entry.subsystem);
}

void LogPane::setLevelFilter(LogLevel level)
{
    if (level == levelFilter_) return;
    levelFilter_ = level;
    if (levelBox_->currentData().toInt() != static_cast<int>(level))
        levelBox_->setCurrentIndex(levelBox_->findData(static_cast<int>(level)));
    rebuild();
}

void LogPane::setSubsystemFilter(LogSubsystemMask mask)
{
    if (mask == subsystemFilter_) return;
    subsystemFilter_ = mask;
    rebuild();
}

void LogPane::rebuild()
{
    QStringList lines;
    for (const LogEntry& entry : buffer_.entries())
        if (passesFilter(entry)) lines << formatEntry(entry);

    view_->setPlainText(lines.join(QLatin1Char('\n')));
    view_->moveCursor(QTextCursor::End);
    updateThresholdNote();
}

void LogPane::appendIfVisible(const LogEntry& entry)
{
    if (!passesFilter(entry)) {
        // The COUNT still moved even though the line did not, and the note reports the count.
        updateThresholdNote();
        return;
    }
    view_->appendPlainText(formatEntry(entry));
    updateThresholdNote();
}

void LogPane::updateThresholdNote()
{
    // The one thing that will otherwise be filed as a bug: this pane cannot show a record that was
    // never recorded. `logging/level` is the global threshold and it is applied before the buffer;
    // the filter above only narrows. Saying which threshold is in force, in the same words the
    // Logging preference page uses, is what turns "the filter is broken" into "I need to change
    // the setting".
    const QString threshold = levelLabel(core::logThreshold());
    QString text = catalog::translate(catalog::key::PrefsLogThreshold).arg(threshold);

    if (const std::size_t discarded = buffer_.discardedCount(); discarded > 0) {
        text += QLatin1Char(' ')
                + catalog::translate(catalog::key::PrefsLogDiscarded)
                      .arg(static_cast<qint64>(discarded));
    }
    note_->setText(text);
}

QString LogPane::visibleText() const
{
    return view_->toPlainText();
}

int LogPane::visibleRecordCount() const
{
    const QString text = view_->toPlainText();
    if (text.isEmpty()) return 0;
    return static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
}

void LogPane::copyToClipboard()
{
    if (QClipboard* clipboard = QApplication::clipboard()) clipboard->setText(visibleText());
}

void LogPane::clear()
{
    buffer_.clear();
    rebuild();
}

} // namespace rawpage::ui
