// Stage 1 T4 -- `core/cmd/KeyBindingStore`, `core/cmd/Chord`, and the §8 collision census.
//
// Guide cases A-30 … A-37 in Stage1-AppShell-Testing-Guide.md §5.4.
//
// THIS TEST PARSES `Features.md` §8 AND COMPARES IT TO THE STORE, ROW FOR ROW (decision D1-9).
// That is deliberate and it is the whole value of the file. §8 is declared the single source of
// truth for key bindings; a test written against a hand-copied list would make the COPY the source
// of truth and let the document drift in silence. Here, a documentation edit the code has not
// absorbed breaks the build.
//
// The parser is deliberately strict. A table-format change is a test failure BY DESIGN, and every
// failure names the row it could not handle -- guide case A-30 requires exactly that, because
// "the store is wrong" (S2) and "the parser is wrong" (S3) need to be told apart from the output.

#include <catch2/catch_test_macros.hpp>

#include "core/cmd/Chord.h"
#include "core/cmd/KeyBindingStore.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace rawpage::core;

namespace {

// ---------------------------------------------------------------------------------------
// The §8 parser
// ---------------------------------------------------------------------------------------
struct DocRow {
    KeyContext  context;
    std::string cell;       // the raw Key cell, for error messages
    std::string command;    // the raw Command cell, for error messages
    std::vector<Chord> chords;
    bool        descriptive = false;   // `arrows`, `Shift`+drag -- documented, not a literal chord
};

std::string readFeaturesMd()
{
    std::ifstream file(RAWPAGE_FEATURES_MD, std::ios::binary);
    REQUIRE(file.is_open());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::optional<KeyContext> contextForHeading(const std::string& heading)
{
    // Matched on the heading's PREFIX, so the human-facing parenthetical ("(Review mode)",
    // "(active in every context)") can be reworded without breaking the test, while a renamed
    // table still fails loudly.
    struct Entry { const char* prefix; KeyContext context; };
    static const Entry kEntries[] = {
        {"Global",              KeyContext::Global},
        {"Viewer",              KeyContext::Viewer},
        {"Write mode",          KeyContext::Write},
        {"Annotation tools",    KeyContext::AnnotationTools},
        {"Page Thumbnails",     KeyContext::PageThumbnails},
        {"Bookmarks",           KeyContext::Bookmarks},
        {"Comments",            KeyContext::Comments},
        {"Forms \xE2\x80\x94 Design", KeyContext::FormsDesign},   // em dash
        {"Forms \xE2\x80\x94 Fill",   KeyContext::FormsFill},
        {"Find / Search",       KeyContext::FindSearch},
        {"Full Screen",         KeyContext::FullScreen},
        {"Common",              KeyContext::Common},
    };
    for (const Entry& entry : kEntries)
        if (heading.rfind(entry.prefix, 0) == 0) return entry.context;
    return std::nullopt;
}

std::string trimmed(std::string text)
{
    const auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r'; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), notSpace));
    text.erase(std::find_if(text.rbegin(), text.rend(), notSpace).base(), text.end());
    return text;
}

std::vector<DocRow> parseSection8(std::string& parseErrors)
{
    const std::string markdown = readFeaturesMd();

    const std::size_t begin = markdown.find("## 8. Key Bindings");
    const std::size_t end = markdown.find("## 9. System & Infrastructure");
    REQUIRE(begin != std::string::npos);
    REQUIRE(end != std::string::npos);
    REQUIRE(end > begin);

    std::vector<DocRow> rows;
    std::optional<KeyContext> current;

    std::istringstream section(markdown.substr(begin, end - begin));
    std::string line;
    while (std::getline(section, line)) {
        line = trimmed(line);

        if (line.rfind("### ", 0) == 0) {
            current = contextForHeading(line.substr(4));
            continue;   // a heading we do not recognise closes the current table rather than
                        // silently appending its rows to the previous context
        }
        if (!current || line.empty() || line.front() != '|') continue;

        // | Key | Command |
        std::vector<std::string> cells;
        std::string cell;
        std::istringstream row(line.substr(1));
        while (std::getline(row, cell, '|')) cells.push_back(trimmed(cell));
        if (!cells.empty() && cells.back().empty()) cells.pop_back();
        if (cells.size() < 2) continue;
        if (cells[0] == "Key") continue;
        if (cells[0].find_first_not_of("-: ") == std::string::npos) continue;

        DocRow parsed;
        parsed.context = *current;
        parsed.cell = cells[0];
        parsed.command = cells[1];

        // §8 writes a chord in backticks. A cell mentioning `arrows` or drag is a DESCRIPTION of a
        // family of keystrokes, not a chord -- "arrows / Ctrl+arrows: nudge / fine-nudge". Those
        // are classified explicitly here; anything else that fails to parse is a hard failure that
        // names its row, which is what keeps this parser strict rather than forgiving.
        if (parsed.cell.find("arrows") != std::string::npos ||
            parsed.cell.find("drag") != std::string::npos) {
            parsed.descriptive = true;
            rows.push_back(parsed);
            continue;
        }

        std::size_t at = 0;
        while ((at = parsed.cell.find('`', at)) != std::string::npos) {
            const std::size_t close = parsed.cell.find('`', at + 1);
            if (close == std::string::npos) break;
            const std::string text = parsed.cell.substr(at + 1, close - at - 1);
            at = close + 1;
            if (text.empty()) continue;

            if (const std::optional<Chord> chord = Chord::parse(text)) {
                parsed.chords.push_back(*chord);
            } else {
                parseErrors += "  could not parse chord `" + text + "` in row: " + parsed.cell +
                               " | " + parsed.command + "\n";
            }
        }

        if (parsed.chords.empty() && !parsed.descriptive) {
            parseErrors += "  row yielded no chord at all: " + parsed.cell + " | " +
                           parsed.command + "\n";
        }
        rows.push_back(parsed);
    }
    return rows;
}

std::string describe(const Chord& chord) { return chord.toString(); }

std::set<std::string> chordSet(const std::vector<Shadow>& shadows)
{
    std::set<std::string> out;
    for (const Shadow& shadow : shadows) out.insert(shadow.chord.toString());
    return out;
}

} // namespace

// =======================================================================================
// A-30 -- the §8 tables load, row for row
// =======================================================================================

TEST_CASE("the Features key-binding tables parse without an unexplained row", "[keybindings]")
{
    std::string parseErrors;
    const std::vector<DocRow> rows = parseSection8(parseErrors);

    INFO("unparsed rows:\n" << parseErrors);
    CHECK(parseErrors.empty());
    CHECK(rows.size() > 150);
}

TEST_CASE("every documented chord is in the store, in its own context", "[keybindings]")
{
    std::string parseErrors;
    const std::vector<DocRow> rows = parseSection8(parseErrors);
    REQUIRE(parseErrors.empty());

    const KeyBindingStore store;

    for (const DocRow& row : rows) {
        if (row.descriptive) continue;
        for (const Chord& chord : row.chords) {
            INFO("context " << keyContextName(row.context) << ", chord " << describe(chord)
                            << ", documented as: " << row.command);
            // resolveExact semantics: the binding must exist IN THIS CONTEXT, not merely resolve
            // through the Common or Global fallback. A row that only resolved by falling back
            // would mean the store had quietly lost the context-specific meaning.
            const std::vector<KeyBinding> bindings = store.bindingsFor(row.context);
            const bool present = std::any_of(bindings.begin(), bindings.end(),
                [&](const KeyBinding& binding) { return binding.chord == chord; });
            CHECK(present);
        }
    }
}

TEST_CASE("every stored default is documented, with no invented bindings", "[keybindings]")
{
    // The other direction. Without it the store could carry a chord nobody wrote down, which is
    // how an undocumented shortcut ends up shipping.
    std::string parseErrors;
    const std::vector<DocRow> rows = parseSection8(parseErrors);
    REQUIRE(parseErrors.empty());

    std::set<std::pair<int, std::string>> documented;
    for (const DocRow& row : rows)
        for (const Chord& chord : row.chords)
            documented.insert({static_cast<int>(row.context), chord.toString()});

    const KeyBindingStore store;
    for (const KeyBinding& binding : store.allBindings()) {
        INFO("stored binding " << keyContextName(binding.context) << " / "
                               << describe(binding.chord) << " is not in the document");
        CHECK(documented.count({static_cast<int>(binding.context), binding.chord.toString()}) == 1);
    }
}

TEST_CASE("the per-context row counts are reported and non-trivial", "[keybindings]")
{
    // A-30 asks for the number of rows parsed per context. Reported through INFO so a passing run
    // still shows them and a regression in one table is visible rather than hidden in a total.
    const KeyBindingStore store;
    const KeyContext contexts[] = {
        KeyContext::Global,      KeyContext::Common,          KeyContext::Viewer,
        KeyContext::Write,       KeyContext::AnnotationTools, KeyContext::PageThumbnails,
        KeyContext::Bookmarks,   KeyContext::Comments,        KeyContext::FormsDesign,
        KeyContext::FormsFill,   KeyContext::FindSearch,      KeyContext::FullScreen,
    };
    for (KeyContext context : contexts) {
        INFO(keyContextName(context) << " = " << store.defaultCountFor(context) << " bindings");
        CHECK(store.defaultCountFor(context) > 0);
    }
    CHECK(store.defaultCount() == store.allBindings().size());
}

// =======================================================================================
// A-31 -- the Write-mode collisions: thirteen since 2026-10-04, twelve before
// =======================================================================================

TEST_CASE("the census reports exactly thirteen Write-mode collisions, split 9 + 4 + 0", "[keybindings]")
{
    // Stage 13's exit criteria depend on this figure, which is why it is asserted at the stage that
    // builds the store rather than discovered eleven stages later. The SPLIT is asserted too: a
    // total of twelve reached with a different split would be a different fact wearing the right
    // number, and that is precisely how issue I-01 arose. THIRTEEN since 2026-10-04: the fourteen
    // annotation-tool arming chords moved from Annotation tools to Viewer (`Stage6-Annotations-Plan.md`
    // Q1, D6-9), and `Ctrl+Alt+M` -- a sticky note there, a margin comment in Write mode -- became a
    // Write-over-Viewer collision. The user chose to count it rather than leave the chord unreachable.
    const KeyBindingStore store;
    const CollisionCensus census = store.census();

    CHECK(chordSet(census.writeModeOverViewer) ==
          std::set<std::string>{"Ctrl+B", "Ctrl+E", "Ctrl+L", "Ctrl+R", "Ctrl+U", "Ctrl+=",
                                "Ctrl+Shift+=", "Ctrl+Shift+N", "Ctrl+Alt+M"});
    CHECK(chordSet(census.writeModeOverGlobal) ==
          std::set<std::string>{"Ctrl+D", "Ctrl+K", "Ctrl+Shift+A", "Ctrl+Shift+L"});

    CHECK(census.writeModeOverViewer.size() == 9);
    CHECK(census.writeModeOverGlobal.size() == 4);
    CHECK(census.writeModeOverCommon.size() == 0);
    CHECK(census.writeModeTotal() == 13);
}

// =======================================================================================
// A-32 -- the two wider numbers that are NOT "thirteen"
// =======================================================================================

TEST_CASE("class 1 is exactly the six chords a context takes back from Global", "[keybindings]")
{
    const KeyBindingStore store;
    CHECK(chordSet(store.census().classOne) ==
          std::set<std::string>{"Ctrl+D", "Ctrl+K", "F11", "Ctrl+Shift+L", "Ctrl+Shift+A",
                                "Ctrl+Shift+G"});
}

TEST_CASE("class 2 is exactly the nine permitted Common overrides", "[keybindings]")
{
    const KeyBindingStore store;
    CHECK(chordSet(store.census().classTwo) ==
          std::set<std::string>{"Ctrl+X", "Ctrl+C", "Ctrl+V", "Ctrl+A", "Delete", "Backspace",
                                "Esc", "F2", "Ctrl+F"});
}

TEST_CASE("shadows() reports class 1 and never class 2", "[keybindings]")
{
    // §8's Common heading permits contexts to override it, so a class-2 entry is not a shadow. A
    // report that included all fifteen would be noise, and a noisy report gets ignored -- which
    // would cost us the six that genuinely make a Global chord unreachable.
    const KeyBindingStore store;
    const std::set<std::string> reported = chordSet(store.shadows());
    const std::set<std::string> classTwo = chordSet(store.census().classTwo);

    CHECK(reported.size() == 6);
    for (const std::string& chord : classTwo) {
        if (reported.count(chord) == 0) continue;
        // Ctrl+D and a few others appear in both classes legitimately; only flag a chord that is
        // class 2 and NOT class 1.
        const std::set<std::string> classOne = chordSet(store.census().classOne);
        INFO("chord " << chord << " is reported as a shadow but is only a permitted Common override");
        CHECK(classOne.count(chord) == 1);
    }
}

// =======================================================================================
// A-33 -- each collision resolves to the right id per context
// =======================================================================================

TEST_CASE("the class-1 chords each mean the right thing in each context", "[keybindings]")
{
    // The one thing the whole KeyContext model exists for.
    const KeyBindingStore store;
    const auto at = [&](const char* chord, KeyContext context) {
        return store.lookup(*Chord::parse(chord), context);
    };

    CHECK(at("Ctrl+K", KeyContext::Viewer) == ActionId::FilePreferences);
    CHECK(at("Ctrl+K", KeyContext::Write) == ActionId::WriteInsertHyperlink);

    CHECK(at("Ctrl+D", KeyContext::Viewer) == ActionId::FileDocumentProperties);
    CHECK(at("Ctrl+D", KeyContext::Write) == ActionId::WriteFontDialog);
    CHECK(at("Ctrl+D", KeyContext::FormsDesign) == ActionId::FormDuplicateField);

    CHECK(at("F11", KeyContext::Viewer) == ActionId::ViewFullScreen);
    CHECK(at("F11", KeyContext::FullScreen) == ActionId::PresentExit);

    CHECK(at("Ctrl+Shift+L", KeyContext::Viewer) == ActionId::PaneLayers);
    CHECK(at("Ctrl+Shift+L", KeyContext::Write) == ActionId::WriteBulletedList);

    CHECK(at("Ctrl+Shift+A", KeyContext::Viewer) == ActionId::PaneAttachments);
    CHECK(at("Ctrl+Shift+A", KeyContext::Write) == ActionId::WriteAllCaps);

    CHECK(at("Ctrl+Shift+G", KeyContext::Viewer) == ActionId::PaneSignatures);
    CHECK(at("Ctrl+Shift+G", KeyContext::FindSearch) == ActionId::FindPrevious);
}

TEST_CASE("all thirteen Write-mode chords resolve to their word-processor meaning", "[keybindings]")
{
    const KeyBindingStore store;
    const auto inWrite = [&](const char* chord) {
        return store.lookup(*Chord::parse(chord), KeyContext::Write);
    };

    CHECK(inWrite("Ctrl+B") == ActionId::WriteBold);
    CHECK(inWrite("Ctrl+E") == ActionId::WriteAlignCenter);
    CHECK(inWrite("Ctrl+L") == ActionId::WriteAlignLeft);
    CHECK(inWrite("Ctrl+R") == ActionId::WriteAlignRight);
    CHECK(inWrite("Ctrl+U") == ActionId::WriteUnderline);
    CHECK(inWrite("Ctrl+=") == ActionId::WriteSubscript);
    CHECK(inWrite("Ctrl+Shift+=") == ActionId::WriteSuperscript);
    CHECK(inWrite("Ctrl+Shift+N") == ActionId::WriteStyleNormal);
    CHECK(inWrite("Ctrl+D") == ActionId::WriteFontDialog);
    CHECK(inWrite("Ctrl+K") == ActionId::WriteInsertHyperlink);
    CHECK(inWrite("Ctrl+Shift+A") == ActionId::WriteAllCaps);
    CHECK(inWrite("Ctrl+Shift+L") == ActionId::WriteBulletedList);
    CHECK(inWrite("Ctrl+Alt+M") == ActionId::WriteInsertMarginComment);

    // And the Viewer meanings they displace are still intact where Write is not active.
    CHECK(store.lookup(*Chord::parse("Ctrl+B"), KeyContext::Viewer) == ActionId::BookmarkNew);
    CHECK(store.lookup(*Chord::parse("Ctrl+="), KeyContext::Viewer) == ActionId::ViewZoomIn);
    CHECK(store.lookup(*Chord::parse("Ctrl+Alt+M"), KeyContext::Viewer) == ActionId::AnnotStickyNote);
}

// =======================================================================================
// A-34 -- resolution order
// =======================================================================================

TEST_CASE("resolution is innermost, then Common, then Global", "[keybindings]")
{
    const KeyBindingStore store;

    // Ctrl+F is defined in all three layers: Common (Find), Global (absent), FindSearch and
    // Comments. In Comments it must be the comment search, not the general Find.
    CHECK(store.lookup(*Chord::parse("Ctrl+F"), KeyContext::Comments) == ActionId::FindInComments);
    CHECK(store.lookup(*Chord::parse("Ctrl+F"), KeyContext::FindSearch) == ActionId::FindFind);
    CHECK(store.lookup(*Chord::parse("Ctrl+F"), KeyContext::Viewer) == ActionId::FindFind);

    // A chord defined only in Common reaches every context.
    CHECK(store.lookup(*Chord::parse("Ctrl+Z"), KeyContext::Write) == ActionId::EditUndo);
    CHECK(store.lookup(*Chord::parse("Ctrl+Z"), KeyContext::Bookmarks) == ActionId::EditUndo);

    // A chord defined only in Global reaches every context too -- but Common is consulted FIRST,
    // which is what the ordering assertion above is really pinning down.
    CHECK(store.lookup(*Chord::parse("Ctrl+O"), KeyContext::Write) == ActionId::FileOpen);

    // An unbound chord resolves to nothing rather than to something plausible.
    CHECK_FALSE(store.lookup(*Chord::parse("Ctrl+Alt+Shift+F12"), KeyContext::Viewer).has_value());
}

// Stage 6 T5, decision D6-9 (`Stage6-Annotations-Plan.md` §3), guide case A-40's store half: the FIRST
// context chain. Annotation tools resolves through the Viewer before the layers -- and only it does.
TEST_CASE("annotation tools resolve through the viewer and then the layers", "[keybindings][D6-9]")
{
    const KeyBindingStore store;
    const KeyContext tools = KeyContext::AnnotationTools;

    // A Viewer chord still acts with a tool armed: zoom, page, fit, and the arming chords themselves --
    // so one tool can be swapped for another without disarming first.
    CHECK(store.lookup(*Chord::parse("Ctrl+="), tools) == ActionId::ViewZoomIn);
    CHECK(store.lookup(*Chord::parse("Page Down"), tools) == ActionId::NavNextScreen);
    CHECK(store.lookup(*Chord::parse("Ctrl+Alt+H"), tools) == ActionId::AnnotHighlight);
    CHECK(store.lookup(*Chord::parse("Ctrl+Alt+M"), tools) == ActionId::AnnotStickyNote);

    // Its own rows win where they collide with the Viewer's: `Ctrl+E` is the properties bar either way,
    // through two ids (§8 binds both); `Delete` is the selected annotation, not Common's `edit.delete`.
    CHECK(store.lookup(*Chord::parse("Ctrl+E"), tools) == ActionId::AnnotProperties);
    CHECK(store.lookup(*Chord::parse("Ctrl+E"), KeyContext::Viewer) == ActionId::ViewTogglePropertiesBar);
    CHECK(store.lookup(*Chord::parse("Delete"), tools) == ActionId::AnnotDeleteSelected);
    CHECK(store.lookup(*Chord::parse("K"), tools) == ActionId::AnnotKeepToolSelected);
    CHECK_FALSE(store.lookup(*Chord::parse("K"), KeyContext::Viewer).has_value());

    // The Viewer's widget-native arrows are reached too, and stay the widget's: no action, a binding.
    const std::optional<KeyBinding> up = store.resolve(*Chord::parse("Up"), tools);
    REQUIRE(up.has_value());
    CHECK(up->kind == BindingKind::WidgetNative);

    // Then Common and Global, as for every context.
    CHECK(store.lookup(*Chord::parse("Ctrl+Z"), tools) == ActionId::EditUndo);
    CHECK(store.lookup(*Chord::parse("Ctrl+O"), tools) == ActionId::FileOpen);

    // NO OTHER CONTEXT HAS A CHAIN: a Viewer chord means nothing from a pane.
    CHECK_FALSE(store.lookup(*Chord::parse("Ctrl+Alt+H"), KeyContext::Bookmarks).has_value());
    CHECK_FALSE(store.lookup(*Chord::parse("Ctrl+="), KeyContext::PageThumbnails).has_value());
    CHECK(keyContextFallback(KeyContext::AnnotationTools) == KeyContext::Viewer);
    for (std::size_t i = 0; i < kKeyContextCount; ++i) {
        const auto context = static_cast<KeyContext>(i);
        if (context != KeyContext::AnnotationTools) CHECK_FALSE(keyContextFallback(context).has_value());
    }
}

TEST_CASE("a widget-native row resolves to no action but is still a known binding", "[keybindings]")
{
    // ShortcutBinder needs the difference: "nothing bound" may fall through, but a widget-native
    // chord must NOT be swallowed by the shortcut layer -- the focused widget is expecting it.
    const KeyBindingStore store;
    const Chord up = *Chord::parse("Up");

    CHECK_FALSE(store.lookup(up, KeyContext::PageThumbnails).has_value());

    const std::optional<KeyBinding> binding = store.resolve(up, KeyContext::PageThumbnails);
    REQUIRE(binding.has_value());
    CHECK(binding->kind == BindingKind::WidgetNative);
    CHECK_FALSE(binding->action.has_value());
}

// =======================================================================================
// A-35 / A-36 -- remapping
// =======================================================================================

TEST_CASE("a remap takes effect and the old meaning stops", "[keybindings]")
{
    KeyBindingStore store;
    const Chord chord = *Chord::parse("Ctrl+M");

    REQUIRE(store.lookup(chord, KeyContext::Viewer) == ActionId::ViewZoomTo);
    REQUIRE(store.rebind(chord, KeyContext::Viewer, ActionId::CompareDocuments));
    CHECK(store.lookup(chord, KeyContext::Viewer) == ActionId::CompareDocuments);

    // The remap is scoped to its context and does not leak.
    CHECK(store.lookup(chord, KeyContext::Write) != ActionId::CompareDocuments);

    store.clearOverride(chord, KeyContext::Viewer);
    CHECK(store.lookup(chord, KeyContext::Viewer) == ActionId::ViewZoomTo);
}

TEST_CASE("a remap round-trips through the settings key family", "[keybindings]")
{
    // D1-10's `shortcuts/<context>/<action>` family, exercised without touching a file: core/ may
    // not write to disk, and this class deliberately has no I/O.
    KeyBindingStore first;
    REQUIRE(first.rebind(*Chord::parse("Ctrl+Alt+Z"), KeyContext::Viewer, ActionId::ViewZoomTo));
    REQUIRE(first.rebind(*Chord::parse("F8"), KeyContext::Write, ActionId::WriteWordCount));

    const std::vector<KeyBindingStore::OverrideEntry> saved = first.exportOverrides();
    REQUIRE(saved.size() == 2);

    // The persisted form uses the PERMANENT string id, never the enum value.
    for (const KeyBindingStore::OverrideEntry& entry : saved)
        CHECK(entry.actionStringId.find('.') != std::string::npos);

    KeyBindingStore second;
    CHECK(second.importOverrides(saved) == 0);
    CHECK(second.lookup(*Chord::parse("Ctrl+Alt+Z"), KeyContext::Viewer) == ActionId::ViewZoomTo);
    CHECK(second.lookup(*Chord::parse("F8"), KeyContext::Write) == ActionId::WriteWordCount);
}

TEST_CASE("a remap that shadows a Global chord is reported, not forbidden", "[keybindings]")
{
    // §8 ships six class-1 collisions by design, so forbidding shadowing would make the specified
    // defaults themselves illegal.
    KeyBindingStore store;
    const std::size_t before = store.shadows().size();

    REQUIRE(store.rebind(*Chord::parse("Ctrl+O"), KeyContext::Bookmarks, ActionId::BookmarkGoTo));

    CHECK(store.lookup(*Chord::parse("Ctrl+O"), KeyContext::Bookmarks) == ActionId::BookmarkGoTo);
    CHECK(store.shadows().size() == before + 1);
}

TEST_CASE("remapping onto a widget-native chord is refused", "[keybindings]")
{
    // The one restriction. Binding an action to Tab inside Forms - Fill would take the key from
    // the focus chain and leave the user unable to move between fields: a trap, not a preference.
    KeyBindingStore store;
    const Result<void> refused =
        store.rebind(*Chord::parse("Tab"), KeyContext::FormsFill, ActionId::FormCommitField);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == ErrorCode::InvalidArgument);
}

