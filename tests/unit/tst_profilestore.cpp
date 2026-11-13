// tst_profilestore -- Stage 6 T10: the custom-stamp library, `core/settings/ProfileStore` (D6-16; guide A-79,
// `Stage6-Annotations-Testing-Guide.md` §5.13).
//
//   * A stamp added is two files under the root -- its image as it was given, and its description -- and a new
//     store over the same root reads it back: name, category, favourite, format.
//   * A-79's damage: a TRUNCATED description, one naming an image that is not there, one out of range and a
//     damaged favourites file are each REPORTED by file name, and every other stamp still loads.
//   * What is not an image -- a PDF among it -- is refused by `add` by key, naming the file the person chose, and
//     nothing is written: a page is `addPage`'s.
//   * Stage 7 T13 (D7-36, Stage 7 guide A-90): a PAGE stamp is its page as the copier copied it, its size in its
//     description and its picture beside it; a page with no picture, or a damaged one, still loads; a page that is
//     not one is refused by key, and its damaged file is reported by `pageOf`. The restart carries one too.
//   * Built-in favourites live in `favourites.json`, by `/Name`; a custom stamp's in its own file.
//   * A-79's restart: the library written by ONE process and read by ANOTHER (`unit.profile_store.restart`,
//     `RunProfileRestart.cmake`), with a control -- an empty root must make the reader fail.
//
// WHAT IT DOES NOT COVER (convention 19): the palette that shows the library and the dialog that adds to it --
// `tests/ui/tst_stamps` -- and the product's root, `%APPDATA%\RawPage\stamps\`, which `app/` chooses and every
// case here replaces with a scratch directory, so no case reads or writes the library of whoever runs it.

#include <catch2/catch_test_macros.hpp>

#include "core/settings/ProfileStore.h"
#include "pdf/ops/ObjectGraphCopier.h"
#include "pdf/ops/PageStamp.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

using rawpage::core::CustomStamp;
using rawpage::core::ProfileStore;

namespace {

constexpr std::int64_t kNow = 1791106200;

std::string keyOf(const rawpage::core::Error& error)
{
    return std::string(error.messageKey.data(), static_cast<std::size_t>(error.messageKey.size()));
}

QByteArray corpus(const char* relative)
{
    QFile file(QStringLiteral(RAWPAGE_CORPUS_DIR "/") + QLatin1String(relative));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    REQUIRE(file.write(bytes) == bytes.size());
}

// What one process writes and another reads: a stamp in a category of its own, a favourite signature, and a
// favourite built-in.
void writeLibrary(ProfileStore& store)
{
    auto seal = store.add(QStringLiteral("Seal"), QStringLiteral("Office"), corpus("stamps/seal-rgba.png"), kNow);
    REQUIRE(seal);
    auto signature = store.add(QStringLiteral("My signature"), QLatin1String(ProfileStore::kSignaturesCategory),
                               corpus("stamps/signature.jpg"), kNow + 1);
    REQUIRE(signature);
    REQUIRE(store.setFavourite(signature.value().id, true));
    REQUIRE(store.setFavourite(QStringLiteral("NotApproved"), true));
}

std::string checkLibrary(const ProfileStore& store)
{
    if (!store.damaged().isEmpty()) return "damaged: " + store.damaged().join(QLatin1Char(',')).toStdString();
    std::vector<CustomStamp> images;   // the image stamps -- a page stamp beside them is `checkPage`'s (Stage 7 T13)
    for (const CustomStamp& stamp : store.stamps())
        if (stamp.format != QLatin1String("pdf")) images.push_back(stamp);
    if (images.size() != 2) return "stamps: " + std::to_string(images.size());
    const CustomStamp& seal = images[0];
    const CustomStamp& signature = images[1];
    if (seal.name != QLatin1String("Seal") || seal.category != QLatin1String("Office") || seal.favourite || seal.format != QLatin1String("png"))
        return "the seal";
    if (signature.category != QLatin1String(ProfileStore::kSignaturesCategory) || !signature.favourite || signature.format != QLatin1String("jpg"))
        return "the signature";
    if (!store.isFavourite(QStringLiteral("NotApproved")) || store.isFavourite(QStringLiteral("Approved"))) return "the built-in favourite";
    auto bytes = store.imageBytes(signature.id);
    if (!bytes || bytes.value() != corpus("stamps/signature.jpg")) return "the signature's image";
    return {};
}

// Stage 7 T13: refs-every-kind.pdf's page 3 as a stamp's page, as the backend makes one.
rawpage::core::StampPage pageThree()
{
    const QByteArray bytes = corpus("assembly/refs-every-kind.pdf");
    auto copied = rawpage::pdf::ops::ObjectGraphCopier::copyFile(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), {2});
    REQUIRE(copied);
    auto page = rawpage::pdf::ops::PageStamp::fromPayload(copied.value().graph);
    REQUIRE(page);
    return page.value();
}

