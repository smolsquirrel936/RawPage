#include "ui/shell/ShortcutBinder.h"

#include "core/Log.h"
#include "core/cmd/ActionRegistry.h"
#include "core/cmd/KeyBindingStore.h"

#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QShortcut>
#include <QWidget>

#include <algorithm>
#include <array>
#include <string>

namespace rawpage::ui {
namespace {

using core::ActionId;
using core::Chord;
using core::KeyContext;
using core::LogSubsystem;

// ===============================================================================================
// THE KEY TABLE -- one table, both directions.
// ===============================================================================================
// Everything else is generic, and that is not a shortcut: every printable ASCII key in Qt's enum
// IS its ASCII code point (`Qt::Key_A == 0x41`, `Qt::Key_Equal == 0x3d`), so the letters, the
// digits and the thirteen punctuation keys §8 binds (`'`, `*`, `+`, `,`, `-`, `.`, `/`, `;`, `<`,
// `=`, `>`, `[`, `]`) need no rows at all. The F-keys are likewise arithmetic. What is left is the
// set §8 spells as WORDS, and those are the only ones a table can get wrong.
//
// The table is consulted FIRST in both directions, which is what keeps `Space` spelled `Space`
// rather than as a literal blank -- a settings-file key (`shortcuts/<context>/<action>`) holding a
// bare space would be a key nobody could read or hand-edit.
struct NamedKey {
    Qt::Key          key;
    std::string_view name;   // exactly the spelling `Chord::parse` canonicalizes §8 to
};

constexpr std::array<NamedKey, 15> kNamedKeys{{
    {Qt::Key_Space,     "Space"},
    {Qt::Key_Tab,       "Tab"},
    {Qt::Key_Backspace, "Backspace"},
    {Qt::Key_Return,    "Enter"},
    {Qt::Key_Escape,    "Esc"},
    {Qt::Key_Delete,    "Delete"},
    {Qt::Key_Home,      "Home"},
    {Qt::Key_End,       "End"},
    {Qt::Key_PageUp,    "Page Up"},
    {Qt::Key_PageDown,  "Page Down"},
    {Qt::Key_Up,        "Up"},
    {Qt::Key_Down,      "Down"},
    {Qt::Key_Left,      "Left"},
    {Qt::Key_Right,     "Right"},
    // A bare modifier is a legitimate §8 binding -- `Alt` alone opens the ribbon key tips -- and
    // `Chord::parse` keeps it distinct from `Alt+Enter` by making the modifier the KEY. So the
    // conversion has to be able to name it, in both directions.
    {Qt::Key_Alt,       "Alt"},
}};

std::optional<std::string> nameForKey(int key)
{
    // Two normalizations before the lookup, both of them things the keyboard does rather than
    // things §8 says. The numeric keypad's Enter is a different key code and the same key; and
    // Shift+Tab does not arrive as Tab at all -- Qt turns it into Backtab, which is why
    // `Ctrl+Shift+Tab` would otherwise resolve to nothing while every other Shift chord worked.
    if (key == Qt::Key_Enter) key = Qt::Key_Return;
    if (key == Qt::Key_Backtab) key = Qt::Key_Tab;

    for (const NamedKey& named : kNamedKeys)
        if (named.key == key) return std::string(named.name);

    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return "F" + std::to_string(key - Qt::Key_F1 + 1);

    // The printable-ASCII identity. Space is excluded because the table above owns it.
    if (key > 0x20 && key <= 0x7e) return std::string(1, static_cast<char>(key));

    return std::nullopt;
}

std::optional<Qt::Key> keyForName(std::string_view name)
{
    for (const NamedKey& named : kNamedKeys)
        if (named.name == name) return named.key;

    if (name.size() >= 2 && name.front() == 'F') {
        const std::string_view digits = name.substr(1);
        if (std::all_of(digits.begin(), digits.end(),
                        [](char c) { return c >= '0' && c <= '9'; })) {
            const int number = std::stoi(std::string(digits));
            if (number >= 1 && number <= 35)
                return static_cast<Qt::Key>(Qt::Key_F1 + number - 1);
        }
    }

    if (name.size() == 1) {
        const char c = name.front();
        if (c > 0x20 && c <= 0x7e) return static_cast<Qt::Key>(c);
    }

    return std::nullopt;
}

std::uint8_t modifiersFor(Qt::KeyboardModifiers modifiers)
{
    std::uint8_t out = 0;
    if (modifiers & Qt::ControlModifier) out |= static_cast<std::uint8_t>(core::ChordModifier::Ctrl);
    if (modifiers & Qt::AltModifier)     out |= static_cast<std::uint8_t>(core::ChordModifier::Alt);
    if (modifiers & Qt::ShiftModifier)   out |= static_cast<std::uint8_t>(core::ChordModifier::Shift);
    return out;
}

QString contextText(KeyContext context)
{
    const std::string_view name = core::keyContextName(context);
    return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
}

QString chordText(const Chord& chord)
{
    const std::string text = chord.toString();
    return QString::fromLatin1(text.data(), static_cast<qsizetype>(text.size()));
}

QString actionText(ActionId id)
{
    const std::string_view name = core::actionStringId(id);
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

} // namespace

// ===============================================================================================

ShortcutBinder::ShortcutBinder(core::ActionRegistry& registry,
                               const core::KeyBindingStore& bindings,
                               QWidget* scope)
    : QObject(scope), registry_(registry), bindings_(bindings), scope_(scope)
{
    // ON THE APPLICATION, NOT ON THE WINDOW. A key event is delivered to the FOCUS WIDGET, and
    // reaches the window only if every widget between them declined it -- so a filter on the
    // window would see `Ctrl+O` typed into a tab strip and never see it typed into a text field.
    // The application object is the only one that sees every event before its receiver does.
    //
    // The `window() == scope_` test in `eventFilter` is what keeps a second window's binder from
    // dispatching the same chord a second time. See the class comment.
    if (qApp) qApp->installEventFilter(this);
}

ShortcutBinder::~ShortcutBinder()
{
    // Qt removes the filter when this object is destroyed, but `qApp` may already be gone during
    // teardown and an explicit removal costs nothing. The reason to be explicit is I-12's: an
    // object reached after its destructor started is the class of bug that is invisible until the
    // day it is not, and a filter is exactly such a reach.
    if (qApp) qApp->removeEventFilter(this);
}

// --- 1. The focus chain (D1-5) -----------------------------------------------------------------

const char* ShortcutBinder::contextPropertyName() { return "rawpageKeyContext"; }

void ShortcutBinder::declareContext(QWidget* widget, KeyContext context)
{
    if (!widget) return;

    if (context == KeyContext::Common) {
        // Refused rather than stored. `core/cmd/KeyContext.h` states it: `Common` is a resolution
        // LAYER, not a place the focus can be, and `activeContext()` returning it would make the
        // innermost-then-Common-then-Global order resolve Common twice and the real context never.
        RAWPAGE_LOG_ERROR(LogSubsystem::Ui)
            << "shortcuts: refusing to declare Common as a widget context on"
            << widget->objectName();
        return;
    }

    const std::string_view name = core::keyContextName(context);
    widget->setProperty(contextPropertyName(),
                        QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size())));
}