// =======================================================================================
// A-37 -- a saved shortcut for an action that no longer exists
// =======================================================================================

TEST_CASE("a saved shortcut for an unknown action is dropped, not fatal", "[keybindings]")
{
    // A settings file written by a newer build, or one naming an id a later stage retired without
    // an alias row, must not prevent startup.
    KeyBindingStore store;
    const std::vector<KeyBindingStore::OverrideEntry> entries{
        {KeyContext::Viewer, "no.such.action", "Ctrl+Alt+Q"},
        {KeyContext::Viewer, "view.zoomTo", "Ctrl+Alt+W"},
        {KeyContext::Viewer, "view.fitPage", "this is not a chord`"},
    };

    CHECK(store.importOverrides(entries) == 2);

    // The good one still applied -- one bad row must not discard the rest of the file.
    CHECK(store.lookup(*Chord::parse("Ctrl+Alt+W"), KeyContext::Viewer) == ActionId::ViewZoomTo);
}

// =======================================================================================
// Chord itself
// =======================================================================================

TEST_CASE("chords round-trip through their canonical spelling", "[chord]")
{
    const char* samples[] = {"Ctrl+O", "Ctrl+Shift+S", "Ctrl+Alt+F", "F11", "Esc", "Delete",
                             "Page Down", "Alt+Enter", "Ctrl+=", "Ctrl+Shift+>", "+", "-", "*",
                             "/", "Ctrl+'", "Ctrl+;", "Alt"};
    for (const char* sample : samples) {
        INFO("chord " << sample);
        const std::optional<Chord> chord = Chord::parse(sample);
        REQUIRE(chord.has_value());
        CHECK(chord->toString() == sample);
    }
}

