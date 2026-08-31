// THE CONTROL's header: interfaces and buffers, no engine. This is what the real one looks like.
#ifndef FIXTURE_BACKEND_H
#define FIXTURE_BACKEND_H

namespace rawpage::core {
struct RasterImage { int width; int height; };
class IPageRasterizer { public: virtual ~IPageRasterizer() = default; virtual RasterImage raster() = 0; };
}
#endif
