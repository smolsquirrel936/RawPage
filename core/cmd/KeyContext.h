// The input context a chord is dispatched against.
//
// The list is NOT invented here. It is exactly the set of tables in
// [`Features.md` §8](../../claude-plan/Features.md#8-key-bindings-authoritative-reference), which is
// the single source of truth for key bindings: the eleven contexts it names -- of which `Global` is
// one -- plus `Common`, which §8 documents as a table rather than as a context. Twelve values. Adding a context
// here without adding its table there would create a context nothing can bind to; adding a table
// there without a value here would make that table unreachable. Stage 1 T4's `tst_keybindings`
// parses §8 and asserts the two agree, so the drift is a build failure rather than a discovery.
//
// WHY `Common` IS IN THIS ENUM EVEN THOUGH IT IS NOT A PLACE THE FOCUS CAN BE:
// `ShortcutBinder::activeContext()` never returns it. It exists because resolution is layered --
// innermost context first, then `Common`, then `Global` -- and `KeyBindingStore` needs to *store* a
// binding at the `Common` layer to express "Ctrl+C means copy everywhere unless a context says
// otherwise". Leaving it out would force a parallel representation for the same idea.

#ifndef RAWPAGE_CORE_CMD_KEYCONTEXT_H
#define RAWPAGE_CORE_CMD_KEYCONTEXT_H

#include <cstddef>
#include <optional>
#include <string_view>

namespace rawpage::core {

enum class KeyContext {
    // Layers, in resolution order from outermost inward.
    Global,          // active in every context; the outermost layer
    Common,          // applies everywhere unless a context below overrides the chord

    // The remaining ten, one per §8 table.
    Viewer,          // Review mode -- the fixed-layout page surface
    Write,           // Write mode -- the reflowable word-processor surface
    AnnotationTools, // an annotation tool is armed or an annotation is selected
    PageThumbnails,  // the Page Thumbnails pane has focus
    Bookmarks,       // the Bookmarks pane has focus
    Comments,        // the Comments pane has focus
    FormsDesign,     // Form Design mode
    FormsFill,       // filling a form as an end user
    FindSearch,      // the find bar or the search pane has focus
    FullScreen,      // Full Screen and Presentation
};

inline constexpr std::size_t kKeyContextCount = 12;   // 11 contexts from §8, of which Global is one, plus Common

// The order `KeyBindingStore` walks when resolving a chord: the innermost context that is active,
// then `Common`, then `Global`. That order IS the model -- it is what lets `Ctrl+K` mean Preferences
// in the Viewer and Insert-hyperlink in Write mode.
constexpr bool keyContextIsLayer(KeyContext context)
{
    return context == KeyContext::Global || context == KeyContext::Common;
}

constexpr std::string_view keyContextName(KeyContext context)
{
    switch (context) {
    case KeyContext::Global:          return "Global";
    case KeyContext::Common:          return "Common";
    case KeyContext::Viewer:          return "Viewer";
    case KeyContext::Write:           return "Write";
    case KeyContext::AnnotationTools: return "AnnotationTools";
    case KeyContext::PageThumbnails:  return "PageThumbnails";
    case KeyContext::Bookmarks:       return "Bookmarks";
    case KeyContext::Comments:        return "Comments";
    case KeyContext::FormsDesign:     return "FormsDesign";
    case KeyContext::FormsFill:       return "FormsFill";
    case KeyContext::FindSearch:      return "FindSearch";
    case KeyContext::FullScreen:      return "FullScreen";
    }
    return "?";
}

// The inverse, over the SAME table -- which is the only reason it is safe. Two callers need it
// (`core/settings/Settings` reading a `shortcuts/<context>/<action>` key, and
// `ui/shell/UiCustomization` reading an exported customization), and before T11 the first one
// carried its own copy of this loop. A second list of context names is a list that disagrees with
// this one the first time a context is added -- the same reasoning as D1-6, at a smaller scale.
constexpr std::optional<KeyContext> keyContextFromName(std::string_view name)
{
    for (std::size_t i = 0; i < kKeyContextCount; ++i) {
        const auto context = static_cast<KeyContext>(i);
        if (keyContextName(context) == name) return context;
    }
    return std::nullopt;
}

} // namespace rawpage::core

#endif // RAWPAGE_CORE_CMD_KEYCONTEXT_H
