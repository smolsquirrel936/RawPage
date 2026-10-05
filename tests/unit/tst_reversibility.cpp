// tst_reversibility -- apply then revert is the identity, for every registered command over the corpus.
// Stage 3 T5, guide cases A-43 and A-44 (`Stage3-Mutation-Testing-Guide.md` §5.7).
//
// THE PROPERTY: for every `Commands.def` row × every editable corpus file × the first and last page ×
// each sample the row's generator makes, `apply()` then `revert()` restores `Document::modelHash()` AND
// leaves the real `pdf/ops/MutationSinkImpl` with an EMPTY delta -- nothing to write. The model and the
// sink are checked separately because a revert can restore one and forget the other, and each is a
// different user-visible bug: a model left rotated is a wrong screen, a sink left rotated is a file
// saved with a change the user undid.
//
// THE HARNESS IS A FUNCTION THAT RETURNS FAILURES rather than a block of CHECKs, so that A-44 can run
// the SAME harness over planted liars and assert it reports them. A harness that cannot read a wrong
// answer passes every command, and the control is what says this one can.
//
// It also requires `apply()` to CHANGE something (the hash or the delta). Without that, a command
// whose apply and revert both do nothing passes the property trivially (convention 22).
//
// The tag `[revert:Class]` is what `tools/mutation-lint` (T6) looks for, per command class (D3-12).
//
// WHAT IT DOES NOT COVER (convention 19):
//   * the ENGINE. The backend is `tests/unit/CosBackend.h`, whose page tree comes from the first-party
//     walk; the files whose readers disagree are refused by the shipped backend, and the one measured
//     disagreement, `malformed/pages-loop.pdf` (guide issue I-07), is excluded here by name for that
//     reason -- the product never lets it be edited.
//   * the journal. Commands are applied to a `Document` directly; `tst_journal` covers running them.
//   * SAVE. An empty delta says there is nothing to write; that a written rotation reads back is T8's.

#include <catch2/catch_test_macros.hpp>

#include "CosBackend.h"
#include "core/cmd/CommandRegistry.h"
#include "core/cmd/commands/AnnotationCommands.h"
#include "core/cmd/commands/PageCommands.h"
#include "core/doc/Document.h"

#include <QJsonDocument>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

using namespace rawpage;
using core::Command;
using core::RotatePageCommand;

