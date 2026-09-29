
## 2026-09-05T16:34:03Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 198
- **Reason** Stage 2 T17: the first capture. PDFium 153.0.8009.0, font bridge installed with I-13 base-14 decline in place.

## 2026-09-06T07:58:12Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 3 (filtered on `forms-acroform`)
- **Reason** I-21: widget annotations now rasterize. PDFium draws every annotation except /Subtype /Widget from FPDF_RenderPageBitmap; widgets need FPDF_FFLDraw and a form-fill environment, which the facade never created. The previous baselines recorded this file -- whose whole purpose is AcroForm widgets with appearance streams -- as its labels and eleven blank spaces. Found by the user in the M-03 walk, beside Acrobat.

## 2026-09-14T09:57:58Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 24 (filtered on `signed/`, `ordinary/rotate-inherited`, `ordinary/hybrid-xrefstm`)
- **Reason** Stage 3 T1: six corpus files added (corpus/signed/ four, ordinary/rotate-inherited.pdf, ordinary/hybrid-xrefstm.pdf). Additions only -- no existing baseline is regenerated, and the filter names only the new files.

## 2026-09-18T18:39:48Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 9 (filtered on `cropbox-margins`, `dests-named`, `ordinary/transitions`)
- **Reason** Stage 4 T1: three corpus files added (ordinary/cropbox-margins.pdf, ordinary/dests-named.pdf, ordinary/transitions.pdf). Additions only -- no existing baseline is regenerated, and the filter names only the new files.

## 2026-09-29T12:29:04Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 84 (filtered on `compare/`, `columns-`, `table-ruled`, `rtl-text`, `cjk-vertical`, `hyphenated`, `text-tounicode`, `cjk-readable`, `search-classes`, `text-900`)
- **Reason** Stage 5 T1: 27 corpus files added (ordinary/columns-two, columns-three, table-ruled, rtl-text, cjk-vertical, hyphenated, text-tounicode, cjk-readable, search-classes, text-900, and the 17 files of compare/). Additions only -- no existing baseline is regenerated, and the filter names only the new files.
