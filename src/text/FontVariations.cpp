#include "text/BinaryReader.h"
#include "text/FontFile.h"

#include <algorithm>
#include <cmath>

namespace prism::text {
namespace {
double fixed(BinaryReader &reader) { return static_cast<std::int32_t>(reader.u32()) / 65536.0; }

double f2dot14(BinaryReader &reader) { return reader.i16() / 16384.0; }

std::vector<std::uint16_t> packedPoints(BinaryReader &reader, std::size_t total, bool &all) {
    std::size_t count = reader.u8();
    if (count & 0x80u) {
        count = ((count & 0x7Fu) << 8) | reader.u8();
    }
    all = count == 0;
    std::vector<std::uint16_t> points;
    std::uint16_t last = 0;
    while (points.size() < count && reader.ok()) {
        const std::uint8_t control = reader.u8();
        const std::size_t run = (control & 0x7Fu) + 1u;
        for (std::size_t i = 0; i < run && points.size() < count; ++i) {
            last =
                static_cast<std::uint16_t>(last + ((control & 0x80u) ? reader.u16() : reader.u8()));
            points.push_back(last);
        }
    }
    if (all) {
        points.resize(total);
        for (std::size_t i = 0; i < total; ++i) {
            points[i] = static_cast<std::uint16_t>(i);
        }
    }
    return points;
}

std::vector<double> packedDeltas(BinaryReader &reader, std::size_t count) {
    std::vector<double> deltas;
    deltas.reserve(count);
    while (deltas.size() < count && reader.ok()) {
        const std::uint8_t control = reader.u8();
        const std::size_t run = (control & 0x3Fu) + 1u;
        for (std::size_t i = 0; i < run && deltas.size() < count; ++i) {
            if (control & 0x80u) {
                deltas.push_back(0);
            } else if (control & 0x40u) {
                deltas.push_back(reader.i16());
            } else {
                deltas.push_back(static_cast<std::int8_t>(reader.u8()));
            }
        }
    }
    deltas.resize(count, 0);
    return deltas;
}

double tupleScalar(std::span<const double> coords, std::span<const double> peak,
                   std::span<const double> start, std::span<const double> end, bool intermediate) {
    double scalar = 1;
    for (std::size_t a = 0; a < coords.size() && a < peak.size(); ++a) {
        const double p = peak[a];
        const double c = coords[a];
        if (p == 0) {
            continue;
        }
        if (c == 0) {
            return 0;
        }
        if (intermediate) {
            const double s = start[a];
            const double e = end[a];
            if (c < s || c > e) {
                return 0;
            }
            scalar *= c < p ? (c - s) / (p - s) : (c > p ? (e - c) / (e - p) : 1);
        } else {
            if (c < std::min(0.0, p) || c > std::max(0.0, p)) {
                return 0;
            }
            scalar *= c / p;
        }
    }
    return scalar;
}

void inferDeltas(std::span<const double> originalX, std::span<const double> originalY,
                 std::span<const std::uint16_t> ends, const std::vector<bool> &touched,
                 std::vector<double> &dx, std::vector<double> &dy) {
    std::size_t begin = 0;
    for (const std::uint16_t endIndex : ends) {
        const std::size_t end = static_cast<std::size_t>(endIndex) + 1;
        std::vector<std::size_t> refs;
        for (std::size_t i = begin; i < end; ++i) {
            if (touched[i]) {
                refs.push_back(i);
            }
        }
        if (!refs.empty() && refs.size() < end - begin) {
            for (std::size_t r = 0; r < refs.size(); ++r) {
                const std::size_t a = refs[r];
                const std::size_t b = refs[(r + 1) % refs.size()];
                for (std::size_t i = a + 1;; ++i) {
                    if (i == end) {
                        i = begin;
                    }
                    if (i == b) {
                        break;
                    }
                    const auto interpolate = [&](std::span<const double> original,
                                                 std::vector<double> &delta) {
                        const double pa = original[a];
                        const double pb = original[b];
                        const double p = original[i];
                        const double lo = std::min(pa, pb);
                        const double hi = std::max(pa, pb);
                        const double dlo = pa <= pb ? delta[a] : delta[b];
                        const double dhi = pa <= pb ? delta[b] : delta[a];
                        if (p <= lo) {
                            return dlo;
                        }
                        if (p >= hi) {
                            return dhi;
                        }
                        return dlo + (p - lo) / (hi - lo) * (dhi - dlo);
                    };
                    dx[i] = interpolate(originalX, dx);
                    dy[i] = interpolate(originalY, dy);
                }
            }
        }
        begin = end;
    }
}
}

std::span<const VariationAxis> FontFile::axes() const noexcept { return axes_; }

bool FontFile::loadVariations(std::size_t fvar, std::size_t gvar, std::size_t avar) {
    if (fvar == 0 || gvar == 0) {
        return true;
    }
    BinaryReader reader(bytes_);
    reader.seek(fvar + 4);
    const std::uint16_t axesOffset = reader.u16();
    reader.skip(2);
    const std::uint16_t axisCount = reader.u16();
    const std::uint16_t axisSize = reader.u16();
    for (std::uint16_t a = 0; a < axisCount; ++a) {
        reader.seek(fvar + axesOffset + static_cast<std::size_t>(a) * axisSize);
        VariationAxis axis{};
        axis.tag = reader.u32();
        axis.minimum = fixed(reader);
        axis.defaultValue = fixed(reader);
        axis.maximum = fixed(reader);
        axes_.push_back(axis);
    }
    if (avar != 0) {
        reader.seek(avar + 6);
        const std::uint16_t count = reader.u16();
        for (std::uint16_t a = 0; a < count && a < axes_.size(); ++a) {
            const std::uint16_t pairs = reader.u16();
            std::vector<std::pair<double, double>> map;
            for (std::uint16_t p = 0; p < pairs; ++p) {
                const double from = f2dot14(reader);
                const double to = f2dot14(reader);
                map.emplace_back(from, to);
            }
            segmentMaps_.push_back(std::move(map));
        }
    }
    tables_.gvar = gvar;
    coords_.assign(axes_.size(), 0.0);
    return reader.ok();
}

bool FontFile::setVariation(std::span<const std::pair<std::uint32_t, double>> settings) {
    if (axes_.empty()) {
        return false;
    }
    variation_.assign(settings.begin(), settings.end());
    native_.reset();
    nativeTried_ = false;
    for (std::size_t a = 0; a < axes_.size(); ++a) {
        const VariationAxis &axis = axes_[a];
        double value = axis.defaultValue;
        for (const auto &[name, requested] : settings) {
            if (name == axis.tag) {
                value = std::clamp(requested, axis.minimum, axis.maximum);
            }
        }
        double normalized = 0;
        if (value < axis.defaultValue && axis.defaultValue > axis.minimum) {
            normalized = (value - axis.defaultValue) / (axis.defaultValue - axis.minimum);
        } else if (value > axis.defaultValue && axis.maximum > axis.defaultValue) {
            normalized = (value - axis.defaultValue) / (axis.maximum - axis.defaultValue);
        }
        if (a < segmentMaps_.size() && segmentMaps_[a].size() >= 2) {
            const auto &map = segmentMaps_[a];
            for (std::size_t k = 1; k < map.size(); ++k) {
                if (normalized <= map[k].first) {
                    const auto [x0, y0] = map[k - 1];
                    const auto [x1, y1] = map[k];
                    normalized = x1 > x0 ? y0 + (normalized - x0) / (x1 - x0) * (y1 - y0) : y1;
                    break;
                }
            }
        }
        coords_[a] = std::round(normalized * 16384) / 16384;
    }
    return true;
}

bool FontFile::varied() const noexcept {
    return std::any_of(coords_.begin(), coords_.end(), [](double c) { return c != 0; });
}

void FontFile::glyphDeltas(std::uint16_t glyph, std::span<const double> x,
                           std::span<const double> y, std::span<const std::uint16_t> ends,
                           std::vector<double> &dx, std::vector<double> &dy) const {
    const std::size_t total = x.size();
    dx.assign(total, 0);
    dy.assign(total, 0);
    if (!varied() || tables_.gvar == 0) {
        return;
    }
    BinaryReader reader(bytes_);
    const std::size_t g = tables_.gvar;
    reader.seek(g + 4);
    const std::uint16_t axisCount = reader.u16();
    const std::uint16_t sharedCount = reader.u16();
    const std::size_t sharedOffset = g + reader.u32();
    const std::uint16_t glyphs = reader.u16();
    const std::uint16_t flags = reader.u16();
    const std::size_t dataOffset = g + reader.u32();
    if (glyph >= glyphs || axisCount != axes_.size()) {
        return;
    }
    std::size_t start = 0;
    std::size_t stop = 0;
    if (flags & 1u) {
        reader.seek(g + 20 + 4 * static_cast<std::size_t>(glyph));
        start = reader.u32();
        stop = reader.u32();
    } else {
        reader.seek(g + 20 + 2 * static_cast<std::size_t>(glyph));
        start = 2u * reader.u16();
        stop = 2u * reader.u16();
    }
    if (stop <= start) {
        return;
    }
    const std::size_t base = dataOffset + start;
    reader.seek(base);
    const std::uint16_t countWord = reader.u16();
    const std::size_t serialized = base + reader.u16();
    const std::size_t tuples = countWord & 0x0FFFu;
    BinaryReader data(bytes_, serialized);
    bool sharedAll = false;
    std::vector<std::uint16_t> sharedPoints;
    if (countWord & 0x8000u) {
        sharedPoints = packedPoints(data, total, sharedAll);
    }
    std::vector<double> peak(axisCount);
    std::vector<double> lo(axisCount);
    std::vector<double> hi(axisCount);
    for (std::size_t t = 0; t < tuples && reader.ok(); ++t) {
        const std::uint16_t size = reader.u16();
        const std::uint16_t index = reader.u16();
        if (index & 0x8000u) {
            for (auto &p : peak) {
                p = f2dot14(reader);
            }
        } else {
            const std::size_t resume = reader.offset();
            reader.seek(sharedOffset + 2u * axisCount * (index & 0x0FFFu));
            for (auto &p : peak) {
                p = f2dot14(reader);
            }
            reader.seek(resume);
            if ((index & 0x0FFFu) >= sharedCount) {
                return;
            }
        }
        const bool intermediate = (index & 0x4000u) != 0;
        if (intermediate) {
            for (auto &v : lo) {
                v = f2dot14(reader);
            }
            for (auto &v : hi) {
                v = f2dot14(reader);
            }
        }
        const std::size_t next = data.offset() + size;
        const double scalar = tupleScalar(coords_, peak, lo, hi, intermediate);
        if (scalar != 0) {
            bool all = sharedAll;
            std::vector<std::uint16_t> points = sharedPoints;
            if (index & 0x2000u) {
                points = packedPoints(data, total, all);
            }
            const std::vector<double> tx = packedDeltas(data, points.size());
            const std::vector<double> ty = packedDeltas(data, points.size());
            std::vector<double> ux(total, 0);
            std::vector<double> uy(total, 0);
            std::vector<bool> touched(total, false);
            for (std::size_t i = 0; i < points.size(); ++i) {
                if (points[i] < total) {
                    ux[points[i]] += tx[i];
                    uy[points[i]] += ty[i];
                    touched[points[i]] = true;
                }
            }
            if (!all && !ends.empty()) {
                inferDeltas(x, y, ends, touched, ux, uy);
            }
            for (std::size_t i = 0; i < total; ++i) {
                dx[i] += ux[i] * scalar;
                dy[i] += uy[i] * scalar;
            }
        }
        data.seek(next);
    }
}
}