namespace {

namespace fs = std::filesystem;

// The instant every in-place command of the sweep stamps (2026-10-10T00:00:00Z).
constexpr std::int64_t kEditInstant = 1791590400;

// What page `page` owns, in its listing order.
std::vector<core::AnnotationId> ownedIds(const core::Document& document, int page)
{
    std::vector<core::AnnotationId> ids;
    const core::AnnotationStore& store = document.annotations();
    if (!store.status()) return ids;
    for (const std::size_t index : store.onPage(page)) ids.push_back(store.at(index).id);
    return ids;
}

// Makes the commands to try on page `page` of `document`.
using Generator = std::function<std::vector<std::unique_ptr<Command>>(const core::Document& document, int page)>;
using Generators = std::map<std::string, Generator>;

// THE PRODUCTION GENERATORS: one per `Commands.def` row. A row with none fails A-43.
const Generators& registeredGenerators()
{
    static const Generators table{
        {"page.rotate",
         [](const core::Document&, int page) {
             std::vector<std::unique_ptr<Command>> out;
             for (int degrees : {90, 180, 270})
                 out.push_back(std::make_unique<RotatePageCommand>(page, degrees));
             return out;
         }},
        // Stage 6 T3: a highlight on the page -- a set of one, and a set of two on one page.
        {"annot.add",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             if (!document.annotations().status()) return out;
             const auto make = [page](double y) {
                 core::Annotation a = core::newAnnotation(core::AnnotationSubtype::Highlight, page,
                                                          QStringLiteral("Reversibility"), 1791504000);
                 a.quadPoints = {36, y + 12, 136, y + 12, 36, y, 136, y};
                 a.rect = *core::boundsOfPoints(a.quadPoints);
                 a.color = {1, 1, 0};
                 return a;
             };
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{make(36)}));
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{make(36), make(60)}));
             // Stage 6 T6: a NOTE AND ITS POP-UP, which name each other, and a REPLACE -- a caret with an
             // underline, squiggly and strikeout in its group -- each one command, as the tools make them.
             core::Annotation note = core::newAnnotation(core::AnnotationSubtype::Text, page,
                                                         QStringLiteral("Reversibility"), 1791504000);
             note.rect = core::PdfRect{core::PdfPoint{36, 80}, core::PdfSize{20, 20}};
             note.icon = QStringLiteral("Comment");
             note.flags = core::AnnotationFlag::Print | core::AnnotationFlag::NoZoom | core::AnnotationFlag::NoRotate;
             core::Annotation popup = core::newAnnotation(core::AnnotationSubtype::Popup, page, QString(), 1791504000);
             popup.rect = core::PdfRect{core::PdfPoint{60, 0}, core::PdfSize{200, 100}};
             popup.hasAppearance = false;
             popup.open = false;
             popup.parent = note.id;
             note.popup = popup.id;
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::vector<core::Annotation>{note, popup}));
             core::Annotation caret = core::newAnnotation(core::AnnotationSubtype::Caret, page,
                                                          QStringLiteral("Reversibility"), 1791504000);
             caret.rect = core::PdfRect{core::PdfPoint{133, 36}, core::PdfSize{6, 8}};
             caret.contents = QStringLiteral("replacement");
             std::vector<core::Annotation> group{caret};
             for (const core::AnnotationSubtype kind : {core::AnnotationSubtype::StrikeOut, core::AnnotationSubtype::Underline,
                                                        core::AnnotationSubtype::Squiggly}) {
                 core::Annotation mark = core::newAnnotation(kind, page, QStringLiteral("Reversibility"), 1791504000);
                 mark.quadPoints = {36, 48, 136, 48, 36, 36, 136, 36};
                 mark.rect = *core::markupRect(mark.quadPoints);
                 mark.inReplyTo = caret.id;
                 mark.replyType = core::ReplyType::Group;
                 group.push_back(std::move(mark));
             }
             out.push_back(std::make_unique<core::AddAnnotationCommand>(std::move(group)));
             return out;
         }},
        // Every annotation the page owns as ONE set, and its first alone -- including, on
        // annots-direct.pdf, a direct dictionary and an annotation two pages list.
        {"annot.delete",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             const core::AnnotationStore& store = document.annotations();
             if (!store.status()) return out;
             std::vector<core::AnnotationId> ids;
             for (const std::size_t index : store.onPage(page)) ids.push_back(store.at(index).id);
             if (ids.empty()) return out;
             out.push_back(std::make_unique<core::DeleteAnnotationCommand>(ids));
             if (ids.size() > 1)
                 out.push_back(std::make_unique<core::DeleteAnnotationCommand>(std::vector<core::AnnotationId>{ids.front()}));
             return out;
         }},
        // Stage 6 T5 -- the four that change annotations IN PLACE, each over what the page owns.
        // A MOVE of every annotation the page owns as one set, and of its first alone: every kind can be
        // moved, because its appearance goes with its rectangle (D6-5).
        {"annot.move",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             const std::vector<core::AnnotationId> ids = ownedIds(document, page);
             if (ids.empty()) return out;
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(ids, 7.5, -12.25, kEditInstant));
             out.push_back(std::make_unique<core::MoveAnnotationCommand>(std::vector<core::AnnotationId>{ids.front()},
                                                                         -3.0, 0.0, kEditInstant));
             return out;
         }},
        // NEW TEXT on every annotation whose text it does not draw -- free text draws its own (T7's).
        {"annot.edit",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             const core::AnnotationStore& store = document.annotations();
             if (!store.status()) return out;
             std::vector<core::Annotation> after;
             for (const std::size_t index : store.onPage(page)) {
                 core::Annotation a = store.at(index);
                 if (a.subtype == core::AnnotationSubtype::FreeText) continue;
                 a.contents = a.contents + QStringLiteral(" (edited)");
                 a.modified = core::PdfDate::fromUtcSeconds(kEditInstant);
                 after.push_back(std::move(a));
             }
             if (after.empty()) return out;
             out.push_back(std::make_unique<core::EditAnnotationCommand>(after));
             return out;
         }},
        // A NEW COLOUR on the page's highlights, the one kind this build redraws; an opacity on them too.
        {"annot.setAppearance",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             const core::AnnotationStore& store = document.annotations();
             if (!store.status()) return out;
             std::vector<core::AnnotationId> ids;
             for (const std::size_t index : store.onPage(page)) {
                 const core::Annotation& a = store.at(index);
                 if (a.subtype == core::AnnotationSubtype::Highlight && a.quadPoints.size() >= 8) ids.push_back(a.id);
             }
             if (ids.empty()) return out;
             core::AnnotationAppearance appearance;
             appearance.color = std::vector<double>{0.25, 0.5, 0.75};
             appearance.opacity = 0.3;
             out.push_back(std::make_unique<core::SetAnnotationAppearanceCommand>(ids, appearance, kEditInstant));
             return out;
         }},
        // EVERY LOCK set on what the page owns, and every lock cleared -- whichever changes something.
        {"annot.lock",
         [](const core::Document& document, int page) {
             std::vector<std::unique_ptr<Command>> out;
             const core::AnnotationStore& store = document.annotations();
             const std::vector<core::AnnotationId> ids = ownedIds(document, page);
             if (ids.empty()) return out;
             bool anyUnlocked = false;
             bool anyLocked = false;
             for (const std::size_t index : store.onPage(page)) {
                 const std::uint32_t locks = store.at(index).flags & core::LockAnnotationCommand::kLockFlags;
                 anyUnlocked = anyUnlocked || locks != core::LockAnnotationCommand::kLockFlags;
                 anyLocked = anyLocked || locks != 0;
             }
             if (anyUnlocked)
                 out.push_back(std::make_unique<core::LockAnnotationCommand>(ids, core::LockAnnotationCommand::kLockFlags, 0, kEditInstant));
             if (anyLocked)
                 out.push_back(std::make_unique<core::LockAnnotationCommand>(ids, 0, core::LockAnnotationCommand::kLockFlags, kEditInstant));
             return out;
         }},
    };
    return table;
}

