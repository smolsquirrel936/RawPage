// tests/unit/TinyPdf.h -- a PDF of a few objects, built inside a test. Stage 6 T2.
//
// The hostile annotation cases (guide A-20, A-22) need inputs no corpus file should carry -- a
// million-point /InkList, one annotation listed by a thousand pages, a /Rect at infinity -- and a
// corpus file holding each would be a megabyte of damage committed to make a point a test can make in
// ten lines. So they are built here, with a real cross-reference table, and read through the same
// `pdf/ops/AnnotationReader` the product uses. Not a generator: the corpus stays the corpus
// (`corpus/PROVENANCE.md`), and nothing built here is written to disk.

#ifndef RAWPAGE_TESTS_UNIT_TINYPDF_H
#define RAWPAGE_TESTS_UNIT_TINYPDF_H

#include "core/doc/AnnotationStore.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/MutationSinkImpl.h"

#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rawpage::tests {

class TinyPdf {
public:
    // Object numbers start at 1 and are handed out in order.
    int reserve()
    {
        objects_.emplace_back();
        return static_cast<int>(objects_.size());
    }
    int add(std::string body)
    {
        objects_.push_back(std::move(body));
        return static_cast<int>(objects_.size());
    }
    void set(int number, std::string body) { objects_[static_cast<std::size_t>(number - 1)] = std::move(body); }

    std::string bytes(int root) const
    {
        std::string out = "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n";
        std::vector<std::size_t> offsets;
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            offsets.push_back(out.size());
            out += std::to_string(i + 1) + " 0 obj\n" + objects_[i] + "\nendobj\n";
        }
        const std::size_t xref = out.size();
        out += "xref\n0 " + std::to_string(objects_.size() + 1) + "\n0000000000 65535 f \n";
        for (const std::size_t offset : offsets) {
            char line[24];
            std::snprintf(line, sizeof line, "%010zu 00000 n \n", offset);
            out += line;
        }
        out += "trailer\n<< /Size " + std::to_string(objects_.size() + 1) + " /Root " + std::to_string(root)
               + " 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
        return out;
    }

private:
    std::vector<std::string> objects_;
};

// A document of `pageCount` pages. `annotsFor(page, pdf, pageObject)` returns the text written after
// `/Annots` on that page (empty: no /Annots key), and may add the annotation objects it refers to.
inline std::string annotatedDocument(int pageCount,
                                     const std::function<std::string(int, TinyPdf&, int)>& annotsFor)
{
    TinyPdf pdf;
    const int catalog = pdf.reserve();
    const int pages = pdf.reserve();
    std::vector<int> pageObjects;
    for (int i = 0; i < pageCount; ++i) pageObjects.push_back(pdf.reserve());
    std::string kids;
    for (int i = 0; i < pageCount; ++i) {
        const std::string annots = annotsFor(i, pdf, pageObjects[static_cast<std::size_t>(i)]);
        pdf.set(pageObjects[static_cast<std::size_t>(i)],
                "<< /Type /Page /Parent " + std::to_string(pages) + " 0 R /MediaBox [ 0 0 612 792 ]"
                    + (annots.empty() ? std::string() : " /Annots " + annots) + " >>");
        kids += std::to_string(pageObjects[static_cast<std::size_t>(i)]) + " 0 R ";
    }
    pdf.set(catalog, "<< /Type /Catalog /Pages " + std::to_string(pages) + " 0 R >>");
    pdf.set(pages, "<< /Type /Pages /Kids [ " + kids + "] /Count " + std::to_string(pageCount) + " >>");
    return pdf.bytes(catalog);
}

// The first-party reading of `bytes`, exactly as the backend makes it at open -- or nullopt when the
// sink cannot walk a page tree.
inline std::optional<core::AnnotationStore::Reading> readAnnotations(const std::string& bytes)
{
    auto sink = pdf::ops::MutationSinkImpl::open(std::string_view(bytes));
    if (!sink) return std::nullopt;
    return pdf::ops::AnnotationReader::read(sink.value()->table(), sink.value()->pages());
}

} // namespace rawpage::tests

#endif // RAWPAGE_TESTS_UNIT_TINYPDF_H
