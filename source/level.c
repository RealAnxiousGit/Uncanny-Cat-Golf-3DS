// Reads the level text files written by tools/convert/levels.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ucg.h"

static const char *OBJ_NAMES[OBJ_COUNT] = {"player", "goal", "ghost", "coin"};

static void bbox(Shape *s) {
    if (s->kind == SHAPE_CIRCLE) {
        s->x0 = s->c.x - s->r; s->x1 = s->c.x + s->r;
        s->y0 = s->c.y - s->r; s->y1 = s->c.y + s->r;
        return;
    }
    s->x0 = s->x1 = s->pts[0].x;
    s->y0 = s->y1 = s->pts[0].y;
    for (int i = 1; i < s->n; i++) {
        if (s->pts[i].x < s->x0) s->x0 = s->pts[i].x;
        if (s->pts[i].x > s->x1) s->x1 = s->pts[i].x;
        if (s->pts[i].y < s->y0) s->y0 = s->pts[i].y;
        if (s->pts[i].y > s->y1) s->y1 = s->pts[i].y;
    }
}

bool level_load(LevelDef *lv, const char *path) {
    memset(lv, 0, sizeof(*lv));
    FILE *f = fopen(path, "r");
    if (!f) return false;
    static char line[16384];
    int cap_s = 64, cap_o = 16;
    lv->shapes = calloc(cap_s, sizeof(Shape));
    lv->objs = calloc(cap_o, sizeof(ObjDef));
    lv->shade[0] = lv->shade[1] = lv->shade[2] = lv->shade[3] = 1;
    bool ok = false;
    while (fgets(line, sizeof(line), f)) {
        char key[16] = {0};
        int off = 0;
        if (sscanf(line, "%15s %n", key, &off) < 1) continue;
        char *rest = line + off;
        rest[strcspn(rest, "\r\n")] = 0;
        if (!strcmp(key, "id")) snprintf(lv->id, sizeof(lv->id), "%s", rest);
        else if (!strcmp(key, "author")) snprintf(lv->author, sizeof(lv->author), "%s", rest);
        else if (!strcmp(key, "bounds")) lv->bounds = atoi(rest);
        else if (!strcmp(key, "par")) lv->par = atoi(rest);
        else if (!strcmp(key, "ontime")) lv->on_time = strtof(rest, NULL);
        else if (!strcmp(key, "music")) lv->music = atoi(rest);
        else if (!strcmp(key, "ranks"))
            sscanf(rest, "%d %d %d %d %d %d", &lv->ranks[0], &lv->ranks[1], &lv->ranks[2], &lv->ranks[3],
                   &lv->ranks[4], &lv->ranks[5]);
        else if (!strcmp(key, "shade"))
            sscanf(rest, "%f %f %f %f", &lv->shade[0], &lv->shade[1], &lv->shade[2], &lv->shade[3]);
        else if (!strcmp(key, "flags")) {
            if (strstr(rest, "always_O_B")) lv->flags |= LF_ALWAYS_OB;
            if (strstr(rest, "never_O_B")) lv->flags |= LF_NEVER_OB;
            if (strstr(rest, "mirror_level")) lv->flags |= LF_MIRROR;
            if (strstr(rest, "hideScoring")) lv->flags |= LF_HIDE_SCORING;
            if (strstr(rest, "unskippable")) lv->flags |= LF_UNSKIPPABLE;
        } else if (!strcmp(key, "image")) {
            char which[8] = {0};
            sscanf(rest, "%d %7s", &lv->image_size, which);
            lv->has_over = strchr(which, 'o') != NULL;
        } else if (!strcmp(key, "poly") || !strcmp(key, "circ")) {
            if (lv->nshapes == cap_s) {
                cap_s *= 2;
                lv->shapes = realloc(lv->shapes, cap_s * sizeof(Shape));
            }
            Shape *s = &lv->shapes[lv->nshapes];
            memset(s, 0, sizeof(*s));
            char *p = rest;
            s->layers = (uint32_t)strtoul(p, &p, 10);
            if (key[0] == 'p') {
                s->kind = SHAPE_POLY;
                s->n = (int)strtol(p, &p, 10);
                if (s->n < 3) continue;
                s->pts = malloc(s->n * sizeof(vec2));
                for (int i = 0; i < s->n; i++) {
                    s->pts[i].x = strtof(p, &p);
                    s->pts[i].y = strtof(p, &p);
                }
            } else {
                s->kind = SHAPE_CIRCLE;
                s->c.x = strtof(p, &p);
                s->c.y = strtof(p, &p);
                s->r = strtof(p, &p);
            }
            bbox(s);
            lv->nshapes++;
        } else if (!strcmp(key, "obj")) {
            char name[24] = {0};
            int n = 0;
            ObjDef o;
            memset(&o, 0, sizeof(o));
            if (sscanf(rest, "%23s %f %f %f %f %f%n", name, &o.pos.x, &o.pos.y, &o.rot, &o.sx, &o.sy, &n) < 6) continue;
            o.type = -1;
            for (int i = 0; i < OBJ_COUNT; i++)
                if (!strcmp(name, OBJ_NAMES[i])) o.type = i;
            if (o.type < 0) continue;
            char *p = rest + n;
            while (*p && o.nparams < MAX_PARAMS) {
                while (*p == ' ') p++;
                char *eq = strchr(p, '=');
                if (!eq) break;
                int kl = (int)(eq - p);
                if (kl > 23) kl = 23;
                memcpy(o.pkey[o.nparams], p, kl);
                o.pkey[o.nparams][kl] = 0;
                o.pval[o.nparams] = strtof(eq + 1, &p);
                if (!strcmp(o.pkey[o.nparams], "hidden")) o.hidden = o.pval[o.nparams] != 0;
                o.nparams++;
            }
            if (lv->nobjs == cap_o) {
                cap_o *= 2;
                lv->objs = realloc(lv->objs, cap_o * sizeof(ObjDef));
            }
            lv->objs[lv->nobjs++] = o;
        } else if (!strcmp(key, "end")) {
            ok = true;
        }
    }
    fclose(f);
    if (!ok || lv->bounds <= 0) {
        level_free(lv);
        return false;
    }
    return true;
}

void level_free(LevelDef *lv) {
    for (int i = 0; i < lv->nshapes; i++) free(lv->shapes[i].pts);
    free(lv->shapes);
    free(lv->objs);
    memset(lv, 0, sizeof(*lv));
}

float obj_param(const ObjDef *o, const char *key, float def) {
    for (int i = 0; i < o->nparams; i++)
        if (!strcmp(o->pkey[i], key)) return o->pval[i];
    return def;
}