std::vector<std::string> missingGenerators(const Generators& generators)
{
    std::vector<std::string> missing;
    for (const QLatin1String typeId : core::CommandRegistry::typeIds()) {
        std::string id(typeId.data(), static_cast<std::size_t>(typeId.size()));
        if (generators.find(id) == generators.end())
            missing.push_back(std::move(id));
    }
    return missing;
}

// Every corpus file the product may edit, as far as a headless suite can tell: every PDF outside
// encrypted/ (refused by the pre-flight, D3-7), minus the file its two readers count differently.
std::vector<fs::path> corpusFiles()
{
    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(RAWPAGE_CORPUS_DIR)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".pdf")
            continue;
        const fs::path relative = fs::relative(entry.path(), RAWPAGE_CORPUS_DIR);
        if (relative.begin()->string() == "encrypted")
            continue;
        if (relative.generic_string() == "malformed/pages-loop.pdf")
            continue;
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct Report {
    std::size_t files = 0;            // editable: opened with a sink
    std::vector<std::string> notEditable;   // the first-party writer could not open them, or refused them
    std::size_t combinations = 0;
    std::vector<std::string> failures;
};

Report runReversibility(const Generators& generators, const std::vector<fs::path>& files)
{
    Report report;
    for (const fs::path& path : files) {
        const std::string label = fs::relative(path, RAWPAGE_CORPUS_DIR).generic_string();
        tests::CosBackend backend;
        auto handle = backend.open(QString::fromStdString(path.string()), nullptr);
        pdf::ops::MutationSinkImpl* sink = handle ? backend.sinkFor(handle.value()) : nullptr;
        if (!sink) {
            report.notEditable.push_back(label);
            continue;
        }
        core::Document document;
        if (!document.hydrate(backend, handle.value()) || document.pages().count() == 0) {
            report.failures.push_back(label + ": opened with a sink but did not hydrate");
            continue;
        }
        ++report.files;

        const auto emptyDelta = [&]() -> std::optional<bool> {
            auto delta = sink->delta();
            if (!delta)
                return std::nullopt;
            return delta.value().isEmpty();
        };

        std::vector<int> pages{0};
        if (document.pages().count() > 1)
            pages.push_back(document.pages().count() - 1);

        // A failed revert leaves the document different from the file, so every later combination on
        // it would inherit the failure: the rest of the file is abandoned, by returning.
        const auto runFile = [&]() {
            for (const auto& [typeId, generate] : generators) {
                for (int page : pages) {
                    for (auto& command : generate(document, page)) {
                        ++report.combinations;
                        std::ostringstream where;
                        where << typeId << " on " << label << " page " << page << " "
                              << QJsonDocument(command->serialize()).toJson(QJsonDocument::Compact).toStdString()
                              << ": ";
                        const QByteArray before = document.modelHash();
                        if (emptyDelta() != true) {
                            report.failures.push_back(where.str() + "the sink was not clean before apply");
                            continue;
                        }
                        if (!command->apply(document, *sink)) {
                            report.failures.push_back(where.str() + "apply returned false");
                            continue;
                        }
                        if (document.modelHash() == before && emptyDelta() == true) {
                            report.failures.push_back(where.str() + "apply changed neither the model nor the sink");
                            continue;
                        }
                        if (!command->revert(document, *sink)) {
                            report.failures.push_back(where.str() + "revert returned false");
                            return;
                        }
                        const bool modelRestored = document.modelHash() == before;
                        const std::optional<bool> sinkClean = emptyDelta();
                        if (!modelRestored)
                            report.failures.push_back(where.str() + "revert did not restore the model hash");
                        if (!sinkClean.has_value())
                            report.failures.push_back(where.str() + "the sink could not compute its delta");
                        else if (!*sinkClean)
                            report.failures.push_back(where.str() + "revert left the sink with a non-empty delta");
                        if (!modelRestored || sinkClean != true)
                            return;
                    }
                }
            }
        };
        runFile();
    }
    return report;
}

