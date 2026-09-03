#include "pdf/cos/filters/JpxFilter.h"

#include <openjpeg.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace rawpage::pdf::cos::filters {
namespace {

core::Error failed(const char* what)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                       QStringLiteral("JPXDecode: ") + QString::fromLatin1(what)};
}

// A read cursor over the stream we were given. OpenJPEG's stream abstraction is callback-based and
// has no memory source of its own, so this is the smallest thing that satisfies it -- and it copies
// nothing: a stream is already an offset and a length into the document buffer.
struct MemorySource {
    const std::uint8_t* data = nullptr;
    OPJ_SIZE_T size = 0;
    OPJ_SIZE_T at = 0;
};

OPJ_SIZE_T sourceRead(void* buffer, OPJ_SIZE_T bytes, void* user)
{
    auto* source = static_cast<MemorySource*>(user);
    if (source->at >= source->size)
        return static_cast<OPJ_SIZE_T>(-1);            // OpenJPEG's spelling of end-of-stream
    const OPJ_SIZE_T count = std::min(bytes, source->size - source->at);
    std::memcpy(buffer, source->data + source->at, count);
    source->at += count;
    return count;
}

OPJ_OFF_T sourceSkip(OPJ_OFF_T bytes, void* user)
{
    auto* source = static_cast<MemorySource*>(user);
    if (bytes < 0)
        return -1;
    const auto want = static_cast<OPJ_SIZE_T>(bytes);
    const OPJ_SIZE_T count = std::min(want, source->size - std::min(source->at, source->size));
    source->at += count;
    return static_cast<OPJ_OFF_T>(count);
}

OPJ_BOOL sourceSeek(OPJ_OFF_T position, void* user)
{
    auto* source = static_cast<MemorySource*>(user);
    if (position < 0 || static_cast<OPJ_SIZE_T>(position) > source->size)
        return OPJ_FALSE;
    source->at = static_cast<OPJ_SIZE_T>(position);
    return OPJ_TRUE;
}

// OpenJPEG's default handlers print to stdout. A reader that narrated every warning from a damaged
// image would train its user to ignore output that matters -- the same argument `DctFilter` makes
// about libjpeg's `emit_message`.
void quiet(const char*, void*) {}

// `/JPXDecode` data is a raw J2K codestream or a JP2 box container, and they need different
// codecs. Handing OpenJPEG the wrong one reports "not a valid codestream" on a perfectly good file.
CODEC_FORMAT formatOf(std::string_view input)
{
    static const std::uint8_t kJp2Signature[] = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20};
    if (input.size() >= sizeof(kJp2Signature)
        && std::memcmp(input.data(), kJp2Signature, sizeof(kJp2Signature)) == 0) {
        return OPJ_CODEC_JP2;
    }
    return OPJ_CODEC_J2K;
}

} // namespace

core::Result<Bytes> jpxDecode(std::string_view input)
{
    JpxInfo ignored;
    return jpxDecode(input, ignored);
}

core::Result<Bytes> jpxDecode(std::string_view input, JpxInfo& info)
{
    if (input.empty())
        return failed("empty stream");

    MemorySource source{reinterpret_cast<const std::uint8_t*>(input.data()),
                        static_cast<OPJ_SIZE_T>(input.size()), 0};

    // OPJ_TRUE: this is an input stream. The flag is easy to miss and the failure is a silent
    // "cannot read header" on a file that is fine.
    opj_stream_t* stream = opj_stream_default_create(OPJ_TRUE);
    if (stream == nullptr)
        return failed("cannot create the stream");

    opj_stream_set_read_function(stream, &sourceRead);
    opj_stream_set_skip_function(stream, &sourceSkip);
    opj_stream_set_seek_function(stream, &sourceSeek);
    opj_stream_set_user_data(stream, &source, nullptr);
    opj_stream_set_user_data_length(stream, static_cast<OPJ_UINT64>(input.size()));

    opj_codec_t* codec = opj_create_decompress(formatOf(input));
    if (codec == nullptr) {
        opj_stream_destroy(stream);
        return failed("cannot create the decoder");
    }
    opj_set_info_handler(codec, &quiet, nullptr);
    opj_set_warning_handler(codec, &quiet, nullptr);
    opj_set_error_handler(codec, &quiet, nullptr);

    opj_dparameters_t parameters{};
    opj_set_default_decoder_parameters(&parameters);

    opj_image_t* image = nullptr;
    auto cleanup = [&]() {
        if (image != nullptr)
            opj_image_destroy(image);
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
    };

    if (!opj_setup_decoder(codec, &parameters)) {
        cleanup();
        return failed("cannot configure the decoder");
    }
    if (!opj_read_header(stream, codec, &image) || image == nullptr) {
        cleanup();
        return failed("cannot read the header");
    }
    if (!opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) {
        cleanup();
        return failed("cannot decode the codestream");
    }

    const OPJ_UINT32 components = image->numcomps;
    if (components == 0 || image->comps == nullptr) {
        cleanup();
        return failed("no components");
    }

    // Components may be subsampled and may have different sizes. This filter produces INTERLEAVED
    // samples at the first component's resolution, which is what `/JPXDecode` is defined to hand
    // to the image dictionary; anything cleverer is the image pipeline's business, not a filter's.
    const OPJ_UINT32 width = image->comps[0].w;
    const OPJ_UINT32 height = image->comps[0].h;
    if (width == 0 || height == 0) {
        cleanup();
        return failed("zero-sized image");
    }

    info.width = static_cast<int>(width);
    info.height = static_cast<int>(height);
    info.components = static_cast<int>(components);
    info.bitsPerComponent = static_cast<int>(image->comps[0].prec);

    Bytes out;
    out.resize(static_cast<std::size_t>(width) * height * components);

    for (OPJ_UINT32 component = 0; component < components; ++component) {
        const opj_image_comp_t& plane = image->comps[component];
        // A signed component is stored around zero and has to be shifted before it is a sample;
        // `prec` above 8 is scaled down rather than truncated, because dropping the high bits of a
        // 12-bit medical scan produces a picture that is wrong in an interesting-looking way.
        const int shift = plane.sgnd ? (1 << (plane.prec - 1)) : 0;
        const int maximum = (1 << plane.prec) - 1;
        for (OPJ_UINT32 y = 0; y < height; ++y) {
            for (OPJ_UINT32 x = 0; x < width; ++x) {
                // Nearest-neighbour for a subsampled plane. Not a resampler -- the alternative is
                // no samples at all for that component.
                const OPJ_UINT32 sx = plane.w == width ? x : x * plane.w / width;
                const OPJ_UINT32 sy = plane.h == height ? y : y * plane.h / height;
                int value = 0;
                if (plane.data != nullptr && sx < plane.w && sy < plane.h)
                    value = plane.data[sy * plane.w + sx] + shift;
                value = std::clamp(value, 0, maximum);
                if (plane.prec > 8)
                    value = value * 255 / maximum;
                else if (plane.prec < 8)
                    value = maximum > 0 ? value * 255 / maximum : 0;
                const std::size_t index =
                    (static_cast<std::size_t>(y) * width + x) * components + component;
                out[index] = static_cast<std::uint8_t>(value);
            }
        }
    }

    cleanup();
    return out;
}

} // namespace rawpage::pdf::cos::filters
