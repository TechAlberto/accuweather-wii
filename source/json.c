#include "json.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_DEPTH 64

typedef struct {
    const char *p;
    int depth;
    int error;
} parser_t;

static void skip_ws(parser_t *ps)
{
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r' || *ps->p == '\n')
        ps->p++;
}

static json_value *value_new(json_type t)
{
    json_value *v = (json_value *)calloc(1, sizeof(json_value));
    if (v)
        v->type = t;
    return v;
}

static json_value *parse_value(parser_t *ps);

static char *parse_string_raw(parser_t *ps)
{
    if (*ps->p != '"') {
        ps->error = 1;
        return NULL;
    }
    ps->p++;

    size_t cap = 32, len = 0;
    char *out = (char *)malloc(cap);
    if (!out) {
        ps->error = 1;
        return NULL;
    }

    while (*ps->p && *ps->p != '"') {
        unsigned char c = (unsigned char)*ps->p++;
        if (c == '\\') {
            char e = *ps->p++;
            switch (e) {
            case '"': c = '"'; break;
            case '\\': c = '\\'; break;
            case '/': c = '/'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'u': {
                unsigned code = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *ps->p++;
                    code <<= 4;
                    if (h >= '0' && h <= '9') code |= (unsigned)(h - '0');
                    else if (h >= 'a' && h <= 'f') code |= (unsigned)(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') code |= (unsigned)(h - 'A' + 10);
                    else { ps->error = 1; break; }
                }
                if (code >= 0xd800 && code <= 0xdbff && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    unsigned low = 0;
                    ps->p += 2;
                    for (int i = 0; i < 4; i++) {
                        char h = *ps->p++;
                        low <<= 4;
                        if (h >= '0' && h <= '9') low |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') low |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') low |= (unsigned)(h - 'A' + 10);
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
                }
                if (code < 0x80) {
                    c = (unsigned char)code;
                } else if (code < 0x800) {
                    if (len + 3 > cap) { cap *= 2; out = (char *)realloc(out, cap); }
                    out[len++] = (char)(0xc0 | (code >> 6));
                    out[len++] = (char)(0x80 | (code & 0x3f));
                    continue;
                } else if (code < 0x10000) {
                    if (len + 4 > cap) { cap *= 2; out = (char *)realloc(out, cap); }
                    out[len++] = (char)(0xe0 | (code >> 12));
                    out[len++] = (char)(0x80 | ((code >> 6) & 0x3f));
                    out[len++] = (char)(0x80 | (code & 0x3f));
                    continue;
                } else {
                    if (len + 5 > cap) { cap *= 2; out = (char *)realloc(out, cap); }
                    out[len++] = (char)(0xf0 | (code >> 18));
                    out[len++] = (char)(0x80 | ((code >> 12) & 0x3f));
                    out[len++] = (char)(0x80 | ((code >> 6) & 0x3f));
                    out[len++] = (char)(0x80 | (code & 0x3f));
                    continue;
                }
                break;
            }
            default:
                ps->error = 1;
                c = (unsigned char)e;
                break;
            }
        }
        if (len + 2 > cap) {
            cap *= 2;
            char *n = (char *)realloc(out, cap);
            if (!n) { ps->error = 1; break; }
            out = n;
        }
        out[len++] = (char)c;
    }

    if (*ps->p != '"') {
        ps->error = 1;
        free(out);
        return NULL;
    }
    ps->p++;
    out[len] = 0;
    return out;
}

static json_value *parse_array(parser_t *ps)
{
    json_value *v = value_new(JSON_ARR);
    if (!v) { ps->error = 1; return NULL; }
    ps->p++; /* [ */

    int cap = 0;
    skip_ws(ps);
    if (*ps->p == ']') { ps->p++; return v; }

    while (*ps->p) {
        json_value *item = parse_value(ps);
        if (!item) break;

        if (v->u.array.count == cap) {
            cap = cap ? cap * 2 : 8;
            json_value **n = (json_value **)realloc(v->u.array.items,
                                                   (size_t)cap * sizeof(json_value *));
            if (!n) { ps->error = 1; json_free(item); break; }
            v->u.array.items = n;
        }
        v->u.array.items[v->u.array.count++] = item;

        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; skip_ws(ps); continue; }
        if (*ps->p == ']') { ps->p++; return v; }
        ps->error = 1;
        break;
    }
    return v;
}

static json_value *parse_object(parser_t *ps)
{
    json_value *v = value_new(JSON_OBJ);
    if (!v) { ps->error = 1; return NULL; }
    ps->p++; /* { */

    int cap = 0;
    skip_ws(ps);
    if (*ps->p == '}') { ps->p++; return v; }

    while (*ps->p) {
        skip_ws(ps);
        char *key = parse_string_raw(ps);
        if (!key) break;
        skip_ws(ps);
        if (*ps->p != ':') { free(key); ps->error = 1; break; }
        ps->p++;
        json_value *item = parse_value(ps);
        if (!item) { free(key); break; }

        if (v->u.object.count == cap) {
            cap = cap ? cap * 2 : 8;
            char **nk = (char **)realloc(v->u.object.keys, (size_t)cap * sizeof(char *));
            json_value **nv = (json_value **)realloc(v->u.object.values,
                                                     (size_t)cap * sizeof(json_value *));
            if (!nk || !nv) { ps->error = 1; free(key); json_free(item); break; }
            v->u.object.keys = nk;
            v->u.object.values = nv;
        }
        v->u.object.keys[v->u.object.count] = key;
        v->u.object.values[v->u.object.count] = item;
        v->u.object.count++;

        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; return v; }
        ps->error = 1;
        break;
    }
    return v;
}

