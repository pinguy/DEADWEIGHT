#!/usr/bin/env python3
import re
import sys


def has_call(src, name):
    return re.search(r"\b" + re.escape(name) + r"\s*\(", src) is not None


def has_func_def(src, name):
    return re.search(r"\b(?:void\s*\*|int|unsigned\s+long)\s+" + re.escape(name) + r"\s*\(", src) is not None


def insert_after_includes(src, block):
    lines = src.splitlines(True)
    last = -1
    for i, line in enumerate(lines):
        if line.lstrip().startswith("#include "):
            last = i
    if last < 0:
        return block + "\n" + src
    lines.insert(last + 1, block + "\n")
    return "".join(lines)


def drop_include(src, header):
    return re.sub(rf"^\s*#include\s+<{re.escape(header)}>\s*\n", "", src, flags=re.M)


def ensure_include(src, header):
    if re.search(rf"^\s*#include\s+<{re.escape(header)}>\s*$", src, flags=re.M):
        return src
    lines = src.splitlines(True)
    last = -1
    for i, line in enumerate(lines):
        if line.lstrip().startswith("#include "):
            last = i
    inc = f"#include <{header}>\n"
    if last < 0:
        return inc + src
    lines.insert(last + 1, inc)
    return "".join(lines)


def static_texture_scratch(src, actions):
    if "copt_texH" in src or "#define TS" not in src:
        return src
    if (
        "float *hh=malloc(TS*TS*sizeof(float));" not in src
        and "unsigned char *alb=malloc(TS*TS*4);" not in src
    ):
        return src
    changed = False
    src2 = src.replace(
        "float *hh=malloc(TS*TS*sizeof(float));\n  unsigned char *alb=malloc(TS*TS*4), *nrm=malloc(TS*TS*4);",
        "float *hh=copt_texH;\n  unsigned char *alb=copt_texA, *nrm=copt_texN;",
    )
    if src2 != src:
        changed = True
        src = src2
    src2 = src.replace("unsigned char *alb=malloc(TS*TS*4);", "unsigned char *alb=copt_texA;")
    if src2 != src:
        changed = True
        src = src2
    src2 = src.replace("free(hh); free(alb); free(nrm);\n", "")
    src2 = src2.replace("  free(alb);\n", "")
    if src2 != src:
        changed = True
        src = src2
    if changed:
        src = re.sub(
            r"(#define\s+TS\s+[^\n]+\n)",
            r"\1static float copt_texH[TS*TS];\nstatic unsigned char copt_texA[TS*TS*4], copt_texN[TS*TS*4];\n",
            src,
            count=1,
        )
        actions.append("static texture scratch")
    return src


def static_level_batches(src, actions):
    if "copt_batch_store" in src:
        return src
    old_decl = "static float *batch[3]; static int bn[3], bcap[3];"
    if old_decl not in src:
        return src
    new_decl = (
        "#ifndef COPT_BATCH_FLOATS\n"
        "#define COPT_BATCH_FLOATS 65536\n"
        "#endif\n"
        "static float copt_batch_store[3][COPT_BATCH_FLOATS], *batch[3]={copt_batch_store[0],copt_batch_store[1],copt_batch_store[2]};\n"
        "static int bn[3];"
    )
    src = src.replace(old_decl, new_decl, 1)
    src = src.replace(
        "if(bn[b]+8>bcap[b]){ bcap[b]=bcap[b]?bcap[b]*2:4096; batch[b]=realloc(batch[b],bcap[b]*sizeof(float)); }",
        "if(bn[b]+8>COPT_BATCH_FLOATS)return;",
    )
    actions.append("static level batches")
    return src


def libc_indirection(src, actions):
    if "copt_load_c" in src or "dlopen" not in src or "dlsym" not in src:
        return src
    need_snprintf = has_call(src, "snprintf") and "#define snprintf" not in src and "p_snprintf" not in src
    need_strtoul = has_call(src, "strtoul") and "#define strtoul" not in src and "p_strtoul" not in src
    need_memset = has_call(src, "memset") and not has_func_def(src, "memset")
    need_memcpy = has_call(src, "memcpy") and not has_func_def(src, "memcpy")
    if not (need_snprintf or need_strtoul or need_memset or need_memcpy):
        return src

    leftover_heap = any(has_call(src, name) for name in ("malloc", "realloc", "free", "calloc"))
    unsupported_stdio = any(has_call(src, name) for name in ("printf", "fprintf", "puts", "fputs", "fopen", "fclose"))
    unsupported_stdlib = any(has_call(src, name) for name in ("atoi", "atof", "qsort", "system", "exit"))

    if not unsupported_stdio and need_snprintf:
        src = drop_include(src, "stdio.h")
    if not leftover_heap and not unsupported_stdlib and need_strtoul:
        src = drop_include(src, "stdlib.h")
    if need_memset or need_memcpy:
        src = drop_include(src, "string.h")
        src = ensure_include(src, "stddef.h")
    if need_snprintf:
        src = ensure_include(src, "stddef.h")

    block = ["/* c_optimizer: keep common libc helpers off DT_NEEDED. */"]
    if need_memset:
        block.append("void* memset(void*d,int c,size_t n){ unsigned char*p=d; while(n--)*p++=(unsigned char)c; return d; }")
    if need_memcpy:
        block.append("void* memcpy(void*d,const void*s,size_t n){ unsigned char*a=d; const unsigned char*b=s; while(n--)*a++=*b++; return d; }")
    if need_snprintf:
        block.append("typedef int(*copt_snprintf_t)(char*,size_t,const char*,...);")
        block.append("static copt_snprintf_t copt_snprintf;")
    if need_strtoul:
        block.append("typedef unsigned long(*copt_strtoul_t)(const char*,char**,int);")
        block.append("static copt_strtoul_t copt_strtoul;")
    if need_snprintf or need_strtoul:
        block.append("static void copt_load_c(void){")
        block.append('  void*h=dlopen("libc.so.6",1);')
        if need_snprintf:
            block.append('  copt_snprintf=(copt_snprintf_t)dlsym(h,"snprintf");')
        if need_strtoul:
            block.append('  copt_strtoul=(copt_strtoul_t)dlsym(h,"strtoul");')
        block.append("}")
        if need_snprintf:
            block.append("#define snprintf copt_snprintf")
        if need_strtoul:
            block.append("#define strtoul copt_strtoul")

    src = insert_after_includes(src, "\n".join(block))
    if need_snprintf or need_strtoul:
        src2 = re.sub(r"(int\s+main\s*\([^)]*\)\s*\{\n)", r"\1  copt_load_c();\n", src, count=1)
        src = src2
    actions.append("libc indirection")
    return src


def main():
    if len(sys.argv) != 3:
        print("usage: source_prepass.py <input.c> <output.c>", file=sys.stderr)
        return 2
    inp, out = sys.argv[1], sys.argv[2]
    with open(inp, "r") as f:
        src = f.read()
    actions = []
    src = static_texture_scratch(src, actions)
    src = static_level_batches(src, actions)
    src = libc_indirection(src, actions)
    with open(out, "w") as f:
        f.write(src)
    print(", ".join(actions) if actions else "none")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
