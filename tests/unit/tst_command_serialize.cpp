// tst_command_serialize -- `core/cmd/CommandRegistry` and every command's `serialize()`. Stage 3 T5, guide
// cases A-42 and A-45 (`Stage3-Mutation-Testing-Guide.md` §5.7), decisions D3-10 and D3-12.
//
// WHAT THIS SUITE IS TRYING TO CATCH: a `Commands.def` row that does not round-trip; a command whose
// `name()` is a sentence, or a key a shipped catalog lacks (guide issue I-01); a hostile serialized
// command -- the recovery journal is a file on disk -- that crashes, or that becomes a command.
//
// The tag `[serialize:Class]` is what `tools/mutation-lint` (T6) looks for, per command class (D3-12).
//
// WHAT IT DOES NOT COVER (convention 19): whether the recovery journal writes these objects to disk
// and reads them back (T9), and whether `ui/` resolves `name()` into the undo menu (T10). The catalog
// check reads the `.ts` sources as text; it does not load a compiled `.qm`.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/AttachmentCommands.h"
#include "core/cmd/commands/BookmarkCommands.h"
#include "core/cmd/commands/DestinationCommands.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "core/cmd/commands/PageCommands.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <functional>
#include <map>
#include <string>

using namespace rawpage;
using core::Command;
using core::CommandRegistry;
using core::RotatePageCommand;

namespace {

// Stage 6 T3: a highlight as `core::newAnnotation` makes one, every field a created annotation carries.
core::Annotation sampleHighlight(int page, const QString& author = QStringLiteral("Ana"))
{
    core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page, author, 1791504000);
    a.quadPoints = {72, 714, 272, 714, 72, 700, 272, 700};
    a.rect = *core::boundsOfPoints(a.quadPoints);
    a.color = {1, 1, 0};
    a.opacity = 0.5;
    a.contents = QStringLiteral("Zo\u00EB's note");
    return a;
}

// One or more sample commands per `Commands.def` row. A row with no entry here FAILS the round-trip
// case, so a new command cannot arrive without a sample.
// Stage 7 T11: a bookmark RawPage made, to a page and a view.
core::OutlineItem sampleBookmark(int ordinal)
{
    core::OutlineItem item;
    item.id = core::newBookmarkId(QString(32, QLatin1Char('9')), ordinal);
    item.title = QStringLiteral("Section %1").arg(ordinal);
    core::Destination at;
    at.pageIndex = ordinal;
    at.fit = core::Destination::Fit::XYZ;
    at.hasX = true;
    at.x = 36.5;
    at.hasY = true;
    at.y = 700;
    item.action = core::OutlineAction::goTo(at);
    item.destination = at;
    return item;
}

// Every kind of target a command may carry -- `Other` is the file's alone.
std::vector<core::OutlineAction> sampleActions()
{
    std::vector<core::OutlineAction> out;
    core::OutlineAction none;
    out.push_back(none);
    core::Destination fit;
    fit.pageIndex = 4;
    fit.fit = core::Destination::Fit::Fit;
    out.push_back(core::OutlineAction::goTo(fit));
    core::OutlineAction named;
    named.kind = core::OutlineAction::Kind::GoToNamed;
    named.name = QStringLiteral("dest-alpha");
    named.destination.pageIndex = 6;
    out.push_back(named);
    core::OutlineAction remote;
    remote.kind = core::OutlineAction::Kind::GoToRemote;
    remote.file = QStringLiteral("other 報告.pdf");
    remote.destination = fit;
    out.push_back(remote);
    core::OutlineAction launch;
    launch.kind = core::OutlineAction::Kind::Launch;
    launch.file = QStringLiteral("notes.txt");
    out.push_back(launch);
    core::OutlineAction uri;
    uri.kind = core::OutlineAction::Kind::Uri;
    uri.uri = QStringLiteral("https://example.com/a?b=1");
    out.push_back(uri);
    core::OutlineAction script;
    script.kind = core::OutlineAction::Kind::JavaScript;
    script.script = QStringLiteral("app.alert('never run');");
    out.push_back(script);
    core::OutlineAction print;
    print.kind = core::OutlineAction::Kind::Named;
    print.name = QStringLiteral("Print");
    out.push_back(print);
    core::OutlineAction submit;
    submit.kind = core::OutlineAction::Kind::SubmitForm;
    submit.uri = QStringLiteral("https://example.com/submit");
    out.push_back(submit);
    // Stage 7 T12 (D7-12): the other form actions.
    core::OutlineAction reset;
    reset.kind = core::OutlineAction::Kind::ResetForm;
    out.push_back(reset);
    core::OutlineAction import;
    import.kind = core::OutlineAction::Kind::ImportData;
    import.file = QStringLiteral("answers.fdf");
    out.push_back(import);
    core::OutlineAction show;
    show.kind = core::OutlineAction::Kind::Hide;
    show.name = QStringLiteral("form.field");
    show.hide = false;
    out.push_back(show);
    return out;
}

