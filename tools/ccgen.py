#!/usr/bin/env python3
"""ccgen -- generate the callback bank of cachecliff.

The bank is a set of functions with a common signature,

    uint32_t cc_fNNNN(uint32_t x, cc_ctx *c);

each one a straight-line mix of integer statements: arithmetic, loads and
stores at data-dependent addresses of the data region, a few branches and
small loops. The functions are different from each other (constants, order and
number of statements) so that no two share code, but they are built from the
same few statement kinds, drawn with the weights of --mix. Their sizes come
from --sizes, in a stratified way: every group of 64 functions holds the same
multiset of sizes, so any number of whole groups has the same average work per
call.

ccgen also computes, in Python, what the validation runs of cachecliff must
return (cc_validate_* in cc_table.c). The generated C is thus checked against
an independent implementation of its own statements.

The generator is deterministic: the same arguments give the same files.
Only the Python standard library is used.
"""

import argparse
import hashlib
import os
import random
import sys

GROUP = 64  # functions per bank file
M32 = 0xFFFFFFFF
VERSION = "0.1.0"

# The statement kinds and their default weights.
DEFAULT_MIX = {
    "alu1": 14,
    "alu2": 12,
    "alu3": 12,
    "mul": 7,
    "ld1": 12,
    "ld2": 6,
    "st": 7,
    "brr": 10,
    "brh": 8,
    "loop": 12,
}
DEFAULT_SIZES = [6, 8, 12, 16, 24, 32, 48, 64]  # statements per function
CHASE_PRIME = 0x9E3779B1  # scatter multiplier, odd


def parse_mix(text):
    mix = dict(DEFAULT_MIX)
    if not text:
        return mix
    for item in text.split(","):
        key, _, val = item.partition("=")
        if key not in mix:
            raise SystemExit(
                f"ccgen: unknown statement kind {key!r} ({', '.join(mix)})"
            )
        mix[key] = int(val)
    if sum(mix.values()) <= 0:
        raise SystemExit("ccgen: the mix has no weight")
    return mix


def odd(rng):
    return rng.getrandbits(32) | 1


def make_statement(rng, kind):
    """A statement as a tuple: (kind, parameters...)."""
    if kind == "alu1":
        return (kind, odd(rng), rng.randint(3, 17), rng.getrandbits(32))
    if kind == "alu2":
        return (kind, rng.randint(3, 13), rng.getrandbits(32))
    if kind == "alu3":
        return (kind, rng.getrandbits(32), rng.randint(5, 19))
    if kind == "mul":
        return (kind, odd(rng))
    if kind == "ld1":
        return (kind, rng.getrandbits(32))
    if kind == "ld2":
        return (kind, rng.randint(2, 11), rng.getrandbits(32))
    if kind == "st":
        return (kind, rng.getrandbits(32))
    if kind == "brr":  # a branch that is taken rarely (predictable)
        bits = rng.randint(4, 7)
        return (kind, (1 << bits) - 1, rng.getrandbits(bits), rng.getrandbits(32))
    if kind == "brh":  # a branch that goes both ways about equally often
        return (kind, rng.randint(2, 20), rng.getrandbits(32), rng.getrandbits(32))
    if kind == "loop":
        return (kind, rng.randint(2, 6), odd(rng))
    raise ValueError(kind)


def balanced_order(rng, sizes):
    """The sizes in an order where every aligned prefix of 2, 4, 8 ... entries has
    about the same mean as all of them.

    The sizes are split into two halves with nearly equal sums (largest first
    into the half that is smaller so far), each half is ordered the same way,
    and the halves follow each other in a random order. A sweep over the first
    4, 8, 16 ... functions then compares the same amount of work per call; with
    sizes in a plain random order the first few functions differ a lot.
    """
    if len(sizes) <= 2:
        out = list(sizes)
        rng.shuffle(out)
        return out
    halves = ([], [])
    sums = [0, 0]
    for size in sorted(sizes, reverse=True):
        k = 0 if sums[0] <= sums[1] else 1
        halves[k].append(size)
        sums[k] += size
    first, second = (0, 1) if rng.random() < 0.5 else (1, 0)
    return balanced_order(rng, halves[first]) + balanced_order(rng, halves[second])


