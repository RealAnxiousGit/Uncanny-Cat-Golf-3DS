// Circle movement against the level's static shapes, following the steps of
// Godot 4's PhysicsServer2D body_test_motion (recovery, cast, rest info), so the
// cat bounces the way it does in the original game.
#include <stddef.h>

#include "ucg.h"

#define SAFE_MARGIN 0.08f            // CharacterBody2D.safe_margin default
#define MIN_CONTACT_DEPTH (SAFE_MARGIN * 0.05f)
#define RECOVER_STEPS 4
#define CAST_STEPS 8

static Shape *dyn_shapes;
static int dyn_n;
static int exclude_idx = -1;

void phys_set_dynamic(Shape *shapes, int n) {
    dyn_shapes = shapes;
    dyn_n = n;
}

void phys_set_exclude(int shape_index) { exclude_idx = shape_index; }

static inline int total_shapes(const LevelDef *lv) { return lv->nshapes + dyn_n; }

// shape i, or NULL when it's switched off / excluded
static inline const Shape *shape_at(const LevelDef *lv, int i, uint32_t mask) {
    if (i == exclude_idx) return NULL;
    const Shape *s = i < lv->nshapes ? &lv->shapes[i] : &dyn_shapes[i - lv->nshapes];
    return (s->layers & mask) ? s : NULL;
}

static vec2 closest_on_segment(vec2 p, vec2 a, vec2 b) {
    vec2 ab = v2sub(b, a);
    float l2 = v2len2(ab);
    float t = l2 > 0 ? clampf(v2dot(v2sub(p, a), ab) / l2, 0, 1) : 0;
    return v2add(a, v2mul(ab, t));
}