// What the restart's writer adds beside `writeLibrary`'s: a page stamp and its picture.
void writePage(ProfileStore& store)
{
    auto page = store.addPage(QStringLiteral("Page three"), QStringLiteral("Pages"), pageThree(), kNow + 2);
    REQUIRE(page);
    REQUIRE(store.setPreview(page.value().id, corpus("stamps/seal-rgba.png")));
}

std::string checkPage(const ProfileStore& store)
{
    const auto found = std::find_if(store.stamps().begin(), store.stamps().end(),
                                    [](const CustomStamp& s) { return s.format == QLatin1String("pdf"); });
    if (found == store.stamps().end()) return "no page stamp";
    if (found->name != QLatin1String("Page three") || found->category != QLatin1String("Pages")) return "the page stamp's name";
    const rawpage::core::StampPage want = pageThree();
    auto page = store.pageOf(found->id);
    if (!page) return "the page: " + keyOf(page.error());
    if (page.value() != want) return "the page's graph or size";
    if (store.previewBytes(found->id) != std::optional<QByteArray>(corpus("stamps/seal-rgba.png"))) return "the page's picture";
    return {};
}

} // namespace

TEST_CASE("profile store: a page stamp is its page, its size and its picture, and a new store reads all three",
          "[profilestore][A-90][D7-36]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    ProfileStore store(root.filePath(QStringLiteral("stamps")));
    store.load();
    const rawpage::core::StampPage three = pageThree();
    auto page = store.addPage(QStringLiteral("Page three"), QStringLiteral("Pages"), three, kNow);
    REQUIRE(page);
    CHECK(page.value().format == QStringLiteral("pdf"));
    const QString id = page.value().id;
    // No picture yet: the stamp is whole without one.
    CHECK_FALSE(store.previewBytes(id).has_value());
    CHECK(QDir(store.root()).entryList(QDir::Files).size() == 2);   // the page and its description
    // An image stamp's bytes it has none of; a picture that is no PNG is refused.
    CHECK(keyOf(store.imageBytes(id).error()) == "error.stamps.damaged");
    CHECK(keyOf(store.setPreview(id, corpus("stamps/signature.jpg")).error()) == "error.stamps.notAnImage");
    REQUIRE(store.setPreview(id, corpus("stamps/seal-rgba.png")));
    CHECK(QDir(store.root()).entryList(QDir::Files).size() == 3);

    ProfileStore again(store.root());
    again.load();
    CHECK(again.damaged().isEmpty());
    INFO(checkPage(again));
    CHECK(checkPage(again).empty());
    REQUIRE(again.find(id));
    CHECK(again.find(id)->pageWidth == three.width);
    CHECK(again.find(id)->pageHeight == three.height);

    // A DAMAGED picture is no picture -- the stamp still loads and places. A damaged PAGE is reported when it is read.
    writeFile(QDir(store.root()).filePath(id + QStringLiteral(".preview.png")), QByteArray("not a png"));
    ProfileStore third(store.root());
    third.load();
    CHECK(third.damaged().isEmpty());
    CHECK_FALSE(third.previewBytes(id).has_value());
    REQUIRE(third.pageOf(id));
    writeFile(QDir(store.root()).filePath(id + QStringLiteral(".pdf")), QByteArray("truncated"));
    CHECK(keyOf(third.pageOf(id).error()) == "error.stamps.damaged");
    // A size no page has, in the description: reported by name at load.
    QFile description(QDir(store.root()).filePath(id + QStringLiteral(".json")));
    REQUIRE(description.open(QIODevice::ReadOnly));
    QByteArray json = description.readAll();
    description.close();
    json.replace("\"pageWidth\"", "\"pageWidth\": 0, \"was\"");
    writeFile(description.fileName(), json);
    ProfileStore fourth(store.root());
    fourth.load();
    CHECK(fourth.damaged() == QStringList{id + QStringLiteral(".json")});

    // Removed: all three files go.
    REQUIRE(again.remove(id));
    CHECK(QDir(store.root()).entryList(QDir::Files).isEmpty());

    // Refused before anything is written: no page, and a page no size can show.
    ProfileStore empty(root.filePath(QStringLiteral("other")));
    empty.load();
    CHECK(keyOf(empty.addPage(QStringLiteral("x"), QStringLiteral("Pages"), rawpage::core::StampPage{QByteArray("nope"), 100, 100}, kNow,
                              QStringLiteral("nope.pdf")).error()) == "error.stamps.notAPage");
    CHECK(keyOf(empty.addPage(QStringLiteral("x"), QStringLiteral("Pages"), rawpage::core::StampPage{three.graph, 1.0, 100}, kNow).error())
          == "error.stamps.notAPage");
    CHECK(keyOf(empty.addPage(QStringLiteral(" "), QStringLiteral("Pages"), three, kNow).error()) == "error.stamps.badName");
    CHECK_FALSE(QDir(empty.root()).exists());
}