const std::map<std::string, std::function<std::vector<std::unique_ptr<Command>>()>>& samples()
{
    static const std::map<std::string, std::function<std::vector<std::unique_ptr<Command>>()>> table{
        {"page.rotate",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             for (int page : {0, 1, 899, RotatePageCommand::kMaxPageIndex})
                 for (int degrees : {90, 180, 270, -90})
                     out.push_back(std::make_unique<RotatePageCommand>(page, degrees));
             // Stage 7 T8: a SET -- the `pages` form -- including its limit.
             for (int degrees : {90, 180, 270})
                 out.push_back(std::make_unique<RotatePageCommand>(std::vector<int>{0, 2, 5, RotatePageCommand::kMaxPageIndex}, degrees));
             return out;
         }},
        // Stage 6 T3: one annotation, a set across pages, and the limit of a page index.
        {"annot.add",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{sampleHighlight(0)}));
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{
                 sampleHighlight(0), sampleHighlight(3, QStringLiteral("\u6797")), sampleHighlight(0)}));
             out.push_back(std::make_unique<core::AddAnnotationCommand>(
                 std::vector<core::Annotation>{sampleHighlight(core::AnnotationCommandLimits::kMaxPageIndex)}));
             // Stage 7 T12 (D7-12, guide A-88): a LINK with every target a command carries -- and `Other`, which an
             // annotation CARRIES as the file wrote it (`OutlineAction::fromJson`) -- each with a border and a highlight.
             std::vector<core::OutlineAction> targets = sampleActions();
             core::OutlineAction other;
             other.kind = core::OutlineAction::Kind::Other;
             other.name = QStringLiteral("Rendition");
             targets.push_back(other);
             std::vector<core::Annotation> links;
             for (const core::OutlineAction& action : targets) {
                 core::Annotation link = core::newLink(1, core::PdfRect{core::PdfPoint{36, 700}, core::PdfSize{200, 14}},
                                                       {36, 714, 236, 714, 36, 700, 236, 700}, action, QStringLiteral("Serialize"),
                                                       1791504000);
                 link.borderWidth = 2.0;
                 link.borderStyle = QStringLiteral("D");
                 link.dashPattern = {3, 1};
                 link.color = {1, 0, 0};
                 link.highlight = QStringLiteral("P");
                 links.push_back(std::move(link));
             }
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::move(links)));
             return out;
         }},
        // Every kind of identity a command can name (D6-4).
        {"annot.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}));
             out.push_back(std::make_unique<core::DeleteAnnotationCommand>(std::vector<core::AnnotationId>{
                 core::AnnotationId::ofObject(1LL << 40), core::AnnotationId::ofDirect(2, 3),
                 core::AnnotationId::ofCreated(core::newAnnotationName())}));
             return out;
         }},
        // Stage 6 T5: the four in-place commands, each over one identity and over every kind (D6-4).
        {"annot.edit",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation fromFile = sampleHighlight(0);
             fromFile.id = core::AnnotationId::ofObject(12);
             fromFile.nm = QStringLiteral("producer");
             out.push_back(std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{fromFile}));
             core::Annotation direct = sampleHighlight(2);
             direct.id = core::AnnotationId::ofDirect(2, 3);
             out.push_back(std::make_unique<core::EditAnnotationCommand>(std::vector<core::Annotation>{direct, sampleHighlight(1)}));
             return out;
         }},
        {"annot.move",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, 10.5, -2.25, 1791504000));
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofDirect(0, 1), core::AnnotationId::ofCreated(core::newAnnotationName())},
                 0.0, 0.1, 0));
             return out;
         }},
        {"annot.setAppearance",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::AnnotationAppearance colour;
             colour.color = std::vector<double>{0, 0.5, 1};
             out.push_back(std::make_unique<core::SetAnnotationAppearanceCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, colour, 1791504000));
             core::AnnotationAppearance all;
             all.interiorColor = std::vector<double>{};
             all.opacity = 0.25;
             all.borderWidth = 3.0;
             out.push_back(std::make_unique<core::SetAnnotationAppearanceCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofDirect(1, 0), core::AnnotationId::ofObject(7)}, all, 1791504000));
             return out;
         }},
        {"annot.lock",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::LockAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, core::AnnotationFlag::Locked, 0, 1791504000));
             out.push_back(std::make_unique<core::LockAnnotationCommand>(
                 std::vector<core::AnnotationId>{core::AnnotationId::ofObject(12)}, 0, core::LockAnnotationCommand::kLockFlags, 1791504000));
             return out;
         }},
        // Stage 6 T9: a reply to an annotation from the file and one to a reply RawPage made; statuses in both
        // models, as a set (D6-12).
        {"annot.reply",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation parent = sampleHighlight(0);
             parent.id = core::AnnotationId::ofObject(12);
             const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
             core::Annotation first = core::newReply(parent, page, QStringLiteral("Agreed."), {1, 1, 0}, QStringLiteral("Ana"), 1791504000);
             core::Annotation second = core::newReply(first, page, QStringLiteral("Zoë too"), {1, 1, 0}, QStringLiteral("Zoë"), 1791504060);
             out.push_back(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<core::Annotation>{first}));
             out.push_back(std::make_unique<core::ReplyToAnnotationCommand>(std::vector<core::Annotation>{first, second}));
             return out;
         }},
        {"annot.setStatus",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation parent = sampleHighlight(0);
             parent.id = core::AnnotationId::ofObject(12);
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("Accepted"), QStringLiteral("Ben"), 1791504000)}));
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Marked"), QStringLiteral("Ben"), 1791504000),
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Unmarked"), QStringLiteral("Ana"), 1791504001)}));
             out.push_back(std::make_unique<core::SetAnnotationStatusCommand>(std::vector<core::Annotation>{
                 core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("None"), QStringLiteral("Ana"), 1791504002),
                 core::newStateReply(parent, QStringLiteral("Marked"), QStringLiteral("Marked"), QStringLiteral("Ana"), 1791504003)}));
             return out;
         }},
        // Stage 6 T11 (D6-17): one file, and a set of two -- one EMPTY, which is a file too, and one named outside ASCII.
        {"attachment.add",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::AddAttachmentCommand>(std::vector<core::DocumentAttachment>{
                 {QStringLiteral("readme (2).txt"),
                  core::newEmbeddedFile(QStringLiteral("readme.txt"), QByteArray("notes\n"), QStringLiteral("Notes"), 1791504000)}}));
             out.push_back(std::make_unique<core::AddAttachmentCommand>(std::vector<core::DocumentAttachment>{
                 {QStringLiteral("empty.bin"), core::newEmbeddedFile(QStringLiteral("empty.bin"), QByteArray(), QString(), 1791504000)},
                 {QStringLiteral("Zo\u00EB \u5831\u544A.csv"),
                  core::newEmbeddedFile(QStringLiteral("Zo\u00EB \u5831\u544A.csv"), QByteArray("a,b\n1,2\n"), QStringLiteral("t\u00E4ble"), 1791504000)}}));
             return out;
         }},
        {"attachment.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteAttachmentCommand>(std::vector<QString>{QStringLiteral("setup.exe")}));
             out.push_back(std::make_unique<core::DeleteAttachmentCommand>(
                 std::vector<QString>{QStringLiteral("appendix.pdf"), QStringLiteral("readme.txt")}));
             return out;
         }},
        // Stage 7 T2: the page-order commands -- one page, a set, and the limit of a page index; a delete and a
        // duplicate carry the key they were made with.
        {"page.move",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{3}, 0));
             out.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{0, 2, 899}, 5));
             out.push_back(std::make_unique<core::MovePagesCommand>(std::vector<int>{RotatePageCommand::kMaxPageIndex}, 0));
             return out;
         }},
        {"page.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeletePagesCommand>(std::vector<int>{0}));
             out.push_back(std::make_unique<core::DeletePagesCommand>(std::vector<int>{1, 4, 5, 899}, QString(32, QLatin1Char('a'))));
             return out;
         }},
        {"page.duplicate",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{0}));
             out.push_back(std::make_unique<core::DuplicatePagesCommand>(std::vector<int>{2, 3, RotatePageCommand::kMaxPageIndex}));
             return out;
         }},
        // Stage 7 T5 (D7-6): the payload rides in the line, as base64 -- a blank page's, and bytes of every value (the
        // command carries the payload; reading it is the sink's).
        {"page.insert",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             auto blank = pdf::ops::ObjectGraphCopier::blankPages(core::PdfSize{612, 792}, 2);
             REQUIRE(blank);
             out.push_back(std::make_unique<core::InsertPagesCommand>(0, blank.value()));
             core::CopiedPages binary;
             for (int b = 0; b < 256; ++b) binary.graph.append(static_cast<char>(b));
             binary.pageCount = 3;
             out.push_back(std::make_unique<core::InsertPagesCommand>(RotatePageCommand::kMaxPageIndex, binary,
                                                                      QString(32, QLatin1Char('b'))));
             return out;
         }},
        {"page.replace",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             auto blank = pdf::ops::ObjectGraphCopier::blankPages(core::PdfSize{595, 842}, 2);
             REQUIRE(blank);
             out.push_back(std::make_unique<core::ReplacePagesCommand>(std::vector<int>{1, 7}, blank.value()));
             return out;
         }},
        // Stage 7 T9 (D7-15, D7-32): one box set for every page, and a box per page with every box stated; a resize each
        // way; labels in two styles, one with a prefix in another script; thumbnails set, and taken away.
        {"page.crop",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             const core::PdfRect letter{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
             out.push_back(std::make_unique<core::CropPagesCommand>(
                 std::vector<int>{0, 3}, std::vector<core::PageBoxes>{core::PageBoxes{letter, core::PdfRect{{36.5, 36}, {540, 720.25}}, {}, {}, {}}}));
             out.push_back(std::make_unique<core::CropPagesCommand>(
                 std::vector<int>{1, 2},
                 std::vector<core::PageBoxes>{core::PageBoxes{letter, {}, letter, letter, letter},
                                              core::PageBoxes{core::PdfRect{{-10, -10}, {632, 812}}, letter, {}, core::PdfRect{{9, 9}, {594, 774}}, {}}},
                 QString(32, QLatin1Char('c'))));
             return out;
         }},
        {"page.resize",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0}, core::PdfSize{595.276, 841.89}, true,
                                                                      core::ResizeAnchor::Centre));
             out.push_back(std::make_unique<core::ResizePagesCommand>(std::vector<int>{0, 1, 2}, core::PdfSize{612, 1008}, false,
                                                                      core::ResizeAnchor::BottomRight, QString(32, QLatin1Char('d'))));
             return out;
         }},
        {"page.renumber",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{0, 1, 2}, core::PageLabelStyle::RomanLower,
                                                                        QString(), 1));
             out.push_back(std::make_unique<core::RenumberPagesCommand>(std::vector<int>{3, 7}, core::PageLabelStyle::LettersUpper,
                                                                        QStringLiteral("附錄-"), 26, QString(32, QLatin1Char('e'))));
             return out;
         }},
        {"page.thumbnails",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::PageThumbnail thumbnail{3, 2, QByteArray()};
             for (int b = 0; b < 18; ++b) thumbnail.rgb.append(static_cast<char>(b * 14));
             out.push_back(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 4},
                                                                                std::vector<core::PageThumbnail>{thumbnail, thumbnail}));
             out.push_back(std::make_unique<core::SetEmbeddedThumbnailsCommand>(std::vector<int>{0, 1, 2}, std::vector<core::PageThumbnail>{},
                                                                                QString(32, QLatin1Char('f'))));
             return out;
         }},
        // Stage 7 T11 (D7-10): one item, a tree, an item by its object and one by its key; every action kind a command
        // can carry; a delete each way; a move to the top level and under an item.
        {"bookmark.create",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::CreateBookmarkCommand>(core::OutlinePosition{}, std::vector<core::OutlineItem>{sampleBookmark(0)}));
             core::OutlineItem chapter = sampleBookmark(1);
             chapter.children.push_back(sampleBookmark(2));
             chapter.openByDefault = true;
             out.push_back(std::make_unique<core::CreateBookmarkCommand>(core::OutlinePosition{core::OutlineItemId::ofObject(26), 2},
                                                                         std::vector<core::OutlineItem>{chapter, sampleBookmark(3)}));
             return out;
         }},
        {"bookmark.edit",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::EditBookmarkCommand>(core::OutlineItemId::ofObject(26),
                                                                       core::EditBookmarkCommand::Properties{QStringLiteral("Chapter 1"), {}, false, false, false}));
             out.push_back(std::make_unique<core::EditBookmarkCommand>(
                 core::newBookmarkId(QString(32, QLatin1Char('a')), 4),
                 core::EditBookmarkCommand::Properties{QStringLiteral("第一章 é"), std::array<double, 3>{1.0, 0.25, 0.0}, true, true, true}));
             return out;
         }},
        {"bookmark.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteBookmarkCommand>(std::vector<core::OutlineItemId>{core::OutlineItemId::ofObject(27)}, false));
             out.push_back(std::make_unique<core::DeleteBookmarkCommand>(
                 std::vector<core::OutlineItemId>{core::OutlineItemId::ofObject(26), core::newBookmarkId(QString(32, QLatin1Char('b')), 0)}, true));
             return out;
         }},
        {"bookmark.reparent",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::ReparentBookmarkCommand>(core::OutlineItemId::ofObject(29), core::OutlinePosition{{}, 0}));
             out.push_back(std::make_unique<core::ReparentBookmarkCommand>(core::newBookmarkId(QString(32, QLatin1Char('c')), 1),
                                                                           core::OutlinePosition{core::OutlineItemId::ofObject(28), 0}));
             return out;
         }},
        {"bookmark.setTarget",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             for (const core::OutlineAction& action : sampleActions())
                 out.push_back(std::make_unique<core::SetBookmarkTargetCommand>(core::OutlineItemId::ofObject(26), action));
             return out;
         }},
        {"bookmark.generate",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::OutlineItem heading = sampleBookmark(5);
             heading.children.push_back(sampleBookmark(6));
             out.push_back(std::make_unique<core::GenerateBookmarksCommand>(core::OutlinePosition{}, std::vector<core::OutlineItem>{heading},
                                                                            core::GenerateBookmarksCommand::Source::Headings));
             out.push_back(std::make_unique<core::GenerateBookmarksCommand>(core::OutlinePosition{{}, 3}, std::vector<core::OutlineItem>{sampleBookmark(7)},
                                                                            core::GenerateBookmarksCommand::Source::Import));
             return out;
         }},
        // Stage 7 T12 (D7-35): a destination in the tree, from a view; a rename to a name with spaces and punctuation; a
        // delete of one and of several.
        {"destination.create",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::NamedDestination entry;
             entry.name = QStringLiteral("chapter-1");
             entry.destination.pageIndex = 4;
             entry.destination.fit = core::Destination::Fit::XYZ;
             entry.destination.hasY = true;
             entry.destination.y = 640;
             out.push_back(std::make_unique<core::CreateDestinationCommand>(entry));
             return out;
         }},
        {"destination.rename",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::RenameDestinationCommand>(QStringLiteral("dest-alpha"),
                                                                            QStringLiteral("Chapter 1: the start (draft)")));
             return out;
         }},
        {"destination.delete",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             out.push_back(std::make_unique<core::DeleteDestinationCommand>(std::vector<QString>{QStringLiteral("old-delta")}));
             out.push_back(std::make_unique<core::DeleteDestinationCommand>(
                 std::vector<QString>{QStringLiteral("dest-alpha"), QStringLiteral("dest-beta")}));
             return out;
         }},
        // Stage 6 T13 (D6-20): an import that only adds -- an exchange's names, an Acrobat-shaped one among them --
        // one that only replaces, and one that does both.
        {"annot.import",
         [] {
             std::vector<std::unique_ptr<Command>> out;
             core::Annotation acrobat = sampleHighlight(0);
             acrobat.nm = QStringLiteral("5043e2eb-3e13-d3bf-03ab-158c24668db9");
             acrobat.id = core::AnnotationId::ofCreated(acrobat.nm);
             out.push_back(std::make_unique<core::ImportAnnotationsCommand>(
                 std::vector<core::Annotation>{acrobat, sampleHighlight(2)}, std::vector<core::Annotation>{}));
             core::Annotation fromFile = sampleHighlight(0);
             fromFile.id = core::AnnotationId::ofObject(12);
             fromFile.nm = QStringLiteral("producer");
             out.push_back(std::make_unique<core::ImportAnnotationsCommand>(std::vector<core::Annotation>{},
                                                                            std::vector<core::Annotation>{fromFile}));
             out.push_back(std::make_unique<core::ImportAnnotationsCommand>(std::vector<core::Annotation>{acrobat},
                                                                            std::vector<core::Annotation>{fromFile}));
             return out;
         }},
    };
    return table;
}

