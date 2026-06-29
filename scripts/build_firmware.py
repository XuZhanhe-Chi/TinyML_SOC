#!/usr/bin/env python3
import argparse
import os
import shlex
import shutil
import subprocess
import hashlib
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Toolchain:
    prefix: str
    gcc: str
    gxx: str
    objcopy: str
    objdump: str


def _which(name: str) -> str | None:
    return shutil.which(name)


def _find_toolchain(prefix: str | None) -> Toolchain:
    if prefix:
        prefixes = [prefix]
    else:
        prefixes = [
            os.environ.get("RISCV_TOOLCHAIN_PREFIX", "").strip(),
            "riscv-none-elf-",
            "riscv64-unknown-elf-",
            "riscv32-unknown-elf-",
        ]
        prefixes = [p for p in prefixes if p]

    for p in prefixes:
        gcc = _which(p + "gcc")
        gxx = _which(p + "g++")
        objcopy = _which(p + "objcopy")
        objdump = _which(p + "objdump")
        if gcc and gxx and objcopy and objdump:
            return Toolchain(prefix=p, gcc=gcc, gxx=gxx, objcopy=objcopy, objdump=objdump)

    raise SystemExit(
        "找不到 RISC-V toolchain，请安装并确保在 PATH；或设置 RISCV_TOOLCHAIN_PREFIX（例如 riscv-none-elf-）。"
    )


def _run(cmd: list[str], cwd: Path | None = None) -> None:
    printable = " ".join(shlex.quote(x) for x in cmd)
    print(f"[CMD] {printable}")
    subprocess.check_call(cmd, cwd=str(cwd) if cwd else None)


def _write_readmemh_words_le32(bin_path: Path, hex_path: Path) -> None:
    data = bin_path.read_bytes()
    pad = (-len(data)) % 4
    if pad:
        data += b"\x00" * pad

    with hex_path.open("w", encoding="utf-8") as f:
        for i in range(0, len(data), 4):
            b0, b1, b2, b3 = data[i : i + 4]
            word = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
            f.write(f"{word:08X}\n")


