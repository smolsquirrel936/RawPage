// pdf/cos/CosObject -- implementation. Stage 2 T6. See the header for why this type exists.

#include "pdf/cos/CosObject.h"

#include <utility>

namespace rawpage::pdf::cos {

namespace {

// The object `get()` returns for an absent key. Function-local rather than a namespace-scope
// object so its initialization order cannot matter, and `const` so nothing can accidentally give
// every missing lookup in the process a value.
const CosObject& nullObject() noexcept
{
    static const CosObject instance;
    return instance;
}

} // namespace

CosObject CosObject::makeNull()
{
    return CosObject{};
}

CosObject CosObject::makeBoolean(bool value)
{
    CosObject object;
    object.type_ = CosType::Boolean;
    object.boolean_ = value;
    return object;
}

CosObject CosObject::makeInteger(long long value)
{
    CosObject object;
    object.type_ = CosType::Integer;
    object.integer_ = value;
    // `real_` is set for integers too, so `numberValue()` answers for both without the caller
    // switching on the type first. `pdf/cos/Lexer`'s Token does the same thing one layer down and
    // for the same reason.
    object.real_ = static_cast<double>(value);
    return object;
}

CosObject CosObject::makeReal(double value)
{
    CosObject object;
    object.type_ = CosType::Real;
    object.real_ = value;
    return object;
}

CosObject CosObject::makeString(std::string bytes, StringForm form)
{
    CosObject object;
    object.type_ = CosType::String;
    object.stringForm_ = form;
    object.text_ = std::move(bytes);
    return object;
}

CosObject CosObject::makeName(std::string name)
{
    CosObject object;
    object.type_ = CosType::Name;
    object.text_ = std::move(name);
    return object;
}

CosObject CosObject::makeArray(std::vector<CosObject> items)
{
    CosObject object;
    object.type_ = CosType::Array;
    object.array_ = std::move(items);
    return object;
}

CosObject CosObject::makeDictionary(std::vector<Entry> entries)
{
    CosObject object;
    object.type_ = CosType::Dictionary;
    object.dict_ = std::move(entries);
    return object;
}

CosObject CosObject::makeStream(std::vector<Entry> entries, CosStreamData data)
{
    CosObject object;
    object.type_ = CosType::Stream;
    object.dict_ = std::move(entries);
    object.stream_ = data;
    return object;
}

CosObject CosObject::makeStream(CosObject dictionary, CosStreamData data)
{
    CosObject object;
    object.type_ = CosType::Stream;
    object.dict_ = std::move(dictionary.dict_);
    object.stream_ = data;
    // The dictionary's span deliberately does NOT come along. It covers `<< ... >>`, and a stream
    // is that plus its data plus `endstream` -- the caller sets the real one.
    return object;
}

CosObject CosObject::makeReference(long long number, long long generation)
{
    CosObject object;
    object.type_ = CosType::Reference;
    object.ref_ = CosRef{number, generation};
    return object;
}

const CosObject* CosObject::find(std::string_view key) const noexcept
{
    // Backwards, because a duplicated key resolves to the LAST occurrence -- the header says why,
    // and searching from the end is how that is expressed rather than a scan plus a comparison.
    for (auto entry = dict_.rbegin(); entry != dict_.rend(); ++entry) {
        if (entry->key == key)
            return &entry->value;
    }
    return nullptr;
}

const CosObject& CosObject::get(std::string_view key) const noexcept
{
    const CosObject* found = find(key);
    return found ? *found : nullObject();
}

void CosObject::append(CosObject item)
{
    array_.push_back(std::move(item));
    clearSpan();
}

void CosObject::insert(std::string key, CosObject value)
{
    dict_.push_back(Entry{std::move(key), std::move(value)});
    clearSpan();
}

void CosObject::setStreamData(const CosStreamData& data)
{
    stream_ = data;
    clearSpan();
}

std::string_view CosObject::raw(std::string_view source) const noexcept
{
    if (!span_.valid)
        return {};
    // Three separate guards rather than one sum: `offset + length` can overflow, and the
    // overflowed result compares happily against `source.size()`. This function is called by the
    // fuzz body on spans produced from hostile input, so the arithmetic is the subject.
    if (span_.offset > source.size() || span_.length > source.size())
        return {};
    if (span_.length > source.size() - span_.offset)
        return {};
    return source.substr(span_.offset, span_.length);
}

std::string_view CosObject::rawStreamData(std::string_view source) const noexcept
{
    if (type_ != CosType::Stream)
        return {};
    if (stream_.dataOffset > source.size() || stream_.dataLength > source.size())
        return {};
    if (stream_.dataLength > source.size() - stream_.dataOffset)
        return {};
    return source.substr(stream_.dataOffset, stream_.dataLength);
}

bool operator==(const CosObject& a, const CosObject& b) noexcept
{
    if (a.type_ != b.type_)
        return false;

    switch (a.type_) {
    case CosType::Null:
        return true;
    case CosType::Boolean:
        return a.boolean_ == b.boolean_;
    case CosType::Integer:
        return a.integer_ == b.integer_;
    case CosType::Real:
        return a.real_ == b.real_;
    case CosType::String:
        // The FORM is part of the value here, and that is a deliberate choice rather than an
        // oversight: `(A)` and `<41>` carry the same bytes, and a rewriter that swapped one for
        // the other would change the file. A caller that means "the same bytes regardless of
        // spelling" compares `text()`.
        return a.stringForm_ == b.stringForm_ && a.text_ == b.text_;
    case CosType::Name:
        return a.text_ == b.text_;
    case CosType::Array:
        return a.array_ == b.array_;
    case CosType::Dictionary:
    case CosType::Stream: {
        if (a.dict_.size() != b.dict_.size())
            return false;
        for (std::size_t index = 0; index < a.dict_.size(); ++index) {
            if (a.dict_[index].key != b.dict_[index].key)
                return false;
            if (!(a.dict_[index].value == b.dict_[index].value))
                return false;
        }
        if (a.type_ == CosType::Stream) {
            // The stream's IDENTITY is its bytes, and two streams parsed from two buffers have no
            // shared offset space -- so comparing `dataOffset` would answer a question nobody
            // asked. The length is comparable and the offsets are not; a caller that wants the
            // content compared says so with `rawStreamData()` and its own buffers.
            return a.stream_.dataLength == b.stream_.dataLength;
        }
        return true;
    }
    case CosType::Reference:
        return a.ref_ == b.ref_;
    }
    return false;
}

// No `default:` label, deliberately. Every enumerator is listed above, so adding one to `CosType`
// without handling it here is a -Wswitch error under the -Werror this layer builds with -- which
// is the same mechanism `ui/shell/ThemeManager` uses to keep its colour tokens complete.
const char* cosTypeName(CosType type) noexcept
{
    switch (type) {
    case CosType::Null:       return "Null";
    case CosType::Boolean:    return "Boolean";
    case CosType::Integer:    return "Integer";
    case CosType::Real:       return "Real";
    case CosType::String:     return "String";
    case CosType::Name:       return "Name";
    case CosType::Array:      return "Array";
    case CosType::Dictionary: return "Dictionary";
    case CosType::Stream:     return "Stream";
    case CosType::Reference:  return "Reference";
    }
    return "?";
}

} // namespace rawpage::pdf::cos
