// pdf/ops/DestinationReader -- the document's named destinations, read first-party. Stage 7 T12, decisions D7-10
// and D7-35 (`Stage7-Assembly-Plan.md` §3).
//
// -------------------------------------------------------------------------------------------
// WHY A SECOND READER, AND WHAT KEEPS IT HONEST
// -------------------------------------------------------------------------------------------
// PDFium reads the named destinations for the panes (Stage 4's D4-3) -- names and pages, and nothing about
// which table a name is in or what its value was written as. Editing needs both: an entry stays in its table
// (D7-35) and a pair not changed is written as it was (D7-10). So this reads the same two tables from the COS
// layer -- the `/Names /Dests` name tree, every leaf in order, and the catalog's `/Dests` dictionary -- and the
// backend holds the two readings to agree, name by name and page by page (`agreesWith`), or the table is
// read-only and says why (D6-3's rule), exactly as `OutlineReader` is held to the outline.
//
// It reads what PDFium reads, the way PDFium reads it: a tree key is a STRING, decoded as a text string; a
// dictionary key a NAME; a value an explicit array, a dictionary's `/D`, or a reference to either; the page
// a reference to a page object, or an integer taken as that index.
//
// BOUNDED: a visited set over the tree's nodes and `core::kMaxNamedDestinations` entries. A walk cut short
// says so (`truncated`), and a table read short is read-only -- an edit of a table read in part would write the
// part it did not read out of the document.

#ifndef RAWPAGE_PDF_OPS_DESTINATIONREADER_H
#define RAWPAGE_PDF_OPS_DESTINATIONREADER_H

#include "core/doc/Destinations.h"
#include "pdf/cos/CosObject.h"
#include "pdf/ops/ReferenceRepair.h"

#include <map>
#include <string>
#include <vector>

namespace rawpage::pdf::ops {

class DestinationReader {
public:
    struct Entry {
        core::NamedDestination::Form form = core::NamedDestination::Form::NameTree;
        std::string key;              // the name as written: a tree key's string bytes, a dictionary key's name bytes
        cos::CosObject keyObject;     // a tree key as written -- literal or hex -- kept when its pair is
        cos::CosObject value;         // as written: an array, a dictionary with /D, or a reference to either
        long long page = -1;          // the page object its array names; -1 for none
        int index = -1;               // an integer page (a producer's error PDFium reads as an index); -1 for none
        core::Destination view;       // the fit and its parameters, the page left unresolved
    };

    struct Reading {
        std::vector<Entry> entries;   // the tree's pairs in tree order, then the dictionary's in its order
        bool truncated = false;       // a cycle, a bound, or a node that does not fetch: the table is read-only
    };

    // Both tables, read through `view` (a ledger put back over the disk).
    static Reading read(const ReferenceRepair::View& view);

    // The name a person sees: a tree key decoded as a text string, a dictionary key's bytes as Latin-1 -- PDFium's.
    static QString nameOf(const Entry& entry);

    // The model: every entry's page placed by `indexOf` -- an integer page as itself, a page object not there as -1.
    static core::Destinations model(const Reading& reading, const std::map<long long, int>& indexOf);

    // THE AGREEMENT (D7-10): `engine`, PDFium's reading, against `resolved` -- this reading placed by the same tree:
    // the same number of entries, every name once, each to the same page. Success, or
    // `error.destination.readingsDisagree` naming the first name that differs and how.
    static core::Status agreesWith(const core::Destinations& engine, const core::Destinations& resolved);
};

} // namespace rawpage::pdf::ops

#endif // RAWPAGE_PDF_OPS_DESTINATIONREADER_H
