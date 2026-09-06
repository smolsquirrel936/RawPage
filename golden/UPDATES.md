
## 2026-09-05T16:34:03Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 198
- **Reason** Stage 2 T17: the first capture. PDFium 153.0.8009.0, font bridge installed with I-13 base-14 decline in place.

## 2026-09-06T07:58:12Z

- **PDFium** 153.0.8009.0
- **Images regenerated** 3 (filtered on `forms-acroform`)
- **Reason** I-21: widget annotations now rasterize. PDFium draws every annotation except /Subtype /Widget from FPDF_RenderPageBitmap; widgets need FPDF_FFLDraw and a form-fill environment, which the facade never created. The previous baselines recorded this file -- whose whole purpose is AcroForm widgets with appearance streams -- as its labels and eleven blank spaces. Found by the user in the M-03 walk, beside Acrobat.