def make_function(rng, nstmt, mix):
    kinds = list(mix)
    weights = [mix[k] for k in kinds]
    return [make_statement(rng, rng.choices(kinds, weights)[0]) for _ in range(nstmt)]


def c_hex(v):
    return f"0x{v:08x}u"


def render_statement(s):
    k = s[0]
    if k == "alu1":
        return f"x = x * {c_hex(s[1])} + (x >> {s[2]}) + {c_hex(s[3])};"
    if k == "alu2":
        return f"x ^= (x << {s[1]}) + {c_hex(s[2])};"
    if k == "alu3":
        return f"x = (x + {c_hex(s[1])}) ^ (x >> {s[2]});"
    if k == "mul":
        return f"x *= {c_hex(s[1])};"
    if k == "ld1":
        return f"x += d[(x ^ {c_hex(s[1])}) & m];"
    if k == "ld2":
        return f"x ^= d[((x >> {s[1]}) + {c_hex(s[2])}) & m];"
    if k == "st":
        return f"d[(x ^ {c_hex(s[1])}) & m] = x;"
    if k == "brr":
        return f"if ((x & {c_hex(s[1])}) == {c_hex(s[2])}) x += {c_hex(s[3])};"
    if k == "brh":
        return f"if (x & (1u << {s[1]})) x ^= {c_hex(s[2])}; else x += {c_hex(s[3])};"
    if k == "loop":
        return f"for (i = 0; i < {s[1]}u; i++) x = x * {c_hex(s[2])} + i;"
    raise ValueError(k)


def render_function(name, stmts, index=None):
    uses_loop = any(s[0] == "loop" for s in stmts)
    what = f"Callback {index} of the bank" if index is not None else "A callback"
    lines = [
        "/**",
        f" * @brief {what}: {len(stmts)} statements.",
        " * @param[in] x Value from the caller.",
        " * @param[in] c The data region.",
        " * @return The new value.",
        " */",
        f"CC_FN({name})",
        "{",
    ]
    if any(s[0] in ("ld1", "ld2", "st") for s in stmts):
        lines.append("    uint32_t *d = c->data;")
        lines.append("    const uint32_t m = c->mask;")
    else:
        lines.append("    (void)c;")
    if uses_loop:
        lines.append("    uint32_t i;")
    for s in stmts:
        lines.append("    " + render_statement(s))
    lines.append("    return x;")
    lines.append("}")
    return "\n".join(lines)


# ---- an independent implementation of the statements, for the validation values


def run_statement(s, x, d, m):
    k = s[0]
    if k == "alu1":
        return (x * s[1] + (x >> s[2]) + s[3]) & M32
    if k == "alu2":
        return x ^ ((((x << s[1]) & M32) + s[2]) & M32)
    if k == "alu3":
        return ((x + s[1]) & M32) ^ (x >> s[2])
    if k == "mul":
        return (x * s[1]) & M32
    if k == "ld1":
        return (x + d[(x ^ s[1]) & m]) & M32
    if k == "ld2":
        return x ^ d[(((x >> s[1]) + s[2]) & M32) & m]
    if k == "st":
        d[(x ^ s[1]) & m] = x
        return x
    if k == "brr":
        return (x + s[3]) & M32 if (x & s[1]) == s[2] else x
    if k == "brh":
        return x ^ s[2] if x & (1 << s[1]) else (x + s[3]) & M32
    if k == "loop":
        for i in range(s[1]):
            x = (x * s[2] + i) & M32
        return x
    raise ValueError(k)


def run_function(stmts, x, d, m):
    for s in stmts:
        x = run_statement(s, x, d, m)
    return x


def xorshift(s):
    s ^= (s << 13) & M32
    s ^= s >> 17
    s ^= (s << 5) & M32
    return s