QByteArray compact(const QJsonObject& json)
{
    return QJsonDocument(json).toJson(QJsonDocument::Compact);
}

std::string keyOf(const core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

bool catalogHas(const char* file, QLatin1String key)
{
    QFile catalog(QStringLiteral(RAWPAGE_I18N_DIR "/") + QLatin1String(file));
    REQUIRE(catalog.open(QIODevice::ReadOnly));
    const QByteArray text = catalog.readAll();
    return text.contains("<source>" + QByteArray(key.data(), key.size()) + "</source>");
}

} // namespace

// ==========================================================================================
// A-42 -- the registry round-trips what it registered and refuses what it did not
// ==========================================================================================

TEST_CASE("every Commands.def row round-trips through CommandRegistry", "[cmd][serialize][serialize:RotatePageCommand][serialize:AddAnnotationCommand][serialize:DeleteAnnotationCommand][serialize:EditAnnotationCommand][serialize:MoveAnnotationCommand][serialize:SetAnnotationAppearanceCommand][serialize:LockAnnotationCommand][serialize:ReplyToAnnotationCommand][serialize:SetAnnotationStatusCommand][serialize:AddAttachmentCommand][serialize:DeleteAttachmentCommand][serialize:ImportAnnotationsCommand][serialize:MovePagesCommand][serialize:DeletePagesCommand][serialize:DuplicatePagesCommand][serialize:InsertPagesCommand][serialize:ReplacePagesCommand][serialize:CropPagesCommand][serialize:ResizePagesCommand][serialize:RenumberPagesCommand][serialize:SetEmbeddedThumbnailsCommand][serialize:CreateBookmarkCommand][serialize:EditBookmarkCommand][serialize:DeleteBookmarkCommand][serialize:ReparentBookmarkCommand][serialize:SetBookmarkTargetCommand][serialize:GenerateBookmarksCommand][serialize:CreateDestinationCommand][serialize:RenameDestinationCommand][serialize:DeleteDestinationCommand]")
{
    const auto typeIds = CommandRegistry::typeIds();
    REQUIRE_FALSE(typeIds.empty());
    std::size_t roundTrips = 0;
    for (const QLatin1String typeId : typeIds) {
        const std::string id(typeId.data(), static_cast<std::size_t>(typeId.size()));
        INFO("type id: " << id);
        const auto sample = samples().find(id);
        REQUIRE(sample != samples().end());      // a registered command with no sample is a failure

        for (const auto& command : sample->second()) {
            const QJsonObject json = command->serialize();
            INFO("serialized: " << compact(json).toStdString());
            CHECK(json.value(QStringLiteral("type")).toString() == QLatin1String(typeId));
            CHECK(command->typeId() == typeId);

            auto back = CommandRegistry::deserialize(json);
            REQUIRE(back);
            CHECK(back.value()->typeId() == typeId);
            CHECK(compact(back.value()->serialize()) == compact(json));
            CHECK(back.value()->name() == command->name());
            CHECK(back.value()->scope() == command->scope());
            CHECK(back.value()->operation() == command->operation());
            ++roundTrips;
        }
    }
    // Every sample table entry names a registered row: a sample for a deleted row is a stale test.
    for (const auto& [id, make] : samples()) {
        INFO("sampled type id: " << id);
        bool registered = false;
        for (const QLatin1String typeId : typeIds)
            registered = registered || typeId == QLatin1String(id.c_str());
        CHECK(registered);
    }
    // page.rotate: 16 one-page samples and, since Stage 7 T8, 3 sets.
    // Stage 7 T9: crop, resize, renumber and thumbnails, two each.
    // Stage 7 T11: the six bookmark commands -- two each, and a target of every kind a command can carry (9; 12 since
    // Stage 7 T12, the three form actions added). Stage 7 T12: annot.add's fourth sample, the links; the three destination
    // commands, 1, 1 and 2.
    CHECK(roundTrips == 16 + 3 + 4 + 2 + 2 + 2 + 2 + 2 + 2 + 3 + 2 + 2 + 3 + 3 + 2 + 2 + 2 + 1 + 2 + 2 + 2 + 2 + 2 + 2 + 2 + 2 + 12 + 2
                            + 1 + 1 + 2);
}

