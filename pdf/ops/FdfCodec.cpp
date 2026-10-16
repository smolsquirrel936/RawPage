// pdf/ops/FdfCodec -- implementation. Stage 6 T13. See the header for why an FDF is read by the PDF reader.

#include "pdf/ops/FdfCodec.h"

#include "core/doc/XfdfCodec.h"
#include "pdf/cos/CosObject.h"
#include "pdf/cos/IncrementalWriter.h"
#include "pdf/cos/XrefRepair.h"
#include "pdf/cos/filters/Filter.h"
#include "pdf/ops/AnnotationOps.h"
#include "pdf/ops/AnnotationReader.h"
#include "pdf/ops/AttachmentFiles.h"
#include "pdf/ops/EmbeddedFileReader.h"

#include <QFile>
#include <QFileInfo>

#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace rawpage::pdf::ops {
namespace {

using cos::CosObject;

core::Error exchangeError(const char* key, const QString& context)
{
    return core::Error{core::ErrorCode::ParseFailure, QLatin1String(key), context};
}

// A value written inline or as a reference, resolved; the fetched object kept in `holder`.
const CosObject* resolve(cos::XrefTable& table, const CosObject* value, std::optional<cos::IndirectObject>& holder)
{
    if (value == nullptr) return nullptr;
    if (!value->isReference()) return value;
    auto fetched = table.fetch(value->reference().number);
    if (!fetched) return nullptr;
    holder = std::move(fetched).value();
    return &holder->object;
}

QString textOf(const CosObject* value)
{
    if (value == nullptr || !value->isString()) return QString();
    return EmbeddedFileReader::decodeTextString(value->text());
}

QString hexOf(const CosObject& value)
{
    if (!value.isString()) return QString();
    return QString::fromLatin1(QByteArray::fromStdString(value.text()).toHex().toUpper());
}

// The catalog's `/FDF` dictionary: through the trailer's `/Root`, else the first object that has one -- a file whose
// trailer was lost still says what it carries.
const CosObject* fdfDictionary(cos::XrefTable& table, std::optional<cos::IndirectObject>& rootHolder,
                               std::optional<cos::IndirectObject>& fdfHolder)
{
    const CosObject* root = resolve(table, table.trailer().find("Root"), rootHolder);
    if (root == nullptr || !root->isDictionary() || root->find("FDF") == nullptr) {
        root = nullptr;
        for (const auto& [number, entry] : table.entries()) {
            (void)entry;
            auto fetched = table.fetch(number);
            if (!fetched || !fetched.value().object.isDictionary() || fetched.value().object.find("FDF") == nullptr) continue;
            rootHolder = std::move(fetched).value();
            root = &rootHolder->object;
            break;
        }
    }
    if (root == nullptr) return nullptr;
    const CosObject* fdf = resolve(table, root->find("FDF"), fdfHolder);
    return fdf != nullptr && fdf->isDictionary() ? fdf : nullptr;
}

std::optional<long long> referenceNumber(const std::optional<core::AnnotationId>& id)
{
    if (!id || id->kind != core::AnnotationId::Kind::Object) return std::nullopt;
    return id->object;
}

std::string serialized(long long number, const CosObject& object)
{
    return std::to_string(number) + " 0 obj\n" + cos::serializeObject(object) + "\nendobj\n";
}

std::string serializedStream(long long number, const CosObject& dictionary, const QByteArray& data)
{
    const CosObject withLength = AnnotationOps::withEntry(dictionary, "Length", CosObject::makeInteger(data.size()));
    return std::to_string(number) + " 0 obj\n" + cos::serializeObject(withLength) + "\nstream\n" + data.toStdString()
           + "\nendstream\nendobj\n";
}

} // namespace