TEST_CASE("profile store: a stamp is its image and its description and a new store reads both", "[profilestore][A-79]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    ProfileStore store(root.filePath(QStringLiteral("stamps")));
    store.load();   // a root that does not exist yet is an empty library, not a damaged one
    CHECK(store.stamps().empty());
    CHECK(store.damaged().isEmpty());

    writeLibrary(store);
    const QStringList files = QDir(store.root()).entryList(QDir::Files, QDir::Name);
    CHECK(files.size() == 5);   // two images, two descriptions, the favourites
    CHECK(store.categories() == QStringList({QLatin1String(ProfileStore::kSignaturesCategory), QStringLiteral("Office")}));

    ProfileStore again(store.root());
    again.load();
    INFO(checkLibrary(again));
    CHECK(checkLibrary(again).empty());

    // Renamed, refiled and removed -- each written at once.
    const QString seal = again.stamps()[0].id;
    REQUIRE(again.rename(seal, QStringLiteral("Office seal")));
    REQUIRE(again.setCategory(seal, QStringLiteral("Seals")));
    ProfileStore third(store.root());
    third.load();
    REQUIRE(third.find(seal));
    CHECK(third.find(seal)->name == QStringLiteral("Office seal"));
    CHECK(third.find(seal)->category == QStringLiteral("Seals"));
    REQUIRE(third.remove(seal));
    ProfileStore fourth(store.root());
    fourth.load();
    CHECK(fourth.stamps().size() == 1);
    CHECK(QDir(store.root()).entryList(QDir::Files).size() == 3);
}