// Stage 6 T13 (D6-20): an import is a journal line carrying two sets -- what it adds must be what an add accepts,
// what it replaces what an edit accepts, and neither twice.
TEST_CASE("CommandRegistry refuses every malformed import by Result", "[cmd][serialize][hostile][annotops][T13]")
{
    core::Annotation added = sampleHighlight(0);
    core::Annotation replaced = sampleHighlight(1);
    replaced.id = core::AnnotationId::ofObject(12);
    replaced.nm = QStringLiteral("producer");
    const QJsonObject a = added.toJson();
    const QJsonObject r = replaced.toJson();
    const auto import = [](const QJsonArray& addedArray, const QJsonArray& replacedArray) {
        return QJsonObject{{"type", "annot.import"}, {"added", addedArray}, {"replaced", replacedArray}};
    };
    const auto with = [](QJsonObject annotation, const char* key, const QJsonValue& value) {
        annotation.insert(QLatin1String(key), value);
        return annotation;
    };
    QJsonArray tooMany;
    for (std::size_t i = 0; i <= core::AnnotationCommandLimits::kMaxAnnotations; ++i) tooMany.append(r);
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"no added", QJsonObject{{"type", "annot.import"}, {"replaced", QJsonArray{r}}}},
        {"no replaced", QJsonObject{{"type", "annot.import"}, {"added", QJsonArray{a}}}},
        {"both empty", import(QJsonArray{}, QJsonArray{})},
        {"added an object", QJsonObject{{"type", "annot.import"}, {"added", QJsonObject{}}, {"replaced", QJsonArray{}}}},
        {"an added annotation a number", import(QJsonArray{1}, QJsonArray{})},
        {"an added annotation with an object id", import(QJsonArray{with(a, "id", "obj:12")}, QJsonArray{})},
        {"an added annotation whose nm is not its id", import(QJsonArray{with(a, "nm", "other")}, QJsonArray{})},
        {"one name added twice", import(QJsonArray{a, a}, QJsonArray{})},
        {"a replaced annotation with no rect", [&] {
             QJsonObject broken = r;
             broken.remove(QStringLiteral("rect"));
             return import(QJsonArray{}, QJsonArray{broken});
         }()},
        {"a replaced annotation with opacity 2", import(QJsonArray{}, QJsonArray{with(r, "opacity", 2.0)})},
        {"one identity replaced twice", import(QJsonArray{}, QJsonArray{r, r})},
        {"too many", import(QJsonArray{}, tooMany)},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    // THE CONTROL: each set alone, and both, are accepted.
    CHECK(CommandRegistry::deserialize(import(QJsonArray{a}, QJsonArray{})));
    CHECK(CommandRegistry::deserialize(import(QJsonArray{}, QJsonArray{r})));
    CHECK(CommandRegistry::deserialize(import(QJsonArray{a}, QJsonArray{r})));
}

// Stage 6 T11 (D6-17): an attachment command is a journal line, and its bytes may be large -- every shape apply()
// would refuse is refused as the malformed line it is, and none reaches a command.
TEST_CASE("CommandRegistry refuses every malformed attachment command by Result", "[cmd][serialize][hostile][attachments]")
{
    const QJsonObject good = core::DocumentAttachment{
        QStringLiteral("a.txt"), core::newEmbeddedFile(QStringLiteral("a.txt"), QByteArray("abc"), QString(), 1791504000)}.toJson();
    const auto withFile = [&good](const std::function<void(QJsonObject&)>& change) {
        QJsonObject attachment = good;
        QJsonObject file = attachment.value(QStringLiteral("file")).toObject();
        change(file);
        attachment.insert(QStringLiteral("file"), file);
        return QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{attachment}}};
    };
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"no files", QJsonObject{{"type", "attachment.add"}}},
        {"empty files", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{}}}},
        {"a file that is not an object", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{1}}}},
        {"no key", QJsonObject{{"type", "attachment.add"},
                               {"files", QJsonArray{QJsonObject{{"file", good.value(QStringLiteral("file"))}}}}}},
        {"one key twice", QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{good, good}}}},
        {"no bytes", withFile([](QJsonObject& f) { f.remove(QStringLiteral("data")); })},
        {"bytes that are not base64", withFile([](QJsonObject& f) { f.insert(QStringLiteral("data"), QStringLiteral("!!!")); })},
        {"a size that disagrees with the bytes", withFile([](QJsonObject& f) { f.insert(QStringLiteral("size"), 4); })},
        {"a negative size", withFile([](QJsonObject& f) { f.insert(QStringLiteral("size"), -1); })},
        {"a checksum that is not sixteen bytes", withFile([](QJsonObject& f) { f.insert(QStringLiteral("checksum"), QStringLiteral("abcd")); })},
        {"no name", withFile([](QJsonObject& f) { f.remove(QStringLiteral("name")); })},
        // S6 guide I-18: the bound binds what a command CREATES -- an attached file's name and its key.
        {"a name past the bound", withFile([](QJsonObject& f) {
             f.insert(QStringLiteral("name"), QString(core::kMaxAttachmentNameLength + 1, QLatin1Char('n')));
         })},
        {"a key past the bound", [&good] {
             QJsonObject attachment = good;
             attachment.insert(QStringLiteral("key"), QString(core::kMaxAttachmentNameLength + 1, QLatin1Char('k')));
             return QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{attachment}}};
         }()},
        {"a delete with no keys", QJsonObject{{"type", "attachment.delete"}}},
        {"a delete of an empty key", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{QString()}}}},
        {"a delete of one key twice", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{"a", "a"}}}},
        {"a delete of a number", QJsonObject{{"type", "attachment.delete"}, {"keys", QJsonArray{7}}}},
    };
    for (const auto& [what, json] : hostile) {
        INFO(what << ": " << compact(json).toStdString());
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    // THE CONTROL: the good line parses.
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "attachment.add"}, {"files", QJsonArray{good}}}));
}

