/* SPDX-License-Identifier: BSD-2-Clause */
/* iOS 26 libxpc existence-only LWCR subset. No trust/signature assertion.
 * Unknown operators, categories, versions and extra predicates stay unsupported. */
#ifndef B2L_LWCR_H
#define B2L_LWCR_H
#define B2L_LWCR_KEY_MAX 256u
struct b2l_lwcr_der { const uint8_t *p; size_t n; };
static int
b2l_lwcr_take(struct b2l_lwcr_der *in, uint8_t *tag, struct b2l_lwcr_der *value)
{
    if (in->n < 2) return EINVAL;
    *tag = in->p[0];
    if ((*tag & 31u) == 31u) return ENOTSUP;
    size_t head = 2, n = in->p[1];
    if (n & 128u) {
        size_t count = n & 127u;
        if (!count || count > sizeof(size_t) || count > in->n - 2) return EINVAL;
        if (!in->p[2]) return EINVAL;
        n = 0;
        for (size_t i = 0; i < count; i++) {
            if (n > (SIZE_MAX >> 8)) return EINVAL;
            n = (n << 8) | in->p[2 + i];
        }
        if (n < 128) return EINVAL;
        head += count;
    }
    if (n > in->n - head) return EINVAL;
    value->p = in->p + head; value->n = n;
    in->p += head + n; in->n -= head + n;
    return 0;
}
static int
b2l_lwcr_expect(struct b2l_lwcr_der *in, uint8_t expected, struct b2l_lwcr_der *value)
{
    uint8_t tag; int error = b2l_lwcr_take(in, &tag, value);
    return error ? error : tag == expected ? 0 : ENOTSUP;
}
static bool
b2l_lwcr_text(struct b2l_lwcr_der v, const char *text)
{return v.n == strlen(text) && !memcmp(v.p, text, v.n);}
static int
b2l_lwcr_integer(struct b2l_lwcr_der *in, unsigned expected)
{
    struct b2l_lwcr_der v; int error = b2l_lwcr_expect(in, 2, &v);
    if (error) return error;
    if (!v.n || (v.n > 1 && ((!v.p[0] && !(v.p[1] & 128u)) ||
        (v.p[0] == 255 && (v.p[1] & 128u))))) return EINVAL;
    return v.n == 1 && v.p[0] == expected ? 0 : ENOTSUP;
}
/* Validate all framing first, including branches outside the supported subset.
 * Depth/node budgets prevent attacker-controlled recursive work. */
static int
b2l_lwcr_validate(struct b2l_lwcr_der in, unsigned depth, unsigned *nodes)
{
    if (depth > 12) return ENOTSUP;
    while (in.n) {
        if (++*nodes > 256) return ENOTSUP;
        struct b2l_lwcr_der v; uint8_t tag;
        int error = b2l_lwcr_take(&in, &tag, &v);
        if (error) return error;
        if (tag & 32u) {
            error = b2l_lwcr_validate(v, depth + 1, nodes);
            if (error) return error;
        }
    }
    return 0;
}
/* A dictionary member is SEQUENCE { UTF8String key, one complete DER value }. */
static int
b2l_lwcr_member(struct b2l_lwcr_der *dict, struct b2l_lwcr_der *key,
    struct b2l_lwcr_der *value)
{
    struct b2l_lwcr_der pair, ignored; uint8_t tag;
    int error = b2l_lwcr_expect(dict, 0x30, &pair);
    if (error) return error;
    error = b2l_lwcr_expect(&pair, 0x0c, key);
    if (error) return error;
    if (!key->n || memchr(key->p, 0, key->n)) return EINVAL;
    *value = pair;
    error = b2l_lwcr_take(&pair, &tag, &ignored);
    return error ? error : pair.n ? EINVAL : 0;
}
static int
b2l_lwcr_single(struct b2l_lwcr_der encoded, const char *name,
    struct b2l_lwcr_der *value)
{
    struct b2l_lwcr_der dict, key;
    int error = b2l_lwcr_expect(&encoded, 0xb0, &dict);
    if (error || encoded.n) return error ? error : EINVAL;
    error = b2l_lwcr_member(&dict, &key, value);
    if (error) return error;
    return dict.n || !b2l_lwcr_text(key, name) ? ENOTSUP : 0;
}
static int
b2l_lwcr_entitlement_exists(const void *bytes, size_t length,
    char key[B2L_LWCR_KEY_MAX])
{
    key[0] = 0;
    struct b2l_lwcr_der in = {bytes, length}, app, dict, reqs = {0};
    unsigned nodes = 0, seen = 0;
    int error = b2l_lwcr_validate(in, 0, &nodes);
    if (error) return error;
    error = b2l_lwcr_expect(&in, 0x70, &app);
    if (error || in.n) return error ? error : EINVAL;
    error = b2l_lwcr_integer(&app, 1);
    if (error) return error;
    error = b2l_lwcr_expect(&app, 0xb0, &dict);
    if (error || app.n) return error ? error : EINVAL;
    while (dict.n) {
        struct b2l_lwcr_der name, value;
        error = b2l_lwcr_member(&dict, &name, &value);
        if (error) return error;
        unsigned bit, expected;
        if (b2l_lwcr_text(name, "ccat")) { bit = 1; expected = 0; }
        else if (b2l_lwcr_text(name, "comp")) { bit = 2; expected = 1; }
        else if (b2l_lwcr_text(name, "vers")) { bit = 4; expected = 1; }
        else if (b2l_lwcr_text(name, "reqs")) { bit = 8; expected = 0; }
        else return ENOTSUP;
        if (seen & bit) return EINVAL;
        seen |= bit;
        if (bit == 8) reqs = value;
        else {
            error = b2l_lwcr_integer(&value, expected);
            if (error || value.n) return error ? error : EINVAL;
        }
    }
    if (seen != 15) return ENOTSUP;
    struct b2l_lwcr_der entitlements, query, array, predicate, name;
    if ((error = b2l_lwcr_single(reqs, "entitlements", &entitlements)) ||
        (error = b2l_lwcr_single(entitlements, "$query", &query))) return error;
    error = b2l_lwcr_expect(&query, 0x30, &array);
    if (error || query.n) return error ? error : EINVAL;
    error = b2l_lwcr_expect(&array, 0x30, &predicate);
    if (error || array.n) return error ? error : ENOTSUP;
    error = b2l_lwcr_integer(&predicate, 1);
    if (error) return error;
    error = b2l_lwcr_expect(&predicate, 0x0c, &name);
    if (error || predicate.n) return error ? error : ENOTSUP;
    if (!name.n || name.n >= B2L_LWCR_KEY_MAX || memchr(name.p, 0, name.n)) return ENOTSUP;
    /* All shipped entitlement names are ASCII; do not normalize other strings. */
    for (size_t i = 0; i < name.n; i++) if (name.p[i] < 0x21 || name.p[i] > 0x7e) return ENOTSUP;
    memcpy(key, name.p, name.n); key[name.n] = 0;
    return 0;
}
#endif
