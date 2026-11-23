// core/doc/Imposition -- implementation. Stage 7 T16 (D7-8, D7-46). The header says what each layout puts where.

#include "core/doc/Imposition.h"

#include <QJsonArray>
#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace rawpage::core {
namespace {

Error badSpec(const char* field)
{
    return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.badSpec"), QLatin1String(field)};
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

PdfSize oriented(PdfSize paper, bool landscape)
{
    const double small = std::min(paper.width, paper.height);
    const double large = std::max(paper.width, paper.height);
    return landscape ? PdfSize{large, small} : PdfSize{small, large};
}

// The cell's size on a sheet of `sheet` for `grid`.
PdfSize cellSize(const ImpositionSpec& spec, PdfSize sheet, const ImpositionGrid& grid)
{
    const double areaWidth = sheet.width - 2.0 * spec.margin;
    const double areaHeight = sheet.height - 2.0 * spec.margin;
    return PdfSize{(areaWidth - (grid.columns - 1) * spec.gutter) / grid.columns, (areaHeight - (grid.rows - 1) * spec.gutter) / grid.rows};
}

double fitScale(PdfSize cell, PdfSize page)
{
    if (!(page.width > 0.0) || !(page.height > 0.0)) return 0.0;
    return std::min(cell.width / page.width, cell.height / page.height);
}

bool duplexSides(const ImpositionSpec& spec)
{
    return spec.layout == ImpositionLayout::Booklet || spec.duplex != DuplexFlip::OneSided;
}

bool turnsBack(const ImpositionSpec& spec, bool landscape)
{
    return (landscape && spec.duplex == DuplexFlip::LongEdge) || (!landscape && spec.duplex == DuplexFlip::ShortEdge);
}

// Distinct values, a hundredth of a point apart at least, in order.
std::vector<double> distinct(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    std::vector<double> out;
    for (const double v : values) {
        if (out.empty() || v - out.back() > 0.01) out.push_back(v);
    }
    return out;
}

constexpr std::array<std::pair<ImpositionLayout, std::string_view>, 2> kLayouts{{{ImpositionLayout::NUp, "nUp"}, {ImpositionLayout::Booklet, "booklet"}}};
constexpr std::array<std::pair<ImpositionOrder, std::string_view>, 2> kOrders{{{ImpositionOrder::Across, "across"}, {ImpositionOrder::Down, "down"}}};
constexpr std::array<std::pair<ImpositionBinding, std::string_view>, 2> kBindings{{{ImpositionBinding::Left, "left"}, {ImpositionBinding::Right, "right"}}};
constexpr std::array<std::pair<SheetOrientation, std::string_view>, 3> kOrientations{
    {{SheetOrientation::Auto, "auto"}, {SheetOrientation::Portrait, "portrait"}, {SheetOrientation::Landscape, "landscape"}}};
constexpr std::array<std::pair<DuplexFlip, std::string_view>, 3> kFlips{
    {{DuplexFlip::OneSided, "oneSided"}, {DuplexFlip::LongEdge, "longEdge"}, {DuplexFlip::ShortEdge, "shortEdge"}}};

template <typename E, std::size_t N>
std::string_view nameIn(const std::array<std::pair<E, std::string_view>, N>& table, E value) noexcept
{
    for (const auto& [e, name] : table) {
        if (e == value) return name;
    }
    return table.front().second;
}

template <typename E, std::size_t N>
bool valueIn(const std::array<std::pair<E, std::string_view>, N>& table, const QString& name, E& into)
{
    for (const auto& [e, n] : table) {
        if (name == QLatin1String(n.data(), static_cast<qsizetype>(n.size()))) {
            into = e;
            return true;
        }
    }
    return false;
}

} // namespace

std::string_view impositionLayoutName(ImpositionLayout layout) noexcept { return nameIn(kLayouts, layout); }
std::string_view duplexFlipName(DuplexFlip flip) noexcept { return nameIn(kFlips, flip); }

Status validate(const ImpositionSpec& spec)
{
    if (spec.layout == ImpositionLayout::NUp
        && std::find(kNUpCounts.begin(), kNUpCounts.end(), spec.perSheet) == kNUpCounts.end())
        return badSpec("perSheet");
    if (!inRange(spec.sheet.width, kMinSheetSide, kMaxSheetSide) || !inRange(spec.sheet.height, kMinSheetSide, kMaxSheetSide))
        return badSpec("sheet");
    if (!inRange(spec.margin, 0.0, kMaxImpositionMargin)) return badSpec("margin");
    if (!inRange(spec.gutter, 0.0, kMaxImpositionGutter)) return badSpec("gutter");
    if (!inRange(spec.creep, 0.0, kMaxCreep)) return badSpec("creep");
    if (spec.anyMarks() && spec.margin < kMinMarkMargin)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.marginTooSmall"), QString::number(kMinMarkMargin)};
    return {};
}

ImpositionGrid impositionGrid(const ImpositionSpec& spec, bool landscape) noexcept
{
    if (spec.layout == ImpositionLayout::Booklet) return ImpositionGrid{2, 1, landscape};
    int wide = 2;
    int narrow = 1;
    switch (spec.perSheet) {
    case 4: wide = 2; narrow = 2; break;
    case 6: wide = 3; narrow = 2; break;
    case 8: wide = 4; narrow = 2; break;
    case 9: wide = 3; narrow = 3; break;
    case 16: wide = 4; narrow = 4; break;
    default: break;
    }
    return landscape ? ImpositionGrid{wide, narrow, true} : ImpositionGrid{narrow, wide, false};
}

bool impositionLandscape(const ImpositionSpec& spec, PdfSize firstPage) noexcept
{
    if (spec.orientation == SheetOrientation::Portrait) return false;
    if (spec.orientation == SheetOrientation::Landscape) return true;
    const double portrait = fitScale(cellSize(spec, oriented(spec.sheet, false), impositionGrid(spec, false)), firstPage);
    const double landscape = fitScale(cellSize(spec, oriented(spec.sheet, true), impositionGrid(spec, true)), firstPage);
    return landscape > portrait + 1e-9;
}

std::vector<SideOrder> impositionOrder(const ImpositionSpec& spec, int pageCount, const ImpositionGrid& grid)
{
    std::vector<SideOrder> sides;
    if (pageCount <= 0 || grid.columns <= 0 || grid.rows <= 0) return sides;
    const auto page = [pageCount](int index) { return index < pageCount ? index : -1; };
    if (spec.layout == ImpositionLayout::Booklet) {
        const int padded = std::max(4, (pageCount + 3) / 4 * 4);
        const int sheets = padded / 4;
        const bool left = spec.binding == ImpositionBinding::Left;
        for (int s = 0; s < sheets; ++s) {
            // By page index from 0: the header's table less one.
            const int outerHigh = padded - 2 * s - 1;   // P-2s
            const int outerLow = 2 * s;                 // 2s+1
            const int innerLow = 2 * s + 1;             // 2s+2
            const int innerHigh = padded - 2 * s - 2;   // P-2s-1
            SideOrder front{s, false, false, left ? std::vector<int>{page(outerHigh), page(outerLow)} : std::vector<int>{page(outerLow), page(outerHigh)}};
            SideOrder back{s, true, turnsBack(spec, grid.landscape),
                           left ? std::vector<int>{page(innerLow), page(innerHigh)} : std::vector<int>{page(innerHigh), page(innerLow)}};
            sides.push_back(std::move(front));
            sides.push_back(std::move(back));
        }
        return sides;
    }
    const int perSide = grid.cells();
    const int count = (pageCount + perSide - 1) / perSide;
    const bool duplex = spec.duplex != DuplexFlip::OneSided;
    for (int k = 0; k < count; ++k) {
        SideOrder side;
        side.sheet = duplex ? k / 2 : k;
        side.back = duplex && k % 2 == 1;
        side.turned = side.back && turnsBack(spec, grid.landscape);
        side.cells.assign(static_cast<std::size_t>(perSide), -1);
        for (int i = 0; i < perSide; ++i) {
            int row = 0;
            int column = 0;
            if (spec.order == ImpositionOrder::Across) {
                row = i / grid.columns;
                column = i % grid.columns;
            } else {
                column = i / grid.rows;
                row = i % grid.rows;
            }
            if (spec.reversed) column = grid.columns - 1 - column;
            side.cells[static_cast<std::size_t>(row * grid.columns + column)] = page(k * perSide + i);
        }
        sides.push_back(std::move(side));
    }
    return sides;
}

Result<ImpositionPlan> planImposition(const ImpositionSpec& spec, const std::vector<PdfSize>& shown)
{
    if (Status valid = validate(spec); !valid) return valid.error();
    if (shown.empty()) return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.noPages"), QString()};
    if (shown.size() > static_cast<std::size_t>(kMaxImposedPages))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.tooManyPages"), QString::number(shown.size())};
    for (const PdfSize& size : shown) {
        if (!(std::isfinite(size.width) && size.width > 0.0 && std::isfinite(size.height) && size.height > 0.0))
            return badSpec("pageSize");
    }
    ImpositionPlan plan;
    const bool landscape = impositionLandscape(spec, shown.front());
    plan.sheet = oriented(spec.sheet, landscape);
    plan.grid = impositionGrid(spec, landscape);
    const PdfSize cell = cellSize(spec, plan.sheet, plan.grid);
    if (!(cell.width >= kMinCell) || !(cell.height >= kMinCell))
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.sheetTooSmall"),
                     QStringLiteral("%1x%2").arg(cell.width, 0, 'f', 1).arg(cell.height, 0, 'f', 1)};