core::Result<core::Fdf> FdfCodec::read(const QByteArray& bytes)
{
    if (bytes.size() > core::kMaxExchangeBytes)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.exchange.tooLarge"), QString::number(bytes.size())};
    if (!bytes.startsWith("%FDF-")) return exchangeError("error.exchange.notExchange", QString());

    auto opened = cos::openWithRepair(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    if (!opened) return exchangeError("error.exchange.malformed", QString(opened.error().messageKey));
    cos::XrefTable& table = opened.value().table;

    std::optional<cos::IndirectObject> rootHolder;
    std::optional<cos::IndirectObject> fdfHolder;
    const CosObject* fdf = fdfDictionary(table, rootHolder, fdfHolder);
    if (fdf == nullptr) return exchangeError("error.exchange.malformed", QStringLiteral("FDF"));

    core::Fdf out;
    const auto note = [&out](core::ExchangeNoteKind kind, const QString& name, int page) {
        out.notes.push_back(core::ExchangeNote{kind, name, page});
    };

    // The document the comments were made on: `/UF`, else `/F` -- a string, or a file specification.
    out.file = textOf(fdf->find("UF"));
    if (out.file.isEmpty()) {
        std::optional<cos::IndirectObject> holder;
        const CosObject* f = resolve(table, fdf->find("F"), holder);
        if (f != nullptr && f->isString()) out.file = textOf(f);
        else if (f != nullptr && f->isDictionary()) out.file = !textOf(f->find("UF")).isEmpty() ? textOf(f->find("UF")) : textOf(f->find("F"));
    }
    out.file = out.file.left(1024);
    {
        std::optional<cos::IndirectObject> holder;
        const CosObject* id = resolve(table, fdf->find("ID"), holder);
        if (id != nullptr && id->isArray() && id->items().size() == 2) {
            out.originalId = hexOf(id->items()[0]).left(64);
            out.modifiedId = hexOf(id->items()[1]).left(64);
        }
    }

    std::optional<cos::IndirectObject> annotsHolder;
    const CosObject* annots = resolve(table, fdf->find("Annots"), annotsHolder);
    if (annots == nullptr || !annots->isArray()) return out;   // an FDF of form data alone carries no comment
    if (annots->items().size() > core::kMaxExchangeAnnotations)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.exchange.tooMany"),
                           QString::number(core::kMaxExchangeAnnotations)};

    // ---- Each entry read; the objects it points at kept by number, to be named below.
    struct Entry {
        core::Annotation annotation;
        std::optional<long long> object;     // its number in the FDF; none for a direct dictionary
        std::optional<long long> irt, popup, parent;
        bool kept = true;
    };
    std::vector<Entry> entries;
    for (const CosObject& item : annots->items()) {
        Entry entry;
        std::optional<cos::IndirectObject> holder;
        const CosObject* d = &item;
        if (item.isReference()) {
            entry.object = item.reference().number;
            d = resolve(table, &item, holder);
        }
        if (d == nullptr || !d->isDictionary()) {
            note(core::ExchangeNoteKind::UnknownElement, QString(), -1);
            continue;
        }
        AnnotationReader::Parsed parsed = AnnotationReader::parseDictionary(table, *d);
        core::Annotation& a = parsed.annotation;
        if (a.subtype == core::AnnotationSubtype::Unknown || a.subtype == core::AnnotationSubtype::Link
            || a.subtype == core::AnnotationSubtype::Widget) {
            note(core::ExchangeNoteKind::UnknownElement, a.subtype == core::AnnotationSubtype::Unknown ? a.subtypeName
                                                                                                       : QString(core::annotationSubtypeName(a.subtype)),
                 -1);
            continue;
        }
        const CosObject* page = d->find("Page");
        a.page = page != nullptr && page->isInteger() && page->integerValue() >= 0 && page->integerValue() <= 8'388'606
                     ? static_cast<int>(page->integerValue())
                     : -1;
        a.hasAppearance = false;
        if (parsed.truncated) note(core::ExchangeNoteKind::GeometryTruncated, a.nm, a.page);
        if (a.subtype == core::AnnotationSubtype::FileAttachment && a.attachment) {
            core::Result<QByteArray> data = EmbeddedFileReader::dataOf(table, d->find("FS"));
            if (data && data.value().size() <= core::kMaxAttachmentBytes) {
                a.attachment->data = std::move(data).value();
                a.attachment->carried = true;
                a.attachment->size = a.attachment->data.size();
            }
        }
        // Stage 6 T14 (D6-18): a sound's samples, from its `/Sound` stream, its filters undone.
        if (a.subtype == core::AnnotationSubtype::Sound && a.sound) {
            std::optional<cos::IndirectObject> soundHolder;
            const CosObject* stream = resolve(table, d->find("Sound"), soundHolder);
            if (stream != nullptr && stream->isStream()) {
                auto decoded = cos::decodeStream(*stream, table.source());
                if (decoded && decoded.value().size() <= static_cast<std::size_t>(core::kMaxSoundBytes)) {
                    a.sound->samples = QByteArray(reinterpret_cast<const char*>(decoded.value().data()),
                                                  static_cast<qsizetype>(decoded.value().size()));
                    a.sound->carried = true;
                }
            }
        }
        for (QString* text : {&a.contents, &a.richText, &a.author, &a.subject}) {
            if (text->size() > core::kMaxExchangeTextLength) {
                text->truncate(core::kMaxExchangeTextLength);
                note(core::ExchangeNoteKind::TextTooLong, a.nm, a.page);
            }
        }
        entry.irt = referenceNumber(a.inReplyTo);
        entry.popup = referenceNumber(a.popup);
        entry.parent = referenceNumber(a.parent);
        a.inReplyTo.reset();
        a.popup.reset();
        a.parent.reset();
        entry.annotation = std::move(a);
        entries.push_back(std::move(entry));
    }

    // ---- Names: every annotation but a pop-up its own (`core/doc/Fdf.h`), once.
    std::map<long long, QString> nameOfObject;
    std::set<QString> names;
    for (Entry& e : entries) {
        core::Annotation& a = e.annotation;
        if (a.subtype == core::AnnotationSubtype::Popup) continue;
        if (a.nm.isEmpty()) a.nm = core::contentNameOf(a);
        if (a.nm.size() > core::kMaxExchangeNameLength) {
            note(core::ExchangeNoteKind::NameTooLong, a.nm.left(64), a.page);
            e.kept = false;
        } else if (!names.insert(a.nm).second) {
            note(core::ExchangeNoteKind::DuplicateName, a.nm, a.page);
            e.kept = false;
        } else {
            a.id = core::AnnotationId::ofCreated(a.nm);
            if (e.object) nameOfObject.emplace(*e.object, a.nm);
        }
    }
    // A pop-up's parent: its own `/Parent`, else the annotation whose `/Popup` names it.
    std::map<long long, QString> parentOfPopup;
    for (const Entry& e : entries) {
        if (e.kept && e.popup && e.annotation.subtype != core::AnnotationSubtype::Popup)
            parentOfPopup.emplace(*e.popup, e.annotation.nm);
    }
    std::set<QString> popupsOf;   // the parents whose pop-up is kept
    for (Entry& e : entries) {
        core::Annotation& a = e.annotation;
        if (a.subtype != core::AnnotationSubtype::Popup) continue;
        QString parent;
        if (e.parent) {
            if (const auto found = nameOfObject.find(*e.parent); found != nameOfObject.end()) parent = found->second;
        }
        if (parent.isEmpty() && e.object) {
            if (const auto found = parentOfPopup.find(*e.object); found != parentOfPopup.end()) parent = found->second;
        }
        // No parent, a parent with a pop-up already, or a name a comment of the file already has: the pop-up goes,
        // rather than two annotations answering to one name.
        if (parent.isEmpty() || popupsOf.count(parent) != 0 || names.count(core::popupNameFor(parent)) != 0) {
            note(core::ExchangeNoteKind::PopupWithoutParent, QString(), a.page);
            e.kept = false;
            continue;
        }
        popupsOf.insert(parent);
        a.nm = core::popupNameFor(parent);
        a.id = core::AnnotationId::ofCreated(a.nm);
        a.parent = core::AnnotationId::ofCreated(parent);
    }
    // ---- References, by name.
    for (Entry& e : entries) {
        if (!e.kept) continue;
        core::Annotation& a = e.annotation;
        if (a.subtype == core::AnnotationSubtype::Popup) continue;
        if (e.irt) {
            if (const auto found = nameOfObject.find(*e.irt); found != nameOfObject.end())
                a.inReplyTo = core::AnnotationId::ofCreated(found->second);
            else
                note(core::ExchangeNoteKind::ReferenceDropped, a.nm, a.page);
        }
        if (popupsOf.count(a.nm) != 0) a.popup = core::AnnotationId::ofCreated(core::popupNameFor(a.nm));
    }
    for (Entry& e : entries) {
        if (!e.kept) continue;
        if (e.annotation.subtype == core::AnnotationSubtype::Popup) {
            // A pop-up's page is its parent's -- what it is drawn beside.
            for (const Entry& p : entries) {
                if (p.kept && p.annotation.id == *e.annotation.parent) e.annotation.page = p.annotation.page;
            }
        }
        out.annotations.push_back(std::move(e.annotation));
    }
    return out;
}

