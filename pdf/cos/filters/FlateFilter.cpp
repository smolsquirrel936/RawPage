#include "pdf/cos/filters/FlateFilter.h"

#include <zlib.h>

#include <array>

namespace rawpage::pdf::cos::filters {
namespace {

core::Error failed(const char* what)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String("error.pdf.filterFailed"),
                       QString::fromLatin1(what)};
}

// `windowBits` is 15 for a zlib-wrapped stream and -15 for a bare deflate one. Everything else is
// identical, which is why the retry below is one call rather than a second implementation.
bool inflateWith(std::string_view input, int windowBits, Bytes& out)
{
    out.clear();
    if (input.empty())
        return true;

    z_stream stream{};
    if (inflateInit2(&stream, windowBits) != Z_OK)
        return false;

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    std::array<unsigned char, 16384> buffer{};
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = buffer.size() - stream.avail_out;
        out.insert(out.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(produced));
        if (status == Z_STREAM_END)
            break;
        if (status != Z_OK) {
            // Z_BUF_ERROR and Z_DATA_ERROR partway through are what a TRUNCATED stream looks like.
            // Whatever came out before the failure is kept; the caller decides whether an
            // incomplete result is usable, and T8's report is where "incomplete" gets said.
            break;
        }
    } while (stream.avail_in > 0 || stream.avail_out == 0);

    inflateEnd(&stream);
    return status == Z_STREAM_END || !out.empty();
}

} // namespace

core::Result<Bytes> flateDecode(std::string_view input)
{
    Bytes out;
    if (inflateWith(input, 15, out))
        return out;

    // A bare deflate stream with no two-byte zlib wrapper. Some producers emit them; the retry
    // costs one call and rescues a file class that would otherwise decode to nothing at all.
    if (inflateWith(input, -15, out))
        return out;

    return failed("flate: the stream produced no output under either window setting");
}

namespace {

core::Result<Bytes> flateEncodeAt(std::string_view input, int level)
{
    z_stream stream{};
    if (deflateInit(&stream, level) != Z_OK)
        return failed("flate: deflateInit failed");

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());

    Bytes out;
    out.reserve(input.size() / 2 + 64);

    std::array<unsigned char, 16384> buffer{};
    int status = Z_OK;
    do {
        stream.next_out = buffer.data();
        stream.avail_out = static_cast<uInt>(buffer.size());
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            deflateEnd(&stream);
            return failed("flate: deflate failed");
        }
        const std::size_t produced = buffer.size() - stream.avail_out;
        out.insert(out.end(), buffer.begin(),
                   buffer.begin() + static_cast<std::ptrdiff_t>(produced));
    } while (status != Z_STREAM_END);

    deflateEnd(&stream);
    return out;
}

} // namespace

core::Result<Bytes> flateEncode(std::string_view input)
{
    return flateEncodeAt(input, Z_DEFAULT_COMPRESSION);
}

core::Result<Bytes> flateEncodeFast(std::string_view input)
{
    return flateEncodeAt(input, Z_BEST_SPEED);
}

} // namespace rawpage::pdf::cos::filters