TEST_CASE("a bare modifier is a chord, and is not the same as that modifier plus a key", "[chord]")
{
    // §8's Global table binds `Alt` alone to the ribbon key tips.
    const Chord alt = *Chord::parse("Alt");
    const Chord altEnter = *Chord::parse("Alt+Enter");
    CHECK(alt.key() == "Alt");
    CHECK(alt.modifiers() == 0);
    CHECK(altEnter.hasAlt());
    CHECK(alt != altEnter);
}

TEST_CASE("'+' as a key survives, rather than being eaten as a separator", "[chord]")
{
    // The Page Thumbnails table binds `+` to larger thumbnails. A greedy split on every '+' would
    // turn that row into an empty key and lose the binding silently.
    const std::optional<Chord> plus = Chord::parse("+");
    REQUIRE(plus.has_value());
    CHECK(plus->key() == "+");
    CHECK(plus->modifiers() == 0);
}

TEST_CASE("descriptive notations are rejected rather than guessed at", "[chord]")
{
    CHECK_FALSE(Chord::parse("arrows").has_value());
    CHECK_FALSE(Chord::parse("Ctrl+`arrows").has_value());
    CHECK_FALSE(Chord::parse("").has_value());
    CHECK_FALSE(Chord::parse("   ").has_value());
}

TEST_CASE("the arrow glyphs the document uses map to spelled names", "[chord]")
{
    // Features.md is written for a human and says U+2192. A settings-file key must not depend on
    // that surviving a code-page round trip -- issues I-04 and I-07 are both that hazard.
    CHECK(Chord::parse("\xE2\x86\x92")->key() == "Right");
    CHECK(Chord::parse("\xE2\x86\x90")->key() == "Left");
    CHECK(Chord::parse("\xE2\x86\x91")->key() == "Up");
    CHECK(Chord::parse("\xE2\x86\x93")->key() == "Down");
    CHECK(Chord::parse("Alt+\xE2\x86\x92")->toString() == "Alt+Right");
}
