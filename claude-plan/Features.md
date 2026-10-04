# RawPage — Feature Specification (Capability Catalog)

**RawPage** is a desktop PDF editor for Windows whose purpose is to **view, edit, review, assemble,
fill, secure and sign PDF documents in one application**, with a signature *dual-mode editing
model*: a fixed-layout **Review mode** for object-level work on the PDF as-authored, and a
reflowable **Write mode** that turns a PDF into a word-processor document and back. The platform
posture is **Windows-first with a portable core** — the OS-specific surface is enumerated in §10 so
it can sit behind a thin abstraction from day one. The product is modeled on **Right PDF
Professional (Windows)**, whose published user manual is the primary requirements source.

> ## Reference catalog only
>
> This document is a **reference catalog**. It enumerates the *full* capability surface a product
> in this category has. **Neither the technology stack nor the v1 scope is decided here** — no
> language, framework, PDF engine, or release cut appears below. Choosing the stack is the job of
> `Tech-Stack-Plan.md`; deciding what ships when is the job of `Implementation-Plan.md`. If you
> find yourself wanting to write "we'll use library X" or "v1 drops Y", it belongs in one of those
> two documents, not this one.

> ## Removed from scope — accessibility (2026-08-23)
>
> **On the user's instruction, accessibility is out of the product entirely.** This catalog no longer
> enumerates it, and no stage builds it. This block is the single record of that decision; every other
> document cross-references here rather than restating it.
>
> **Removed — the product's own interface:** accessible names, roles and values for screen readers;
> screen-reader reading order; high-contrast themes and deference to the OS high-contrast setting
> (Stage 1 decision **D1-15**, withdrawn); the "reduce motion" setting; Stage 14's UI accessibility
> pass and its automation-name audit test; the **A3** acceptance criterion **R-8** and its manual case
> **M-08**; Stage 1 precondition **P-2** (Narrator).
>
> **Removed — document accessibility as a feature:** §1.6 Read Aloud with `platform::ISpeech`,
> `WinSpeech` and `ReadAloudBar` (taking the platform interface count down by one — **corrected at
> Stage 1 T7**: that was a count of §4.3's table *rows*, and the interface count is **seventeen**,
> because `ICertificateStore` and `ICryptoProvider` share a row and `IPrinting` had never been
> written down at all. `Tech-Stack-Plan.md` §4.3 owns the figure);
> the accessibility checker and its report dialog; alternate text, table-header scope and artifact
> marking; **PDF/UA** (ISO 14289) as a conformance target. PDF/A and PDF/X are unaffected.
>
> **Deliberately KEPT, and not to be removed as collateral:**
> - **Full keyboard operability.** Every command reachable without a mouse is core architecture —
>   `ActionRegistry`, `ShortcutBinder` and the §8 tables exist for it — and a power-user feature, not
>   an accessibility one. Visible keyboard focus stays with it.
> - **The tag tree and document structure** (`core/doc/TagTree`, `core/text/LayoutAnalyzer`,
>   §5.5 below). It is the durable home of Write mode's structure under decision **O2**: the composer
>   *writes* it and the converter reads it back. Removing it would gut Stage 13 for reasons that have
>   nothing to do with accessibility. The a11y framing is gone; the structure is not.
> - **Reflow view** and **invert/substitute page colors**, which are reading conveniences under §1.2's
>   non-destructive view transforms rather than parts of the accessibility surface.
>
> **What this costs, recorded once so it is not rediscovered as a surprise:** the EU Accessibility Act
> has applied since June 2025, and EN 301 549 / Section 508 gate most public-sector and large-
> enterprise procurement. This decision closes those channels and drops a feature the reference
> product ships. It was taken knowingly.

### Sources