TEST_CASE("CommandRegistry returns NotFound for a type id it does not register", "[cmd][serialize]")
{
    for (const char* unknown : {"page.rotat", "page.rotate ", "PAGE.ROTATE", "", "test.lying"}) {
        INFO("type: '" << unknown << "'");
        auto result = CommandRegistry::deserialize(
            QJsonObject{{"type", QLatin1String(unknown)}, {"page", 0}, {"degrees", 90}});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::NotFound);
        CHECK(keyOf(result.error()) == "error.command.unknownType");
        CHECK(result.error().context == QLatin1String(unknown));
    }
}

TEST_CASE("every command name is a catalog key present in both shipped locales", "[cmd][serialize][i18n]")
{
    // D3-10(a), guide issue I-01: `core/` returns a key and arguments, and `ui/` resolves them.
    for (const auto& [id, make] : samples()) {
        for (const auto& command : make()) {
            const core::CatalogText name = command->name();
            const std::string key(name.key.data(), static_cast<std::size_t>(name.key.size()));
            INFO("type id: " << id << ", key: " << key);
            CHECK(key.rfind("command.", 0) == 0);
            CHECK(key.find(' ') == std::string::npos);
            CHECK(catalogHas("rawpage_en.ts", name.key));
            CHECK(catalogHas("rawpage_zh_TW.ts", name.key));
        }
    }
    // THE CONTROL: the lookup can say no.
    CHECK_FALSE(catalogHas("rawpage_en.ts", QLatin1String("command.page.rotat")));
    CHECK_FALSE(catalogHas("rawpage_zh_TW.ts", QLatin1String("command.page.rotat")));
}

// ==========================================================================================
// A-45 -- hostile serialized commands
// ==========================================================================================