std::optional<KeyContext> ShortcutBinder::declaredContext(const QWidget* widget)
{
    if (!widget) return std::nullopt;

    const QVariant declared = widget->property(contextPropertyName());
    if (!declared.isValid()) return std::nullopt;

    // Resolved through `keyContextFromName`, over the same table `keyContextName` wrote it with --
    // the third caller of that function, and for the reason its own comment gives: a second list of
    // context names is a list that disagrees the first time a context is added.
    const QByteArray name = declared.toString().toLatin1();
    const std::optional<KeyContext> context =
        core::keyContextFromName(std::string_view(name.constData(), name.size()));
    if (!context) {
        RAWPAGE_LOG_WARNING(LogSubsystem::Ui)
            << "shortcuts: widget" << widget->objectName() << "declares an unknown key context"
            << declared.toString();
    }
    return context;
}

KeyContext ShortcutBinder::contextFor(const QWidget* focus)
{
    // The INNERMOST declaration wins -- a Comments pane inside a Viewer is Comments, which is the
    // whole reason the walk goes outward rather than the other way. `parentWidget()` rather than
    // `parent()`, so a widget parented to a plain QObject ends the chain instead of walking into
    // something that has no focus of its own.
    for (const QWidget* widget = focus; widget; widget = widget->parentWidget())
        if (const std::optional<KeyContext> declared = declaredContext(widget)) return *declared;

    // NOTHING DECLARED IS NOT AN ERROR. It is the whole of Stage 1: the Viewer surface is Stage 2,
    // the panes are Stage 4 and later, Write mode is Stage 13. `Global` is the correct answer for a
    // shell that has none of them yet, and it is what makes every §8 Global default work today.
    return KeyContext::Global;
}