| # | Source | Used for |
|---|---|---|
| S1 | [Right PDF Professional (Windows) User Manual — About this User Manual](https://www.rightpdf.com/zh-TW/helpcontent/Right-PDF-Pro/20933-About-this-User-Manual) — full 12-chapter table of contents | The top-level functional decomposition of §1–§7 and the §9 infrastructure surface |
| S2 | [Editing PDFs — Write mode vs Review mode](https://www.rightpdf.com/en/helpcontent/Right-PDF-Pro/10368-Editing-PDFs) | §3's dual-mode model, the *Convert to Writable* / *Prevent Further Edits* lifecycle, and the per-mode capability split |
| S3 | [Page through a document](https://www.rightpdf.com/en/helpcontent/Right-PDF-Pro/10127-Page-through-a-document) | §1 page-control surface (first/prev/next/last, page-number box, arrow-key paging) |
| S4 | [Right PDF Pro (Windows) User Manual, full PDF, rev. 2025-05-05](https://cms.rightpdf.com/Backend/Export/EN/Right_PDF_Pro_(Windows)_UserManual_en.pdf) | Cross-check of chapter contents |
| S5 | ISO 32000-1 / 32000-2 (PDF 1.7 / PDF 2.0), ISO 19005 (PDF/A), ISO 15930 (PDF/X) | §9 format-support list and conformance targets. *(ISO 14289 (PDF/UA) was dropped 2026-08-23 with the accessibility removal.)* |
| S6 | Acrobat-family and word-processor keyboard conventions | §8 key-binding defaults — **see the §8 provenance note** |

**Honesty note on §8.** The reference manual (S1–S4) does *not* publish a consolidated keyboard
shortcut table; it documents shortcuts only incidentally (S3 confirms arrow-key paging and the
page-number box). The §8 tables are therefore **specified for this product**, following
Acrobat-family conventions in reading/review contexts and word-processor conventions in Write mode.
They are authoritative *for RawPage* — every downstream document treats §8 as the single source of
truth for chords — but they are **not** claimed to reproduce Right PDF's defaults. Where a chord is
inherited from a well-known convention it is marked *(Acrobat)* or *(word-processor)*.

> ### Legend
>
> - ⭐ = **signature feature** — something that defines what RawPage *is*, not incidental surface.
> - *(Acrobat)* / *(word-processor)* = the convention a default key binding is inherited from.
> - "destructive" on a page operation = it modifies the saved document, as opposed to a view-only change.

RawPage has **seven major functionalities**:

1. **View & Find** ⭐ — the workspace, page display, navigation, comparison, and search/indexing.
2. **Create & Export** — every path into and out of PDF.
3. **Edit** ⭐ — dual-mode content editing (Review mode and Write mode) plus rich media.
4. **Review & Annotate** ⭐ — comments, markup, measurement, and collaborative review.
5. **Assemble** — page, bookmark, structure, and document-level composition, including batch.
6. **Forms** — interactive form authoring, filling, and data exchange.
7. **Secure & Sign** ⭐ — encryption, permissions, redaction, digital IDs, and signatures.

Then: **§8 Key Bindings** (authoritative), **§9 System & Infrastructure**, **§10 Portability Note**.

---

## 1. View & Find ⭐

### 1.1 Workspace

**Ribbon and command surface**
- Ribbon organized into tabs: File, Home, Comment, Edit, Pages, Forms, Protect, View, Help.
- Contextual ribbon tabs appear for the current selection (Table Tools, Picture Tools, Field Tools).
- Show/hide individual ribbon tabs.
- Create custom ribbon groups and place existing commands into them.
- Collapse the ribbon to a single row of tab labels.
- Quick Access Toolbar: add a command from any ribbon control.
- Quick Access Toolbar: remove a command.
- Quick Access Toolbar: reorder commands — **by dragging a button to its new place**, and as two
  commands (`ui.qatMoveLeft` / `ui.qatMoveRight`) so the batch runner and a chord can reach the
  same operation a pointer can.
- Quick Access Toolbar: **lock it**, so a drag cannot reorder it by accident. Unlocked by default.
- Quick Access Toolbar: position above or below the ribbon.
- Reset the Quick Access Toolbar and ribbon to factory defaults.
- Export the UI customization (ribbon + QAT + shortcuts) to a file.
- Import a UI customization file, for deploying a house layout to a team.
- Tool finder: a search box that locates any command by name and runs it.
- Show ribbon key tips (accelerator letters overlaid on the ribbon).

**Panes and layout**
- Dockable sidebar hosting: Page Thumbnails, Bookmarks, Layers, Tags, Attachments, Comments, Signatures, Destinations, Form Fields, Content, Document Map, Search.
- Dock a pane left or right; float it as a separate window; auto-hide it to a tab strip.
- Resize the sidebar; remember its width per pane.
- Remember which panes were open, globally and per document.
- Status bar with the page-number box, page controls, zoom controls, and view-mode buttons.
- Light theme, dark theme, and follow-system theme.
- Touch mode that enlarges hit targets for pen and finger input.
- Document message bar that reports document-level conditions (this document contains a form; this document is signed; this document is protected; this document is in Write mode).

**Document windows**
- Tabbed document interface: many PDFs open in one window.
- Drag a document tab out of the window to detach it into its own window.
- Drag a detached window's tab back to re-dock it.
- Open a second view of the *same* document in a new window.
- Synchronize scrolling between two views of the same document.
- Tile open documents horizontally.
- Tile open documents vertically.
- Cascade open documents.
- Merge all detached windows back into one tabbed window.
- Switch documents from a window list.
- Restore the previous session's open documents and window layout on launch.

### 1.2 Page display

**Layout**
- Single Page.
- Single Page Continuous (vertical scroll).
- Two-Up (facing pages).
- Two-Up Continuous.
- **In the paged modes, scrolling past a page's edge turns the page** — by wheel as well as by
  arrow key (§8), landing at the near edge of the new one. A paged mode shows one page at a time;
  it does not trap the reader on it.
- **In the two-up modes, a page step is a SPREAD step.** Next/previous page, and the edge-of-page
  turn above, move to the next spread rather than to the page beside the current one — which is
  already on screen, so stepping onto it would show the reader nothing.
- Show cover page separately in Two-Up modes.
- Right-to-left binding for RTL documents.
- Horizontal-scroll layout for wide documents.

**Zoom**
- Zoom in / zoom out by preset steps.
- Set an arbitrary zoom percentage.
- Marquee zoom: drag a rectangle to zoom to it.
- Dynamic (loupe) zoom: drag to zoom continuously.
- Loupe tool: a magnifier window over a small area — re-rasterized sharp rather than an enlarged
  frame, and its magnification stepped from 1.5× to 8× with `Alt`+wheel (S4 decisions D4-20 and
  D4-22, `Stage4-ViewingSurface-Plan.md` §3).
- Pan & Zoom window: a thumbnail with a draggable viewport rectangle.
- Fit Page (fit the page **as a reader is shown it** — the CropBox — inside the window; not the
  sheet outside it, which is [`Stage4-ViewingSurface-Testing-Guide.md` §8](./Stage4-ViewingSurface-Testing-Guide.md)'s **I-10**).
- Fit Width.
- Fit Height.
- Fit Visible (fit the content bounding box, ignoring white margins).
- Actual Size (100% at the monitor's physical DPI).
- Zoom to the current selection.
- Reflow view: re-wrap the text of a tagged document to the window width.

**View transforms (non-destructive)**
- Rotate the view clockwise / counter-clockwise, without modifying the document.
- Reset the view rotation.
- Invert / substitute page colors for high-contrast reading.
- Set a custom page background color.

**Scroll and pan**
- Continuous scrolling with the wheel, scrollbar, or keyboard.
- Page-at-a-time paging that snaps to page boundaries.
- Hand tool panning by dragging the page.
- Temporary hand tool while a modifier key is held, without leaving the current tool.
- Auto-scroll at an adjustable speed, with pause and reverse.
- Choose whether the wheel scrolls or zooms, and which modifier swaps the two.

**Split views**
- Split the window horizontally into two synchronized panes of one document.
- Split the window vertically.
- Spreadsheet split: freeze a top row and a left column while the rest scrolls.
- Remove the split.

**Rendering fidelity controls**
- Line Weight display: draw hairlines at their true width instead of a minimum of one pixel.
- Toggle anti-aliasing separately for text, images, and line art.
- Show a transparency grid behind transparent content.
- Render text-only placeholders instead of large images, for slow documents.
- Choose a rendering resolution cap, trading fidelity for responsiveness.

**Reading and presentation**
- Reading mode: hide the ribbon and panes, keep the window chrome.
- Full Screen mode: one page filling the display, no chrome.
- Presentation transitions between pages (fade, wipe, dissolve, none) with a per-page override.
- Auto-advance pages on a timer, with loop-after-last-page.
- Blank the screen to black or white during a presentation.
- On-screen pointer / laser-pointer cursor in Full Screen mode.
- Choose which monitor a **presentation** occupies on a multi-monitor desktop, and move it to the next
  monitor while presenting. `F11` full screen stays on the monitor the window is on — decided by the
  user on 2026-09-26, after Stage 4's M-02 walk found this line read both ways.

### 1.3 Rulers, grid, and guides

- Show/hide horizontal and vertical rulers.
- Choose the measurement unit: points, inches, millimetres, centimetres, picas.
- Step to the next unit by pressing the corner box where the two rulers meet, which names the
  current one — in the order above, wrapping from picas to points, for every open document. *(Added on the
  user's request 2026-10-03, as the command `view.cycleRulerUnit`, which has no chord;
  `Stage5-TextSearch-Testing-Guide.md` A-115.)*
- Show/hide a layout grid.
- Configure grid spacing, subdivisions, colour, and origin offset.
- Snap dragged objects to the grid.
- Drag a guide out of a ruler onto the page — shown **dotted** where it will land, snapped, until
  the button is released. *(Added on the user's request 2026-09-26; `Stage4-ViewingSurface-Testing-Guide.md` A-89.)*
- Move, lock, and delete individual guides; clear all guides.
- Snap dragged objects to guides.
- Snap dragged objects to the edges and centres of other objects.
- Read out the cursor's page coordinates live **on the rulers**: a red, two-pixel marker on each ruler at the
  pointer, with the reading beside it in the ruler's unit. *(Decided by the user 2026-09-26 — this
  line said "in the status bar", which was never built; `Stage4-ViewingSurface-Testing-Guide.md` §8
  I-17.)*

### 1.4 Navigation

- First page / previous page / next page / last page controls (per S3).
- Page-number box: type a page number and commit to jump (per S3).
- Arrow-key paging through a multipage document (per S3).
- Jump by *logical page label* (e.g. "iv", "A-12") as well as by physical index. **Where a document
  carries labels the page box shows the LABEL and the label wins an ambiguous entry** — typing "1" on
  a document whose front matter is roman goes to the page printed 1, not to the first sheet — because
  the box displays the label and typing what was just read must land where it was read. A document
  with no labels takes a physical number exactly as before. *(Decided at Stage 4 T12, which built it.)*
- Go to Page dialog for an explicit jump.
- Previous view / next view: retrace the history of view changes, not just pages.
- Previous document / next document in the tab set.
- Navigate by clicking a Page Thumbnail.
- Adjust the thumbnail size in the pane.
- Navigate by clicking a bookmark.
- Expand or collapse the bookmark tree to a chosen depth.
- Document Map: navigate a heading outline derived from the document's structure.
- Named Destinations pane: list and jump to every named destination.
- Follow links and buttons embedded in the page.
- Follow an *article thread* through a multi-column layout.
- Jump to the next/previous comment in the document.
- Jump to the next/previous form field.
- Jump to the next/previous search result.
- Jump to the next/previous difference in a document comparison.

### 1.5 Document comparison ⭐

- Compare two PDF files and report their differences.
- The Compare dialog remembers, for each older file, the newer file it was last compared against, and offers it again.
- Side-by-side comparison view with synchronized scrolling.
- Overlay comparison view with per-document colour tinting, one aligned page pair at a time or all pairs in one continuous scroll.
- Detect text differences (insertions, deletions, replacements, moves).
- Detect page-appearance differences (graphics, images, position shifts).
- Detect page insertions, deletions, and reorderings.
- Generate a difference summary page listing every change.
- Navigate difference-by-difference from the summary.
- Ignore-lists: exclude headers/footers, page numbers, or whitespace-only changes.
- Choose a comparison sensitivity (text-only, text and layout, appearance).
- Export the comparison as an annotated PDF report.

### ~~1.6 Read Aloud~~ ⤵ withdrawn

⤵ **Removed 2026-08-23** — see *Removed from scope* at the top of this document. The heading is kept
as a breadcrumb so that §1.7 and §1.8 keep their numbers and every existing cross-reference to them
still resolves. `platform::ISpeech`, `platform/win/WinSpeech` and `ui/shell/ReadAloudBar` go with it.

### 1.7 AI assistant

- Summarize the whole document.
- Summarize the current selection or page range.
- Ask a natural-language question answered from the open document, with page citations.
- Translate the current selection into a chosen language.
- Propose a table of contents from the document's headings.
- Extract structured data (tables, key/value pairs) from a selection.
- Choose the model provider and endpoint.
- Disable the assistant entirely, and confirm no document content is transmitted while it is off.
- Per-document opt-out for confidential files.

### 1.8 Search and find ⭐

**Find (in the current document)**
- Incremental find toolbar that highlights as you type.
- Find next / find previous.
- Highlight all matches at once.
- Whole-words-only matching.
- Case-sensitive matching.
- Diacritic-insensitive matching.
- Regular-expression matching.
- Include bookmarks in the search.
- Include comments in the search.
- Include form field names and values in the search.
- Include file attachments in the search.
- Include the OCR text layer of scanned pages in the search.
- Search only within the current selection or a page range.

**Advanced Search**
- Search across every PDF in a folder, optionally recursing into subfolders.
- Search a prebuilt catalog index.
- Search across every component file of a PDF Portfolio.
- Result list showing the page, a text snippet, and the containing file.
- Click a result to jump to and highlight it in place.
- Sort results by relevance, by page, or by file.
- Filter results by date, author, or subject metadata.
- Refine a completed search within its own results.
  - `Enter` in the pane's query, author, subject or date field **narrows a list that exists**: the filters
    apply, and a query that differs from the last one the list came from refines the list by it. With no
    list, or once the folder or *Include subfolders* has changed, `Enter` starts a new search, as it always
    does from the folder field. From the result list it opens the current result. *(Added 2026-10-03, the
    user's design; `Stage5-TextSearch-Testing-Guide.md` I-21.)*
  - A refined list names the queries it was built from (*"Refined: corpus → Hebrew"*), and a result opened
    from it is shown under the last of them. *(2026-10-03, I-22.)*
  - A *Clear* button empties the list and its refinements and keeps every field as typed.
- Export the result list.

**Indexing**
- Build a full-text catalog index over a folder tree.
- Index options: exclude numbers, apply a stop-word list, include custom document properties, include XMP metadata.
- Schedule index rebuilds.
- Rebuild, purge, and validate an existing index.
- View an index's build log and statistics.
- Embed a full-text index inside a single PDF.
- Remove an embedded index.

---

## 2. Create & Export

### 2.1 Create a PDF

- Create a new blank PDF, opened directly in Write mode.
- Create from a single file (Word, Excel, PowerPoint, RTF, plain text, HTML, Markdown, image).
- Create from many files at once, each becoming its own PDF.
- Create one PDF from many files merged in a chosen order.
- Create from the clipboard's current contents.
- Create from a screen-region capture.
- Create from a scanner.
- Create from a web page URL.
- Create by printing from any application to a virtual PDF printer.
- Create by extracting a page range from an open PDF.

**Scanner acquisition**
- Choose the scanner device and driver interface.
- Set resolution, colour mode (colour / greyscale / black-and-white), and paper size.
- Scan from the automatic document feeder, with duplex support.
- Scan multiple pages into one PDF, prompting between sheets for a flatbed.
- Append scanned pages to the open document.
- Deskew scanned pages automatically.
- Despeckle and background-clean scanned pages.
- Detect and drop blank pages.
- Detect and warn on multi-feed.
- Compress scanned output with a chosen quality target.

**OCR**
- OCR a scanned PDF to add an invisible searchable text layer, leaving the image untouched.
- OCR a scanned PDF to *editable text*, replacing the image with recognized text and fonts.
- Select one or more recognition languages.
- Restrict OCR to a page range.
- Downsample images during OCR to control output size.
- Review and correct low-confidence recognitions.
- Report the OCR confidence per page.
- Skip pages that already contain a text layer.

**Web capture**
- Capture a single URL.
- Capture a site to a chosen link depth.
- Stay on the same server while following links.
- Preserve hyperlinks in the captured PDF.
- Add a header/footer carrying the source URL and capture timestamp.
- Append a newly captured page to an existing PDF.
- Capture from a browser extension in Edge, Chrome, and Firefox.
- Capture only the current browser selection.

### 2.2 PDF Portfolios

- Create a portfolio container.
- Choose a portfolio layout or template.
- Add files to a portfolio.
- Add a whole folder, preserving its structure.
- Create nested folders inside a portfolio.
- Open a component file in place.
- Extract a component file to disk.
- Delete, rename, and reorder component files.
- Sort components by name, size, type, or date.
- Edit portfolio-level metadata and a cover sheet.
- Search across every component.
- Apply security to the portfolio as a whole.
- Convert a portfolio to a single flat merged PDF.

### 2.3 Batch conversion (Converter)

- Convert many source files to PDF in one run.
- Convert many PDFs to another format in one run.
- Conversion profiles that bundle every setting under a name.
- Page setup per profile: size, orientation, margins.
- Image handling per profile: colour downsampling, compression method, quality.
- Font handling per profile: embed, subset, or substitute.
- Target PDF version per profile.
- PDF/A and PDF/X conformance targets per profile.
- Generate bookmarks from source-document headings.
- Preserve source hyperlinks and cross-references as PDF links.
- Convert source comments into PDF comments.
- Convert tracked changes into PDF markup.
- Destination folder, or "beside the source file".
- Output filename patterns with tokens (name, date, counter, source folder).
- Overwrite / skip / auto-rename policy on collision.
- Mirror the source folder structure into the destination.
- Post-conversion action: open, email, or reveal in the file manager.
- Watched (hot) folders that convert anything dropped into them.
- Run the converter from the command line.
- Per-run report listing successes, skips, and failures.

### 2.4 Office and printer integration

- Office ribbon add-in for Word, Excel, PowerPoint, and Visio: Create PDF.
- Office add-in: Create PDF and attach to a new email.
- Office add-in: per-application conversion preferences.
- Word: map heading styles to PDF bookmarks.
- Word: convert cross-references and the TOC into working links.
- Word: convert comments into PDF comments.
- Word: include or exclude tracked changes.
- Word: produce a tagged PDF.
- Excel: choose which sheets convert.
- Excel: fit-to-page and print-area handling.
- PowerPoint: include speaker notes.
- PowerPoint: include or skip hidden slides.
- PowerPoint: carry slide transitions into PDF page transitions.
- Outlook add-in: convert a selected message to PDF.
- Outlook add-in: convert a whole mail folder to one PDF, with a bookmark per message.
- Outlook add-in: append a message to an existing PDF archive.
- Virtual printer: print from any application to produce a PDF.
- Virtual printer: prompt for the filename, or auto-name from the job.
- Virtual printer: overwrite, append-to, or insert-into an existing PDF.
- Virtual printer: named printer profiles selectable per job.
- Virtual printer: apply a watermark or security scheme at print time.

### 2.5 Save and export

**Save**
- Save, writing an incremental update that preserves the original bytes.
- Save As to a new file, fully rewritten and compacted.
- Save a Copy without switching the open document to the new path.
- Save As Optimized, applying a size-reduction profile.
- Save As with a target PDF version.
- Save As a conformance target: PDF/A-1b, PDF/A-2b, PDF/A-3b, PDF/X-1a, PDF/X-4.
- Validate conformance and report every violation with its page and object.
- Revert to the last saved state.
- Automatic recovery of unsaved work after a crash.

**Export to other formats**
- Export to Word (.docx).
- Export to Excel (.xlsx), one sheet per detected table or per page.
- Export to PowerPoint (.pptx), one slide per page.
- Export to RTF.
- Export to plain text, with or without layout preservation.
- Export to HTML, single-file or with a companion asset folder.
- Export pages as PNG, JPEG, BMP, or TIFF.
- Export all pages into one multi-page TIFF.
- Choose export DPI, colour space, and anti-aliasing.
- Export a page range or the current selection only.
- Export every embedded image at its native resolution.
- Export the annotation summary as a PDF or a CSV.
- Export form data as FDF, XFDF, XML, or CSV.
- Export bookmarks as an indented text outline.
- Export the document's tag tree.

**Content reuse**
- Copy selected text with its formatting.
- Copy a selected table as a table, preserving cells.
- Copy a dragged region as a bitmap image (snapshot).
- Copy a page as a vector image.
- Drag selected content directly into another application.

**Optimize and reduce**
- Downsample and recompress colour, greyscale, and monochrome images independently.
- Subset embedded fonts; unembed fonts that are standard.
- Discard objects: form fields, JavaScript, embedded thumbnails, alternate images, links, comments.
- Discard user data: metadata, file attachments, hidden layer content, document info.
- Clean up: remove unreferenced objects, compress the object stream, flatten the page tree.
- Audit space usage: a per-category report of where the bytes are.
- Compare estimated output size before committing.
- Apply an optimization profile in batch.

**Flatten**
- Flatten annotations into page content.
- Flatten form fields into page content, keeping their rendered values.
- Flatten layers into a single visible layer.
- Flatten transparency to a chosen resolution.
- Flatten only a page range.

---

## 3. Edit ⭐

### 3.1 The dual-mode model ⭐

- **Review mode** — the document keeps its authored PDF structure; edits act on individual objects and annotations (per S2, this is the default mode when opening an existing PDF).
- **Write mode** — the document is reflowable and edited like a word-processor file (per S2, this is the default mode for a newly created blank PDF).
- Convert to Writable: analyse a fixed PDF and reconstruct a reflowable document from it (per S2).
- Convert to Writable Excel: reconstruct a table-heavy PDF as a spreadsheet, edit with formulas, and convert back (per S2).
- Toggle freely between Review mode and Write mode on the same document (per S2).
- Prevent Further Edits: finalize the document back to a fixed PDF and end the ability to return to Write mode (per S2).
- Conversion fidelity report: list the elements that could not be faithfully reflowed, with page references.
- Warn before conversion when a document contains features that Write mode cannot represent (signatures, XFA, layers).
- Preserve the original fixed rendition alongside the writable one, so the conversion is reversible until finalized.

### 3.2 Review-mode editing

**Text**
- Click into an existing text block and edit its characters in place.
- Change the font, size, colour, and style of a selected run.
- Change character spacing, word spacing, and horizontal scaling.
- Change line spacing within a block.
- Reflow a paragraph within its own block boundary when text grows or shrinks.
- Add a new text block anywhere on the page.
- Delete a text block.
- Move and resize a text block.
- Detect and warn when an edit requires a font that is not embedded, and offer to embed or substitute.
- Find and replace text in place across the document.

**Images and objects**
- Insert an image onto a page.
- Replace an existing image in place, preserving its placement box.
- Move, scale, rotate, flip, and crop an image.
- Set an image's opacity and blend mode.
- Extract or export a selected image.
- Edit a selected image in an external editor and re-import it.
- Select vector objects and change their fill and stroke.
- Select multiple objects; align them (left/centre/right/top/middle/bottom).
- Distribute selected objects horizontally or vertically.
- Group and ungroup objects.
- Change z-order: bring to front, bring forward, send backward, send to back.
- Nudge a selection with the arrow keys; fine-nudge with a modifier.
- Set exact position and size numerically.
- Lock an object against accidental edits.
- Delete a selected object.

**Structure and page furniture**
- Touch up the reading order of a page.
- Define an article thread by drawing boxes in reading order.
- Reorder and delete boxes in an article thread.
- Insert or edit a page background object.
- Insert or edit a watermark object.
- Edit document metadata: title, author, subject, keywords, custom properties.
- Edit XMP metadata directly.
- Set the document's initial view (page, zoom, layout, open panes, full-screen-on-open).
- Set the document's language.

### 3.3 Write-mode editing

**Text entry and character formatting** (per S2: "full-scale text editing")
- Type, select, and delete text with full reflow across pages.
- Font family, size, and colour.
- Bold, italic, underline, double underline, strikethrough.
- Superscript and subscript.
- Small caps and all caps.
- Text highlight colour.
- Character spacing and kerning.
- Clear all formatting from a selection.
- Format Painter: copy formatting from one run and apply it to another.

**Paragraph formatting**
- Align left, centre, right, justify.
- First-line, left, and right indents, set numerically or by ruler markers.
- Line spacing and space-before / space-after.
- Custom tab stops with left/centre/right/decimal alignment and leaders.
- Paragraph borders and shading.
- Keep with next, keep lines together, page-break-before.
- Widow and orphan control.
- Right-to-left paragraph direction.

**Styles and lists**
- Named paragraph and character styles with a style gallery.
- Apply, modify, and create styles.
- Heading levels 1–6 that feed the Document Map, the TOC, and bookmark generation.
- Bulleted lists with selectable bullet characters and images.
- Numbered lists with selectable numbering formats and a start value.
- Multilevel lists with per-level formatting.
- Increase and decrease list indent.
- Restart or continue numbering.

**Page and section layout** (per S2: "page layout configuration")
- Page size, from a preset list or custom dimensions.
- Portrait / landscape orientation.
- Margins, including mirrored margins for duplex binding.
- Multiple text columns with a settable gutter and an optional separator line.
- Page breaks, column breaks, and section breaks.
- Per-section page setup, headers, footers, and numbering.
- Vertical alignment of text on the page.
- Line numbers.
- Page borders.

**Headers, footers, and fields**
- Insert a header and a footer per section.
- Different first page; different odd and even pages.
- Insert fields: page number, page count, "page N of M", date, time, filename, path, document title, author, custom.
- Position, font, and margin control for header/footer content.
- Update all fields on demand and on save.

**Tables** (per S2: cell/row/column management, sizing, AutoSum)
- Insert a table by choosing dimensions.
- Draw a table freehand.
- Convert selected text to a table, and a table back to text.
- Insert and delete rows, columns, and cells.
- Merge and split cells.
- Split a table into two.
- Set row height and column width numerically.
- Distribute rows evenly; distribute columns evenly.
- AutoFit to contents or to the window.
- Cell vertical and horizontal alignment.
- Cell margins and padding.
- Cell and table borders, with per-edge control.
- Cell and table shading, including banded rows.
- Table styles.
- Repeat a header row across page breaks.
- Prevent rows from breaking across pages.
- Sort table rows by one or more columns.
- AutoSum a column or row.
- Simple cell formulas with cell references (SUM, AVERAGE, COUNT, MIN, MAX, PRODUCT).
- Number formatting per cell.
- Nested tables.

**Illustrations and objects**
- Insert a picture from a file.
- Insert a shape (line, arrow, rectangle, ellipse, polygon, connector, callout).
- Insert a text box.
- Insert a chart from tabular data.
- Insert a diagram (org chart, process, cycle, hierarchy).
- Insert a symbol or special character.
- Insert an equation.
- Insert a horizontal rule.
- Set text wrapping around an object (inline, square, tight, behind, in front).
- Anchor an object to a paragraph or to the page.
- Group, order, align, and distribute objects.
- Add a caption to a figure or table, with automatic numbering.

**Document structure**
- Insert a table of contents built from heading styles, with hyperlinked entries.
- Choose TOC depth, leader style, and page-number alignment.
- Update the TOC when headings change.
- Insert bookmarks from headings (per S2).
- Insert, edit, and remove hyperlinks: URL, email, or an internal target.
- Insert cross-references to headings, figures, tables, and bookmarks.
- Insert footnotes and endnotes with automatic numbering.
- Insert an index with marked entries.
- Document Map pane for outline navigation (per S2).
- Word, character, paragraph, and page counts.

**Proofing and revision**
- Spell check as you type, with red underlines.
- Full-document spell check pass.
- Per-language dictionaries; set the language of a selection.
- Custom dictionaries: add, remove, edit, import, export.
- Ignore-once / ignore-all / add-to-dictionary.
- AutoCorrect entries.
- Find and Replace with formatting criteria and wildcards.
- Replace all, with a replacement count.
- Change tracking: record insertions, deletions, formatting changes, and moves (per S2).
- Show or hide markup; show it inline or in balloons.
- Accept or reject a change, all changes, or all changes by one reviewer.
- Navigate change-to-change.
- Per-reviewer colours and a reviewer list.
- Reviewer comments attached to a revision.
- Compare two writable documents and merge revisions.

### 3.4 Rich media and 3D

**Multimedia**
- Insert a video file with a defined play area.
- Insert an audio file.
- Link to a streamed URL instead of embedding.
- Choose the poster frame or a poster image.
- Set the activation trigger: page open, click, or page visible.
- Set the deactivation trigger: page close, click, or leaving the page.
- Play in place, in a floating window, or full-screen.
- Set player controls visibility and playback volume.
- Add alternate renditions with per-rendition requirements, so a viewer can fall back.
- Edit the play area's size, position, and border.
- Multimedia trust preferences: allow embedded media, allow media from trusted documents only, block all.

**3D models**
- Insert a 3D model (U3D, PRC) with a defined canvas.
- Set the default view, projection, and background.
- Choose the render mode (solid, transparent, wireframe, illustration, shaded).
- Choose the lighting scheme.
- Orbit, pan, zoom, walk, and fly navigation.
- Part tree: show, hide, isolate, and set the transparency of parts.
- Create, name, and switch named 3D views.
- Add a cross-section with an adjustable plane.
- Measure distance, angle, and radius on the model.
- Comment on a 3D view, with the view state stored in the comment.
- Set the poster image shown before activation.
- 3D toolbar visibility and JavaScript-driven animation playback.

---

## 4. Review & Annotate ⭐

### 4.1 Annotation tools

**Notes and text markup**
- Sticky note anchored at a point, with a pop-up body.
- Highlight selected text.
- Underline selected text.
- Strikethrough selected text.
- Squiggly-underline selected text.
- Replace text: strike the selection and attach replacement text.
- Insert text at the cursor: a caret mark with inserted text.
- Text callout: a box with a leader line pointing into the page.
- Free text box placed directly on the page.
- Typewriter-style free text that types directly onto the page without a box.

**Drawing**
- Line.
- Arrow, with selectable head styles at each end.
- Rectangle.
- Oval / ellipse.
- Polygon.
- Polyline (open).
- Cloud (revision cloud) with an adjustable jag size.
- Pencil / freehand ink.
- Ink eraser that removes whole or partial strokes.
- Connected-line dimension callouts.

**Stamps and signatures-as-markup**
- Apply a built-in standard stamp (Approved, Draft, Confidential, …).
- Apply a built-in dynamic stamp that fills in the author, date, and time.
- Create a custom stamp from a PDF page or an image.
- Organize stamps into categories.
- Mark stamps as favourites for one-click access.
- Stamp palette pane for browsing and applying stamps.
- Place a scanned handwritten signature image as a stamp.
- Place typed or drawn initials.
- Resize and rotate a placed stamp.

**Attachments and audio**
- Attach a file as a comment, shown as a pushpin on the page.
- Attach a file to the document as a whole, via the Attachments pane.
- Record an audio comment from the microphone.
- Attach an existing audio file as a comment.
- Play an audio comment inline.
- Paste the clipboard's image as a stamp comment.

**Redaction marks**
- Mark an area for redaction (the mark is an annotation; applying it is §7).
- Mark selected text for redaction.
- Set redaction overlay text, colour, and repeat behaviour.
- Choose a redaction code set and code.

**Measurement**
- Distance between two points, with a running readout.
- Perimeter along a multi-segment path.
- Area of a closed polygon.
- Set the measurement scale (page units : real-world units).
- Choose the display unit and decimal precision.
- Snap measurement endpoints to paths, endpoints, midpoints, and intersections.
- Write the measurement into a comment so it persists.
- Measurement history log for a session.
- Calibrate the scale by measuring a known distance.

**Tool behaviour and appearance**
- Set the author identity applied to new comments.
- Set default colour, opacity, line thickness, line style, and blend mode per tool.
- Make the current settings the tool's default.
- Keep the tool selected after use, instead of reverting to the selection tool.
- Copy and paste comments, including across documents.
- Duplicate a comment onto other pages, or all pages.
- Align, resize, and group selected comments.
- Lock a comment so it cannot be moved or deleted.
- Make a comment read-only for other reviewers.
- Set a comment's subject line independently of its body.
- Show comment pop-ups always, on hover, or never.

### 4.2 Comment management

- Comments pane listing every comment in the document.
- Group the list by page, author, date, type, status, or checkmark.
- Sort the list within a grouping.
- Filter by author, type, status, checkmark, or date range.
- Search comment text.
- Expand and collapse comment threads.
- Reply to a comment; reply to a reply, forming a thread.
- Set a review status: Accepted, Rejected, Cancelled, Completed, None.
- Set a personal checkmark, independent of status.
- See the status history with author and timestamp.
- Delete a comment; delete all comments; delete everything matching the current filter.
- Spell check comment text.
- Import comments from an FDF or XFDF file.
- Export comments to FDF or XFDF.
- Export only the currently filtered comments.
- Migrate comments from one document to a revised version of it, matching by text and position.
- Report comments that could not be migrated.
- Create a comments summary in a chosen layout: document and comments with connector lines, document and comments on separate pages, comments only, document only with sequence numbers.
- Choose which comment types and which filter the summary includes.
- Print the document with comments, or with the summary appended.
- Number comments sequentially for reference in discussion.

### 4.3 Sharing and collaboration

- Send a document for shared review by email.
- Send a document for shared review via a shared network folder.
- Track a review: who was invited, who responded, when.
- Send a reminder to reviewers who have not responded.
- Merge returned comments from many reviewers into the master document.
- Resolve conflicts when two reviewers changed the same thing.
- End a review and lock further comment submission.
- Join a live collaborative editing session on a shared document.
- See other participants' cursors and selections during a session.
- Per-participant colour assignment.
- Session chat alongside the document.
- Leave and rejoin a session without losing local state.

---

## 5. Assemble

### 5.1 Page operations

- Rotate pages destructively, by 90°, 180°, or 270°.
- Restrict a rotation to a page range, to odd or even pages, or to pages of one orientation.
- Extract a page range as one new document.
- Extract a page range as one file per page.
- Delete the extracted pages from the source in the same operation.
- Delete a page range.
- Replace a page range with pages from another PDF.
- Duplicate a page range within the document.
- Move a page range to a new position.
- Insert pages from another PDF at a chosen position.
- Insert blank pages of a chosen size.
- Insert pages from the clipboard.
- Insert pages from a scanner.
- Split a document every N pages.
- Split a document at every top-level bookmark.
- Split a document at a chosen bookmark level.
- Split a document by a maximum output file size.
- Split a document at blank pages.
- Split a document at pages matching a text pattern.
- Renumber pages: set a page-label style (Arabic, Roman upper/lower, letters), a prefix, and a start value, per page range.
- Crop pages by dragging a crop box, or by numeric margins.
- Set the CropBox, TrimBox, BleedBox, and ArtBox independently.
- Remove white margins automatically.
- Apply a crop to a page range, or to all pages of the same size.
- Undo a crop by restoring the MediaBox.
- Resize / scale pages to a target page size, with content scaling and centring options.
- Change a page's size without scaling its content.
- Flatten one page's annotations into its content.

### 5.2 Page Thumbnails pane

- Drag a thumbnail to reorder pages.
- Multi-select thumbnails with click, shift-click, and ctrl-click.
- Adjust the thumbnail size.
- Context actions on the selection: rotate, delete, extract, crop, insert, replace.
- Cut, copy, and paste pages between positions.
- Drag pages from one open document's thumbnail pane into another's.
- Show page labels alongside thumbnails.
- Embed page thumbnails into the file, or remove embedded thumbnails.
- Show which pages carry comments, form fields, or links.

### 5.3 Merge documents

- Combine several PDFs into one, in an explicit order.
- Include non-PDF files, converting them during the merge.
- Reorder, remove, and preview inputs before merging.
- Choose a page range from each input.
- Add a bookmark per source file, named from its filename or title.
- Preserve each source's bookmarks under that per-file bookmark.
- Preserve links, comments, form fields, and layers from each source.
- Rename colliding form field names, or merge them deliberately.
- Report inputs that were skipped and why (encrypted, corrupt, unsupported).
- Insert a separator page between merged documents.

### 5.4 Bookmarks

- Create a bookmark from the current view.
- Create a bookmark from selected text, using the text as the title.
- Rename a bookmark.
- Delete a bookmark, with a choice about its children.
- Set a bookmark's destination to the current view.
- Set a bookmark's action instead of a destination: open a file, open a URL, run JavaScript, print, submit a form, go to another document's page.
- Reorder bookmarks among siblings.
- Promote and demote bookmarks to build a hierarchy.
- Drag bookmarks to restructure the tree.
- Auto-generate bookmarks from the document's headings.
- Auto-generate bookmarks by font size and style thresholds.
- Auto-generate bookmarks from lines matching a text pattern.
- Set a bookmark's text colour and style.
- Set the tree's default expanded/collapsed state on open.
- Generate a table of contents *page* from the bookmark tree, with working links.
- Import and export the bookmark tree as an indented text or XML file.

### 5.5 Tags and document structure

Retained after the 2026-08-23 accessibility removal, deliberately: this is the logical structure of
the document, and it is where Write mode persists its own model under decision **O2** — the composer
writes the tag tree and the converter reads it back. The accessibility surface that used to sit on
top of it is gone; the structure is not.

- View the document's tag tree.
- Add, delete, and retype tags.
- Move tags to fix the logical structure.
- Auto-tag an untagged document.
- Touch up the logical order with a visual order overlay.
- Set a language on the document, a section, or a run.
- Mark decorative content as an artifact.

⤵ **Removed 2026-08-23:** alternate text on figures; table header cells and their scope; the
accessibility check, its per-rule report and fix-from-report; PDF/UA verification; page tab order
derived from structure.

### 5.6 Layers (optional content)

- View the layer list.
- Show and hide a layer.
- Lock a layer's visibility so a viewer cannot change it.
- Rename a layer.
- Create a layer.
- Delete a layer, with a choice to delete or orphan its content.
- Merge one layer into another.
- Flatten all layers into page content.
- Set a layer's default visibility on open.
- Set a layer's visibility for printing and for export independently of screen.
- Assign selected content to a layer.
- Group layers into a nested layer tree.
- Define a radio-button group of mutually exclusive layers.

### 5.7 Headers, footers, and Bates numbering

- Add a header and footer with left, centre, and right content areas.
- Insert fields: page number, page count, date, time, filename, path, custom text.
- Choose the font, size, colour, and underline.
- Set the distance from each page edge.
- Shrink page content to avoid overlapping the header/footer.
- Apply to a page range, or to odd or even pages only.
- Preview before applying.
- Update an existing header/footer in place.
- Remove a header/footer added by RawPage.
- Save a header/footer configuration as a named profile.
- Load and delete profiles.
- Bates numbering: prefix, suffix, digit count, and start number.
- Bates numbering continued across many documents in a single run, in a chosen order.
- Report the first and last Bates number assigned per document.
- Remove Bates numbering.

### 5.8 Watermarks

- Add a text watermark.
- Add an image or PDF-page watermark.
- Set opacity, rotation, and scale (absolute, or relative to the page).
- Position by anchor and offset, or tile across the page.
- Place the watermark behind or in front of page content.
- Apply to a page range.
- Show on screen but not when printing, or vice versa.
- Preview before applying.
- Update an existing watermark.
- Remove a watermark added by RawPage.
- Save, load, and delete watermark profiles.

### 5.9 Imposition

- N-up: 2, 4, 6, 8, 9, or 16 pages per sheet.
- Choose the page order within a sheet (across, down, reverse).
- Booklet / saddle-stitch imposition, with left or right binding.
- Choose the sheet size and orientation.
- Set gutters between imposed pages.
- Creep / shingling compensation for thick booklets.
- Add crop marks, registration marks, and a colour bar.
- Add a sheet label with the sheet number.
- Duplex settings: flip on long or short edge.
- Frame each imposed page.
- Produce a new imposed document, or impose only at print time.

### 5.10 Links and destinations

- Create a link by dragging a rectangle.
- Create links from selected text.
- Auto-detect URLs in the text and turn them into links.
- Set link appearance: visible or invisible, border width, style, colour.
- Set link highlight style: none, invert, outline, inset.
- Link actions: go to a page, go to a named destination, open a file, open a URL, run JavaScript, submit a form, reset a form, import form data, show/hide a field, play media, print.
- Edit a link's rectangle and action.
- Delete a link.
- Select all links on a page or in the document.
- Align, resize, and distribute selected links.
- Verify every link and report broken targets.
- Create a named destination from the current view.
- Rename and delete named destinations.
- List named destinations and sort them by name or page.
- Repair links whose destinations moved after page reordering.

### 5.11 Batch processing

- Build an action sequence from a library of actions.
- Reorder actions within a sequence.
- Set each action's parameters, or mark it "prompt at run time".
- Built-in sequences: apply security, apply watermark, OCR, optimize, flatten, Bates-number, export, remove hidden data.
- Input sources: a file list, a folder, a folder tree, or a watched folder.
- Output destination, filename pattern, and collision policy.
- Continue-on-error or stop-on-error.
- Run a sequence over open documents instead of files.
- Save, rename, duplicate, and delete sequences.
- Export a sequence to a file and import one, for sharing with a team.
- Run a sequence from the command line.
- Per-run log and a summary report of processed, skipped, and failed files.
- Schedule a sequence to run at a set time.
- Dry-run a sequence and report what *would* change.

---

## 6. Forms

### 6.1 Form preferences and display

- Highlight all form fields with a configurable colour.
- Highlight required fields distinctly.
- Show field tooltips on hover.
- Auto-complete field entries from previous input, with a manageable history.
- Clear the auto-complete history.
- Warn when a form's calculation order is ambiguous.
- Show a document message bar when a form is detected.
- Show the focus rectangle on the active field.

### 6.2 Creating fields

- Enter and leave Form Design mode.
- Create a text field.
- Create a check box.
- Create a radio button group.
- Create a combo box (drop-down).
- Create a list box.
- Create a push button.
- Create a digital signature field.
- Create a barcode field.
- Auto-detect fields on a static PDF and generate them (form field recognition).
- Review and correct auto-detected fields.
- Copy a field.
- Duplicate a field across a page range.
- Create multiple copies of a field in a rows-and-columns grid.
- Place a field by dragging, or by entering exact coordinates.
- Align, centre, distribute, and size-match selected fields.
- Snap fields to the grid and to each other.
- Nudge and fine-nudge a field.
- Delete a field.
- Show or hide the field name overlay in Design mode.

### 6.3 Field properties

**General**
- Name, tooltip.
- Form field visibility: visible, hidden, visible-but-does-not-print, hidden-but-printable.
- Orientation (0/90/180/270).
- Read-only.
- Required.
- Locked (protect the field's own properties from edits).

**Appearance**
- Border colour, style (solid, dashed, beveled, inset, underline), and thickness.
- Fill colour.
- Font, size (including auto-size), and text colour.

**Options, per field type**
- Text field: multiline, scroll long text, allow rich text, character limit, comb field with a fixed cell count, password masking, file-select, spell check, default value, alignment.
- Check box: check style (check, circle, cross, diamond, square, star), export value, checked-by-default.
- Radio group: choice list, button style, export value per button, "buttons with the same name and value are selected in unison".
- Combo box: item list with display text and export value, sortable, editable (allow custom text), commit-on-selection-change.
- List box: item list, multiple selection, sorted.
- Push button: label, icon, layout (label only, icon only, and their combinations), icon scaling and placement, behaviour on press (none, invert, outline, push), rollover and down icons.
- Barcode: symbology (PDF417, QR, Data Matrix), which fields to encode, encoding format, error-correction level, X-dimension, and a decode-condition check.
- Signature field: action on signing, lock-fields-after-signing list.

**Actions**
- Triggers: mouse up, mouse down, mouse enter, mouse exit, on focus, on blur.
- Actions: go to a page view, open a file, open a URL, run JavaScript, submit a form, reset a form, import form data, show/hide a field, play media, print, run a menu item.
- Order multiple actions on one trigger.
- Edit and delete an action.

**Format, Validate, Calculate**
- Format category: none, number, percentage, date, time, special (zip, zip+4, phone, SSN), arbitrary mask, custom script.
- Number format: decimals, separator style, currency symbol and placement, negative-number styling.
- Validate: accept only values in a numeric range; run a custom validation script.
- Calculate: sum, product, average, minimum, or maximum of a picked field list.
- Calculate: simplified field notation expression.
- Calculate: custom calculation script.
- Edit the field calculation order explicitly.
- Report a calculation that references a missing field.

### 6.4 Tab order

- Order fields by row.
- Order fields by column.
- Order fields by the document's structure tree.
- Reorder manually by dragging in the Fields pane.
- Show numbered tab-order badges on the page.
- Set the tab order per page.
- Set whether tabbing wraps to the next page.

### 6.5 Data and lifecycle

- Fill a form as an end user: type into text fields, toggle check boxes, choose from lists, pick dates.
- Auto-complete suggestions while typing.
- Validate required fields on submit and report which are missing.
- Save a filled form with its data.
- Print the form, with or without field values.
- Print only the field values onto a pre-printed form.
- Reset the whole form.
- Reset only selected fields.
- Clear the form automatically when the document opens.
- Import form data from FDF, XFDF, XML, or a CSV/TSV row.
- Export form data to FDF, XFDF, XML, or CSV.
- Merge the data from many filled copies of one form into a single spreadsheet.
- Append filled data to an existing data file.
- Normalize a static XFA form to an AcroForm so it can be edited and filled.
- Report the parts of a dynamic XFA form that could not be normalized.
- Flatten a form, replacing fields with their rendered values.
- Distribute a form read-only except for its fields.
- Support the common Acrobat-compatible form JavaScript surface (`this.getField`, `event.value`, `AFNumber_Format`, `AFDate_Format`, `app.alert`), with a documented and sandboxed subset.
- Disable form JavaScript entirely, and report when a document's script was blocked.

---

## 7. Secure & Sign ⭐

### 7.1 Choosing and applying protection

- Choose a security method: none, password, certificate, a saved security scheme, or a rights-management label.
- View the current security settings of the open document.
- Remove all security, given the owner password.
- Show a document message bar when the document is protected, naming the restrictions.

### 7.2 Password security

- Set a document-open (user) password.
- Set a permissions (owner) password.
- Require the open password to view at all.
- Encryption algorithm: RC4 40-bit, RC4 128-bit, AES-128, AES-256.
- Encrypt all document contents.
- Encrypt all contents *except* metadata, so search indexers still work.
- Encrypt only file attachments.
- Printing permission: not allowed, low-resolution only, high-resolution.
- Changing permission: none, page assembly only, form-fill and signing, commenting plus form-fill, anything except extraction.
- Allow or forbid copying text, images, and other content.
- Allow or forbid content extraction under the ISO 32000 accessibility permission bit. **Kept deliberately** after the 2026-08-23 removal: this is a flag in the PDF encryption dictionary that any conforming reader must round-trip, so `Permissions` has to model it whether or not we ship accessibility features of our own. Not doing so would silently drop a bit from every protected document we save.
- Enforce a minimum password strength and warn on weak passwords.
- Change or remove passwords on an already-protected document.

### 7.3 Certificate security

- Encrypt for a chosen list of recipients' public certificates.
- Pick recipients from the system certificate store, from trusted contacts, or from a file.
- Look recipients up on a directory (LDAP) server.
- Grant different permissions to different recipients.
- Include yourself as a recipient so you can reopen the file.
- Show which certificate a protected document is encrypted for.
- Report an expired or revoked recipient certificate before applying.

### 7.4 Security schemes

- Save the current security configuration as a named scheme.
- Apply a scheme to the open document.
- Apply a scheme in a batch run.
- Edit and delete schemes.
- Mark schemes as favourites.
- Export and import schemes, for deploying an organizational policy.
- Show the scheme's audit trail: who applied it and when.

### 7.5 Digital IDs and trust

- Create a self-signed digital ID.
- Import a digital ID from a PKCS#12 (.pfx/.p12) file.
- Use a digital ID from the operating system's certificate store.
- Use a digital ID on a smart card, token, or HSM.
- Set a default signing ID.
- Set how long an unlocked ID stays usable before the password is required again.
- Export an ID's public certificate for distribution.
- Delete an ID.
- Show an ID's details: subject, issuer, validity, key usage, thumbprint.
- Trusted identities: import contacts and certificates.
- Set a certificate's trust level: for signing, for certifying documents, for dynamic content, for embedded JavaScript, for privileged system operations.
- Export a trusted-identity list.
- Configure a directory (LDAP) server for certificate lookup.
- Configure a timestamp authority (RFC 3161) with credentials.
- Set the default timestamp server.

### 7.6 Signing

- Apply an approval signature.
- Apply a certifying signature, choosing what remains permitted afterwards: no changes, form-fill only, form-fill and commenting.
- Apply an invisible signature with no page appearance.
- Sign into an existing signature field.
- Place a new signature field and sign it in one step.
- Enter a reason, location, and contact info with the signature.
- Choose the signature's appearance from a saved list.
- Create a signature appearance: name text, imported graphic, or a handwritten image, plus which labels to show (reason, date, location, DN, version).
- Edit and delete appearances.
- Embed a trusted timestamp in the signature.
- Embed revocation information (OCSP response or CRL) for long-term validation.
- Target a PAdES profile: B-B, B-T, B-LT, B-LTA.
- Lock chosen fields when the signature is applied.
- Sign many documents in one batch run.
- Report why signing failed (expired certificate, no private key, document permissions).

### 7.7 Validating signatures

- Validate a single signature.
- Validate every signature in the document.
- Signature panel listing each signature with its status.
- Show validity details: whether the document was modified since signing, whether the identity is trusted, the certificate chain, revocation status, and the signing time source.
- Distinguish the signer's clock from a trusted timestamp.
- View the document *as it was* at the moment of each signature.
- Compare a signed version against the current version and list the changes.
- Show which changes after signing were permitted by the certifying signature and which were not.
- Validation preferences: verify as of signing time or as of now, require revocation checking, allow expired timestamps, trust the Windows certificate store as a root source.
- Add a signer's certificate to trusted identities directly from the validation report.
- Report clearly and non-alarmingly when a signature cannot be validated offline.
- Export a validation report.

### 7.8 Secure delivery

- Protect with a password and attach to a new email in one step.
- Create an electronic envelope: a cover PDF carrying encrypted attachments.
- Choose an envelope template.
- Add and remove envelope attachments.
- Apply security to the envelope, so its attachments inherit protection.
- Apply a Microsoft Purview / Azure Information Protection label.
- View a document's current rights-management label and its restrictions.
- Track a rights-protected document's access.
- Revoke access to a rights-protected document.
- Sign in and out of the rights-management service.

### 7.9 Removing sensitive content

**Redaction**
- Apply marked redactions, permanently removing the underlying content.
- Search for text, a pattern, or a regular expression across the document and mark every hit for redaction.
- Redact across a page range in one pass.
- Redact whole pages.
- Overlay redacted areas with a colour, with text, or with a code from a code set.
- Manage redaction code sets: create, edit, import, export.
- Automatically also redact matching text in comments, bookmarks, metadata, and form values.
- Produce a redaction report listing what was removed and where.
- Save the redacted output as a new file, keeping the original intact.
- Warn when applying redactions to a signed document.

**Hidden content**
- Find hidden content: metadata, embedded content and attachments, JavaScript, hidden layers, off-page and clipped content, overlapping text, deleted-but-recoverable objects, bookmarks, links, comments, form data, and embedded search indexes.
- Review each category and choose what to remove.
- Remove all hidden data in one "sanitize" pass.
- Sanitize as part of a batch sequence.
- Report what sanitizing removed.
- Verify after sanitizing that no flagged category remains.

**Trust and containment**
- Trust manager: allow or block PDF-initiated internet access, with per-host allow and block lists.
- Block or quarantine attachments of dangerous file types.
- Warn before running a document's JavaScript, opening an external file, or submitting a form.
- Designate privileged locations (files, folders, hosts) that bypass warnings.
- Open untrusted documents in a restricted "protected view" with editing disabled.
- Disable JavaScript globally, or only for untrusted documents.
- Keep an audit log of security operations performed in the application.

---

## 8. Key Bindings (authoritative reference)

These tables are the **single source of truth for key bindings**. Downstream documents
(`Implementation-Plan.md`, the testing guides) cross-reference them and must not redefine a chord.
See the *honesty note on §8* above for provenance.

Every chord is dispatched against an active **input context**. A chord may legitimately mean
different things in different contexts and is then listed **once per context**. There is one table
per context, plus a **Common** table whose chords apply everywhere unless a context overrides them.

The contexts are: **Global**, **Viewer (Review mode)**, **Write mode**, **Annotation tools**,
**Page Thumbnails pane**, **Bookmarks pane**, **Comments pane**, **Forms — Design**,
**Forms — Fill**, **Find / Search**, and **Full Screen & Presentation**.

### What "the twelve deliberate collisions" counts

The figure quoted throughout the document set means one specific thing: **chords that mean something
different in Write mode than in the context they would otherwise resolve to.** It is the set that
makes `KeyContext` load-bearing, because Write mode is where a reader's chord and a word-processor's
chord disagree most.

| Write mode overrides… | Count | Chords |
|---|---|---|
| **Viewer** | **8** | `Ctrl+B` `Ctrl+E` `Ctrl+L` `Ctrl+R` `Ctrl+U` `Ctrl+=` `Ctrl+Shift+=` `Ctrl+Shift+N` |
| **Global** | **4** | `Ctrl+D` `Ctrl+K` `Ctrl+Shift+A` `Ctrl+Shift+L` |
| **Common** | 0 | — |
| | **12** | |

Other, wider counts exist over these tables and are **not** what "twelve" refers to — a census of
every context gives **6** chords where any context redefines a `Global` chord, **9** where a context
overrides a `Common` one, and **53** chords appearing in more than one table at all (mostly the
ordinary per-context meanings of `Enter`, `Delete` and the arrow keys). Stating the scope here so
that downstream documents cite a defined number rather than an ambiguous one. *(Re-derived
2026-09-29 by parsing these tables, a gesture cell such as `Alt`+drag counted as one key; the method
reproduced the previous **52** and **6** before the edit. **53** is 52 plus Stage 5's `Alt`+drag, now
in Viewer and Annotation tools with different meanings. `Ctrl+Shift+F` MOVED from Find / Search to
Global rather than appearing in both, so it adds nothing, and the **6** does not move.)*

### Global (active in every context)

| Key | Command |
|---|---|
| `Ctrl+N` | New blank PDF (opens in Write mode) |
| `Ctrl+O` | Open… *(Acrobat)* |
| `Ctrl+W` | Close the current document tab |
| `Ctrl+S` | Save (incremental) *(Acrobat)* |
| `Ctrl+Shift+S` | Save As… |
| `Ctrl+P` | Print… *(Acrobat)* |
| `Ctrl+D` | Document Properties *(Acrobat)* |
| `Ctrl+K` | Preferences *(Acrobat)* |
| `Ctrl+Q` | Quit |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | Next / previous document tab |
| `Ctrl+F6` / `Ctrl+Shift+F6` | Next / previous document window |
| `F4` | Show / hide the sidebar |
| `F9` | Show / hide the ribbon |
| `F11` | Enter / leave Full Screen |
| `Alt` | Show ribbon key tips — on `Alt`'s **release**, and only when nothing else was pressed, clicked or wheeled while it was held (Windows' menu convention; S4 decision D4-22, so that `Alt`+wheel can set the Loupe's magnification) |
| `Ctrl+Shift+U` | Focus the tool finder |
| `Ctrl+Shift+F` | Open the Advanced Search pane *(Acrobat)* — MOVED here from the Find / Search table on 2026-09-29 (S5 decision D5-15, `Stage5-TextSearch-Plan.md` §3): bound in that context alone it could not be reached from the page, and bound in both the store counts it as a Global chord a context takes back, which it is not |
| `Ctrl+Shift+P` | Page Thumbnails pane |
| `Ctrl+Shift+B` | Bookmarks pane |
| `Ctrl+Shift+C` | Comments pane |
| `Ctrl+Shift+I` | Form Fields pane |
| `Ctrl+Shift+L` | Layers pane |
| `Ctrl+Shift+E` | Tags pane |
| `Ctrl+Shift+A` | Attachments pane |
| `Ctrl+Shift+G` | Signatures pane |
| `Ctrl+Shift+O` | Document Map pane |
| `F1` | Help |

### Viewer (Review mode)

| Key | Command |
|---|---|
| `Ctrl+=` / `Ctrl+-` | Zoom in / zoom out *(Acrobat)* |
| `Ctrl+0` | Fit Page *(Acrobat)* |
| `Ctrl+1` | Actual Size (100%) *(Acrobat)* |
| `Ctrl+2` | Fit Width *(Acrobat)* |
| `Ctrl+3` | Fit Visible *(Acrobat)* |
| `Ctrl+4` | Reflow view |
| `Ctrl+M` | Zoom To… (arbitrary percentage) |
| `Ctrl+Shift+N` | Go to Page… *(Acrobat)* |
| `↑` / `↓` / `←` / `→` | Scroll the page (paging at page boundaries, per S3) |
| `Page Down` / `Page Up` | Next / previous screen |
| `Space` / `Shift+Space` | Scroll one screen down / up |
| `Ctrl+Page Down` / `Ctrl+Page Up` | Next / previous page |
| `Home` / `End` | First / last page |
| `Alt+←` / `Alt+→` | Previous view / next view (view history) |
| `Ctrl+Shift+=` / `Ctrl+Shift+-` | Rotate the **view** clockwise / counter-clockwise |
| `Ctrl+R` | Rotate **pages** (destructive) — opens the dialog |
| `H` | Hand tool |
| `V` | Selection tool |
| `Z` | Marquee zoom tool |
| `S` | Snapshot (copy a dragged region as an image) |
| `Ctrl+B` | New bookmark from the current view |
| `Ctrl+E` | Show / hide the contextual properties bar *(Acrobat)* |
| `Ctrl+L` | Reading mode (hide the ribbon and panes) |
| `Ctrl+U` | Show / hide rulers |
| `Ctrl+'` | Show / hide the grid |
| `Ctrl+;` | Toggle Snap to Grid |
| `Ctrl+Shift+H` | Toggle single-page ↔ continuous scrolling |
| `Ctrl+Shift+T` | Toggle Two-Up |
| `Alt`+drag | Select a column of text — with the Selection tool (`V`), the spans of every line the rectangle crosses (S5 decision D5-8, 2026-09-29) |
| `Ctrl+Alt+C` | Compare documents… |
| `Ctrl+.` / `Ctrl+,` | Next / previous comment |
| `Ctrl+]` / `Ctrl+[` | Next / previous difference (during a comparison) |

### Write mode

| Key | Command |
|---|---|
| `Ctrl+B` | **Bold** *(word-processor)* |
| `Ctrl+I` | *Italic* *(word-processor)* |
| `Ctrl+U` | Underline *(word-processor)* |
| `Ctrl+Shift+>` / `Ctrl+Shift+<` | Increase / decrease font size |
| `Ctrl+Shift+=` | Superscript |
| `Ctrl+=` | Subscript — **overrides Viewer zoom-in in this context** |
| `Ctrl+Shift+A` | All caps *(word-processor)* |
| `Ctrl+Space` | Clear character formatting |
| `Ctrl+D` | Font dialog *(word-processor)* |
| `Ctrl+L` | Align left *(word-processor)* |
| `Ctrl+E` | Align centre *(word-processor)* |
| `Ctrl+R` | Align right *(word-processor)* |
| `Ctrl+J` | Justify *(word-processor)* |
| `Ctrl+Shift+N` | Apply the Normal style *(word-processor)* |
| `Ctrl+Alt+1` / `Ctrl+Alt+2` / `Ctrl+Alt+3` | Apply Heading 1 / 2 / 3 *(word-processor)* |
| `Ctrl+Shift+L` | Bulleted list *(word-processor)* |
| `Ctrl+Shift+7` | Numbered list |
| `Tab` / `Shift+Tab` | Indent / outdent a list item; next / previous cell inside a table |
| `Ctrl+K` | Insert hyperlink *(word-processor)* |
| `Ctrl+H` | Find and Replace *(word-processor)* |
| `Ctrl+Enter` | Page break |
| `Ctrl+Shift+Enter` | Column break |
| `Ctrl+Shift+Space` | Non-breaking space |
| `Ctrl+Shift+8` | Show / hide formatting marks |
| `Ctrl+Shift+R` | Toggle change tracking |
| `Ctrl+Alt+M` | Insert a margin comment on the revision |
| `Alt+=` | AutoSum the current table column or row |
| `F7` | Spell check the document |
| `Shift+F7` | Set the language of the selection |
| `Ctrl+Alt+F` | Toggle Prevent Further Edits (finalize) — confirms first |
| `Ctrl+Shift+W` | Word count |

> **Deliberate collisions between Review and Write mode.** `Ctrl+B`, `Ctrl+D`, `Ctrl+E`, `Ctrl+K`,
> `Ctrl+L`, `Ctrl+R`, `Ctrl+U`, `Ctrl+=`, `Ctrl+Shift+A`, `Ctrl+Shift+L`, `Ctrl+Shift+N`, and
> `Ctrl+Shift+T` each mean one thing in a reading/review context and another in Write mode. This is
> intentional and is the reason a dispatch layer must resolve chords by context rather than
> globally: **Review-side contexts follow Acrobat-family conventions; Write mode follows
> word-processor conventions**, because those are the two sets of muscle memory the product's users
> arrive with. A chord in a *pane* context (Thumbnails, Bookmarks, Comments, Fields) wins only while
> that pane has focus.

### Annotation tools

| Key | Command |
|---|---|
| `Ctrl+Alt+M` | New sticky note at the cursor |
| `Ctrl+Alt+H` | Highlight tool |
| `Ctrl+Alt+U` | Underline tool |
| `Ctrl+Alt+X` | Strikeout tool |
| `Ctrl+Alt+T` | Text box tool |
| `Ctrl+Alt+L` | Line tool |
| `Ctrl+Alt+R` | Rectangle tool |
| `Ctrl+Alt+O` | Oval tool |
| `Ctrl+Alt+N` | Polygon tool |
| `Ctrl+Alt+P` | Pencil / ink tool |
| `Ctrl+Alt+S` | Stamp palette |
| `Ctrl+Alt+D` | Distance measurement |
| `Ctrl+Alt+A` | Area measurement |
| `Ctrl+Alt+K` | Mark for redaction |
| `K` | Toggle "keep the tool selected after use" |
| `Shift`+drag | Constrain to a square / circle / 45° angle |
| `Alt`+drag | Duplicate the annotation being dragged |
| `Enter` | Finish an in-progress polygon or polyline |
| `Esc` | Cancel the in-progress annotation and return to the Selection tool |
| `Delete` | Delete the selected annotation |
| `Ctrl+E` | Open the annotation properties bar |
| `Ctrl+Alt+Enter` | Make the current properties this tool's default |
| `arrows` / `Ctrl+`arrows | Nudge / fine-nudge the selected annotation |

### Page Thumbnails pane

| Key | Command |
|---|---|
| `Enter` | Go to the selected page |
| `↑` / `↓` / `←` / `→` | Move the selection |
| `Ctrl+A` | Select all pages |
| `Delete` | Delete the selected pages |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut / copy / paste pages |
| `Alt+↑` / `Alt+↓` | Move the selected pages earlier / later |
| `Ctrl+R` | Rotate the selected pages… |
| `Ctrl+Shift+X` | Extract the selected pages… |
| `Ctrl+Shift+D` | Duplicate the selected pages |
| `+` / `-` | Larger / smaller thumbnails |

### Bookmarks pane

| Key | Command |
|---|---|
| `Enter` | Go to the bookmark's destination |
| `F2` | Rename the bookmark |
| `Delete` | Delete the bookmark (asks about its children) |
| `Ctrl+B` | New bookmark from the current view |
| `Tab` / `Shift+Tab` | Demote / promote in the hierarchy |
| `Alt+↑` / `Alt+↓` | Move up / down among siblings |
| `→` / `←` | Expand / collapse the selected bookmark |
| `*` / `/` | Expand all / collapse all |
| `Ctrl+Shift+D` | Set the bookmark's destination to the current view |

### Comments pane

| Key | Command |
|---|---|
| `↑` / `↓` | Previous / next comment in the list |
| `Enter` | Open and focus the selected comment's editor |
| `F2` | Edit the comment text in place |
| `Ctrl+Enter` | Reply to the selected comment |
| `Delete` | Delete the selected comment |
| `Space` | Toggle the comment's checkmark |
| `Ctrl+Shift+K` | Set the review status… |
| `Ctrl+F` | Search within comments |
| `Ctrl+.` / `Ctrl+,` | Next / previous comment (also moves the page view) |

### Forms — Design

| Key | Command |
|---|---|
| `Ctrl+Alt+F` | Enter / leave Form Design mode |
| `T` | Text field tool |
| `K` | Check box tool |
| `B` | Radio button tool |
| `D` | Drop-down (combo box) tool |
| `L` | List box tool |
| `U` | Push button tool |
| `G` | Digital signature field tool |
| `X` | Barcode field tool |
| `Tab` / `Shift+Tab` | Select the next / previous field in tab order |
| `Enter` | Open the selected field's Properties |
| `Delete` | Delete the selected field |
| `Ctrl+A` | Select every field on the page |
| `Ctrl+D` | Duplicate the field across pages… |
| `arrows` / `Ctrl+`arrows | Nudge / fine-nudge the field |
| `Ctrl+Shift+`arrows | Resize the selected field |
| `Ctrl+Shift+T` | Show / hide tab-order badges |
| `Ctrl+Alt+G` | Toggle field highlighting |

### Forms — Fill

| Key | Command |
|---|---|
| `Tab` / `Shift+Tab` | Next / previous field |
| `Space` | Toggle the focused check box, or press the focused button |
| `↑` / `↓` | Change the combo/list selection; move a line in a multiline text field |
| `Enter` | Commit the field; on the last field, run the submit action |
| `Ctrl+Enter` | Insert a new line in a multiline text field |
| `Esc` | Revert the focused field to its previous value |
| `F2` | Begin editing the focused field |
| `Ctrl+Alt+G` | Toggle field highlighting |
| `Ctrl+Shift+K` | Show the field's tooltip and validation message |

### Find / Search

| Key | Command |
|---|---|
| `Ctrl+F` | Open the Find toolbar *(Acrobat)* |
| `F3` / `Shift+F3` | Find next / previous |
| `Ctrl+G` / `Ctrl+Shift+G` | Find next / previous (alias) |
| `Enter` | Jump to the highlighted result |
| `↑` / `↓` | Move through the result list |
| `Alt+R` | Toggle regular-expression matching |
| `Alt+W` | Toggle whole-words-only |
| `Alt+C` | Toggle case sensitivity |
| `Esc` | Close Find and clear the match highlighting |

`Ctrl+Shift+F` (Advanced Search) was listed here until 2026-09-29 and is now a **Global** row, which
reaches this context too: resolution falls back to Global.

**`Alt+R` is a common GPU overlay's hotkey.** The NVIDIA App's overlay binds it by default, as a
system-wide hotkey, and Windows gives such a chord to the program that registered it before any
window sees the key. On a machine running that overlay, `Alt+R` therefore never reaches RawPage: the
lone `Alt` shows the ribbon's key tips instead (`Stage5-TextSearch-Testing-Guide.md` §8, **I-17**,
2026-10-03). The row is kept. The answer is to change the overlay's hotkey, or to remap
`find.toggleRegex` on Preferences' Shortcuts page.

### Full Screen & Presentation

| Key | Command |
|---|---|
| `F11` | Leave Full Screen |
| `Esc` | Leave Full Screen |
| `Space` / `Enter` / `→` / `Page Down` | Next page |
| `Shift+Space` / `Backspace` / `←` / `Page Up` | Previous page |
| `Home` / `End` | First / last page |
| `B` | Blank the screen to black |
| `W` | Blank the screen to white |
| `P` | Toggle the laser pointer |
| `Ctrl+L` | Toggle the presentation cursor's visibility |
| `Ctrl+Shift+H` | Toggle auto-advance |
| `Ctrl+Shift+M` | Move Full Screen to the next monitor |

### Common (all contexts, unless a context above overrides the chord)

| Key | Command |
|---|---|
| `Ctrl+Z` | Undo — one journal per document, across every kind of edit |
| `Ctrl+Y` / `Ctrl+Shift+Z` | Redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut / copy / paste |
| `Ctrl+Shift+V` | Paste without formatting |
| `Ctrl+A` | Select all (whatever "all" means in the focused context) |
| `Delete` / `Backspace` | Delete the selection |
| `Esc` | Cancel the current operation, or dismiss the pop-up |
| `F2` | Rename / edit in place |
| `Alt+Enter` | Properties of the current selection |
| `Ctrl+F` | Find |

> **Global defaults recap:** `Ctrl+O` open · `Ctrl+S` save · `Ctrl+P` print · `Ctrl+F` find ·
> `Ctrl+Shift+F` advanced search · `Ctrl+K` preferences · `F4` sidebar · `F9` ribbon ·
> `F11` full screen · `Ctrl+Z`/`Ctrl+Y` undo/redo. **Every chord in every table above is
> remappable per context** from Preferences → Shortcuts, and a remapping that would shadow a
> Global chord inside a context is allowed but reported.

---

## 9. System & Infrastructure

### 9.1 Preferences groups

One settings surface, grouped into pages: **General**, **Documents**, **Page Display**,
**Full Screen**, **Commenting**, **Forms**, **Identity**, **Measuring**, **Reading**,
**Search**, **Security**, **Signatures**, **Trust Manager**, **Units & Guides**, **Spelling**,
**Multimedia & 3D**, **Convert to PDF**, **Convert from PDF**, **Batch**, **JavaScript**,
**Performance & Cache**, **Language**, **Shortcuts**, **Updates**, **AI Assistant**, **Logging**.

- Search the preferences by keyword.
- Reset one page, or all preferences, to defaults.
- Export and import the whole preference set.
- Show which preferences are locked by an administrative policy, and by which policy.

### 9.2 Persistence

- User settings file, separate from any machine-wide policy file.
- Per-document view state: last page, zoom, layout, open panes, sidebar width.
- Recent-files list, with pinning and a clear-list action.
- Session restore: reopen the previous set of documents, tabs, and window layout.
- Autosave of in-progress edits at an interval, into a recovery journal.
- Crash recovery that offers the recovered documents on the next launch.
- Named profile stores for: custom stamps, watermarks, headers/footers, security schemes, batch sequences, signature appearances, conversion profiles, and UI customizations.
- On-disk render cache and thumbnail cache, with a size cap and a purge action.
- Search index cache.
- All user data confined to per-user application-data directories, with a documented layout.
- A fully portable mode that keeps every file beside the executable.

### 9.3 File formats

**Read:** PDF 1.0 through 2.0 · encrypted PDF (RC4 40/128, AES-128, AES-256) ·
linearized PDF · tagged PDF · PDF/A, PDF/X · PDF collections and portfolios ·
static XFA, and dynamic XFA to the extent it can be normalized · FDF, XFDF, XML, CSV/TSV form data ·
U3D and PRC 3D models · common raster formats for import (PNG, JPEG, JPEG 2000, TIFF, BMP, GIF, WebP) ·
Office formats for conversion (DOCX, DOC, XLSX, XLS, PPTX, PPT, VSDX, RTF) · plain text, HTML, Markdown.

**Write:** PDF 1.4 through 2.0 · linearized (fast-web-view) PDF ·
PDF/A-1b, PDF/A-2b, PDF/A-3b · PDF/X-1a, PDF/X-4 ·
DOCX, XLSX, PPTX, RTF, TXT, HTML · PNG, JPEG, BMP, TIFF (including multi-page) ·
FDF, XFDF, XML, CSV form data · XFDF comment exchange.

- Report, before saving, any feature in the document that the chosen target format cannot represent.
- Tolerate and repair malformed PDFs: broken cross-reference tables, bad stream lengths, missing endobj, damaged object streams.
- Report what was repaired, and never silently discard content.

### 9.4 Operating-system integration

- Register as a PDF handler; set as the default PDF application; per-user and per-machine association.
- Explorer context-menu verbs: Open with RawPage, Combine to PDF, Convert to PDF, Print with RawPage.
- Shell thumbnail provider for PDF files.
- Shell preview-pane handler.
- Content-indexing filter so the OS search index can read PDF text.
- Jump list with recent and pinned documents.
- Taskbar progress during long operations (OCR, batch, export).
- Drag files into the window to open or merge them; drag pages and content out to other applications.
- Virtual PDF printer driver, installed as a system printer.
- Office application add-ins (Word, Excel, PowerPoint, Visio, Outlook).
- Browser extensions (Edge, Chrome, Firefox) for web capture and in-browser viewing.
- Scanner acquisition through the OS imaging stack.
- Read digital IDs and root certificates from the OS certificate store.
- Store secrets (signing-ID passwords, service tokens) in the OS credential store, never in plain text.
- OS font enumeration and font fallback for missing embedded fonts.
- GPU-accelerated presentation of rendered pages, with a software fallback.
- Rich clipboard exchange: text, HTML, bitmap, and vector metafile flavours.
- Single-instance behaviour: a second launch opens its file in the running instance.
- Command-line and file-association argument handling.
- Administrative deployment: silent install, machine-wide policy for defaults and locked settings, volume activation.

### 9.5 Logging and diagnostics

- Rolling log file with a size cap and retention count.
- Log levels: error, warning, info, debug, trace.
- Per-subsystem log filters (render, parse, save, sign, ocr, batch, ui).
- In-app log viewer pane with filtering and copy.
- "Collect diagnostics" that bundles the log, the settings, and the environment for a support ticket.
- Crash dump capture, with an opt-in send.
- A safe mode that starts with default settings and no add-ins.
- Structured operation timings, so a slow render or save can be attributed.
- Never write document text or file paths into a log above the debug level.

### 9.6 Command line

- Open a file, optionally at a page, a named destination, or a search term.
- Print a file to a named printer with a chosen page range and duplex setting, without showing the UI.
- Convert files to and from PDF using a named conversion profile.
- Run a named batch sequence over a file list or a folder.
- Merge a file list into one output.
- Apply a security scheme.
- Extract text or form data to stdout.
- Quiet mode; a machine-readable (JSON) report on stdout; documented exit codes.
- `--help` listing every verb and flag.

### 9.7 Display and input

- Per-monitor DPI awareness, including fractional scaling.
- Correct raster resolution per monitor, re-rendered when a window moves between monitors.
- Correct handling of a DPI change while a window is open.
- Remember window placement per monitor arrangement, and recover gracefully when a monitor disappears.
- Pen and touch input: pan, pinch-zoom, and inking with pressure.
- High-precision and inverted mouse-wheel handling.
- Full keyboard operability: every command reachable without a mouse.
- Visible keyboard focus at all times.
- Configurable UI font scale independent of the OS scale.

⤵ **Removed 2026-08-23:** accessible names, roles and values for screen readers; screen-reader
reading order from the tag tree; high-contrast themes and deference to the OS high-contrast setting;
respect for the OS "reduce motion" setting. **Keyboard operability and focus visibility above are
retained on purpose** — they are core architecture and a power-user feature, not an accessibility one.

### 9.8 Licensing, activation, and updates

- Online activation with a product key.
- Offline activation via a request/response file exchange.
- Perpetual and subscription entitlements.
- Deactivation, online and offline, to free a seat.
- Trial mode with a visible remaining-days indicator.
- Show the current entitlement, seat, and expiry.
- Volume/enterprise activation without per-seat interaction.
- Update check on a schedule or on demand; choose a stable or a preview channel.
- Delta updates; download in the background; install on next launch.
- Administratively disable update checks.
- Release notes shown for the version being installed.

### 9.9 Telemetry

- Opt-in, off by default.
- A documented, inspectable list of the events sent.
- Never send document content, file names, or paths.
- View the last payload that was sent.
- Administratively disable telemetry.

---

## 10. Portability Note (Windows-first, portable design)

Everything in §1–§8 above is expressed in terms of *documents*, not *Windows*. The following is
the **complete OS-specific surface** that must sit behind a thin platform abstraction, so the rest
of the product stays platform-agnostic and unit-testable:

| Abstraction | What it covers |
|---|---|
| **Shell integration** | File-type registration and default-handler status, Explorer context-menu verbs, thumbnail provider, preview-pane handler, content-indexing filter, jump lists, taskbar progress |
| **Printing** | Enumerating printers, submitting a print job with duplex/tray/page-range settings, and installing/servicing the **virtual PDF printer driver** |
| **Scanner acquisition** | Enumerating imaging devices and pulling pages, including ADF and duplex control |
| **Certificate and crypto services** | Reading the OS certificate store, signing through a CSP/CNG provider, smart cards and HSMs, chain building, revocation lookup |
| **Secret storage** | Storing signing-ID passwords and service tokens under OS user-key protection |
| **Text-to-speech** | Enumerating voices and speaking text with rate/volume control |
| **Font services** | Enumerating installed fonts, resolving a fallback for a missing embedded font, and rasterizing glyphs |
| **GPU presentation** | Uploading and compositing rendered page tiles on the platform's accelerated 2D surface, with a software fallback |
| **Rich clipboard** | Reading and writing the platform's bitmap, vector-metafile, and HTML clipboard flavours |
| **Single-instance bus** | Detecting an already-running instance and handing it a file to open |
| **Native dialogs** | Print, page-setup, colour, and certificate-picker dialogs where the platform's own dialog is expected |
| **Office bridge** | Driving the installed office suite for high-fidelity conversion, and hosting the office add-ins |
| **Browser bridge** | Hosting the browser extensions' native messaging endpoint |
| **Rights management** | Applying and reading enterprise information-protection labels |
| **Process sandbox** | Launching a restricted child process for protected-view rendering of untrusted files |
| **Power and session awareness** | Reacting to sleep/resume, battery-saver throttling of background rendering, and session lock/logoff |
| **Display topology** | Enumerating monitors, their DPI, and reacting to topology and scale changes |

**Everything else stays platform-agnostic**: the document object model, the annotation and form
models, the security and signature *logic* (calling the crypto abstraction, not the OS), page
rendering orchestration and tiling policy, text extraction, search and indexing, the reflow engine
behind Write mode, all page and document assembly operations, the batch engine, the undo/redo
model, settings, and the string catalogs.
