#include "core/doc/LinkActions.h"

#include "core/doc/Attachments.h"
#include "core/doc/Document.h"
#include "core/text/PageTextModel.h"

#include <QRegularExpression>

#include <algorithm>
#include <limits>

namespace rawpage::core {
namespace {

LinkFollow because(LinkFollow::Step step, const char* reason)
{
    LinkFollow out;
    out.step = step;
    out.reason = QLatin1String(reason);
    return out;
}

// A name Windows would RUN, or hand to something that runs it -- D6-17's list, asked of the name as written and of
// the name Windows would actually open (a trailing dot or space dropped: `evil.exe.` is `evil.exe`).
bool runnable(const QString& file)
{
    return isExecutableName(file) || isExecutableName(safeFileName(file));
}

bool isSpace(char32_t c)
{
    return c == U' ' || c == U'\t' || c == U'\r' || c == U'\n' || c == 0x00A0 || c == 0x2028 || c == 0x2029;
}

} // namespace

QString followableUri(const QString& uri)
{
    const QString trimmed = uri.trimmed();
    if (trimmed.startsWith(QLatin1String("www."), Qt::CaseInsensitive)) return QStringLiteral("http://") + trimmed;
    const qsizetype colon = trimmed.indexOf(QLatin1Char(':'));
    if (colon <= 0) return QString();
    const QString scheme = trimmed.left(colon).toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("ftp")
        || scheme == QLatin1String("mailto"))
        return trimmed;
    return QString();   // javascript:, file:, data:, vbscript:, a program's own scheme -- not a page's to run
}

LinkFollow followLink(const OutlineAction& action, const Destinations& table, int pageCount)
{
    switch (action.kind) {
    case OutlineAction::Kind::None:
        return because(LinkFollow::Step::Nowhere, "links.follow.noTarget");
    case OutlineAction::Kind::GoTo: {
        const int page = action.destination.pageIndex;
        if (page < 0 || page >= pageCount) return because(LinkFollow::Step::Nowhere, "links.follow.pageMissing");
        LinkFollow out;
        out.step = LinkFollow::Step::Navigate;
        out.destination = action.destination;
        return out;
    }
    case OutlineAction::Kind::GoToNamed: {
        const NamedDestination* named = table.find(action.name);
        if (named == nullptr) {
            LinkFollow out = because(LinkFollow::Step::Nowhere, "links.follow.nameMissing");
            out.name = action.name;
            return out;
        }
        if (named->destination.pageIndex < 0 || named->destination.pageIndex >= pageCount)
            return because(LinkFollow::Step::Nowhere, "links.follow.pageMissing");
        LinkFollow out;
        out.step = LinkFollow::Step::Navigate;
        out.destination = named->destination;
        out.name = action.name;
        return out;
    }
    case OutlineAction::Kind::GoToRemote: {
        if (action.file.trimmed().isEmpty()) return because(LinkFollow::Step::Nowhere, "links.follow.noTarget");
        if (runnable(action.file)) {
            LinkFollow out = because(LinkFollow::Step::Refused, "links.follow.executable");
            out.file = action.file;
            return out;
        }
        LinkFollow out;
        out.step = LinkFollow::Step::OpenRemote;
        out.file = action.file;
        out.name = action.name;
        out.destination = action.destination;
        return out;
    }
    case OutlineAction::Kind::Launch: {
        if (action.file.trimmed().isEmpty()) return because(LinkFollow::Step::Nowhere, "links.follow.noTarget");
        LinkFollow out = because(runnable(action.file) ? LinkFollow::Step::Refused : LinkFollow::Step::LaunchFile,
                                 runnable(action.file) ? "links.follow.executable" : "");
        out.file = action.file;
        return out;
    }
    case OutlineAction::Kind::Uri: {
        const QString uri = followableUri(action.uri);
        if (uri.isEmpty()) {
            LinkFollow out = because(LinkFollow::Step::Refused, "links.follow.uriScheme");
            out.uri = action.uri;
            return out;
        }
        LinkFollow out;
        out.step = LinkFollow::Step::OpenUri;
        out.uri = uri;
        return out;
    }
    case OutlineAction::Kind::JavaScript:
        return because(LinkFollow::Step::NotRun, "links.follow.script");
    case OutlineAction::Kind::SubmitForm:
    case OutlineAction::Kind::ResetForm:
    case OutlineAction::Kind::ImportData:
    case OutlineAction::Kind::Hide:
        return because(LinkFollow::Step::NotRun, "links.follow.form");
    case OutlineAction::Kind::Named:
        return because(LinkFollow::Step::NotRun, "links.follow.menu");
    case OutlineAction::Kind::Other:
        return because(LinkFollow::Step::NotRun, "links.follow.other");
    }
    return because(LinkFollow::Step::Nowhere, "links.follow.noTarget");
}

