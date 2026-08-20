// Stage 0 entry point -- decision D0-8 in Stage0-Toolchain-Plan.md.
//
// It does nothing, on purpose. The exit criterion for this stage is that an EMPTY
// rawpage.exe builds; an executable that links all four layer libraries proves the
// target graph is wired, whereas one that links none proves nothing at all. So this
// calls into each layer, forcing the linker to actually resolve a symbol from every
// library rather than merely being handed their names.
//
// Stage 1 replaces this with the real thing: DPI awareness set before any window
// exists, the crash handler installed, the command line parsed, then handoff to
// app/CompositionRoot -- which is the only translation unit in the project permitted
// to name pdf::createBackend() or platform::createPlatform().
//
// The declarations below are deliberately extern rather than #include'd: at Stage 0
// there are no layer headers yet, and inventing four headers whose only content is a
// placeholder would be four files to delete later.

namespace rawpage::core     { void moduleAnchor(); }
namespace rawpage::pdf      { void moduleAnchor(); }
namespace rawpage::platform { void moduleAnchor(); }
namespace rawpage::ui       { void moduleAnchor(); }

int main(int /*argc*/, char** /*argv*/)
{
    rawpage::core::moduleAnchor();
    rawpage::pdf::moduleAnchor();
    rawpage::platform::moduleAnchor();
    rawpage::ui::moduleAnchor();

    return 0;
}
