#include "pdf/cos/filters/Jbig2Filter.h"

namespace rawpage::pdf::cos::filters {

core::Result<Bytes> jbig2Decode(std::string_view input, std::string_view globals)
{
    // The parameters are named and unused on purpose: the signature is the one the decoder will
    // need, so landing it later is a change to this file and to nothing else.
    (void)input;
    (void)globals;

    // NEVER an empty buffer. An empty result would reach the image pipeline as a blank page --
    // indistinguishable from a page that is genuinely blank -- which is convention 6's argument
    // about silent stubs, one layer down from `platform/`. `Unsupported` plus a key of its own is
    // what makes "this build cannot read JBIG2" a thing the log can say.
    return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.pdf.filterJbig2"),
                       QStringLiteral("JBIG2Decode")};
}

} // namespace rawpage::pdf::cos::filters
