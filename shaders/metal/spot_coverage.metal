struct CoveragePolygon {
    float2 points[16];
    uint count;
};
float coverageCross(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
float coveragePolygonArea(thread const CoveragePolygon &p) {
    float result = 0.0f;
    for (uint i = 0; i < p.count; ++i) {
        result += coverageCross(p.points[i], p.points[(i + 1u) % p.count]);
    }
    return abs(result) * 0.5f;
}
CoveragePolygon coverageClip(thread const CoveragePolygon &p, uint axis, float boundary, bool greater) {
    CoveragePolygon result;
    result.count = 0;
    for (uint i = 0; i < p.count; ++i) {
        float2 a = p.points[i], b = p.points[(i + 1u) % p.count];
        float ca = a[axis] - boundary, cb = b[axis] - boundary;
        bool insideA = greater ? ca >= 0.0f : ca <= 0.0f;
        bool insideB = greater ? cb >= 0.0f : cb <= 0.0f;
        if (insideA) { result.points[result.count++] = a; }
        if (insideA != insideB) { result.points[result.count++] = mix(a, b, ca / (ca - cb)); }
    }
    return result;
}
CoveragePolygon coverageBox(thread const CoveragePolygon &p, float2 low, float2 high) {
    CoveragePolygon a = coverageClip(p, 0, low.x, true);
    CoveragePolygon b = coverageClip(a, 0, high.x, false);
    CoveragePolygon c = coverageClip(b, 1, low.y, true);
    return coverageClip(c, 1, high.y, false);
}
float coverageDisk(thread const CoveragePolygon &p, float2 center, float radius) {
    float area = 0.0f, rr = radius * radius;
    for (uint i = 0; i < p.count; ++i) {
        float2 a = p.points[i] - center, b = p.points[(i + 1u) % p.count] - center, delta = b - a;
        float aa = dot(delta, delta), bb = 2.0f * dot(a, delta), cc = dot(a, a) - rr;
        float cuts[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        uint count = 2;
        float discriminant = bb * bb - 4.0f * aa * cc;
        if (aa > 0.0f && discriminant > 0.0f) {
            float root = sqrt(discriminant);
            float t0 = (-bb - root) / (2.0f * aa), t1 = (-bb + root) / (2.0f * aa);
            if (t0 > 0.0f && t0 < 1.0f) { cuts[count++] = t0; }
            if (t1 > 0.0f && t1 < 1.0f) { cuts[count++] = t1; }
        }
        for (uint j = 1; j < count; ++j) {
            for (uint k = j; k > 0 && cuts[k] < cuts[k - 1]; --k) {
                float value = cuts[k]; cuts[k] = cuts[k - 1]; cuts[k - 1] = value;
            }
        }
        for (uint j = 1; j < count; ++j) {
            float2 u = a + delta * cuts[j - 1], v = a + delta * cuts[j], midpoint = (u + v) * 0.5f;
            area += dot(midpoint, midpoint) <= rr ? coverageCross(u, v) * 0.5f :
                    rr * atan2(coverageCross(u, v), dot(u, v)) * 0.5f;
        }
    }
    return abs(area);
}
float roundedSpotPixelArea(thread const CoveragePolygon &pixel, float2 halfSize, float radius) {
    float r = clamp(radius, 0.0f, min(halfSize.x, halfSize.y));
    if (r == 0.0f) {
        CoveragePolygon box = coverageBox(pixel, -halfSize, halfSize);
        return coveragePolygonArea(box);
    }
    CoveragePolygon middle = coverageBox(pixel, float2(-halfSize.x + r, -halfSize.y), float2(halfSize.x - r, halfSize.y));
    float area = coveragePolygonArea(middle);
    for (int side = -1; side <= 1; side += 2) {
        float x0 = side < 0 ? -halfSize.x : halfSize.x - r;
        float x1 = side < 0 ? -halfSize.x + r : halfSize.x;
        CoveragePolygon strip = coverageBox(pixel, float2(x0, -halfSize.y + r), float2(x1, halfSize.y - r));
        area += coveragePolygonArea(strip);
        for (int end = -1; end <= 1; end += 2) {
            float y0 = end < 0 ? -halfSize.y : halfSize.y - r;
            float y1 = end < 0 ? -halfSize.y + r : halfSize.y;
            CoveragePolygon quarter = coverageBox(pixel, float2(x0, y0), float2(x1, y1));
            area += coverageDisk(quarter, float2(float(side) * (halfSize.x - r), float(end) * (halfSize.y - r)), r);
        }
    }
    return area;
}