// ---- A-44's planted liars -----------------------------------------------------------------------

// Applies a real rotation; its revert says it succeeded and restores NOTHING.
class LyingRotateCommand final : public Command {
public:
    explicit LyingRotateCommand(int page) : real_(page, 90) {}
    QLatin1String typeId() const override { return QLatin1String("test.lyingRotate"); }
    core::CatalogText name() const override { return real_.name(); }
    core::Scope scope() const override { return real_.scope(); }
    core::Operation operation() const override { return real_.operation(); }
    bool apply(core::Document& document, core::MutationSink& sink) override { return real_.apply(document, sink); }
    bool revert(core::Document&, core::MutationSink&) override { return true; }
    QJsonObject serialize() const override { return real_.serialize(); }

private:
    RotatePageCommand real_;
};

// Restores the MODEL and forgets the SINK -- the half a model-hash-only harness cannot see.
class ModelOnlyRevertCommand final : public Command {
public:
    explicit ModelOnlyRevertCommand(int page) : page_(page), real_(page, 90) {}
    QLatin1String typeId() const override { return QLatin1String("test.modelOnlyRevert"); }
    core::CatalogText name() const override { return real_.name(); }
    core::Scope scope() const override { return real_.scope(); }
    core::Operation operation() const override { return real_.operation(); }
    bool apply(core::Document& document, core::MutationSink& sink) override
    {
        original_ = static_cast<int>(document.pages().rotation(page_));
        return real_.apply(document, sink);
    }
    bool revert(core::Document& document, core::MutationSink&) override
    {
        return document.pages().setRotation(page_, original_);
    }
    QJsonObject serialize() const override { return real_.serialize(); }

private:
    int page_;
    int original_ = 0;
    RotatePageCommand real_;
};

// Restores the SINK and forgets the MODEL.
class SinkOnlyRevertCommand final : public Command {
public:
    explicit SinkOnlyRevertCommand(int page) : page_(page), real_(page, 90) {}
    QLatin1String typeId() const override { return QLatin1String("test.sinkOnlyRevert"); }
    core::CatalogText name() const override { return real_.name(); }
    core::Scope scope() const override { return real_.scope(); }
    core::Operation operation() const override { return real_.operation(); }
    bool apply(core::Document& document, core::MutationSink& sink) override
    {
        original_ = static_cast<int>(document.pages().rotation(page_));
        return real_.apply(document, sink);
    }
    bool revert(core::Document&, core::MutationSink& sink) override
    {
        return static_cast<bool>(sink.setPageRotation(page_, original_));
    }
    QJsonObject serialize() const override { return real_.serialize(); }

private:
    int page_;
    int original_ = 0;
    RotatePageCommand real_;
};

// Does nothing either way -- passes "restores" trivially, and must fail "changed something".
class InertCommand final : public Command {
public:
    explicit InertCommand(int page) : page_(page) {}
    QLatin1String typeId() const override { return QLatin1String("test.inert"); }
    core::CatalogText name() const override { return {QLatin1String("command.page.rotate"), {}}; }
    core::Scope scope() const override { return core::Scope::page(page_); }
    core::Operation operation() const override { return core::Operation::EditPages; }
    bool apply(core::Document&, core::MutationSink&) override { return true; }
    bool revert(core::Document&, core::MutationSink&) override { return true; }
    QJsonObject serialize() const override { return {}; }

private:
    int page_;
};

