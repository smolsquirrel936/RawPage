# Overlay ports

Empty on purpose.

A dependency lands here when vcpkg lacks a serviceable port **on the `x64-mingw-dynamic` triplet** —
which is a *community* triplet, not a first-class one, and is precisely what risk **A5** asks about.
A port that builds only under MSVC is a finding to be vendored here, **never** a reason to change the
triplet: changing it would silently reintroduce the MSVC ABI question that consuming PDFium through a
C API was designed to avoid.

Each overlay port must carry, in its `portfile.cmake` header: what upstream port it derives from, what
was changed, why, and which stage owns removing it again.

Referenced by `vcpkg-configuration.json` → `overlay-ports`.