KeyContext ShortcutBinder::activeContext() const
{
    // This window's focus widget, not the application's. With two windows open, a ribbon click in
    // one must not be attributed to whatever has focus in the other -- and `QWidget::focusWidget()`
    // answers per window, which is exactly the question being asked.
    return contextFor(scope_ ? scope_->focusWidget() : nullptr);
}

// --- 2. The QtGui boundary ---------------------------------------------------------------------

std::optional<Chord> ShortcutBinder::chordFor(const QKeyEvent* event)
{
    if (!event) return std::nullopt;

    // The Windows key is the OS's. §8 binds nothing with it, and treating `Meta+O` as `O` would
    // fire a command while the user was talking to the shell.
    if (event->modifiers() & Qt::MetaModifier) return std::nullopt;

    const std::optional<std::string> name = nameForKey(event->key());
    if (!name) return std::nullopt;

    std::uint8_t modifiers = modifiersFor(event->modifiers());

    // Shift+Tab arrives as Backtab with the Shift modifier ALREADY set, so nothing to add here --
    // but Backtab with Shift somehow stripped is still a Shift+Tab, and saying so costs one line.
    if (event->key() == Qt::Key_Backtab)
        modifiers |= static_cast<std::uint8_t>(core::ChordModifier::Shift);

    // A BARE MODIFIER IS ITS OWN CHORD, and it carries no modifier set -- `Chord::parse` does the
    // same with §8's `Alt` row, and the two have to agree or the row would never match. Pressing
    // Alt reports AltModifier on some platforms and not others; dropping it here makes the answer
    // the same on both.
    if (event->key() == Qt::Key_Alt) {
        if (modifiers & ~static_cast<std::uint8_t>(core::ChordModifier::Alt)) return std::nullopt;
        return Chord{0, *name};
    }
    if (event->key() == Qt::Key_Control || event->key() == Qt::Key_Shift
        || event->key() == Qt::Key_Meta)
        return std::nullopt;

    return Chord{modifiers, *name};
}

std::optional<Qt::Key> ShortcutBinder::qtKeyFor(const Chord& chord)
{
    return keyForName(chord.key());
}

Qt::KeyboardModifiers ShortcutBinder::qtModifiersFor(const Chord& chord)
{
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    if (chord.hasCtrl()) modifiers |= Qt::ControlModifier;
    if (chord.hasAlt()) modifiers |= Qt::AltModifier;
    if (chord.hasShift()) modifiers |= Qt::ShiftModifier;
    return modifiers;
}

QList<QString> ShortcutBinder::unreachableBindings(const core::KeyBindingStore& bindings)
{
    QList<QString> unreachable;

    // EVERY row, dispatched and widget-native alike. A native row we cannot name is not harmless:
    // the store would answer `WidgetNative` for a chord this class can never form, so the row would
    // be documented, loaded, censused -- and dead. Checking both halves against the same standard
    // is what `KeyBindingDefaults.def` asks for in its own header.
    for (const core::KeyBinding& binding : bindings.allBindings()) {
        if (qtKeyFor(binding.chord)) continue;
        unreachable.append(QStringLiteral("%1 in %2 names a key no keyboard event can produce")
                               .arg(chordText(binding.chord), contextText(binding.context)));
    }
    return unreachable;
}

// --- 3. Resolution and dispatch ----------------------------------------------------------------

ShortcutBinder::Outcome ShortcutBinder::activate(const Chord& chord, KeyContext context) const
{
    // `resolve`, not `lookup`, and the difference is the whole of the WidgetNative case: `lookup`
    // answers nothing for a row the focused widget owns, which is indistinguishable from no row at
    // all. Here they must not be conflated -- one may fall through to a parent, the other must be
    // left to the widget that already implements it.
    const std::optional<core::KeyBinding> binding = bindings_.resolve(chord, context);
    if (!binding) return Outcome::NoBinding;
    if (binding->kind == core::BindingKind::WidgetNative || !binding->action)
        return Outcome::WidgetNative;

    const core::Result<void> dispatched = registry_.dispatch(*binding->action, context);
    if (dispatched) return Outcome::Dispatched;

    // The registry's two refusals mean opposite things to a keyboard, which is why T3 made them
    // separate codes: `Unavailable` is a command answering "not right now" and the key has done its
    // job; `NotFound` is an id nothing has registered, and the key has not been handled at all.
    if (dispatched.error().code == core::ErrorCode::Unavailable) return Outcome::Unavailable;
    return Outcome::NotImplemented;
}

