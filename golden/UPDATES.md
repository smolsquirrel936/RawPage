
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

## 2026-10-04T17:20:24Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 54 (filtered on `annots-900`, `annots-direct`, `annots-every-type`, `annots-flags`, `annots-no-ap`, `annots-report-v1`, `annots-rotated`, `annots-threads`, `annots-unknown-keys`, `ordinary/attachments`, `measure-scale`)
- **Reason** Stage 6 T1: 11 corpus files added (ordinary/annots-every-type, annots-no-ap, annots-threads, annots-flags, annots-rotated, annots-direct, annots-unknown-keys, annots-900, attachments, measure-scale, annots-report-v1). Additions only -- no existing baseline is regenerated, and the filter names only the new files. Every image was looked at in a scratch capture first.

## 2026-10-08T07:12:19Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 31 (filtered on `appearance/`)
- **Reason** Stage 6 T8: the first capture of the appearance baselines (guide A-64, D6-6) -- 31 cases of tests/golden/AppearanceCases: every drawing kind, every line ending, dashes, blends, opacity, clouds, ink, text markup, the caret and notes, free text, a turned page and NoRotate. Additions only -- no corpus baseline is regenerated. Each image was looked at before it was kept -- and one, polyline-endings, was NOT kept (the next entry). (The count was first written here as 33 by mistake and corrected in the same change: 31 cases exist.)

## 2026-10-08T07:13:46Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 1 (filtered on `polyline-endings`)
- **Reason** Stage 6 T8: polyline-endings recaptured -- its first capture was looked at and found to have NO endings: the case spec never set /LE (a defect in the spec, caught by looking, never kept).

## 2026-10-09T08:11:38Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 8 (filtered on `stamp`)
- **Reason** Stage 6 T10: the first capture of the STAMP appearance baselines (guide A-75, D6-16) -- 8 new cases of tests/golden/AppearanceCases: every one of ISO 32000-1 Table 181's fourteen standard stamps, three to a page (stamps-standard-1 … 5), two dynamic stamps with their baked lines (stamps-dynamic), a stamp turned 30 degrees and one turned 90 (stamps-turned), and an image stamp with an alpha ramp over the band (stamp-image). Additions only -- no corpus baseline and none of T8's 31 is regenerated. Each image was looked at before it was kept.

## 2026-10-12T11:12:33Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 1 (filtered on `file-attachments`)
- **Reason** Stage 6 T11: the first capture of the file attachment appearance baseline (D6-17) -- 1 new case of tests/golden/AppearanceCases, file-attachments: a blue pushpin at 16 x 24, a red one at twice that, and a green paper clip, each carrying a file as annot.attachFile makes one. Additions only -- no corpus baseline and none of T8's or T10's appearance baselines is regenerated. The image was looked at before it was kept.

## 2026-10-13T22:35:58Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 2 (filtered on `measure-`)
- **Reason** Stage 6 T12 (D6-14): two new appearance cases -- a distance, a dimension callout on leader lines, a perimeter and an area, each drawing its reading -- each looked at in a scratch root before it was kept
