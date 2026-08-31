#include "ui/shell/RibbonModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <set>

namespace rawpage::ui::ribbon {
namespace {

// Offsets to lines. QJsonDocument reports a byte offset for a SYNTAX error and nothing at all for a
// semantic one, so a semantic diagnostic finds its own line by locating the token it is about. That
// is a substring search rather than a real source map, and it is honest about the one case it gets
// wrong: the same spelling twice in the file resolves to the first. Naming the wrong one of two
// identical tokens is a far smaller cost than naming no line at all, which is what a strict answer
// would have to do.
int lineOfOffset(const QByteArray& bytes, int offset)
{
    if (offset < 0) return 0;
    const int limit = qMin(offset, static_cast<int>(bytes.size()));
    int line = 1;
    for (int i = 0; i < limit; ++i)
        if (bytes.at(i) == '\n') ++line;
    return line;
}

int lineOfToken(const QByteArray& bytes, const QString& token)
{
    if (token.isEmpty()) return 0;
    const QByteArray needle = QByteArrayLiteral("\"") + token.toUtf8() + QByteArrayLiteral("\"");
    const qsizetype at = bytes.indexOf(needle);
    return at < 0 ? 0 : lineOfOffset(bytes, static_cast<int>(at));
}

struct Collector {
    const QByteArray&        bytes;
    std::vector<Diagnostic>& out;

    void at(int line, const QString& message) { out.push_back(Diagnostic{line, message}); }

    void about(const QString& token, const QString& message)
    {
        out.push_back(Diagnostic{lineOfToken(bytes, token), message});
    }
};

QString quoted(const QString& text) { return QLatin1Char('"') + text + QLatin1Char('"'); }

} // namespace

QString Diagnostic::formatted(const QString& fileName) const
{
    // file:line: message -- the shape every compiler and both other gates already emit, so an editor
    // and a CI log annotator can find it without being taught a new format.
    const QString where = line > 0 ? QStringLiteral("%1:%2").arg(fileName).arg(line) : fileName;
    return where + QStringLiteral(": ") + message;
}

QStringList Spec::catalogKeys() const
{
    QStringList keys;
    for (const Tab& tab : tabs) {
        keys << tab.titleKey;

        // AND THE ACCESS KEY, which is derived from the tab id rather than written in the resource
        // (I-42) -- so this is the only place that can enumerate it. `--dump-ui-strings` is what
        // guide case A-72 compares between locales, and `i18n-lint` sees the reference in
        // `RibbonTabBuilder` as a NAMESPACE (`"ribbon.keytip."`, per I-27) and therefore cannot
        // check the nine members one by one. A family nothing enumerates is a family that can lose
        // a member in one locale and stay green everywhere.
        keys << tabAccessKeyKey(tab.id);

        for (const Group& group : tab.groups) keys << group.titleKey;
    }
    return keys;
}

QString tabAccessKeyKey(const QString& tabId)
{
    return QStringLiteral("ribbon.keytip.") + tabId;
}

int Spec::controlCount() const
{
    int count = 0;
    for (const Tab& tab : tabs)
        for (const Group& group : tab.groups) count += static_cast<int>(group.controls.size());
    return count;
}

ParseResult parse(const QByteArray& bytes)
{
    ParseResult result;
    Collector   diag{bytes, result.diagnostics};

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError) {
        diag.at(lineOfOffset(bytes, error.offset),
                QStringLiteral("not valid JSON -- %1").arg(error.errorString()));
        return result;
    }
    if (!document.isObject()) {
        diag.at(1, QStringLiteral("the document root must be an object"));
        return result;
    }

    const QJsonObject root = document.object();

    result.spec.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (result.spec.schemaVersion != kSchemaVersion) {
        diag.about(QStringLiteral("schemaVersion"),
                   QStringLiteral("schemaVersion is %1; this build understands %2")
                       .arg(result.spec.schemaVersion)
                       .arg(kSchemaVersion));
        // Deliberately not an early return. A version bump plus a typo should report both, or the
        // second one appears only after the first is fixed.
    }

    const QJsonValue tabsValue = root.value(QStringLiteral("tabs"));
    if (!tabsValue.isArray()) {
        diag.about(QStringLiteral("tabs"), QStringLiteral("\"tabs\" must be an array"));
        return result;
    }
    const QJsonArray tabs = tabsValue.toArray();
    if (tabs.isEmpty()) {
        diag.about(QStringLiteral("tabs"), QStringLiteral("\"tabs\" is empty -- a ribbon with no "
                                                          "tabs is a resource nobody meant to ship"));
        return result;
    }

    // D1-8's "duplicate placement". Tracked across the WHOLE file rather than per tab: the same
    // command in two groups is how a ribbon acquires two buttons that do the same thing and drift
    // apart in their tooltips, and it is invisible on screen because both of them work.
    std::set<QString> seenTabIds;
    std::set<QString> seenGroupIds;
    std::set<QString> seenActions;