std::string FdfCodec::write(const core::Fdf& exchange)
{
    // The catalog is object 1, each annotation the next, each file it carries after them.
    std::map<QString, long long> numberOf;
    long long next = 2;
    for (const core::Annotation& a : exchange.annotations) {
        if (a.id.kind == core::AnnotationId::Kind::Created) numberOf.emplace(a.id.nm, next);
        ++next;
    }
    const ReferenceResolver resolver = [&numberOf](const core::AnnotationId& id) -> std::optional<cos::CosRef> {
        if (id.kind != core::AnnotationId::Kind::Created) return std::nullopt;
        const auto found = numberOf.find(id.nm);
        if (found == numberOf.end()) return std::nullopt;
        return cos::CosRef{found->second, 0};
    };

    std::string objects;
    std::vector<CosObject> listed;
    long long number = 2;
    for (const core::Annotation& a : exchange.annotations) {
        const long long at = number++;
        core::Result<CosObject> dictionary = AnnotationOps::exchangeDictionary(a, resolver);
        if (!dictionary) continue;
        CosObject d = std::move(dictionary).value();
        if (a.attachment) {
            const core::EmbeddedFile& file = *a.attachment;
            std::vector<CosObject::Entry> spec{
                {"Type", CosObject::makeName("Filespec")},
                {"F", AnnotationOps::textString(file.name)},
                {"UF", AnnotationOps::textString(file.name)},
            };
            if (!file.description.isEmpty()) spec.push_back({"Desc", AnnotationOps::textString(file.description)});
            if (file.carried) {
                const long long stream = next++;
                std::vector<CosObject::Entry> params;
                params.push_back({"Size", CosObject::makeInteger(file.data.size())});
                if (!file.created.raw.isEmpty()) params.push_back({"CreationDate", AnnotationOps::textString(file.created.raw)});
                if (!file.modified.raw.isEmpty()) params.push_back({"ModDate", AnnotationOps::textString(file.modified.raw)});
                if (!file.checksum.isEmpty())
                    params.push_back({"CheckSum", CosObject::makeString(file.checksum.toStdString(), cos::StringForm::Hex)});
                std::vector<CosObject::Entry> streamEntries{{"Type", CosObject::makeName("EmbeddedFile")}};
                if (!file.mime.isEmpty()) streamEntries.push_back({"Subtype", CosObject::makeName(file.mime.toStdString())});
                streamEntries.push_back({"Params", CosObject::makeDictionary(std::move(params))});
                objects += serializedStream(stream, CosObject::makeDictionary(std::move(streamEntries)), file.data);
                spec.push_back({"EF", CosObject::makeDictionary({{"F", CosObject::makeReference(stream, 0)}})});
            }
            d = AnnotationOps::withEntry(d, "FS", CosObject::makeDictionary(std::move(spec)));
        }
        // Stage 6 T14: a sound's samples as its `/Sound` stream, unfiltered as an attached file's are here.
        if (a.sound && a.sound->carried) {
            const long long stream = next++;
            objects += serializedStream(stream, CosObject::makeDictionary({
                {"Type", CosObject::makeName("Sound")},
                {"R", CosObject::makeInteger(a.sound->rate)},
                {"C", CosObject::makeInteger(a.sound->channels)},
                {"B", CosObject::makeInteger(a.sound->bits)},
                {"E", CosObject::makeName(QString(core::soundEncodingName(a.sound->encoding)).toStdString())},
            }), a.sound->samples);
            d = AnnotationOps::withEntry(d, "Sound", CosObject::makeReference(stream, 0));
        }
        objects += serialized(at, d);
        listed.push_back(CosObject::makeReference(at, 0));
    }

    std::vector<CosObject::Entry> fdf{{"Annots", CosObject::makeArray(std::move(listed))}};
    if (!exchange.file.isEmpty()) fdf.push_back({"F", AnnotationOps::textString(exchange.file)});
    if (!exchange.originalId.isEmpty() || !exchange.modifiedId.isEmpty()) {
        const auto idString = [](const QString& hex) {
            return CosObject::makeString(QByteArray::fromHex(hex.toLatin1()).toStdString(), cos::StringForm::Hex);
        };
        fdf.push_back({"ID", CosObject::makeArray({idString(exchange.originalId), idString(exchange.modifiedId)})});
    }
    const CosObject catalog = CosObject::makeDictionary(
        {{"FDF", CosObject::makeDictionary(std::move(fdf))}, {"Type", CosObject::makeName("Catalog")}});

    std::string out = "%FDF-1.2\n%\xE2\xE3\xCF\xD3\n";
    out += serialized(1, catalog);
    out += objects;
    out += "trailer\n<< /Root 1 0 R >>\n%%EOF\n";
    return out;
}