TEST_CASE("CommandRegistry refuses every malformed page-order command by Result", "[cmd][serialize][hostile][pages]")
{
    // Stage 7 T2: a page set is whole numbers, strictly ascending, bounded; a key is 32 lowercase hex digits; a
    // move's target is a whole number in range. The journal is a file on disk (guide A-45's argument).
    const QString key(32, QLatin1Char('b'));
    const QString huge(10 * 1024 * 1024, QLatin1Char('9'));
    QJsonArray tooMany;
    for (int page = 0; page <= core::kMaxPagesPerCommand; ++page) tooMany.append(page);
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"move: pages missing", QJsonObject{{"type", "page.move"}, {"to", 0}}},
        {"move: pages empty", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{}}, {"to", 0}}},
        {"move: pages not ascending", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{3, 1}}, {"to", 0}}},
        {"move: a page twice", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1, 1}}, {"to", 0}}},
        {"move: a page -1", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{-1}}, {"to", 0}}},
        {"move: a page 1.5", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1.5}}, {"to", 0}}},
        {"move: a page a string", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{"1"}}, {"to", 0}}},
        {"move: a page past the limit",
         QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{RotatePageCommand::kMaxPageIndex + 1}}, {"to", 0}}},
        {"move: pages a 10 MB string", QJsonObject{{"type", "page.move"}, {"pages", huge}, {"to", 0}}},
        {"move: a million and one pages", QJsonObject{{"type", "page.move"}, {"pages", tooMany}, {"to", 0}}},
        {"move: to missing", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1}}}},
        {"move: to -1", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1}}, {"to", -1}}},
        {"move: to 0.5", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1}}, {"to", 0.5}}},
        {"move: to a string", QJsonObject{{"type", "page.move"}, {"pages", QJsonArray{1}}, {"to", "0"}}},
        {"delete: key missing", QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{1}}}},
        {"delete: key short", QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{1}}, {"key", key.left(31)}}},
        {"delete: key upper case", QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{1}}, {"key", key.toUpper()}}},
        {"delete: key not hex", QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{1}}, {"key", QString(32, QLatin1Char('g'))}}},
        {"delete: key a number", QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{1}}, {"key", 7}}},
        {"delete: pages an object", QJsonObject{{"type", "page.delete"}, {"pages", QJsonObject{}}, {"key", key}}},
        {"duplicate: key 10 MB", QJsonObject{{"type", "page.duplicate"}, {"pages", QJsonArray{1}}, {"key", huge}}},
        {"duplicate: pages descending", QJsonObject{{"type", "page.duplicate"}, {"pages", QJsonArray{2, 0}}, {"key", key}}},
        // Stage 7 T5: an insert's and a replace's payload, and an insert's place and count.
        {"insert: graph missing", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", 1}}},
        {"insert: graph empty", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", 1}, {"graph", ""}}},
        {"insert: graph not base64", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", 1}, {"graph", "!!!!"}}},
        {"insert: graph a number", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", 1}, {"graph", 7}}},
        {"insert: at -1", QJsonObject{{"type", "page.insert"}, {"at", -1}, {"key", key}, {"pages", 1}, {"graph", "JVBERg=="}}},
        {"insert: at 1.5", QJsonObject{{"type", "page.insert"}, {"at", 1.5}, {"key", key}, {"pages", 1}, {"graph", "JVBERg=="}}},
        {"insert: pages 0", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", 0}, {"graph", "JVBERg=="}}},
        {"insert: pages a million and one",
         QJsonObject{{"type", "page.insert"}, {"at", 0}, {"key", key}, {"pages", core::kMaxPagesPerCommand + 1}, {"graph", "JVBERg=="}}},
        {"insert: key missing", QJsonObject{{"type", "page.insert"}, {"at", 0}, {"pages", 1}, {"graph", "JVBERg=="}}},
        {"replace: graph missing", QJsonObject{{"type", "page.replace"}, {"pages", QJsonArray{1}}, {"key", key}}},
        {"replace: graph 10 MB of not base64", QJsonObject{{"type", "page.replace"}, {"pages", QJsonArray{1}}, {"key", key}, {"graph", huge + QStringLiteral("!")}}},
        {"replace: pages descending", QJsonObject{{"type", "page.replace"}, {"pages", QJsonArray{2, 0}}, {"key", key}, {"graph", "JVBERg=="}}},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
        CHECK(result.error().context.size() < 64);
    }
    SECTION("THE CONTROL: the boundaries themselves are accepted, and the key survives")
    {
        CHECK(CommandRegistry::deserialize(QJsonObject{
            {"type", "page.move"}, {"pages", QJsonArray{0, RotatePageCommand::kMaxPageIndex}}, {"to", RotatePageCommand::kMaxPageIndex}}));
        auto removed = CommandRegistry::deserialize(QJsonObject{{"type", "page.delete"}, {"pages", QJsonArray{0, 1.0}}, {"key", key}});
        REQUIRE(removed);
        const auto* command = dynamic_cast<core::DeletePagesCommand*>(removed.value().get());
        REQUIRE(command != nullptr);
        CHECK(command->key() == key);
        CHECK(command->pages() == std::vector<int>{0, 1});
        auto copied = CommandRegistry::deserialize(QJsonObject{{"type", "page.duplicate"}, {"pages", QJsonArray{4}}, {"key", key}});
        REQUIRE(copied);
        CHECK(dynamic_cast<core::DuplicatePagesCommand*>(copied.value().get())->copyName(2) == key + QStringLiteral("-2"));
    }
}

TEST_CASE("CommandRegistry refuses every malformed box, resize, label or thumbnail command by Result", "[cmd][serialize][hostile][pages][T9]")
{
    // Stage 7 T9: each field checked for presence, type and range before a command exists -- the recovery journal is a
    // file on disk (guide A-45's argument) -- and a thumbnail's compressed bytes checked against the size they CLAIM
    // before anything is decompressed: `qUncompress` allocates what the first four bytes say.
    const QString key(32, QLatin1Char('b'));
    const QJsonArray letter{0, 0, 612, 792};
    const auto crop = [&](QJsonValue boxes) {
        return QJsonObject{{"type", "page.crop"}, {"pages", QJsonArray{0, 1}}, {"key", key}, {"boxes", boxes}};
    };
    const auto resize = [&](const char* field, QJsonValue value) {
        QJsonObject json{{"type", "page.resize"}, {"pages", QJsonArray{0}}, {"key", key}, {"width", 595}, {"height", 842},
                         {"scale", true}, {"anchor", 4}};
        if (value.isUndefined()) json.remove(QLatin1String(field));
        else json.insert(QLatin1String(field), value);
        return json;
    };
    const auto renumber = [&](const char* field, QJsonValue value) {
        QJsonObject json{{"type", "page.renumber"}, {"pages", QJsonArray{0, 1}}, {"key", key}, {"style", "D"}, {"prefix", ""}, {"start", 1}};
        if (value.isUndefined()) json.remove(QLatin1String(field));
        else json.insert(QLatin1String(field), value);
        return json;
    };
    // 2 x 1 pixels claimed as 6 bytes, and the same bytes claiming 4 GB.
    const QByteArray pixels = qCompress(QByteArray(6, '\x40'));
    QByteArray lying = pixels;
    lying[0] = '\xff';
    const auto thumbnails = [&](QJsonValue value) {
        return QJsonObject{{"type", "page.thumbnails"}, {"pages", QJsonArray{0}}, {"key", key}, {"thumbnails", value}};
    };
    const auto one = [&](int width, int height, QByteArray bytes) {
        return QJsonArray{QJsonObject{{"width", width}, {"height", height}, {"rgb", QString::fromLatin1(bytes.toBase64())}}};
    };
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"crop: boxes missing", QJsonObject{{"type", "page.crop"}, {"pages", QJsonArray{0}}, {"key", key}}},
        {"crop: boxes empty", crop(QJsonArray{})},
        {"crop: three boxes for two pages", crop(QJsonArray{QJsonObject{{"media", letter}}, QJsonObject{{"media", letter}}, QJsonObject{{"media", letter}}})},
        {"crop: no media", crop(QJsonArray{QJsonObject{{"crop", letter}}})},
        {"crop: media three numbers", crop(QJsonArray{QJsonObject{{"media", QJsonArray{0, 0, 612}}}})},
        {"crop: media inverted", crop(QJsonArray{QJsonObject{{"media", QJsonArray{612, 792, 0, 0}}}})},
        {"crop: media a string", crop(QJsonArray{QJsonObject{{"media", "0 0 612 792"}}})},
        {"crop: an edge past the bound", crop(QJsonArray{QJsonObject{{"media", QJsonArray{0, 0, 1.0e8, 792}}}})},
        {"crop: a box no box has", crop(QJsonArray{QJsonObject{{"media", letter}, {"bleeed", letter}}})},
        {"crop: a box not an object", crop(QJsonArray{7})},
        {"resize: width missing", resize("width", QJsonValue::Undefined)},
        {"resize: width 2", resize("width", 2)},
        {"resize: height 14401", resize("height", 14401)},
        {"resize: height a string", resize("height", "842")},
        {"resize: scale a number", resize("scale", 1)},
        {"resize: anchor 9", resize("anchor", 9)},
        {"resize: anchor -1", resize("anchor", -1)},
        {"resize: anchor 4.5", resize("anchor", 4.5)},
        {"renumber: style unknown", renumber("style", "X")},
        {"renumber: style missing", renumber("style", QJsonValue::Undefined)},
        {"renumber: prefix a number", renumber("prefix", 3)},
        {"renumber: prefix 257 characters", renumber("prefix", QString(257, QLatin1Char('p')))},
        {"renumber: start 0", renumber("start", 0)},
        {"renumber: start past the bound", renumber("start", core::RenumberPagesCommand::kMaxStart)},
        {"renumber: start a string", renumber("start", "1")},
        {"thumbnails: not an array", thumbnails(7)},
        {"thumbnails: two for one page", thumbnails(QJsonArray{one(2, 1, pixels).at(0), one(2, 1, pixels).at(0)})},
        {"thumbnails: width 0", thumbnails(one(0, 1, pixels))},
        {"thumbnails: width past the bound", thumbnails(one(core::kMaxThumbnailEdge + 1, 1, pixels))},
        {"thumbnails: bytes claiming 4 GB", thumbnails(one(2, 1, lying))},
        {"thumbnails: bytes claiming another size", thumbnails(one(1, 1, pixels))},
        {"thumbnails: bytes not base64", thumbnails(QJsonArray{QJsonObject{{"width", 2}, {"height", 1}, {"rgb", "!!!!"}}})},
        {"thumbnails: bytes missing", thumbnails(QJsonArray{QJsonObject{{"width", 2}, {"height", 1}}})},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    SECTION("THE CONTROL: the same shapes, right, are read")
    {
        CHECK(CommandRegistry::deserialize(crop(QJsonArray{QJsonObject{{"media", letter}, {"crop", QJsonArray{10, 10, 600, 780}}}})));
        CHECK(CommandRegistry::deserialize(resize("anchor", 8)));
        CHECK(CommandRegistry::deserialize(renumber("start", core::RenumberPagesCommand::kMaxStart - 1)));
        auto read = CommandRegistry::deserialize(thumbnails(one(2, 1, pixels)));
        REQUIRE(read);
        const auto* command = dynamic_cast<const core::SetEmbeddedThumbnailsCommand*>(read.value().get());
        REQUIRE(command != nullptr);
        REQUIRE(command->thumbnails().size() == 1);
        CHECK(command->thumbnails().front().rgb == QByteArray(6, '\x40'));
    }
}

TEST_CASE("CommandRegistry refuses every malformed page rotation by Result", "[cmd][serialize][hostile]")
{
    // WHAT THIS CASE DOES NOT COVER on Windows: the sanitizer half of A-45. MinGW has no ASan, so "no
    // sanitizer line" is read from the `portable-asan` job's log, which runs this same case.
    const QString huge(10 * 1024 * 1024, QLatin1Char('9'));
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"empty object", QJsonObject{}},
        {"type missing", QJsonObject{{"page", 0}, {"degrees", 90}}},
        {"type a number", QJsonObject{{"type", 1}, {"page", 0}, {"degrees", 90}}},
        {"type null", QJsonObject{{"type", QJsonValue::Null}, {"page", 0}, {"degrees", 90}}},
        {"page missing", QJsonObject{{"type", "page.rotate"}, {"degrees", 90}}},
        {"degrees missing", QJsonObject{{"type", "page.rotate"}, {"page", 0}}},
        {"page a string", QJsonObject{{"type", "page.rotate"}, {"page", "0"}, {"degrees", 90}}},
        {"page an array", QJsonObject{{"type", "page.rotate"}, {"page", QJsonArray{0}}, {"degrees", 90}}},
        {"page a bool", QJsonObject{{"type", "page.rotate"}, {"page", true}, {"degrees", 90}}},
        {"degrees a string", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", "90"}}},
        {"degrees an object", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", QJsonObject{}}}},
        {"page -1", QJsonObject{{"type", "page.rotate"}, {"page", -1}, {"degrees", 90}}},
        {"page 1e9", QJsonObject{{"type", "page.rotate"}, {"page", 1e9}, {"degrees", 90}}},
        {"page one past the limit",
         QJsonObject{{"type", "page.rotate"}, {"page", RotatePageCommand::kMaxPageIndex + 1}, {"degrees", 90}}},
        {"page 1.5", QJsonObject{{"type", "page.rotate"}, {"page", 1.5}, {"degrees", 90}}},
        {"page 2^63", QJsonObject{{"type", "page.rotate"}, {"page", 9.2233720368547758e18}, {"degrees", 90}}},
        {"degrees 45", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 45}}},
        {"degrees 0", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 0}}},
        {"degrees 360", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 360}}},
        {"degrees -90", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", -90}}},
        {"degrees 90.5", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", 90.5}}},
        {"page a 10 MB string", QJsonObject{{"type", "page.rotate"}, {"page", huge}, {"degrees", 90}}},
        {"degrees a 10 MB string", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"degrees", huge}}},
        // Stage 7 T8: the set form -- never both forms, never an empty, unordered or out-of-range set.
        {"page and pages", QJsonObject{{"type", "page.rotate"}, {"page", 0}, {"pages", QJsonArray{0, 1}}, {"degrees", 90}}},
        {"pages empty", QJsonObject{{"type", "page.rotate"}, {"pages", QJsonArray{}}, {"degrees", 90}}},
        {"pages descending", QJsonObject{{"type", "page.rotate"}, {"pages", QJsonArray{3, 1}}, {"degrees", 90}}},
        {"pages repeated", QJsonObject{{"type", "page.rotate"}, {"pages", QJsonArray{1, 1}}, {"degrees", 90}}},
        {"pages a string", QJsonObject{{"type", "page.rotate"}, {"pages", "0-3"}, {"degrees", 90}}},
        {"pages past the limit",
         QJsonObject{{"type", "page.rotate"}, {"pages", QJsonArray{0, RotatePageCommand::kMaxPageIndex + 1}}, {"degrees", 90}}},
        {"pages without degrees", QJsonObject{{"type", "page.rotate"}, {"pages", QJsonArray{0, 1}}}},
    };

    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
        CHECK(result.error().context.size() < 64);
    }

    SECTION("a 10 MB type string is NotFound, and its context is bounded")
    {
        auto result = CommandRegistry::deserialize(
            QJsonObject{{"type", huge}, {"page", 0}, {"degrees", 90}});
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::NotFound);
        CHECK(result.error().context.size() == 64);
    }
    SECTION("the same 10 MB string through the JSON parser, as the recovery journal will read it")
    {
        QByteArray text = R"({"type":"page.rotate","degrees":90,"page":")";
        text += huge.toLatin1();
        text += "\"}";
        const QJsonDocument parsed = QJsonDocument::fromJson(text);
        REQUIRE(parsed.isObject());
        CHECK_FALSE(CommandRegistry::deserialize(parsed.object()));
    }
    SECTION("THE CONTROL: the boundaries themselves are accepted")
    {
        for (int page : {0, RotatePageCommand::kMaxPageIndex}) {
            for (int degrees : {90, 180, 270}) {
                INFO("page " << page << ", degrees " << degrees);
                auto result = CommandRegistry::deserialize(
                    QJsonObject{{"type", "page.rotate"}, {"page", page}, {"degrees", degrees}});
                REQUIRE(result);
                const auto* rotate = dynamic_cast<RotatePageCommand*>(result.value().get());
                REQUIRE(rotate != nullptr);
                CHECK(rotate->pageIndex() == page);
                CHECK(rotate->degrees() == degrees);
                CHECK(rotate->isValid());
            }
        }
        // A whole number written as a real is still a whole number.
        CHECK(CommandRegistry::deserialize(
            QJsonObject{{"type", "page.rotate"}, {"page", 3.0}, {"degrees", 180.0}}));
    }
}

