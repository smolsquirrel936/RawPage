// pdf/cos/ObjectAllocator -- object numbers for objects a writer adds. Stage 3 T2.
//
// SCOPE. Hands out the number for every object an update ADDS -- today that is only a
// cross-reference stream's own number; from Stage 6 an annotation, from Stage 7 a page. Replacing
// an existing object keeps that object's number and never comes here.
//
// -------------------------------------------------------------------------------------------
// IT NEVER REUSES A NUMBER, AND THAT IS DECISION D3-5
// -------------------------------------------------------------------------------------------
// ISO 32000-1 7.5.4 lets a writer take an entry off the free list and reuse its number with the
// generation incremented. This allocator does not, on the conservative reading
// `Stage3-Mutation-Plan.md` D3-5 records: a reader that caches objects by NUMBER alone -- and some do
// -- serves the deleted object for the new one, silently, while a fresh number costs one xref
// entry. So the first number handed out is at or above BOTH of:
//
//   * the newest `/Size`, which is the file's CLAIM about the next free number; and
//   * one past the largest object number actually present, because `/Size` is a claim and
//     `corpus/malformed/size-too-small.pdf` lies about it. Trusting `/Size` there would hand out a
//     number the file already uses, and the update would shadow a live object.
//
// Free-list entries below that point are therefore never touched, which guide case A-23 asserts
// against a file that has them.
//
// WHAT IT DOES NOT DO. It does not renumber, compact or maintain the free list for writing -- those
// are a full rewrite's concerns (`pdf/cos/FullWriter`, Stage 8), and an incremental update has no
// free list to maintain because it never frees anything.

#ifndef RAWPAGE_PDF_COS_OBJECTALLOCATOR_H
#define RAWPAGE_PDF_COS_OBJECTALLOCATOR_H

namespace rawpage::pdf::cos {

class XrefTable;

class ObjectAllocator {
public:
    // Starts at max(newest /Size, largest object number + 1, 1). Object 0 is the free-list head in
    // every PDF and is never allocated.
    static ObjectAllocator forTable(const XrefTable& table) noexcept;

    explicit ObjectAllocator(long long firstNumber) noexcept;

    // The next number, and it is consumed. Strictly increasing within one allocator.
    long long allocate() noexcept;

    // The first number NOT yet handed out -- which is the `/Size` an update must declare at least,
    // because `/Size` is one more than the highest object number in the file.
    long long nextNumber() const noexcept { return next_; }

    // The first number this allocator could ever hand out. A number at or above it and below
    // `nextNumber()` is one this allocator gave out; below it, the number belongs to the file.
    long long firstNumber() const noexcept { return first_; }

private:
    long long first_ = 1;
    long long next_ = 1;
};

} // namespace rawpage::pdf::cos

#endif // RAWPAGE_PDF_COS_OBJECTALLOCATOR_H