bool ShortcutBinder::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::KeyPress) return QObject::eventFilter(watched, event);

    auto* widget = qobject_cast<QWidget*>(watched);
    if (!widget || !scope_ || widget->window() != scope_)
        return QObject::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const std::optional<Chord> chord = chordFor(keyEvent);
    if (!chord) return QObject::eventFilter(watched, event);

    // HOLDING A MODIFIER IS ONE GESTURE. Auto-repeat is right for `Page Down` and wrong for `Alt`:
    // key tips should open once and stay open, not reopen thirty times a second.
    if (keyEvent->isAutoRepeat() && chord->key() == "Alt")
        return QObject::eventFilter(watched, event);

    // FIRST REFUSAL TO THE FOCUSED WIDGET, through Qt's own handshake -- see the class comment.
    // The probe is `ignore()`d before it is sent because QEvent starts out accepted, and a widget
    // that does nothing with it must read as "no".
    if (QWidget* focus = scope_->focusWidget()) {
        QKeyEvent probe(QEvent::ShortcutOverride, keyEvent->key(), keyEvent->modifiers(),
                        keyEvent->text(), keyEvent->isAutoRepeat(),
                        static_cast<ushort>(keyEvent->count()));
        probe.ignore();
        QCoreApplication::sendEvent(focus, &probe);
        if (probe.isAccepted()) return QObject::eventFilter(watched, event);
    }

    const KeyContext context = activeContext();
    const Outcome    outcome = activate(*chord, context);

    if (outcome == Outcome::NotImplemented) {
        // Named rather than silent, because this is the ordinary state of most of §8 at Stage 1 and
        // "the chord did nothing" has two very different causes -- an id nothing registered, and a
        // chord nothing binds. A log line tells them apart during the manual walk.
        if (const std::optional<core::KeyBinding> binding = bindings_.resolve(*chord, context);
            binding && binding->action) {
            RAWPAGE_LOG_DEBUG(LogSubsystem::Ui)
                << "shortcut:" << chordText(*chord) << "->" << actionText(*binding->action)
                << "is declared but not registered yet";
        }
    }

    // ACCEPTED WHEN CONSUMED, not just filtered out. Returning true from a filter stops delivery
    // but leaves the event's flag saying whatever it said, and a caller that reads the flag to
    // decide whether anybody handled the key -- Qt itself does, when propagating -- would be told
    // the wrong thing.
    if (!consumes(outcome)) return QObject::eventFilter(watched, event);
    keyEvent->accept();
    return true;
}

// --- The self-check ----------------------------------------------------------------------------

QList<QString> ShortcutBinder::shortcutComplaints(const QWidget* root)
{
    QList<QString> complaints;
    if (!root) return complaints;

    // KIND ONE -- an action carrying its own key sequence. Qt would then resolve that key itself,
    // against no context and against no user remap, and the chord would work perfectly right up
    // until §8 said it should mean something else here.
    for (const QAction* action : root->findChildren<QAction*>()) {
        if (action->shortcuts().isEmpty()) continue;
        // THE WORDING AVOIDS THE LITERAL `QAction` FOLLOWED BY A SPACE, and that is not fussiness:
        // `layer-lint`'s `bare-qaction` rule is a deliberately blunt substring scan with one
        // allowed file and no inline waiver, so it matches the phrase in prose as readily as in
        // code. Narrowing the gate to spare a diagnostic would weaken a non-escapable rule for a
        // cosmetic reason; rewording the diagnostic costs nothing. Do not "fix" this back.
        complaints.append(QStringLiteral("action \"%1\" carries the shortcut %2 -- an "
                                         "action-level shortcut bypasses KeyBindingStore's "
                                         "context stack and the user's remap")
                              .arg(action->objectName().isEmpty() ? action->text()
                                                                  : action->objectName(),
                                   action->shortcut().toString()));
    }

    // KIND TWO -- a free-standing QShortcut. Same bypass, different door, and named separately
    // because the fix is different: an action needs its sequence removed, a QShortcut needs
    // deleting and its behaviour registering as a command.
    for (const QShortcut* shortcut : root->findChildren<QShortcut*>()) {
        complaints.append(QStringLiteral("a QShortcut for %1 exists on this window -- every chord "
                                         "resolves through ShortcutBinder, or it is reachable from "
                                         "the keyboard and from nothing else")
                              .arg(shortcut->key().toString()));
    }

    return complaints;
}

} // namespace rawpage::ui