def main() -> int:
    ap = argparse.ArgumentParser(description="Baremetal RV32 firmware builder (elf/bin/readmemh/dump).")
    ap.add_argument("--out-dir", required=True, help="输出目录（建议 build/...）")
    ap.add_argument("--name", required=True, help="输出文件前缀（例如 prog）")
    ap.add_argument("--linker", required=True, help="linker script 路径（.ld）")
    ap.add_argument("--src", action="append", default=[], help="源文件（可重复）")
    ap.add_argument("-I", dest="inc", action="append", default=[], help="include 目录（可重复）")
    ap.add_argument("-D", dest="defs", action="append", default=[], help="宏定义（可重复）")
    ap.add_argument("--cflags", default="", help="额外 C/CXX flags（原样追加）")
    ap.add_argument("--ldflags", default="", help="额外 linker flags（原样追加）")
    ap.add_argument("--toolchain-prefix", default=None, help="toolchain 前缀（例如 riscv-none-elf-）")

    ap.add_argument("--march", default="rv32im_zicsr", help="默认 rv32im_zicsr")
    ap.add_argument("--mabi", default="ilp32", help="默认 ilp32")
    ap.add_argument("--opt", default="-Os", help="优化等级（默认 -Os）")
    ap.add_argument("--debug", action="store_true", help="开启 -g3/-O0，便于调试")
    ap.add_argument("--with-std", action="store_true", help="链接标准库/启动文件（默认关闭）")
    ap.add_argument(
        "--hosted",
        action="store_true",
        help="以 hosted 模式编译（不加 -ffreestanding）；用于需要 C++ STL/flatbuffers 的场景（例如 TFLite Micro）。",
    )
    ap.add_argument("--std", default="c11", help="C 标准（默认 c11）")
    ap.add_argument("--cxx-std", default="c++17", help="C++ 标准（默认 c++17）")
    ap.add_argument("--use-gxx-link", action="store_true", help="强制使用 g++ 链接")

    args = ap.parse_args()

    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    name = args.name
    elf_path = out_dir / f"{name}.elf"
    bin_path = out_dir / f"{name}.bin"
    hex_path = out_dir / f"{name}.hex"
    dump_path = out_dir / f"{name}.dump"
    map_path = out_dir / f"{name}.map"

    srcs = [Path(s).resolve() for s in args.src]
    if not srcs:
        raise SystemExit("至少需要一个 --src")

    linker = Path(args.linker).resolve()
    if not linker.exists():
        raise SystemExit(f"linker 不存在：{linker}")

    tc = _find_toolchain(args.toolchain_prefix)
    cc = tc.gcc
    cxx = tc.gxx

    common = [
        f"-march={args.march}",
        f"-mabi={args.mabi}",
        "-fno-toplevel-reorder",
        "-fno-builtin",
        "-Wall",
        "-Wextra",
        "-Werror=return-type",
    ]
    if not args.hosted:
        common.append("-ffreestanding")

    if args.debug:
        common += ["-O0", "-g3"]
    else:
        common += [args.opt]

    if not args.with_std:
        common += ["-nostdlib", "-nostartfiles"]

    incs = []
    for p in args.inc:
        incs += ["-I", str(Path(p).resolve())]

    defs = []
    for d in args.defs:
        defs += ["-D", d]

    extra_cflags = shlex.split(args.cflags) if args.cflags else []
    extra_ldflags = shlex.split(args.ldflags) if args.ldflags else []

    ld = [
        f"-Wl,-T,{linker}",
        "-Wl,-e,_start",
        "-Wl,--undefined=_start",
        f"-Wl,-Map,{map_path}",
    ]

    obj_dir = out_dir / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)

    objects: list[Path] = []
    has_cxx = False

    for s in srcs:
        suffix = s.suffix
        suffix_lower = suffix.lower()
        extra_lang_flags: list[str] = []
        stdflag: str | None = None
        if suffix_lower in [".cpp", ".cc", ".cxx"]:
            compiler = cxx
            stdflag = f"-std={args.cxx_std}"
            has_cxx = True
        elif suffix_lower in [".c"]:
            compiler = cc
            stdflag = f"-std={args.std}"
        elif suffix_lower in [".s"]:
            compiler = cc
            stdflag = None
            extra_lang_flags = ["-x", "assembler-with-cpp"] if suffix == ".S" else ["-x", "assembler"]
        else:
            raise SystemExit(f"不支持的源文件后缀：{s}")

        # Avoid collisions when multiple sources share the same basename
        # (e.g. `common.cc` appears in several TensorFlow Lite subdirs).
        digest = hashlib.sha1(str(s).encode("utf-8")).hexdigest()[:10]
        obj = obj_dir / f"{s.name}.{digest}.o"
        objects.append(obj)
        cmd = [compiler] + common + incs + defs + extra_cflags + extra_lang_flags
        if stdflag:
            cmd.append(stdflag)
        cmd += ["-c", str(s), "-o", str(obj)]
        _run(cmd)

    # Note: library ordering matters (e.g. "-lgcc"), so extra_ldflags must come
    # after objects; otherwise ld may not pull required symbols from archives.
    link = cxx if (args.use_gxx_link or has_cxx) else cc
    _run(
        [link]
        + common
        + incs
        + defs
        + extra_cflags
        + ld
        + ["-o", str(elf_path)]
        + [str(o) for o in objects]
        + extra_ldflags
    )

    dump_text = subprocess.check_output([tc.objdump, "-d", str(elf_path)], text=True)
    dump_path.write_text(dump_text, encoding="utf-8")

    _run([tc.objcopy, "-O", "binary", str(elf_path), str(bin_path)])
    _write_readmemh_words_le32(bin_path, hex_path)

    print(f"[OK] elf : {elf_path}")
    print(f"[OK] bin : {bin_path}")
    print(f"[OK] hex : {hex_path} (for $readmemh, LE32 words)")
    print(f"[OK] dump: {dump_path}")
    print(f"[OK] map : {map_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