core::Result<core::Fdf> FdfCodec::readEither(const QByteArray& bytes)
{
    qsizetype start = bytes.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    while (start < bytes.size() && (bytes[start] == ' ' || bytes[start] == '\t' || bytes[start] == '\r' || bytes[start] == '\n'))
        ++start;
    if (bytes.mid(start, 5) == "%FDF-") return read(bytes.mid(start));
    if (start < bytes.size() && bytes[start] == '<') return core::XfdfCodec::read(bytes);
    return exchangeError("error.exchange.notExchange", QString());
}

core::Result<core::Fdf> FdfCodec::readFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.exists() && info.size() > core::kMaxExchangeBytes)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.exchange.tooLarge"), info.fileName()};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return core::Error{core::ErrorCode::IoFailure, QLatin1String("error.exchange.unreadable"), info.fileName()};
    const QByteArray bytes = file.read(core::kMaxExchangeBytes + 1);
    if (bytes.size() > core::kMaxExchangeBytes)
        return core::Error{core::ErrorCode::Unsupported, QLatin1String("error.exchange.tooLarge"), info.fileName()};
    return readEither(bytes);
}

core::Status FdfCodec::carryFiles(core::Fdf& exchange,
                                  const std::function<core::Result<QByteArray>(const core::AnnotationId&)>& read)
{
    for (std::size_t i = 0; i < exchange.annotations.size(); ++i) {
        core::Annotation& a = exchange.annotations[i];
        if (a.subtype != core::AnnotationSubtype::FileAttachment || !a.attachment || a.attachment->carried) continue;
        if (i >= exchange.sources.size())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.attachment.noData"), a.attachment->name};
        core::Result<QByteArray> bytes = read(exchange.sources[i]);
        if (!bytes) return bytes.error();
        a.attachment->data = std::move(bytes).value();
        a.attachment->carried = true;
        a.attachment->size = a.attachment->data.size();
    }
    return core::Status{};
}

