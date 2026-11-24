// The settings registry (nova2/keys.h, from Nova 1): every entry well formed;
// families expanding; the readers' ranges, defaults and misuse checks; unknown
// keys failing as unused; and the plain table and Markdown reference naming
// every setting. Nova 1's preset, seeding and docs/keys.md checks wait for
// Nova 2's presets and `nova2 keys` command.
#include <cstdio>
#include <cstring>

#include "check.h"
#include "nova2/config.h"
#include "nova2/io.h"
#include "nova2/keys.h"

using namespace nova2;

namespace {

bool is_group(const char* g) {
    for (u32 i = 0; i < kKeyGroupCount; ++i)
        if (strcmp(kKeyGroups[i], g) == 0)
            return true;
    return false;
}

// The words of a space-separated list, one at a time.
bool next_word(Str s, usize* at, Str* word) {
    while (*at < s.n && s.p[*at] == ' ')
        ++*at;
    if (*at >= s.n)
        return false;
    const usize b = *at;
    while (*at < s.n && s.p[*at] != ' ')
        ++*at;
    *word = Str(s.p + b, *at - b);
    return true;
}

bool has_option(const Key& k, Str v) {
    usize at = 0;
    Str w;
    while (next_word(Str(k.options), &at, &w))
        if (w.eq(v))
            return true;
    return false;
}

// "key", or "key=a|b": an existing switch, number or text, or a choice and
// some of its options.
bool needs_ok(const Key& k) {
    const Str nd(k.needs);
    if (nd.n == 0)
        return true;
    const usize eq = nd.find('=');
    const Key* on = key_find(nd.sub(0, eq));
    if (!on || on == &k)
        return false;
    if (eq == nd.n)
        return on->kind != KeyKind::choice;
    if (on->kind != KeyKind::choice)
        return false;
    Str rest = nd.sub(eq + 1, nd.n - eq - 1);
    while (rest.n) {
        const usize bar = rest.find('|');
        if (!has_option(*on, rest.sub(0, bar)))
            return false;
        rest = bar < rest.n ? rest.sub(bar + 1, rest.n - bar - 1) : Str();
    }
    return true;
}

// Every entry: a valid, unique name; a known group; a label and one line of
// help; a default inside its range; a choice's default among its options; a
// needs naming a real switch. (From Nova 1's keys suite.)
void test_entries() {
    const usize n = key_count();
    u32 bad = 0;
    for (usize i = 0; i < n; ++i) {
        const Key& k = key_at(i);
        bool ok = Config::valid_key(k.name) && strchr(k.name, '<') == nullptr && is_group(k.group) &&
                  k.label[0] && k.help[0] && !strchr(k.help, '\n') && strchr(k.label, '<') == nullptr &&
                  needs_ok(k);
        if (k.family) { // expanded: no placeholder left in its help
            const char* open = strchr(k.family, '<');
            const char* close = open ? strchr(open, '>') : nullptr;
            char ph[32] = {};
            if (open && close && close - open < 31)
                memcpy(ph, open, static_cast<usize>(close - open) + 1);
            ok = ok && ph[0] && !strstr(k.help, ph) && !strstr(k.needs, ph);
        }
        for (usize j = 0; j < i && ok; ++j)
            ok = strcmp(key_at(j).name, k.name) != 0; // no duplicates
        switch (k.kind) {
        case KeyKind::flag:
            ok = ok && (k.def == 0 || k.def == 1) && strcmp(k.def_text, k.def ? "true" : "false") == 0;
            break;
        case KeyKind::whole:
            ok = ok && k.lo <= k.def && k.def <= k.hi && static_cast<double>(static_cast<i64>(k.def)) == k.def &&
                 static_cast<double>(static_cast<i64>(k.lo)) == k.lo;
            break;
        case KeyKind::number:
            ok = ok && ((k.flags & kKeyOpenMin) ? k.lo < k.def : k.lo <= k.def) && k.def <= k.hi;
            break;
        case KeyKind::choice:
            ok = ok && k.options[0] && has_option(k, k.def_text) && !strstr(k.options, "  ");
            break;
        case KeyKind::text:
            ok = ok && !k.options[0];
            break;
        }
        if (k.kind != KeyKind::number)
            ok = ok && !(k.flags & (kKeyLog | kKeyOpenMin));
        if (k.kind != KeyKind::whole && k.kind != KeyKind::number)
            ok = ok && !(k.flags & kKeyMaxComputed);
        if (k.kind == KeyKind::choice || k.kind == KeyKind::text)
            ok = ok && !(k.flags & kKeyComputed);
        if (!ok) {
            fprintf(stderr, "  bad registry entry: %s\n", k.name);
            ++bad;
        }
    }
    CHECK(bad == 0);
    CHECK(n >= 4); // run.ticks, run.threads, world.seed, world.places at least
    CHECK(key_find("world.places") != nullptr && key_find("world.seed") != nullptr);
    CHECK(key_find("world.nodes") == nullptr); // Nova 1's table is gone
}

// A family is written once and expands for every member, with one member's
// own default.
void test_families() {
    const Key* left = key_find("test.left.weight");
    const Key* right = key_find("test.right.weight");
    CHECK(left && right);
    if (!left || !right)
        return;
    CHECK(left->family && strcmp(left->family, "test.<side>.weight") == 0);
    CHECK(strcmp(left->label, "Left weight") == 0);
    CHECK(strcmp(left->group, "Run and output") == 0);
    CHECK(left->def == 1 && right->def == 3);
    Config c;
    Config::ParseError pe;
    CHECK(c.parse("test.left.weight = 2.5\n", &pe));
    CHECK(key_f64(c, "test.left.weight") == 2.5);
    CHECK(key_f64(c, "test.right.weight") == 3);
}

Str g_misused;
u32 g_misuses = 0;

void note_misuse(Str name, const char*) {
    g_misused = name;
    ++g_misuses;
}

bool misused(const char* name, u32 before) { return g_misuses == before + 1 && g_misused.eq(name); }

// The readers: defaults, ranges reported through cfg.fail, and every misuse
// caught.
void test_readers() {
    Config c;
    Config::ParseError pe;
    CHECK(c.parse("world.places = 2\nworld.plcaes = 5\n", &pe));
    CHECK(key_u32(c, "world.places") == 2);
    CHECK(key_u64(c, "world.seed") == 1); // the default
    Str k;
    CHECK(c.first_unused(&k) && k.eq("world.plcaes")); // a typo fails as unused

    const KeyMisuseHook prev = key_set_misuse_hook(note_misuse);
    u32 n = g_misuses;
    CHECK(key_u32(c, "world.plcaes") == 0 && misused("world.plcaes", n)); // no entry
    n = g_misuses;
    (void)key_f64(c, "world.places"); // a whole number read as a number
    CHECK(misused("world.places", n));
    n = g_misuses;
    (void)key_u32(c, "test.count"); // computed: the call site must give the default
    CHECK(misused("test.count", n));
    n = g_misuses;
    (void)key_u32(c, "world.places", key_default(5)); // not computed
    CHECK(misused("world.places", n));
    n = g_misuses;
    (void)key_u32(c, "test.limit"); // the maximum is computed
    CHECK(misused("test.limit", n));
    n = g_misuses;
    (void)key_u32(c, "world.seed"); // over 32 bits
    CHECK(misused("world.seed", n));
    n = g_misuses;
    (void)key_u64(c, "test.offset"); // can be negative
    CHECK(misused("test.offset", n));
    n = g_misuses;
    (void)key_peek_u64(c, "test.count"); // a computed default can't be peeked
    CHECK(misused("test.count", n));
    key_set_misuse_hook(prev);
    CHECK(c.ok()); // none of that touched the config's error

    // Values out of range, or not among a choice's options: cfg.fail, and
    // the minimum (or the default) back.
    Config e;
    CHECK(e.parse("world.places = 0\n", &pe));
    CHECK(key_u32(e, "world.places") == 1 &&
          strstr(e.value_error(), "world.places: must be between 1 and 1000000") != nullptr);
    Config f;
    CHECK(f.parse("test.mode = loud\n", &pe));
    CHECK(key_choice(f, "test.mode").eq("plain") && strstr(f.value_error(), "must be plain or fancy") != nullptr);
    Config g;
    CHECK(g.parse("test.rate = 0\n", &pe));
    (void)key_f64(g, "test.rate");
    CHECK(strstr(g.value_error(), "must be above 0 and at most 1") != nullptr);
    Config h;
    CHECK(h.parse("test.offset = -2\n", &pe));
    CHECK(key_i64(h, "test.offset") == -1 && strstr(h.value_error(), "must be between -1 and 100") != nullptr);
    Config j;
    CHECK(j.parse("test.limit = 300\nworld.seed = 18446744073709551615\n", &pe));
    CHECK(key_u32(j, "test.limit", key_max(256)) == 1 && strstr(j.value_error(), "must be between 1 and 256") != nullptr);
    CHECK(key_u64(j, "world.seed") == 18446744073709551615ull); // no upper limit
    Config m;
    CHECK(m.parse("", &pe));
    CHECK(key_u32(m, "test.count", key_default(12)) == 12); // the computed default
    CHECK(!key_flag(m, "test.switch") && key_str(m, "test.label").empty());

    // The value check, the same ranges.
    double d = 0;
    CHECK(key_check("test.rate", "0.5", KeyAt{}, &d) && d == 0.5);
    CHECK(!key_check("test.rate", "-1", KeyAt{}, &d) && !key_check("test.rate", "x", KeyAt{}, &d));
    CHECK(key_check("test.limit", "100", key_max(100), &d) && !key_check("test.limit", "101", key_max(100), &d));
}

// The plain table and the Markdown reference name every visible setting.
void test_tables() {
    TextBuf t;
    keys_table(t);
    TextBuf md;
    keys_markdown(md);
    CHECK(strstr(t.c_str(), "world.places") != nullptr);
    CHECK(strstr(md.c_str(), "# Settings reference") != nullptr);
    CHECK(strstr(md.c_str(), "world.places") != nullptr);
    CHECK(strstr(md.c_str(), "nova2 keys") != nullptr);
}

} // namespace

void suite_keys() {
    test_entries();
    test_families();
    test_readers();
    test_tables();
}
