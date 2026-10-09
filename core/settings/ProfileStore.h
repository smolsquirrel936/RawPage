// core/settings/ProfileStore -- the custom-stamp library: each stamp a person made, its category, whether it is
// a favourite, and which built-in stamps are. Stage 6 T10, decision D6-16 (`Stage6-Annotations-Plan.md` §3).
//
// ===========================================================================================
// ONE FILE PER STAMP, SO A DAMAGED FILE LOSES ONLY ITSELF
// ===========================================================================================
// Under its root (`%APPDATA%\RawPage\stamps\` in the product, a scratch directory in every case): for each
// stamp, `<id>.json` -- its name, category, favourite flag, image format and when it was made -- beside
// `<id>.png` or `<id>.jpg`, the image exactly as it was given. `favourites.json` lists the BUILT-IN stamps
// marked favourite. A single library file would be one truncation away from an empty palette; here a file
// that does not parse, names an image that is not there, or holds a field out of range is REPORTED by name
// and skipped, and every other stamp loads (guide A-79). Each file is written whole through `QSaveFile` --
// replaced or left as it was -- and the image before its description, so a stamp whose description exists
// has its image.
//
// ===========================================================================================
// WHAT IT HOLDS, AND WHAT IT DOES NOT
// ===========================================================================================
// The image FILE, never its pixels: `core/` names no Qt GUI type (convention 3), so `ui/` decodes a PNG and
// `core/doc/Stamps` passes a JPEG through. This store checks only that the bytes ARE a PNG or a JPEG, by their
// signature -- a PDF is refused here by name, the page source being Stage 7's (Q7). Categories are the
// stamps' own, plus one reserved: `kSignaturesCategory`, which the palette shows in the person's language and
// which holds scanned signatures and drawn or typed initials -- markup, not a digital signature (Stage 11).
// The library is the PERSON'S, not a document's: nothing here is a command, and no undo reaches it.
//
// It writes files, which is `core/settings/`'s one added privilege (`tools/layer-lint/rules.json`) -- no
// waiver. Pure QtCore: `tst_profilestore` in `rawpage_tests`.

#ifndef RAWPAGE_CORE_SETTINGS_PROFILESTORE_H
#define RAWPAGE_CORE_SETTINGS_PROFILESTORE_H

#include "core/Result.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <set>
#include <vector>

namespace rawpage::core {

struct CustomStamp {
    QString id;              // 32 lowercase hex digits: its two files' names
    QString name;            // what the palette calls it
    QString category;        // never empty: `kSignaturesCategory`, or a person's own
    bool favourite = false;
    QString format;          // "png" or "jpg"
    std::int64_t created = 0;   // UTC seconds

    bool operator==(const CustomStamp&) const = default;
};

class ProfileStore {
public:
    // The reserved category of signatures and initials. A person's own category may not start with `:`.
    static constexpr const char* kSignaturesCategory = ":signatures";
    static constexpr int kMaxNameLength = 128;
    static constexpr int kMaxStamps = 10000;
    static constexpr qint64 kMaxImageBytes = 64LL * 1024 * 1024;

    explicit ProfileStore(QString root);

    // `%APPDATA%\RawPage\stamps` -- beside `settings.ini` and `session.json`, roaming with them. Only `app/main.cpp`
    // asks for it: every other composition passes a scratch root, or none (`app::CompositionRoot::Options`).
    static QString defaultRoot();

    // Reads the library from disk, replacing what was held. A file it cannot read is listed in `damaged()` and
    // skipped; a root that does not exist is an empty library.
    void load();

    const QString& root() const noexcept { return root_; }
    const std::vector<CustomStamp>& stamps() const noexcept { return stamps_; }
    // The files `load` could not read, by name, in the order met.
    const QStringList& damaged() const noexcept { return damaged_; }
    std::optional<CustomStamp> find(const QString& id) const;
    // Every category a stamp is filed under, the reserved one first, the rest in sorted order.
    QStringList categories() const;

    // `image` saved as a new stamp. `error.stamps.notAnImage` for bytes that are neither a PNG nor a JPEG --
    // named by `sourceName`, the file the person chose; `error.stamps.badName` for an empty or over-long name
    // or category, or a category starting with `:` that is not the reserved one.
    Result<CustomStamp> add(const QString& name, const QString& category, const QByteArray& image,
                            std::int64_t utcNow, const QString& sourceName = QString());
    Status rename(const QString& id, const QString& name);
    Status setCategory(const QString& id, const QString& category);
    // A custom stamp's flag by its id, or a built-in's by its `/Name` (`Approved`, `DynamicApproved`).
    Status setFavourite(const QString& stamp, bool favourite);
    bool isFavourite(const QString& stamp) const;
    Status remove(const QString& id);
    // The image a stamp was made from, as it was given.
    Result<QByteArray> imageBytes(const QString& id) const;

    // `png` or `jpg` for bytes that start as one does; empty for anything else.
    static QString imageFormatOf(const QByteArray& bytes);

private:
    QString descriptionPath(const QString& id) const;
    QString imagePath(const CustomStamp& stamp) const;
    Status writeDescription(const CustomStamp& stamp) const;
    Status writeFavourites() const;

    QString root_;
    std::vector<CustomStamp> stamps_;
    std::set<QString> builtInFavourites_;
    QStringList damaged_;
};

} // namespace rawpage::core

#endif // RAWPAGE_CORE_SETTINGS_PROFILESTORE_H