    const int count = static_cast<int>(shown.size());
    const std::vector<SideOrder> order = impositionOrder(spec, count, plan.grid);
    const bool booklet = spec.layout == ImpositionLayout::Booklet;
    plan.pagesPadded = booklet ? std::max(4, (count + 3) / 4 * 4) : count;
    plan.sheets = order.empty() ? 0 : order.back().sheet + 1;
    if (booklet && (plan.sheets - 1) * spec.creep > cell.width / 2.0)
        return Error{ErrorCode::InvalidArgument, QLatin1String("error.imposition.creepTooLarge"),
                     QString::number((plan.sheets - 1) * spec.creep, 'f', 2)};

    const double width = plan.sheet.width;
    const double height = plan.sheet.height;
    const double m = spec.margin;
    for (const SideOrder& sideOrder : order) {
        ImposedSide side;
        side.sheet = sideOrder.sheet;
        side.back = sideOrder.back;
        side.turned = sideOrder.turned;
        std::vector<PdfRect> trims;   // each page's place WITHOUT creep: where the stack is trimmed
        for (int at = 0; at < plan.grid.cells(); ++at) {
            const int row = at / plan.grid.columns;
            const int column = at % plan.grid.columns;
            ImposedCell placed;
            placed.page = sideOrder.cells[static_cast<std::size_t>(at)];
            placed.cell = PdfRect{PdfPoint{m + column * (cell.width + spec.gutter), height - m - (row + 1) * cell.height - row * spec.gutter},
                                  cell};
            // A blank is the first page's size; an n-up's empty cell has no page at all, and no rectangle.
            const bool empty = placed.page < 0 && !booklet;
            const PdfSize page = placed.page >= 0 ? shown[static_cast<std::size_t>(placed.page)] : shown.front();
            const double scale = fitScale(cell, page);
            const PdfSize size{page.width * scale, page.height * scale};
            double x = placed.cell.left() + (cell.width - size.width) / 2.0;
            const double y = placed.cell.bottom() + (cell.height - size.height) / 2.0;
            double shift = 0.0;
            if (booklet) {
                x = column == 0 ? placed.cell.right() - size.width : placed.cell.left();
                shift = (column == 0 ? 1.0 : -1.0) * side.sheet * spec.creep;
            }
            if (!empty) trims.push_back(PdfRect{PdfPoint{x, y}, size});
            placed.placed = empty ? PdfRect{} : PdfRect{PdfPoint{x + shift, y}, size};
            side.cells.push_back(placed);
        }
        if (spec.cropMarks && !trims.empty()) {
            std::vector<double> xs;
            std::vector<double> ys;
            double left = trims.front().left();
            double right = trims.front().right();
            double bottom = trims.front().bottom();
            double top = trims.front().top();
            for (const PdfRect& r : trims) {
                xs.push_back(r.left());
                xs.push_back(r.right());
                ys.push_back(r.bottom());
                ys.push_back(r.top());
                left = std::min(left, r.left());
                right = std::max(right, r.right());
                bottom = std::min(bottom, r.bottom());
                top = std::max(top, r.top());
            }
            const double o = kCropMarkOffset;
            const double l = kCropMarkLength;
            // Below the pages, a tick stops short of the label's band (a label asked for).
            const double floor = spec.sheetLabels ? kSheetLabelBand : 0.0;
            for (const double x : distinct(xs)) {
                side.cropMarks.push_back({PdfPoint{x, top + o}, PdfPoint{x, std::min(height, top + o + l)}});
                if (bottom - o > floor) side.cropMarks.push_back({PdfPoint{x, bottom - o}, PdfPoint{x, std::max(floor, bottom - o - l)}});
            }
            for (const double y : distinct(ys)) {
                side.cropMarks.push_back({PdfPoint{left - o, y}, PdfPoint{std::max(0.0, left - o - l), y}});
                side.cropMarks.push_back({PdfPoint{right + o, y}, PdfPoint{std::min(width, right + o + l), y}});
            }
        }
        plan.sides.push_back(std::move(side));
    }
    if (spec.registrationMarks) {
        plan.registration = {PdfPoint{width / 2.0, m / 2.0}, PdfPoint{width / 2.0, height - m / 2.0}, PdfPoint{m / 2.0, height / 2.0},
                             PdfPoint{width - m / 2.0, height / 2.0}};
    }
    if (spec.colourBar) {
        const double barWidth = kColourPatches * kColourPatch;
        plan.colourBar = PdfRect{PdfPoint{width - m - barWidth, height - m / 2.0 - kColourPatch / 2.0}, PdfSize{barWidth, kColourPatch}};
    }
    if (spec.sheetLabels) plan.labelOrigin = PdfPoint{m, kSheetLabelBaseline};
    return plan;
}

