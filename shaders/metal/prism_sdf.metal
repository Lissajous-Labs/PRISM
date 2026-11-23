float sdRoundedRect(float2 p, float2 half_, float4 radii) {
    float radius = p.x > 0.0f ? (p.y > 0.0f ? radii.y : radii.x) : (p.y > 0.0f ? radii.w : radii.z);
    float2 q = abs(p) - half_ + float2(radius);
    return min(max(q.x, q.y), 0.0f) + fast::length(max(q, float2(0.0f))) - radius;
}

float2 sdRoundedRectGradient(float2 p, float2 half_, float4 radii) {
    float radius = p.x > 0.0f ? (p.y > 0.0f ? radii.y : radii.x) : (p.y > 0.0f ? radii.w : radii.z);
    float2 side = float2(p.x > 0.0f ? 1.0f : -1.0f, p.y > 0.0f ? 1.0f : -1.0f);
    float2 q = abs(p) - half_ + float2(radius);
    if (q.x > 0.0f && q.y > 0.0f) {
        return side * fast::normalize(q);
    }
    return q.x > q.y ? float2(side.x, 0.0f) : float2(0.0f, side.y);
}

float sdSegment(float2 p, float2 a, float2 b) {
    float2 edge = b - a;
    float lengthSquared = dot(edge, edge);
    float t = lengthSquared > 0.0f ? clamp(dot(p - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    return length(p - (a + edge * t));
}

struct FieldSample {
    float distance;
    float2 gradient;
};

FieldSample sampleCapsule(float2 p, float2 a, float2 b, float radius) {
    float2 edge = b - a;
    float lengthSquared = dot(edge, edge);
    float t = lengthSquared > 0.0f ? clamp(dot(p - a, edge) / lengthSquared, 0.0f, 1.0f) : 0.0f;
    float2 delta = p - (a + edge * t);
    float norm = fast::length(delta);
    FieldSample result;
    result.distance = norm - radius;
    result.gradient = norm > 0.0f ? delta * fast::divide(1.0f, norm) : float2(0.0f);
    return result;
}

FieldSample sampleSquircle(float2 p, float2 half_, float power) {
    float2 uv = abs(p) / half_;
    float su = pow(uv.x, power);
    float sv = pow(uv.y, power);
    float sum = su + sv;
    FieldSample result;
    result.distance = -min(half_.x, half_.y);
    result.gradient = float2(0.0f);
    if (!(sum > 0.0f)) {
        return result;
    }
    float radial = pow(sum, 1.0f / power);
    float scale = radial / sum;
    float2 gradient = float2(uv.x > 0.0f ? copysign(scale * su / (uv.x * half_.x), p.x) : 0.0f,
                             uv.y > 0.0f ? copysign(scale * sv / (uv.y * half_.y), p.y) : 0.0f);
    float norm = fast::length(gradient);
    if (!(norm > 0.0f)) {
        return result;
    }
    result.distance = (radial - 1.0f) / norm;
    result.gradient = gradient / norm;
    return result;
}

FieldSample sampleCornerSquircle(float2 p, float2 half_, float power, float radius) {
    float r = clamp(radius, 0.0f, min(half_.x, half_.y));
    if (!(r > 0.0f)) {
        return sampleSquircle(p, half_, power);
    }
    float2 s = float2(p.x < 0.0f ? -1.0f : 1.0f, p.y < 0.0f ? -1.0f : 1.0f);
    float2 q = abs(p) - (half_ - r);
    FieldSample result;
    if (q.x > 0.0f && q.y > 0.0f) {
        result = sampleSquircle(q, float2(r), power);
        result.gradient *= s;
        return result;
    }
    if (q.x >= q.y) {
        result.distance = q.x - r;
        result.gradient = float2(s.x, 0.0f);
    } else {
        result.distance = q.y - r;
        result.gradient = float2(0.0f, s.y);
    }
    return result;
}

float crossProduct(float2 a, float2 b) {
    return a.x * b.y - a.y * b.x;
}

float sdTriangle(float2 p, float2 a, float2 b, float2 c) {
    float orientation = crossProduct(b - a, c - a) < 0.0f ? -1.0f : 1.0f;
    float distance = min(sdSegment(p, a, b), min(sdSegment(p, b, c), sdSegment(p, c, a)));
    bool inside = orientation * crossProduct(b - a, p - a) >= 0.0f &&
                  orientation * crossProduct(c - b, p - b) >= 0.0f &&
                  orientation * crossProduct(a - c, p - c) >= 0.0f;
    return inside ? -distance : distance;
}

float sdRoundedTriangle(float2 p, float2 a, float2 b, float2 c, float radius) {
    float sideA = length(c - b);
    float sideB = length(a - c);
    float sideC = length(b - a);
    float perimeter = sideA + sideB + sideC;
    float area = abs(crossProduct(b - a, c - a)) * 0.5f;
    if (!(perimeter > 0.0f) || !(area > 0.0f)) {
        return sdTriangle(p, a, b, c);
    }
    float2 incenter = (a * sideA + b * sideB + c * sideC) / perimeter;
    float inradius = 2.0f * area / perimeter;
    float rounded = clamp(radius, 0.0f, inradius);
    float scale = (inradius - rounded) / inradius;
    return sdTriangle(p, incenter + (a - incenter) * scale, incenter + (b - incenter) * scale,
                      incenter + (c - incenter) * scale) - rounded;
}

float2 superellipsePiece(float2 half_, float power, float cut, bool swapped, float t) {
    float u = t * cut;
    float v = pow(max(1.0f - pow(u, power), 0.0f), 1.0f / power);
    return swapped ? float2(half_.x * v, half_.y * u) : float2(half_.x * u, half_.y * v);
}

float superellipsePieceDistance(float2 q, float2 half_, float power, float cut, bool swapped) {
    float best = 1e30f;
    float bestT = 0.0f;
    for (int i = 0; i <= 32; ++i) {
        float t = float(i) / 32.0f;
        float d = length(superellipsePiece(half_, power, cut, swapped, t) - q);
        if (d < best) {
            best = d;
            bestT = t;
        }
    }
    float low = max(0.0f, bestT - 1.0f / 32.0f);
    float high = min(1.0f, bestT + 1.0f / 32.0f);
    for (int i = 0; i < 20; ++i) {
        float first = high - 0.618034f * (high - low);
        float second = low + 0.618034f * (high - low);
        if (length(superellipsePiece(half_, power, cut, swapped, first) - q) <
            length(superellipsePiece(half_, power, cut, swapped, second) - q)) {
            high = second;
        } else {
            low = first;
        }
    }
    return min(best, length(superellipsePiece(half_, power, cut, swapped, (low + high) * 0.5f) - q));
}

float sdSuperellipse(float2 p, float2 half_, float power) {
    float2 q = abs(p);
    float radial = pow(pow(q.x / half_.x, power) + pow(q.y / half_.y, power), 1.0f / power);
    if (!(radial > 0.0f)) {
        return -min(half_.x, half_.y);
    }
    float cut = pow(0.5f, 1.0f / power);
    float distance = min(superellipsePieceDistance(q, half_, power, cut, false),
                         superellipsePieceDistance(q, half_, power, cut, true));
    return radial < 1.0f ? -distance : distance;
}

float signedCbrt(float value) {
    return copysign(pow(abs(value), 1.0f / 3.0f), value);
}

float2 bezierPoint(float2 p0, float2 p1, float2 p2, float t) {
    float s = 1.0f - t;
    return p0 * (s * s) + p1 * (2.0f * s * t) + p2 * (t * t);
}

float refineBezier(float2 p, float2 p0, float2 p1, float2 p2, float t) {
    float2 a = p1 - p0;
    float2 b = p0 - p1 * 2.0f + p2;
    for (int i = 0; i < 4; ++i) {
        float2 offset = bezierPoint(p0, p1, p2, t) - p;
        float2 tangent = (a + b * t) * 2.0f;
        float slope = dot(tangent, tangent) + dot(offset, b * 2.0f);
        if (!(slope > 0.0f)) {
            break;
        }
        t = clamp(t - dot(offset, tangent) / slope, 0.0f, 1.0f);
    }
    return t;
}

float sdBezier(float2 p, float2 p0, float2 p1, float2 p2) {
    float2 a = p1 - p0;
    float2 b = p0 - p1 * 2.0f + p2;
    float2 d = p0 - p;
    float curvature = dot(b, b);
    float speed = dot(a, a);
    float candidates[5] = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    int count = 2;
    if (curvature <= 1e-6f * max(speed, 1e-6f)) {
        float2 chord = p2 - p0;
        candidates[count++] = clamp(dot(p - p0, chord) / max(dot(chord, chord), 1e-20f), 0.0f, 1.0f);
    } else {
        float kx = dot(a, b) / curvature;
        float ky = (2.0f * speed + dot(d, b)) / (3.0f * curvature);
        float kz = dot(d, a) / curvature;
        float pc = ky - kx * kx;
        float qc = kx * (2.0f * kx * kx - 3.0f * ky) + kz;
        float discriminant = qc * qc + 4.0f * pc * pc * pc;
        if (discriminant >= 0.0f) {
            float root = sqrt(discriminant);
            candidates[count++] = signedCbrt((root - qc) * 0.5f) + signedCbrt((-root - qc) * 0.5f) - kx;
        } else {
            float z = sqrt(-pc);
            float theta = acos(clamp(qc / (2.0f * pc * z), -1.0f, 1.0f)) / 3.0f;
            for (int k = 0; k < 3; ++k) {
                candidates[count++] = 2.0f * z * cos(theta - 2.0943951f * float(k)) - kx;
            }
        }
    }
    float best = length(p0 - p);
    for (int i = 0; i < count; ++i) {
        float t = refineBezier(p, p0, p1, p2, clamp(candidates[i], 0.0f, 1.0f));
        best = min(best, length(bezierPoint(p0, p1, p2, t) - p));
    }
    return best;
}

struct ArcSample {
    float distance;
    float2 gradient;
};

float4 arcBasis(float midAngle, float halfAperture) {
    float turn = 1.5707963f - midAngle;
    return float4(cos(turn), sin(turn), sin(halfAperture), cos(halfAperture));
}

ArcSample sampleArc(float2 p, float2 center, float radius, float thickness, float4 basis,
                    uint cap) {
    float c = basis.x;
    float s = basis.y;
    float2 local = p - center;
    float foldedX = c * local.x - s * local.y;
    float2 q = float2(abs(foldedX), s * local.x + c * local.y);
    float2 endPoint = basis.zw;
    float halfWidth = thickness * 0.5f;
    ArcSample result;
    if (endPoint.y * q.x <= endPoint.x * q.y) {
        float norm = fast::length(q);
        float radial = norm - radius;
        result.distance = abs(radial) - halfWidth;
        float side = radial > 0.0f ? 1.0f : (radial < 0.0f ? -1.0f : 0.0f);
        result.gradient = norm > 0.0f ? q * fast::divide(side, norm) : float2(0.0f);
    } else if (cap == 1u || cap == 2u) {
        float2 frame = float2(q.x * endPoint.y - q.y * endPoint.x,
                              q.x * endPoint.x + q.y * endPoint.y - radius);
        float2 halfExtent = float2(cap == 1u ? 0.0f : halfWidth, halfWidth);
        result.distance = sdRoundedRect(frame, halfExtent, float4(0.0f));
        float2 capGradient = sdRoundedRectGradient(frame, halfExtent, float4(0.0f));
        result.gradient = float2(endPoint.y * capGradient.x + endPoint.x * capGradient.y,
                                 -endPoint.x * capGradient.x + endPoint.y * capGradient.y);
    } else {
        float2 delta = q - endPoint * radius;
        float norm = fast::length(delta);
        result.distance = norm - halfWidth;
        result.gradient = norm > 0.0f ? delta * fast::divide(1.0f, norm) : float2(0.0f);
    }
    result.gradient.x *= foldedX > 0.0f ? 1.0f : (foldedX < 0.0f ? -1.0f : 0.0f);
    result.gradient = float2(c * result.gradient.x + s * result.gradient.y,
                             -s * result.gradient.x + c * result.gradient.y);
    return result;
}

float sdArc(float2 p, float2 center, float radius, float thickness, float midAngle,
            float halfAperture, uint cap) {
    return sampleArc(p, center, radius, thickness, arcBasis(midAngle, halfAperture), cap).distance;
}

float erfInterval(float center, float halfWidth, float sigma) {
    float scale = 1.0f / (1.41421356f * sigma);
    return 0.5f * (erfApprox((center + halfWidth) * scale) - erfApprox((center - halfWidth) * scale));
}

constant float legendreNodes[6] = {-0.9324695142f, -0.6612093865f, -0.2386191861f, 0.2386191861f, 0.6612093865f, 0.9324695142f};
constant float legendreWeights[6] = {0.1713244924f, 0.3607615730f, 0.4679139346f, 0.4679139346f, 0.3607615730f, 0.1713244924f};

int panelsFor(float extent, float sigma) {
    return clamp(int(ceil(extent / (3.0f * sigma))), 1, 8);
}

float shadowDensity(float py, float y, float sigma) {
    float offset = py - y;
    return exp(-offset * offset / (2.0f * sigma * sigma)) / (2.50662827f * sigma);
}

float roundedBoxShadow(float2 p, float2 half_, float radius, float sigma) {
    radius = clamp(radius, 0.0f, min(half_.x, half_.y));
    if (!(sigma > 0.0f)) {
        return abs(p.x) <= half_.x && abs(p.y) <= half_.y ? 1.0f : 0.0f;
    }
    if (radius == 0.0f) {
        return erfInterval(p.x, half_.x, sigma) * erfInterval(p.y, half_.y, sigma);
    }
    float reach = 4.5f * sigma;
    float low = max(-half_.y, p.y - reach);
    float high = min(half_.y, p.y + reach);
    if (!(high > low)) {
        return 0.0f;
    }
    float straight = half_.y - radius;
    float total = 0.0f;
    float middleLow = max(low, -straight);
    float middleHigh = min(high, straight);
    if (middleHigh > middleLow) {
        total += erfInterval(p.x, half_.x, sigma) *
                 erfInterval(p.y - 0.5f * (middleLow + middleHigh), 0.5f * (middleHigh - middleLow), sigma);
    }
    for (int sideIndex = 0; sideIndex < 2; ++sideIndex) {
        float side = sideIndex == 0 ? 1.0f : -1.0f;
        float nearEdge = max(straight, side > 0.0f ? low : -high);
        float farEdge = min(half_.y, side > 0.0f ? high : -low);
        if (!(farEdge > nearEdge)) {
            continue;
        }
        float start = asin(clamp((nearEdge - straight) / radius, 0.0f, 1.0f));
        float end = asin(clamp((farEdge - straight) / radius, 0.0f, 1.0f));
        int panels = panelsFor(radius * (end - start), sigma);
        float panelRadius = (end - start) / (2.0f * float(panels));
        float sum = 0.0f;
        for (int panel = 0; panel < panels; ++panel) {
            float middle = start + float(2 * panel + 1) * panelRadius;
            for (int k = 0; k < 6; ++k) {
                float angle = middle + panelRadius * legendreNodes[k];
                float y = side * (straight + radius * sin(angle));
                float slice = half_.x - radius + radius * cos(angle);
                sum += legendreWeights[k] * shadowDensity(p.y, y, sigma) * erfInterval(p.x, slice, sigma) * radius * cos(angle);
            }
        }
        total += sum * panelRadius;
    }
    return total;
}

uint2 spriteOrigin(ShadowSprite s) { return uint2(s.origin & 0xFFFFu, s.origin >> 16); }

kernel void shadowSprites(device const ShadowSprite *table [[buffer(0)]],
                          texture2d<float, access::write> atlas [[texture(0)]],
                          constant uint &index [[buffer(2)]],
                          uint2 gid [[thread_position_in_grid]]) {
    ShadowSprite s = table[index];
    if (gid.x >= s.width || gid.y >= s.height) {
        return;
    }
    atlas.write(float4(roundedBoxShadow(float2(gid) * s.step, s.half_, s.radius, s.sigma)),
                spriteOrigin(s) + gid);
}

float sampleShadowSprite(device const ShadowSprite *table, texture2d<float> atlas, uint index,
                         float2 p) {
    constexpr sampler spriteSampler(coord::pixel, address::clamp_to_edge, filter::linear);
    ShadowSprite s = table[index];
    float2 q = fast::divide(abs(p), float2(s.step));
    if (q.x >= float(s.width - 1u) || q.y >= float(s.height - 1u)) {
        return 0.0f;
    }
    return atlas.sample(spriteSampler, float2(spriteOrigin(s)) + q + 0.5f).x;
}