static bool point_in_poly(const Shape *s, vec2 p) {
    bool in = false;
    for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        vec2 a = s->pts[i], b = s->pts[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

// Closest point on the shape's outline to p; *inside tells whether p is inside.
static vec2 closest_point(const Shape *s, vec2 p, bool *inside) {
    if (s->kind == SHAPE_CIRCLE) {
        vec2 d = v2sub(p, s->c);
        float l = v2len(d);
        *inside = l < s->r;
        if (l < 1e-6f) return v2add(s->c, v2(s->r, 0));
        return v2add(s->c, v2mul(d, s->r / l));
    }
    vec2 best = s->pts[0];
    float bd = 1e30f;
    for (int i = 0, j = s->n - 1; i < s->n; j = i++) {
        vec2 c = closest_on_segment(p, s->pts[j], s->pts[i]);
        float d = v2len2(v2sub(p, c));
        if (d < bd) { bd = d; best = c; }
    }
    *inside = point_in_poly(s, p);
    return best;
}

// Signed separation between circle (p, r) and shape: < 0 means overlap.
// Also returns the contact normal (pointing from the shape towards the circle).
static float separation(const Shape *s, vec2 p, float r, vec2 *normal) {
    bool inside;
    vec2 c = closest_point(s, p, &inside);
    vec2 d = v2sub(p, c);
    float l = v2len(d);
    vec2 n = l > 1e-6f ? v2mul(d, 1.0f / l) : v2(0, -1);
    if (inside) {
        n = v2mul(n, -1);
        l = -l;
    }
    if (normal) *normal = n;
    return l - r;
}

static bool seg_seg_intersect(vec2 a, vec2 b, vec2 c, vec2 d) {
    float d1 = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    float d2 = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
    float d3 = (d.x - c.x) * (a.y - c.y) - (d.y - c.y) * (a.x - c.x);
    float d4 = (d.x - c.x) * (b.y - c.y) - (d.y - c.y) * (b.x - c.x);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

static float seg_seg_dist2(vec2 a, vec2 b, vec2 c, vec2 d) {
    if (seg_seg_intersect(a, b, c, d)) return 0;
    float m = v2len2(v2sub(a, closest_on_segment(a, c, d)));
    float t = v2len2(v2sub(b, closest_on_segment(b, c, d)));
    if (t < m) m = t;
    t = v2len2(v2sub(c, closest_on_segment(c, a, b)));
    if (t < m) m = t;
    t = v2len2(v2sub(d, closest_on_segment(d, a, b)));
    if (t < m) m = t;
    return m;
}

// Does a circle swept from a to b touch the shape?
static bool sweep_hits(const Shape *s, vec2 a, vec2 b, float r) {
    if (s->kind == SHAPE_CIRCLE) {
        vec2 c = closest_on_segment(s->c, a, b);
        float rr = r + s->r;
        return v2len2(v2sub(c, s->c)) < rr * rr;
    }
    if (separation(s, a, r, NULL) < 0) return true;
    if (point_in_poly(s, b)) return true;
    float r2 = r * r;
    for (int i = 0, j = s->n - 1; i < s->n; j = i++)
        if (seg_seg_dist2(a, b, s->pts[j], s->pts[i]) < r2) return true;
    return false;
}

static bool box_overlap(const Shape *s, float x0, float y0, float x1, float y1) {
    return !(s->x1 < x0 || s->x0 > x1 || s->y1 < y0 || s->y0 > y1);
}

Collision phys_move_circle(const LevelDef *lv, vec2 *pos, float r, vec2 motion, uint32_t mask) {
    Collision col = {false, {0, 0}, -1, 1.0f};
    vec2 p = *pos;

    // 1. recovery: push out of anything we're sinking into
    float reach = r + SAFE_MARGIN;
    for (int it = 0; it < RECOVER_STEPS; it++) {
        bool any = false;
        vec2 rec = v2(0, 0);
        for (int i = 0; i < total_shapes(lv); i++) {
            const Shape *s = shape_at(lv, i, mask);
            if (!s || !box_overlap(s, p.x - reach, p.y - reach, p.x + reach, p.y + reach)) continue;
            vec2 n;
            float depth = -separation(s, v2add(p, rec), reach, &n);
            if (depth > MIN_CONTACT_DEPTH) {
                rec = v2add(rec, v2mul(n, (depth - MIN_CONTACT_DEPTH) * 0.4f));
                any = true;
            }
        }
        p = v2add(p, rec);
        if (!any) break;
    }

    // 2. cast: find how far we can go
    float safe = 1.0f, unsafe = 1.0f;
    int best = -1;
    if (v2len2(motion) > 0) {
        vec2 end = v2add(p, motion);
        float bx0 = fminf(p.x, end.x) - r, by0 = fminf(p.y, end.y) - r;
        float bx1 = fmaxf(p.x, end.x) + r, by1 = fmaxf(p.y, end.y) + r;
        for (int i = 0; i < total_shapes(lv); i++) {
            const Shape *s = shape_at(lv, i, mask);
            if (!s || !box_overlap(s, bx0, by0, bx1, by1)) continue;
            if (!sweep_hits(s, p, end, r)) continue;
            if (separation(s, p, r, NULL) < 0) { // stuck
                safe = unsafe = 0;
                best = i;
                break;
            }
            float lo = 0, hi = 1;
            for (int k = 0; k < CAST_STEPS; k++) {
                float f = (lo + hi) * 0.5f;
                if (separation(s, v2add(p, v2mul(motion, f)), r, NULL) < 0 ||
                    sweep_hits(s, p, v2add(p, v2mul(motion, f)), r))
                    hi = f;
                else
                    lo = f;
            }
            if (lo < safe) {
                safe = lo;
                unsafe = hi;
                best = i;
            }
        }
    }

    // 3. rest info at the unsafe point: the deepest contact gives the normal
    if (best >= 0) {
        vec2 at = v2add(p, v2mul(motion, unsafe));
        float deepest = -1e30f;
        vec2 n = v2(0, -1);
        int who = best;
        for (int i = 0; i < total_shapes(lv); i++) {
            const Shape *s = shape_at(lv, i, mask);
            if (!s || !box_overlap(s, at.x - reach, at.y - reach, at.x + reach, at.y + reach)) continue;
            vec2 nn;
            float depth = -separation(s, at, reach, &nn);
            if (depth > deepest && depth > 0) {
                deepest = depth;
                n = nn;
                who = i;
            }
        }
        col.hit = true;
        col.normal = n;
        col.shape = who;
        col.travel_frac = safe;
        p = v2add(p, v2mul(motion, safe));
    } else {
        p = v2add(p, motion);
    }
    *pos = p;
    return col;
}

bool phys_point_solid(const LevelDef *lv, vec2 p, uint32_t mask) {
    for (int i = 0; i < total_shapes(lv); i++) {
        const Shape *s = shape_at(lv, i, mask);
        if (!s || p.x < s->x0 || p.x > s->x1 || p.y < s->y0 || p.y > s->y1) continue;
        if (s->kind == SHAPE_CIRCLE) {
            if (v2len2(v2sub(p, s->c)) < s->r * s->r) return true;
        } else if (point_in_poly(s, p)) {
            return true;
        }
    }
    return false;
}

bool phys_circle_overlaps(const Shape *s, vec2 p, float r) {
    if (p.x + r < s->x0 || p.x - r > s->x1 || p.y + r < s->y0 || p.y - r > s->y1) return false;
    return separation(s, p, r, NULL) < 0;
}