const char* linkProblemKey(LinkProblem::Kind kind) noexcept
{
    switch (kind) {
    case LinkProblem::Kind::PageMissing: return "links.verify.pageMissing";
    case LinkProblem::Kind::NameMissing: return "links.verify.nameMissing";
    case LinkProblem::Kind::FileMissing: return "links.verify.fileMissing";
    }
    return "links.verify.pageMissing";
}

std::vector<LinkProblem> verifyLinks(const Document& document, const std::function<bool(const QString& file)>& fileExists)
{
    std::vector<LinkProblem> out;
    const int pageCount = document.pages().count();
    const AnnotationStore& store = document.annotations();
    for (int page = 0; page < store.pageCount(); ++page) {
        for (const std::size_t index : store.onPage(page)) {
            const Annotation& link = store.at(index);
            if (link.subtype != AnnotationSubtype::Link) continue;
            const OutlineAction& action = link.action;
            const auto problem = [&](LinkProblem::Kind kind, QString target) {
                out.push_back(LinkProblem{kind, link.id, page, std::move(target)});
            };
            switch (action.kind) {
            case OutlineAction::Kind::GoTo:
                if (action.destination.pageIndex < 0 || action.destination.pageIndex >= pageCount)
                    problem(LinkProblem::Kind::PageMissing, QString());
                break;
            case OutlineAction::Kind::GoToNamed:
                if (const NamedDestination* named = document.destinations().find(action.name); named == nullptr)
                    problem(LinkProblem::Kind::NameMissing, action.name);
                else if (named->destination.pageIndex < 0 || named->destination.pageIndex >= pageCount)
                    problem(LinkProblem::Kind::PageMissing, action.name);
                break;
            case OutlineAction::Kind::GoToRemote:
            case OutlineAction::Kind::Launch:
                if (!action.file.isEmpty() && (!fileExists || !fileExists(action.file)))
                    problem(LinkProblem::Kind::FileMissing, action.file);
                break;
            default:
                break;   // a web address is never fetched; the rest go nowhere to check
            }
        }
    }
    return out;
}