static json_value *parse_value(parser_t *ps)
{
    skip_ws(ps);

    if (ps->depth++ > MAX_DEPTH) {
        ps->error = 1;
        ps->depth--;
        return NULL;
    }

    json_value *out = NULL;

    if (*ps->p == '{') {
        out = parse_object(ps);
    } else if (*ps->p == '[') {
        out = parse_array(ps);
    } else if (*ps->p == '"') {
        char *s = parse_string_raw(ps);
        if (s) {
            out = value_new(JSON_STR);
            if (out)
                out->u.string = s;
            else
                free(s);
        }
    } else if (!strncmp(ps->p, "true", 4)) {
        out = value_new(JSON_BOOL);
        if (out) out->u.boolean = 1;
        ps->p += 4;
    } else if (!strncmp(ps->p, "false", 5)) {
        out = value_new(JSON_BOOL);
        if (out) out->u.boolean = 0;
        ps->p += 5;
    } else if (!strncmp(ps->p, "null", 4)) {
        out = value_new(JSON_NULL);
        ps->p += 4;
    } else {
        char *end = NULL;
        double n = strtod(ps->p, &end);
        if (end && end != ps->p) {
            out = value_new(JSON_NUM);
            if (out) out->u.number = n;
            ps->p = end;
        } else {
            ps->error = 1;
        }
    }

    ps->depth--;
    return out;
}

void json_free(json_value *v)
{
    if (!v)
        return;
    switch (v->type) {
    case JSON_STR:
        free(v->u.string);
        break;
    case JSON_ARR:
        for (int i = 0; i < v->u.array.count; i++)
            json_free(v->u.array.items[i]);
        free(v->u.array.items);
        break;
    case JSON_OBJ:
        for (int i = 0; i < v->u.object.count; i++) {
            free(v->u.object.keys[i]);
            json_free(v->u.object.values[i]);
        }
        free(v->u.object.keys);
        free(v->u.object.values);
        break;
    default:
        break;
    }
    free(v);
}

json_value *json_parse(const char *text)
{
    if (!text)
        return NULL;
    parser_t ps = { text, 0, 0 };
    json_value *v = parse_value(&ps);
    if (ps.error || !v) {
        json_free(v);
        return NULL;
    }
    return v;
}

json_value *json_at(json_value *v, int index)
{
    if (!v)
        return NULL;
    if (v->type == JSON_ARR)
        return (index >= 0 && index < v->u.array.count) ? v->u.array.items[index] : NULL;
    if (v->type == JSON_OBJ)
        return (index >= 0 && index < v->u.object.count) ? v->u.object.values[index] : NULL;
    return NULL;
}

json_value *json_get(json_value *v, const char *path)
{
    char key[128];
    const char *p = path;

    while (*p) {
        const char *dot = strchr(p, '.');
        size_t n = dot ? (size_t)(dot - p) : strlen(p);
        if (n >= sizeof(key))
            return NULL;
        memcpy(key, p, n);
        key[n] = 0;

        if (!v)
            return NULL;

        if (key[0] >= '0' && key[0] <= '9') {
            v = json_at(v, atoi(key));
        } else if (v->type == JSON_OBJ) {
            json_value *next = NULL;
            for (int i = 0; i < v->u.object.count; i++) {
                if (!strcmp(v->u.object.keys[i], key)) {
                    next = v->u.object.values[i];
                    break;
                }
            }
            v = next;
        } else {
            return NULL;
        }

        if (!dot)
            break;
        p = dot + 1;
    }
    return v;
}

int json_count(json_value *v)
{
    if (!v)
        return 0;
    if (v->type == JSON_ARR)
        return v->u.array.count;
    if (v->type == JSON_OBJ)
        return v->u.object.count;
    return 0;
}

const char *json_str(json_value *v, const char *fallback)
{
    if (v && v->type == JSON_STR && v->u.string)
        return v->u.string;
    return fallback;
}

double json_num(json_value *v, double fallback)
{
    if (v && v->type == JSON_NUM)
        return v->u.number;
    if (v && v->type == JSON_BOOL)
        return v->u.boolean ? 1.0 : 0.0;
    return fallback;
}

int json_int(json_value *v, int fallback)
{
    if (v && v->type == JSON_NUM)
        return (int)(v->u.number < 0 ? v->u.number - 0.5 : v->u.number + 0.5);
    if (v && v->type == JSON_BOOL)
        return v->u.boolean;
    return fallback;
}

int json_bool(json_value *v, int fallback)
{
    if (v && v->type == JSON_BOOL)
        return v->u.boolean;
    if (v && v->type == JSON_NUM)
        return v->u.number != 0.0;
    return fallback;
}