    for (const QJsonValue& tabValue : tabs) {
        if (!tabValue.isObject()) {
            diag.about(QStringLiteral("tabs"), QStringLiteral("every entry of \"tabs\" must be an object"));
            continue;
        }
        const QJsonObject tabObject = tabValue.toObject();

        Tab tab;
        tab.id         = tabObject.value(QStringLiteral("id")).toString();
        tab.titleKey   = tabObject.value(QStringLiteral("title")).toString();
        tab.contextual = tabObject.value(QStringLiteral("contextual")).toBool(false);

        if (tab.id.isEmpty()) {
            diag.about(QStringLiteral("tabs"), QStringLiteral("a tab has no \"id\""));
            continue;
        }
        if (!seenTabIds.insert(tab.id).second)
            diag.about(tab.id, QStringLiteral("duplicate tab id %1").arg(quoted(tab.id)));

        if (!tab.titleKey.startsWith(QStringLiteral("ribbon.tab."))) {
            diag.about(tab.id, QStringLiteral("tab %1 has title %2; a tab title must be a catalog "
                                              "key beginning \"ribbon.tab.\"")
                                   .arg(quoted(tab.id), quoted(tab.titleKey)));
        }

        const QJsonArray groups = tabObject.value(QStringLiteral("groups")).toArray();
        if (groups.isEmpty()) {
            diag.about(tab.id, QStringLiteral("tab %1 has no groups").arg(quoted(tab.id)));
            continue;
        }

        for (const QJsonValue& groupValue : groups) {
            const QJsonObject groupObject = groupValue.toObject();

            Group group;
            group.id               = groupObject.value(QStringLiteral("id")).toString();
            group.titleKey         = groupObject.value(QStringLiteral("title")).toString();
            group.collapsePriority = groupObject.value(QStringLiteral("collapsePriority")).toInt(0);

            if (group.id.isEmpty()) {
                diag.about(tab.id, QStringLiteral("a group in tab %1 has no \"id\"").arg(quoted(tab.id)));
                continue;
            }
            if (!seenGroupIds.insert(group.id).second)
                diag.about(group.id, QStringLiteral("duplicate group id %1").arg(quoted(group.id)));

            if (!group.titleKey.startsWith(QStringLiteral("ribbon.group."))) {
                diag.about(group.id,
                           QStringLiteral("group %1 has title %2; a group title must be a catalog "
                                          "key beginning \"ribbon.group.\"")
                               .arg(quoted(group.id), quoted(group.titleKey)));
            }

            const QJsonArray controls = groupObject.value(QStringLiteral("controls")).toArray();

            // D1-8's "empty group". A group with no controls renders as a titled rectangle of
            // nothing, which reads as a rendering bug rather than as a resource mistake -- and the
            // person who removed the last control from it will not be the person who sees it.
            if (controls.isEmpty()) {
                diag.about(group.id,
                           QStringLiteral("group %1 has no controls").arg(quoted(group.id)));
                continue;
            }

            for (const QJsonValue& controlValue : controls) {
                const QJsonObject controlObject = controlValue.toObject();
                const QString     actionId = controlObject.value(QStringLiteral("action")).toString();
                const QString     sizeText =
                    controlObject.value(QStringLiteral("size")).toString(QStringLiteral("small"));

                if (actionId.isEmpty()) {
                    diag.about(group.id, QStringLiteral("a control in group %1 has no \"action\"")
                                             .arg(quoted(group.id)));
                    continue;
                }

                // D1-8's headline rule, and the one A-51 exercises. Resolved through
                // `actionFromStringId`, which honours ALIAS rows -- so a resource written against a
                // retired spelling keeps building, exactly as a user's saved shortcut keeps
                // resolving. Anything else would make D1-16's alias mechanism true for user data
                // and false for our own.
                const std::optional<core::ActionId> resolved =
                    core::actionFromStringId(actionId.toStdString());
                if (!resolved) {
                    diag.about(actionId,
                               QStringLiteral("unknown action id %1 in group %2 -- it is not "
                                              "declared in core/cmd/ActionId.def")
                                   .arg(quoted(actionId), quoted(group.id)));
                    continue;
                }

                if (!seenActions.insert(actionId).second) {
                    diag.about(actionId,
                               QStringLiteral("action %1 is placed more than once; a command "
                                              "belongs in exactly one group")
                                   .arg(quoted(actionId)));
                    continue;
                }

                Control control;
                control.id             = *resolved;
                control.actionStringId = actionId;
                if (sizeText == QLatin1String("large")) {
                    control.size = ControlSize::Large;
                } else if (sizeText == QLatin1String("small")) {
                    control.size = ControlSize::Small;
                } else {
                    diag.about(actionId, QStringLiteral("control %1 has size %2; expected \"large\" "
                                                        "or \"small\"")
                                             .arg(quoted(actionId), quoted(sizeText)));
                    continue;
                }

                group.controls.push_back(std::move(control));
            }

            tab.groups.push_back(std::move(group));
        }

        result.spec.tabs.push_back(std::move(tab));
    }

    return result;
}

QString shippedPath()
{
    // Beside the executable, in `res/`, the way `i18n/` sits beside it. Both are data the running
    // program loads by path rather than data compiled into it -- see the header.
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("res/ribbon.json"));
}

const ParseResult& shipped()
{
    static const ParseResult result = [] {
        ParseResult loaded;
        QFile       file(shippedPath());
        if (!file.open(QIODevice::ReadOnly)) {
            loaded.diagnostics.push_back(
                Diagnostic{0, QStringLiteral("could not be opened: %1").arg(file.errorString())});
            return loaded;
        }
        return parse(file.readAll());
    }();
    return result;
}

} // namespace rawpage::ui::ribbon