core::Status FdfCodec::carrySounds(core::Fdf& exchange,
                                   const std::function<core::Result<QByteArray>(const core::AnnotationId&)>& read)
{
    for (std::size_t i = 0; i < exchange.annotations.size(); ++i) {
        core::Annotation& a = exchange.annotations[i];
        if (a.subtype != core::AnnotationSubtype::Sound || !a.sound || a.sound->carried) continue;
        if (i >= exchange.sources.size())
            return core::Error{core::ErrorCode::NotFound, QLatin1String("error.sound.noData"), a.nm};
        core::Result<QByteArray> samples = read(exchange.sources[i]);
        if (!samples) return samples.error();
        a.sound->samples = std::move(samples).value();
        a.sound->carried = true;
    }
    return core::Status{};
}

core::Status FdfCodec::writeFile(const core::Fdf& exchange, core::ExchangeFormat format, const QString& path, bool overwrite)
{
    const QByteArray bytes = format == core::ExchangeFormat::Xfdf ? core::XfdfCodec::write(exchange)
                                                                  : QByteArray::fromStdString(write(exchange));
    core::Status written = AttachmentFiles::write(bytes, path, overwrite);
    if (written) return written;
    // The same write as an extract's, said as an export's.
    const QString key = QString(written.error().messageKey);
    if (key == QLatin1String("error.attachment.exists"))
        return core::Error{written.error().code, QLatin1String("error.exchange.exists"), written.error().context};
    if (key == QLatin1String("error.attachment.writeFailed"))
        return core::Error{written.error().code, QLatin1String("error.exchange.writeFailed"), written.error().context};
    return written;
}

} // namespace rawpage::pdf::ops
