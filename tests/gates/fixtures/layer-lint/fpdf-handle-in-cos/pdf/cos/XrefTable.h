// FIXTURE -- the same leak one directory deeper into the layer the old rule allowed, and with a
// DIFFERENT spelling again. A first-party xref table that knows what an FPDF_DOCUMENT is has stopped
// being first-party.
#pragma once
namespace rawpage::pdf::cos {
class XrefTable { public: void attach(FPDF_DOCUMENT source); };
}