def fill_data(words, seed):
    """Must match cc_data_fill() of the engine."""
    s = seed | 1
    d = []
    for _ in range(words):
        s = xorshift(s)
        d.append(s)
    return d


def draw(state, n, skew):
    """Must match cc_draw() of the engine. Returns (index, new state)."""
    state = xorshift(state)
    j = (state * n) >> 32
    for _ in range(skew - 1):
        state = xorshift(state)
        j = min(j, (state * n) >> 32)
    return j, state


VALIDATE_WORDS = 4096  # 16 KiB, mask 4095
VALIDATE_SEED = 1
VALIDATE_DRAWS = 2000
VALIDATE_DRAW_FUNCS = 64
VALIDATE_SKEW = 2


def validate_all(funcs, n):
    """Every function of the first n once, in order."""
    d = fill_data(VALIDATE_WORDS, VALIDATE_SEED)
    x = VALIDATE_SEED
    for f in funcs[:n]:
        x = run_function(f, x, d, VALIDATE_WORDS - 1)
    return x


def validate_draw(funcs):
    """Random calls among the first 64 functions."""
    d = fill_data(VALIDATE_WORDS, VALIDATE_SEED)
    x = VALIDATE_SEED
    state = VALIDATE_SEED | 1
    for _ in range(VALIDATE_DRAWS):
        j, state = draw(state, VALIDATE_DRAW_FUNCS, VALIDATE_SKEW)
        x = run_function(funcs[j], x, d, VALIDATE_WORDS - 1)
    return x


# ---- files


HEADER = """/**
 * @file {name}
 * @brief {brief}
 *
 * Generated by ccgen {version} (seed {seed}, {nfuncs} functions, bank id
 * {bank_id}). Do not edit; run tools/ccgen.py to change it.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
"""


def write(path, text):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)


