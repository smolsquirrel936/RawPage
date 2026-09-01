// FIXTURE -- the leak that no FPDF_ CALL announces. A core/ type holding the engine's own handle is
// exactly what the boundary exists to prevent, and it would compile, link and work on this machine.
// Three spellings, because Stage 0's I-15 was a gate that passed on real violations since every
// fixture used one.
#pragma once
namespace rawpage::core::render {
struct RenderRequest {
    FPDF_DOCUMENT document;
    FPDF_PAGE     page;
    FPDF_BITMAP   into;
};
}