std::vector<DetectedUrl> detectUrls(const text::PageTextModel& page)
{
    // The page's characters as one string, each UTF-16 unit remembering the character it came from.
    const std::vector<text::TextChar>& chars = page.chars();
    QString string;
    std::vector<std::size_t> owner;
    string.reserve(static_cast<qsizetype>(chars.size()));
    owner.reserve(chars.size());
    for (std::size_t i = 0; i < chars.size(); ++i) {
        const char32_t c = chars[i].isUnmapped() ? U'�' : chars[i].codepoint;
        const QString piece = isSpace(c) ? QStringLiteral(" ") : QString::fromUcs4(&c, 1);
        for (qsizetype k = 0; k < piece.size(); ++k) owner.push_back(i);
        string += piece;
    }

    // A scheme and a host with a dot, `www.` and a host with a dot, or `mailto:` and an address -- each running to white
    // space. What follows the host is printable ASCII but the three characters no address holds unescaped: a `/URI` is
    // 7-bit ASCII (ISO 32000-1 §12.6.4.7), and a character past it ends the address rather than making one the writer
    // would refuse.
    static const QRegularExpression address(
        QStringLiteral(R"((?<![\w@.])(?:(?:https?://)[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+|www\.[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+)(?::[0-9]{1,5})?(?:[/?#][\x21\x23-\x3B\x3D\x3F-\x7E]*)?|(?<![\w@.])mailto:[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+)"),
        QRegularExpression::CaseInsensitiveOption);

    std::vector<DetectedUrl> out;
    for (QRegularExpressionMatchIterator it = address.globalMatch(string); it.hasNext();) {
        const QRegularExpressionMatch match = it.next();
        qsizetype start = match.capturedStart();
        qsizetype end = match.capturedEnd();
        // A sentence's punctuation after an address is the sentence's: a closing bracket only when the address opened none.
        while (end > start) {
            const QChar last = string.at(end - 1);
            if (QStringLiteral(".,;:!?'\"").contains(last)) {
                --end;
                continue;
            }
            if (last == QLatin1Char(')') || last == QLatin1Char(']')) {
                const QChar open = last == QLatin1Char(')') ? QLatin1Char('(') : QLatin1Char('[');
                const QStringView body = QStringView(string).mid(start, end - start);
                if (body.count(open) < body.count(last)) {
                    --end;
                    continue;
                }
            }
            break;
        }
        if (end <= start) continue;
        DetectedUrl url;
        url.text = string.mid(start, end - start);
        url.uri = url.text.startsWith(QLatin1String("www."), Qt::CaseInsensitive) ? QStringLiteral("http://") + url.text : url.text;
        // The glyphs' boxes: one line's run, so one quadrilateral.
        double left = std::numeric_limits<double>::max(), bottom = std::numeric_limits<double>::max();
        double right = std::numeric_limits<double>::lowest(), top = std::numeric_limits<double>::lowest();
        for (qsizetype k = start; k < end; ++k) {
            const text::TextChar& c = chars[owner[static_cast<std::size_t>(k)]];
            if (c.isGenerated()) continue;
            left = std::min(left, c.box.left());
            bottom = std::min(bottom, c.box.bottom());
            right = std::max(right, c.box.right());
            top = std::max(top, c.box.top());
        }
        if (!(left < right) || !(bottom < top)) continue;
        url.quadPoints = {left, top, right, top, left, bottom, right, bottom};
        url.rect = PdfRect{PdfPoint{left, bottom}, PdfSize{right - left, top - bottom}};
        out.push_back(std::move(url));
    }
    return out;
}

std::vector<Annotation> linksForUrls(const Document& document, int page, const text::PageTextModel& text, const QString& author,
                                     std::int64_t utcNow)
{
    std::vector<PdfRect> covered;
    const AnnotationStore& store = document.annotations();
    if (page >= 0 && page < store.pageCount()) {
        for (const std::size_t index : store.onPage(page)) {
            if (store.at(index).subtype == AnnotationSubtype::Link) covered.push_back(store.at(index).rect);
        }
    }
    std::vector<Annotation> out;
    for (const DetectedUrl& url : detectUrls(text)) {
        const PdfPoint centre{url.rect.left() + url.rect.size.width / 2.0, url.rect.bottom() + url.rect.size.height / 2.0};
        if (std::any_of(covered.begin(), covered.end(), [&centre](const PdfRect& r) { return r.contains(centre); })) continue;
        OutlineAction action;
        action.kind = OutlineAction::Kind::Uri;
        action.uri = url.uri;
        out.push_back(newLink(page, url.rect, url.quadPoints, std::move(action), author, utcNow));
        covered.push_back(url.rect);
    }
    return out;
}

} // namespace rawpage::core
