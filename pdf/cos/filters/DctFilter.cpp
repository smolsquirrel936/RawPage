#include "pdf/cos/filters/DctFilter.h"

#include <csetjmp>
#include <cstdio>
#include <string>

extern "C" {
#include <jpeglib.h>
}

namespace rawpage::pdf::cos::filters {
namespace {

core::Error failed(const std::string& what)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                       QStringLiteral("DCTDecode: ") + QString::fromStdString(what)};
}

// libjpeg signals a fatal error by calling `error_exit`, whose DEFAULT implementation calls
// `exit()`. A PDF reader that terminates the process on a malformed image is not a reader, so the
// handler is replaced with a longjmp back into the caller -- which is the arrangement libjpeg's own
// documentation prescribes and the only one it supports.
struct ErrorManager {
    jpeg_error_mgr base;
    std::jmp_buf escape;
    char message[JMSG_LENGTH_MAX] = {};
};

extern "C" void dctErrorExit(j_common_ptr info)
{
    auto* manager = reinterpret_cast<ErrorManager*>(info->err);
    (*info->err->format_message)(info, manager->message);
    std::longjmp(manager->escape, 1);
}

// Warnings are not errors: a corrupt-but-decodable JPEG emits them by the dozen, and libjpeg's
// default handler prints every one to stderr. A reader that spammed the console over a damaged
// thumbnail would train its user to ignore output that matters.
extern "C" void dctEmitMessage(j_common_ptr, int) {}

} // namespace

core::Result<Bytes> dctDecode(std::string_view input)
{
    DctInfo ignored;
    return dctDecode(input, ignored);
}

core::Result<Bytes> dctDecode(std::string_view input, DctInfo& info)
{
    if (input.empty())
        return failed("empty stream");

    jpeg_decompress_struct decompressor{};
    ErrorManager manager{};
    Bytes out;

    decompressor.err = jpeg_std_error(&manager.base);
    manager.base.error_exit = &dctErrorExit;
    manager.base.emit_message = &dctEmitMessage;

    if (setjmp(manager.escape) != 0) {
        jpeg_destroy_decompress(&decompressor);
        // Whatever decoded before the failure is KEPT when there is any: a truncated scan is the
        // ordinary damaged-file case, and the rows that exist are worth more than nothing. An
        // empty result is a real failure and says so.
        if (!out.empty()) {
            info.components = decompressor.output_components;
            return out;
        }
        return failed(manager.message[0] != '\0' ? manager.message : "decode failed");
    }

    jpeg_create_decompress(&decompressor);
    jpeg_mem_src(&decompressor,
                 reinterpret_cast<const unsigned char*>(input.data()),
                 static_cast<unsigned long>(input.size()));

    // TRUE: require the whole image before returning. We always have the whole buffer -- a stream
    // is an offset and a length -- so there is no suspending source to get wrong, and progressive
    // JPEGs work without a second code path.
    jpeg_read_header(&decompressor, TRUE);

    // Adobe's CMYK JPEGs store inverted samples and say so in the APP14 marker. A decoder that
    // ignores this produces a photographic negative -- which reads as a colour-management problem
    // and is not one. libjpeg exposes the marker's presence; the inversion itself is ours to do,
    // because `/DCTDecode`'s output is defined as the samples the image dictionary interprets.
    const bool adobe = decompressor.saw_Adobe_marker != 0;

    jpeg_start_decompress(&decompressor);

    info.width = static_cast<int>(decompressor.output_width);
    info.height = static_cast<int>(decompressor.output_height);
    info.components = decompressor.output_components;
    info.adobeInverted = adobe && decompressor.output_components == 4;

    const auto rowBytes =
        static_cast<std::size_t>(decompressor.output_width) *
        static_cast<std::size_t>(decompressor.output_components);
    out.reserve(rowBytes * decompressor.output_height);

    std::vector<std::uint8_t> row(rowBytes);
    while (decompressor.output_scanline < decompressor.output_height) {
        JSAMPROW pointer = row.data();
        if (jpeg_read_scanlines(&decompressor, &pointer, 1) != 1)
            break;
        out.insert(out.end(), row.begin(), row.end());
    }

    jpeg_finish_decompress(&decompressor);
    jpeg_destroy_decompress(&decompressor);

    if (info.adobeInverted) {
        for (std::uint8_t& sample : out)
            sample = static_cast<std::uint8_t>(255u - sample);
    }

    return out;
}

core::Result<DctInfo> dctInspect(std::string_view input)
{
    if (input.empty())
        return failed("empty stream");

    jpeg_decompress_struct decompressor{};
    ErrorManager manager{};

    decompressor.err = jpeg_std_error(&manager.base);
    manager.base.error_exit = &dctErrorExit;
    manager.base.emit_message = &dctEmitMessage;

    if (setjmp(manager.escape) != 0) {
        jpeg_destroy_decompress(&decompressor);
        return failed(manager.message[0] != '\0' ? manager.message : "header unreadable");
    }

    jpeg_create_decompress(&decompressor);
    jpeg_mem_src(&decompressor,
                 reinterpret_cast<const unsigned char*>(input.data()),
                 static_cast<unsigned long>(input.size()));
    jpeg_read_header(&decompressor, TRUE);

    DctInfo info;
    info.width = static_cast<int>(decompressor.image_width);
    info.height = static_cast<int>(decompressor.image_height);
    info.components = decompressor.num_components;
    info.adobeInverted = decompressor.saw_Adobe_marker != 0 && decompressor.num_components == 4;

    jpeg_destroy_decompress(&decompressor);
    return info;
}

} // namespace rawpage::pdf::cos::filters