TEST_CASE("profile store: a damaged file is reported by name and the rest of the library loads", "[profilestore][A-79]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    ProfileStore store(root.filePath(QStringLiteral("stamps")));
    store.load();
    writeLibrary(store);
    auto third = store.add(QStringLiteral("Third"), QStringLiteral("Office"), corpus("stamps/seal-rgba.png"), kNow + 2);
    auto fourth = store.add(QStringLiteral("Fourth"), QStringLiteral("Office"), corpus("stamps/seal-rgba.png"), kNow + 3);
    REQUIRE(third);
    REQUIRE(fourth);
    const QDir dir(store.root());

    // TRUNCATED: the seal's description cut in half, as a crash mid-write of anything but `QSaveFile` would leave it.
    const QString sealFile = store.stamps()[0].id + QStringLiteral(".json");
    QFile description(dir.filePath(sealFile));
    REQUIRE(description.open(QIODevice::ReadOnly));
    const QByteArray whole = description.readAll();
    description.close();
    writeFile(dir.filePath(sealFile), whole.left(whole.size() / 2));
    // An image gone, and a category no person can type.
    REQUIRE(QFile::remove(dir.filePath(third.value().id + QStringLiteral(".png"))));
    QByteArray outOfRange = whole;
    outOfRange.replace("\"Office\"", "\":nobody\"");
    writeFile(dir.filePath(fourth.value().id + QStringLiteral(".json")), outOfRange);
    // And the favourites file, truncated.
    writeFile(dir.filePath(QStringLiteral("favourites.json")), QByteArray("{\"builtIn\": [\"NotAppr"));

    ProfileStore damaged(store.root());
    damaged.load();
    const QStringList reported = damaged.damaged();
    INFO(reported.join(QLatin1Char(' ')).toStdString());
    CHECK(reported.size() == 4);
    CHECK(reported.contains(sealFile));
    CHECK(reported.contains(third.value().id + QStringLiteral(".json")));
    CHECK(reported.contains(fourth.value().id + QStringLiteral(".json")));
    CHECK(reported.contains(QStringLiteral("favourites.json")));
    // The rest of the library: the signature, still a favourite.
    REQUIRE(damaged.stamps().size() == 1);
    CHECK(damaged.stamps()[0].name == QStringLiteral("My signature"));
    CHECK(damaged.stamps()[0].favourite);
    CHECK_FALSE(damaged.isFavourite(QStringLiteral("NotApproved")));   // the damaged list lost, not invented
}

TEST_CASE("profile store: what is not an image is refused by name and nothing is written", "[profilestore][A-78]")
{
    QTemporaryDir root;
    REQUIRE(root.isValid());
    ProfileStore store(root.filePath(QStringLiteral("stamps")));
    store.load();
    auto pdf = store.add(QStringLiteral("A page"), QStringLiteral("Office"), corpus("ordinary/text-a4.pdf"), kNow, QStringLiteral("text-a4.pdf"));
    REQUIRE_FALSE(pdf);
    CHECK(keyOf(pdf.error()) == "error.stamps.notAnImage");
    CHECK(pdf.error().context == QStringLiteral("text-a4.pdf"));
    CHECK_FALSE(QDir(store.root()).exists());

    CHECK(keyOf(store.add(QStringLiteral("  "), QStringLiteral("Office"), corpus("stamps/seal-rgba.png"), kNow).error()) == "error.stamps.badName");
    CHECK(keyOf(store.add(QStringLiteral("Seal"), QStringLiteral(":mine"), corpus("stamps/seal-rgba.png"), kNow).error()) == "error.stamps.badName");
    CHECK(ProfileStore::imageFormatOf(corpus("stamps/seal-rgba.png")) == QStringLiteral("png"));
    CHECK(ProfileStore::imageFormatOf(corpus("stamps/signature.jpg")) == QStringLiteral("jpg"));
    CHECK(ProfileStore::imageFormatOf(QByteArray("%PDF-1.7")).isEmpty());
}

// ---- The restart, as two processes (`RunProfileRestart.cmake`) --------------------------------------
// HIDDEN: each needs `RAWPAGE_PROFILE_PROBE_ROOT` and is run by the script, the writer first.

TEST_CASE("profile store restart probe writes", "[.][restart]")
{
    const QString root = qEnvironmentVariable("RAWPAGE_PROFILE_PROBE_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    ProfileStore store(root);
    store.load();
    REQUIRE(store.stamps().empty());
    writeLibrary(store);
    writePage(store);   // Stage 7 T13: and a page stamp with its picture (guide A-90)
}

TEST_CASE("profile store restart probe reads", "[.][restart]")
{
    const QString root = qEnvironmentVariable("RAWPAGE_PROFILE_PROBE_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    ProfileStore store(root);
    store.load();
    std::string wrong = checkPage(store);
    if (wrong.empty()) wrong = checkLibrary(store);
    INFO("what did not survive the restart: " << wrong);
    CHECK(wrong.empty());
    std::printf("profile store restart: a category, a favourite signature, a favourite built-in and a page stamp with "
                "its picture read back by a second process\n");
}
