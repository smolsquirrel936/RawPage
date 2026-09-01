// Legitimate: the facade is where a scoped handle belongs, and a rule forbidding it everywhere
// would leave the facade unable to own anything.
namespace rawpage::pdf::render { ScopedFPDFDocument open(); }