TEST_CASE("CommandRegistry refuses every malformed annotation command by Result", "[cmd][serialize][hostile][annotops]")
{
    // Stage 6 T3. The recovery journal is a file on disk, so `annot.add` and `annot.delete` are parsers of
    // hostile input as `page.rotate` is (Stage 3's A-45). Each entry breaks ONE thing about a good command.
    const QJsonObject good = core::AddAnnotationCommand({sampleHighlight(0)}).serialize();
    const QJsonObject goodAnnotation = good.value("annotations").toArray().at(0).toObject();
    const auto withAnnotation = [&](const char* key, const QJsonValue& value) {
        QJsonObject annotation = goodAnnotation;
        if (value.isUndefined()) annotation.remove(QLatin1String(key));
        else annotation.insert(QLatin1String(key), value);
        return QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{annotation}}};
    };
    QJsonArray tooMany;
    for (std::size_t i = 0; i <= core::AnnotationCommandLimits::kMaxAnnotations; ++i) tooMany.append(QStringLiteral("obj:1"));
    QJsonArray hugeQuads;
    for (std::size_t i = 0; i < core::AnnotationCommandLimits::kMaxCoordinates + 8; ++i) hugeQuads.append(1.0);
    const QString huge(10 * 1024 * 1024, QLatin1Char('9'));

    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"add: annotations missing", QJsonObject{{"type", "annot.add"}}},
        {"add: annotations an object", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonObject{}}}},
        {"add: annotations empty", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{}}}},
        {"add: an annotation a number", QJsonObject{{"type", "annot.add"}, {"annotations", QJsonArray{7}}}},
        {"add: no id", withAnnotation("id", QJsonValue::Undefined)},
        {"add: an object id", withAnnotation("id", "obj:12")},
        {"add: a direct id", withAnnotation("id", "direct:0:1")},
        {"add: nm not the id", withAnnotation("nm", "something-else")},
        {"add: a 10 MB nm", withAnnotation("id", QStringLiteral("nm:") + huge)},
        {"add: an unknown subtype", withAnnotation("subtype", "Bogus")},
        {"add: no rect", withAnnotation("rect", QJsonValue::Undefined)},
        {"add: a 3-number rect", withAnnotation("rect", QJsonArray{1, 2, 3})},
        {"add: a non-finite rect flagged", withAnnotation("rectFinite", false)},
        {"add: page -1", withAnnotation("page", -1)},
        {"add: page past the limit", withAnnotation("page", core::AnnotationCommandLimits::kMaxPageIndex + 1)},
        {"add: too much geometry", withAnnotation("quadPoints", hugeQuads)},
        {"add: a colour of 5", withAnnotation("color", QJsonArray{1, 1, 1, 1, 1})},
        {"add: opacity 2", withAnnotation("opacity", 2.0)},
        {"add: a negative border", withAnnotation("borderWidth", -1.0)},
        // Stage 6 T12 (D6-14): a line's leaders and caption, and a measure dictionary.
        {"add: a leader past any page", withAnnotation("leaderLength", 1.0e7)},
        {"add: a negative leader extension", withAnnotation("leaderExtension", -1.0)},
        {"add: a negative leader offset", withAnnotation("leaderOffset", -2.0)},
        {"add: a caption at the bottom", withAnnotation("captionPosition", "Bottom")},
        {"add: a caption offset of three", withAnnotation("captionOffset", QJsonArray{1, 2, 3})},
        {"add: a measure that is a number", withAnnotation("measure", 7)},
        {"add: a measure unit of 65 characters",
         withAnnotation("measure", QJsonObject{{"x", QJsonArray{QJsonObject{{"unit", QString(65, QLatin1Char('u'))}}}}})},
        {"add: a measure precision of a half",
         withAnnotation("measure", QJsonObject{{"distance", QJsonArray{QJsonObject{{"unit", "ft"}, {"precision", 0.5}}}}})},
        {"add: a measure chain of seventeen", withAnnotation("measure", [] {
             QJsonArray chain;
             for (int i = 0; i < 17; ++i) chain.append(QJsonObject{{"unit", "ft"}, {"factor", 1.0}});
             return QJsonObject{{"area", chain}};
         }())},
        {"add: a 10 MB scale string", withAnnotation("measure", QJsonObject{{"ratio", huge}})},
        {"delete: ids missing", QJsonObject{{"type", "annot.delete"}}},
        {"delete: ids empty", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{}}}},
        {"delete: an id a number", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{12}}}},
        {"delete: an id of no form", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"12"}}}},
        {"delete: object 0", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"obj:0"}}}},
        {"delete: a negative position", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"direct:0:-1"}}}},
        {"delete: a direct page past the limit",
         QJsonObject{{"type", "annot.delete"},
                     {"ids", QJsonArray{QStringLiteral("direct:%1:0").arg(core::AnnotationCommandLimits::kMaxPageIndex + 1)}}}},
        {"delete: an empty nm", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"nm:"}}}},
        {"delete: a 10 MB id", QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{QStringLiteral("nm:") + huge}}}},
        {"delete: too many ids", QJsonObject{{"type", "annot.delete"}, {"ids", tooMany}}},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
        CHECK(result.error().context.size() < 64);
    }

    // THE CONTROL: the good command, and every boundary, is accepted.
    CHECK(CommandRegistry::deserialize(good));
    CHECK(CommandRegistry::deserialize(withAnnotation("page", core::AnnotationCommandLimits::kMaxPageIndex)));
    CHECK(CommandRegistry::deserialize(withAnnotation("opacity", 0.0)));
    CHECK(CommandRegistry::deserialize(
        QJsonObject{{"type", "annot.delete"}, {"ids", QJsonArray{"obj:1", "direct:0:0", "nm:x"}}}));
    // S6 T12: a leader and a measure inside every bound.
    CHECK(CommandRegistry::deserialize(withAnnotation("leaderLength", -20.0)));
    CHECK(CommandRegistry::deserialize(withAnnotation("captionPosition", "Top")));
    CHECK(CommandRegistry::deserialize(withAnnotation(
        "measure", QJsonObject{{"ratio", "1 in = 4 ft"},
                               {"x", QJsonArray{QJsonObject{{"unit", "ft"}, {"factor", 1.0 / 18.0}, {"precision", 100}}}}})));
}