PdfRect onSheet(const PdfRect& rect, const ImposedSide& side, PdfSize sheet) noexcept
{
    if (!side.turned) return rect;
    return PdfRect{PdfPoint{sheet.width - rect.right(), sheet.height - rect.top()}, rect.size};
}

QString sheetLabel(const ImpositionLabels& labels, const ImpositionSpec& spec, const ImposedSide& side, int sheets)
{
    const QString& form = duplexSides(spec) ? (side.back ? labels.back : labels.front) : labels.single;
    return QString(form).arg(side.sheet + 1).arg(sheets);
}

QJsonObject impositionToJson(const ImpositionSpec& spec)
{
    const auto name = [](std::string_view n) { return QString::fromLatin1(n.data(), static_cast<qsizetype>(n.size())); };
    QJsonObject out;
    out.insert(QStringLiteral("layout"), name(nameIn(kLayouts, spec.layout)));
    out.insert(QStringLiteral("perSheet"), spec.perSheet);
    out.insert(QStringLiteral("order"), name(nameIn(kOrders, spec.order)));
    out.insert(QStringLiteral("reversed"), spec.reversed);
    out.insert(QStringLiteral("binding"), name(nameIn(kBindings, spec.binding)));
    out.insert(QStringLiteral("sheet"), QJsonArray{spec.sheet.width, spec.sheet.height});
    out.insert(QStringLiteral("orientation"), name(nameIn(kOrientations, spec.orientation)));
    out.insert(QStringLiteral("margin"), spec.margin);
    out.insert(QStringLiteral("gutter"), spec.gutter);
    out.insert(QStringLiteral("creep"), spec.creep);
    out.insert(QStringLiteral("duplex"), name(nameIn(kFlips, spec.duplex)));
    out.insert(QStringLiteral("cropMarks"), spec.cropMarks);
    out.insert(QStringLiteral("registrationMarks"), spec.registrationMarks);
    out.insert(QStringLiteral("colourBar"), spec.colourBar);
    out.insert(QStringLiteral("sheetLabels"), spec.sheetLabels);
    out.insert(QStringLiteral("frames"), spec.frames);
    return out;
}

