#!/usr/bin/env python3
"""symbollist.py <kernel-src> > abi_symbollist.raw

Rebuilds the flat KMI symbol list that Google's GKI build uses for TRIM_UNUSED_KSYMS
(CONFIG_UNUSED_KSYMS_WHITELIST) for android12-5.10-2022-12, from the in-tree lists named in
build.config.gki.aarch64. Same result as kernel/build's copy_symbols.sh + abi/flatten_symbol_list:
every symbol in a section whose name ends in "whitelist" or "symbol_list", sorted, de-duplicated.

Also accepts a single already-concatenated list (e.g. the CI artifact abi_symbollist):
    symbollist.py --file abi_symbollist > abi_symbollist.raw
"""
import configparser
import re
import sys


def lists_from_build_config(src):
    cfg = open(f"{src}/build.config.gki.aarch64").read()
    main = re.search(r"^KMI_SYMBOL_LIST=(\S+)", cfg, re.M).group(1)
    extra = re.search(r'^ADDITIONAL_KMI_SYMBOL_LISTS="(.*?)"', cfg, re.M | re.S).group(1).split()
    return [f"{src}/{p}" for p in [main] + extra]


def flatten(text):
    sl = configparser.ConfigParser(allow_no_value=True, strict=False)
    sl.optionxform = str
    sl.read_string(text)
    syms = set()
    for sec in sl.sections():
        if sec.endswith(("whitelist", "symbol_list")):
            syms.update(sl[sec])
    return sorted(syms)


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--file":
        text = open(sys.argv[2]).read()
    elif len(sys.argv) == 2:
        text = "\n".join(open(p).read() for p in lists_from_build_config(sys.argv[1]))
    else:
        sys.exit(__doc__)
    print("\n".join(flatten(text)))


if __name__ == "__main__":
    main()