TEST_CASE("CommandRegistry refuses a malformed reply or status by Result", "[cmd][serialize][hostile][annotops][T9]")
{
    // Stage 6 T9 (D6-12). Each entry breaks ONE thing about a good reply or a good status -- what each promises
    // about its annotations is checked in the parser too, so a hostile journal cannot add a visible comment
    // under the name of a status.
    core::Annotation parent = sampleHighlight(0);
    parent.id = core::AnnotationId::ofObject(12);
    const core::PdfRect page{core::PdfPoint{0, 0}, core::PdfSize{612, 792}};
    const QJsonObject reply = core::newReply(parent, page, QStringLiteral("Agreed."), {1, 1, 0}, QStringLiteral("Ana"), 1791504000).toJson();
    const QJsonObject status =
        core::newStateReply(parent, QStringLiteral("Review"), QStringLiteral("Accepted"), QStringLiteral("Ben"), 1791504000).toJson();
    QJsonObject stated = reply;
    stated.insert(QStringLiteral("stateModel"), QStringLiteral("Review"));
    const auto with = [](const char* type, QJsonObject annotation, const char* key, const QJsonValue& value) {
        if (value.isUndefined()) annotation.remove(QLatin1String(key));
        else annotation.insert(QLatin1String(key), value);
        return QJsonObject{{"type", QLatin1String(type)}, {"annotations", QJsonArray{annotation}}};
    };
    const std::vector<std::pair<const char*, QJsonObject>> hostile{
        {"reply: a highlight", with("annot.reply", reply, "subtype", "Highlight")},
        {"reply: no parent", with("annot.reply", reply, "inReplyTo", QJsonValue::Undefined)},
        {"reply: a group member", with("annot.reply", reply, "replyType", "Group")},
        {"reply: a state", with("annot.reply", stated, "state", "Accepted")},
        {"reply: a parent that is a direct dictionary", with("annot.reply", reply, "inReplyTo", "direct:0:1")},
        {"reply: an object id", with("annot.reply", reply, "id", "obj:40")},
        {"status: no model", with("annot.setStatus", status, "stateModel", QJsonValue::Undefined)},
        {"status: an unknown model", with("annot.setStatus", status, "stateModel", "Approval")},
        {"status: a state its model does not allow", with("annot.setStatus", status, "state", "Marked")},
        {"status: an unknown state", with("annot.setStatus", status, "state", "Maybe")},
        {"status: not hidden", with("annot.setStatus", status, "flags", 28)},
        {"status: no parent", with("annot.setStatus", status, "inReplyTo", QJsonValue::Undefined)},
        {"status: a square", with("annot.setStatus", status, "subtype", "Square")},
    };
    for (const auto& [label, json] : hostile) {
        INFO("input: " << label);
        auto result = CommandRegistry::deserialize(json);
        REQUIRE_FALSE(result);
        CHECK(result.error().code == core::ErrorCode::ParseFailure);
        CHECK(keyOf(result.error()) == "error.command.malformed");
    }
    // THE CONTROL: both good ones are accepted, and a reply is not a status nor a status a reply.
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "annot.reply"}, {"annotations", QJsonArray{reply}}}));
    CHECK(CommandRegistry::deserialize(QJsonObject{{"type", "annot.setStatus"}, {"annotations", QJsonArray{status}}}));
    CHECK_FALSE(CommandRegistry::deserialize(QJsonObject{{"type", "annot.reply"}, {"annotations", QJsonArray{status}}}));
    CHECK_FALSE(CommandRegistry::deserialize(QJsonObject{{"type", "annot.setStatus"}, {"annotations", QJsonArray{reply}}}));
}

TEST_CASE("RotatePageCommand normalizes a counter-clockwise turn and marks a non-quarter turn invalid", "[cmd][serialize]")
{
    CHECK(RotatePageCommand(0, -90).degrees() == 270);
    CHECK(RotatePageCommand(0, 450).degrees() == 90);
    CHECK(RotatePageCommand(0, -90).isValid());
    CHECK_FALSE(RotatePageCommand(0, 45).isValid());
    CHECK_FALSE(RotatePageCommand(0, 0).isValid());
    CHECK_FALSE(RotatePageCommand(0, 360).isValid());
    CHECK_FALSE(RotatePageCommand(-1, 90).isValid());
    CHECK_FALSE(RotatePageCommand(RotatePageCommand::kMaxPageIndex + 1, 90).isValid());
    CHECK(RotatePageCommand(4, 90).name()
          == core::CatalogText{QLatin1String("command.page.rotate"), {"5", "90"}});
}