def generate(a):
    if a.funcs % GROUP or a.funcs & (a.funcs - 1):
        raise SystemExit(
            f"ccgen: --funcs must be a power of two and a multiple of {GROUP}"
        )
    mix = parse_mix(a.mix)
    sizes = [int(v) for v in a.sizes.split(",")]
    if len(sizes) & (len(sizes) - 1) or GROUP % len(sizes):
        raise SystemExit(
            f"ccgen: the number of --sizes must be a power of two dividing {GROUP}"
        )
    rng = random.Random(a.seed)

    funcs = []
    for g in range(a.funcs // GROUP):
        # stratified: every block of len(sizes) functions holds each size once, in a
        # balanced order, so any prefix of 2^k functions has the mean size
        del g
        for _ in range(GROUP // len(sizes)):
            for n in balanced_order(rng, sizes):
                funcs.append(make_function(rng, n, mix))

    text_all = "".join(
        render_function(f"cc_f{i:04d}", f, i) for i, f in enumerate(funcs)
    )
    bank_id = int(hashlib.sha256(text_all.encode()).hexdigest()[:8], 16)

    def head_for(name, brief):
        return HEADER.format(
            name=name,
            brief=brief,
            version=VERSION,
            seed=a.seed,
            nfuncs=a.funcs,
            bank_id=f"0x{bank_id:08x}",
        )

    os.makedirs(a.out, exist_ok=True)
    for old in os.listdir(a.out):
        if old.startswith("cc_bank_") and old.endswith(".c"):
            os.remove(os.path.join(a.out, old))

    ngroups = a.funcs // GROUP
    for g in range(ngroups):
        name = f"cc_bank_{g:03d}.c"
        body = [
            head_for(
                name, f"Callbacks {g * GROUP} to {(g + 1) * GROUP - 1} of the bank."
            ),
            '#include "cachecliff.h"\n',
            f"#if CC_NFUNCS > {g * GROUP}\n",
        ]
        for i in range(g * GROUP, (g + 1) * GROUP):
            body.append(render_function(f"cc_f{i:04d}", funcs[i], i) + "\n")
        body.append("#endif\n")
        write(os.path.join(a.out, name), "\n".join(body))

    # the validation values for every table size a user can pick with CC_NFUNCS
    sizes_n = []
    n = GROUP
    while n <= a.funcs:
        sizes_n.append(n)
        n *= 2
    expected = [validate_all(funcs, n) for n in sizes_n]
    draw_expected = validate_draw(funcs)

    lines = [
        head_for(
            "cc_table.c", "The table of callbacks and the values to validate them with."
        ),
        '#include "cachecliff.h"\n',
    ]
    for i in range(a.funcs):
        if i % GROUP == 0:
            lines.append(f"#if CC_NFUNCS > {i}")
        lines.append(f"CC_FN_DECL(cc_f{i:04d});")
        if i % GROUP == GROUP - 1:
            lines.append("#endif")
    lines.append("")
    lines.append("const cc_fn cc_table[CC_NFUNCS] = {")
    for g in range(ngroups):
        lines.append(f"#if CC_NFUNCS > {g * GROUP}")
        for i in range(g * GROUP, (g + 1) * GROUP):
            lines.append(f"    cc_f{i:04d},")
        lines.append("#endif")
    lines.append("};")
    lines.append("")
    lines.append(f"const uint32_t cc_bank_id = 0x{bank_id:08x}u;")
    lines.append(f"const uint32_t cc_bank_nfuncs = {a.funcs}u;")
    lines.append(f"const uint32_t cc_validate_all[{len(expected)}] = {{")
    for n, v in zip(sizes_n, expected):
        lines.append(f"    0x{v:08x}u, /* the first {n} functions */")
    lines.append("};")
    lines.append(f"const uint32_t cc_validate_draw = 0x{draw_expected:08x}u;")
    write(os.path.join(a.out, "cc_table.c"), "\n".join(lines) + "\n")

    mix_text = ",".join(f"{k}={v}" for k, v in mix.items())
    header = [
        head_for("cc_bank.h", "What the bank was generated with."),
        "#ifndef CC_BANK_H",
        "#define CC_BANK_H",
        "",
        "/** @brief Functions in the bank. */",
        f"#define CC_GEN_NFUNCS {a.funcs}",
        "/** @brief Functions per file. */",
        f"#define CC_GEN_GROUP {GROUP}",
        "/** @brief Seed the bank was generated with. */",
        f"#define CC_GEN_SEED {a.seed}",
        "/** @brief Weights of the statement kinds. */",
        f'#define CC_GEN_MIX "{mix_text}"',
        "/** @brief Statements per function (each size equally often in every group). */",
        f'#define CC_GEN_SIZES "{a.sizes}"',
        "",
        "#endif",
        "",
    ]
    write(os.path.join(a.out, "cc_bank.h"), "\n".join(header))
    print(
        f"ccgen: {a.funcs} functions in {ngroups} files -> {a.out} "
        f"(bank-id 0x{bank_id:08x}, {sum(len(f) for f in funcs)} statements)"
    )


def main(argv=None):
    ap = argparse.ArgumentParser(description="generate the callback bank of cachecliff")
    ap.add_argument("--out", default="bank", help="output directory (default: bank)")
    ap.add_argument("--seed", type=int, default=1, help="random seed (default: 1)")
    ap.add_argument(
        "--funcs",
        type=int,
        default=1024,
        help=f"functions in the bank: a power of two, a multiple of {GROUP} (default: 1024)",
    )
    ap.add_argument(
        "--sizes",
        default=",".join(str(v) for v in DEFAULT_SIZES),
        help="statements per function, comma separated; every group of 64 holds "
        "each size equally often (default: %(default)s)",
    )
    ap.add_argument(
        "--mix",
        default="",
        help="weights of the statement kinds, e.g. ld1=30,st=10 (kinds: "
        + ", ".join(DEFAULT_MIX)
        + ")",
    )
    generate(ap.parse_args(argv))


if __name__ == "__main__":
    sys.exit(main())