template <typename Liar>
Generators liar(const char* typeId)
{
    return Generators{{typeId, [](const core::Document&, int page) {
                           std::vector<std::unique_ptr<Command>> out;
                           out.push_back(std::make_unique<Liar>(page));
                           return out;
                       }}};
}

// Every failure names the command, and at least one of them gives `reason`.
bool failuresName(const Report& report, const std::string& typeId, const std::string& reason)
{
    const bool allNamed = std::all_of(report.failures.begin(), report.failures.end(),
                                      [&](const std::string& failure) { return failure.rfind(typeId, 0) == 0; });
    const bool anyReason = std::any_of(report.failures.begin(), report.failures.end(),
                                       [&](const std::string& failure) { return failure.find(reason) != std::string::npos; });
    return allNamed && anyReason;
}

std::vector<fs::path> controlFiles()
{
    return {fs::path(RAWPAGE_CORPUS_DIR) / "ordinary" / "text-a4.pdf",
            fs::path(RAWPAGE_CORPUS_DIR) / "ordinary" / "rotate-inherited.pdf"};
}

} // namespace

// ==========================================================================================
// A-44 -- the harness can read a wrong answer. Declared FIRST: A-43 is trusted only after it.
// ==========================================================================================

TEST_CASE("reversibility harness fails a command whose revert lies", "[cmd][reversibility][control]")
{
    SECTION("revert returns true and restores nothing")
    {
        const Report report = runReversibility(liar<LyingRotateCommand>("test.lyingRotate"), controlFiles());
        INFO("failures: " << report.failures.size());
        REQUIRE_FALSE(report.failures.empty());
        CHECK(failuresName(report, "test.lyingRotate", "revert did not restore the model hash"));
        CHECK(report.files == 2);
    }
    SECTION("revert restores the model and forgets the sink")
    {
        const Report report = runReversibility(liar<ModelOnlyRevertCommand>("test.modelOnlyRevert"), controlFiles());
        REQUIRE_FALSE(report.failures.empty());
        CHECK(failuresName(report, "test.modelOnlyRevert", "non-empty delta"));
    }
    SECTION("revert restores the sink and forgets the model")
    {
        const Report report = runReversibility(liar<SinkOnlyRevertCommand>("test.sinkOnlyRevert"), controlFiles());
        REQUIRE_FALSE(report.failures.empty());
        CHECK(failuresName(report, "test.sinkOnlyRevert", "did not restore the model hash"));
    }
    SECTION("apply and revert both do nothing")
    {
        const Report report = runReversibility(liar<InertCommand>("test.inert"), controlFiles());
        REQUIRE_FALSE(report.failures.empty());
        CHECK(failuresName(report, "test.inert", "changed neither"));
    }
    SECTION("a registered type id with no generator")
    {
        CHECK(missingGenerators(Generators{}) == std::vector<std::string>{"page.rotate", "annot.add", "annot.delete", "annot.edit",
                                                                          "annot.move", "annot.setAppearance", "annot.lock"});
    }
}

// ==========================================================================================
// A-43 -- reversibility over the corpus
// ==========================================================================================

TEST_CASE("reversibility of every registered command over every editable corpus file", "[cmd][reversibility][revert:RotatePageCommand][revert:AddAnnotationCommand][revert:DeleteAnnotationCommand][revert:EditAnnotationCommand][revert:MoveAnnotationCommand][revert:SetAnnotationAppearanceCommand][revert:LockAnnotationCommand]")
{
    const std::vector<std::string> missing = missingGenerators(registeredGenerators());
    INFO("registered type ids with no generator: " << missing.size());
    CHECK(missing.empty());

    const std::vector<fs::path> files = corpusFiles();
    const Report report = runReversibility(registeredGenerators(), files);

    std::cout << "reversibility: " << report.combinations << " combinations over " << report.files
              << " editable corpus files; " << report.failures.size() << " failures; "
              << report.notEditable.size() << " of " << files.size()
              << " files the first-party writer did not open for editing:";
    for (const std::string& label : report.notEditable)
        std::cout << " " << label;
    std::cout << "\n";
    for (const std::string& failure : report.failures)
        UNSCOPED_INFO(failure);
    CHECK(report.failures.empty());

    // Every ordinary and signed file is editable (tst_backendedit's control), so a harness that
    // skipped most of the corpus would show here rather than as a quietly short run.
    CHECK(report.files >= 36);
    CHECK(report.combinations >= report.files * 3);
}
