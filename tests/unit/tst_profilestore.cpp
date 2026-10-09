// tst_profilestore -- Stage 6 T10: the custom-stamp library, `core/settings/ProfileStore` (D6-16; guide A-79,
// `Stage6-Annotations-Testing-Guide.md` §5.13).
//
//   * A stamp added is two files under the root -- its image as it was given, and its description -- and a new
//     store over the same root reads it back: name, category, favourite, format.
//   * A-79's damage: a TRUNCATED description, one naming an image that is not there, one out of range and a
//     damaged favourites file are each REPORTED by file name, and every other stamp still loads.
//   * What is not an image -- a PDF among it, the page source being Stage 7's (Q7) -- is refused by key, naming the
//     file the person chose, and nothing is written.
//   * Built-in favourites live in `favourites.json`, by `/Name`; a custom stamp's in its own file.
//   * A-79's restart: the library written by ONE process and read by ANOTHER (`unit.profile_store.restart`,
//     `RunProfileRestart.cmake`), with a control -- an empty root must make the reader fail.
//
// WHAT IT DOES NOT COVER (convention 19): the palette that shows the library and the dialog that adds to it --
// `tests/ui/tst_stamps` -- and the product's root, `%APPDATA%\RawPage\stamps\`, which `app/` chooses and every
// case here replaces with a scratch directory, so no case reads or writes the library of whoever runs it.

#include <catch2/catch_test_macros.hpp>

#include "core/settings/ProfileStore.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cstdio>
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
    if (store.stamps().size() != 2) return "stamps: " + std::to_string(store.stamps().size());
    const CustomStamp& seal = store.stamps()[0];
    const CustomStamp& signature = store.stamps()[1];
    if (seal.name != QLatin1String("Seal") || seal.category != QLatin1String("Office") || seal.favourite || seal.format != QLatin1String("png"))
        return "the seal";
    if (signature.category != QLatin1String(ProfileStore::kSignaturesCategory) || !signature.favourite || signature.format != QLatin1String("jpg"))
        return "the signature";
    if (!store.isFavourite(QStringLiteral("NotApproved")) || store.isFavourite(QStringLiteral("Approved"))) return "the built-in favourite";
    auto bytes = store.imageBytes(signature.id);
    if (!bytes || bytes.value() != corpus("stamps/signature.jpg")) return "the signature's image";
    return {};
}

} // namespace

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
}

TEST_CASE("profile store restart probe reads", "[.][restart]")
{
    const QString root = qEnvironmentVariable("RAWPAGE_PROFILE_PROBE_ROOT");
    REQUIRE_FALSE(root.isEmpty());
    ProfileStore store(root);
    store.load();
    const std::string wrong = checkLibrary(store);
    INFO("what did not survive the restart: " << wrong);
    CHECK(wrong.empty());
    std::printf("profile store restart: a category, a favourite signature and a favourite built-in read back by a "
                "second process\n");
}