Result<ImpositionSpec> impositionFromJson(const QJsonObject& json)
{
    ImpositionSpec spec;
    const auto text = [&json](const char* key) { return json.value(QLatin1String(key)).toString(); };
    if (!valueIn(kLayouts, text("layout"), spec.layout)) return badSpec("layout");
    if (!valueIn(kOrders, text("order"), spec.order)) return badSpec("order");
    if (!valueIn(kBindings, text("binding"), spec.binding)) return badSpec("binding");
    if (!valueIn(kOrientations, text("orientation"), spec.orientation)) return badSpec("orientation");
    if (!valueIn(kFlips, text("duplex"), spec.duplex)) return badSpec("duplex");
    const QJsonValue perSheet = json.value(QStringLiteral("perSheet"));
    if (!perSheet.isDouble() || perSheet.toDouble() != std::floor(perSheet.toDouble())) return badSpec("perSheet");
    spec.perSheet = perSheet.toInt();
    const QJsonValue sheet = json.value(QStringLiteral("sheet"));
    if (!sheet.isArray() || sheet.toArray().size() != 2 || !sheet.toArray().at(0).isDouble() || !sheet.toArray().at(1).isDouble())
        return badSpec("sheet");
    spec.sheet = PdfSize{sheet.toArray().at(0).toDouble(), sheet.toArray().at(1).toDouble()};
    for (const auto& [key, into] : {std::pair{"margin", &spec.margin}, std::pair{"gutter", &spec.gutter}, std::pair{"creep", &spec.creep}}) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isDouble()) return badSpec(key);
        *into = value.toDouble();
    }
    for (const auto& [key, into] : {std::pair{"reversed", &spec.reversed}, std::pair{"cropMarks", &spec.cropMarks},
                                    std::pair{"registrationMarks", &spec.registrationMarks}, std::pair{"colourBar", &spec.colourBar},
                                    std::pair{"sheetLabels", &spec.sheetLabels}, std::pair{"frames", &spec.frames}}) {
        const QJsonValue value = json.value(QLatin1String(key));
        if (!value.isBool()) return badSpec(key);
        *into = value.toBool();
    }
    if (Status valid = validate(spec); !valid) return valid.error();
    return spec;
}

} // namespace rawpage::core
