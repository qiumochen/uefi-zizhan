#!/usr/bin/env python3
"""Build the host-only M3 A' test boot image.

Layout (kernel field, uncompressed):
  BootShim | UEFI FD | DTB | PONGFAT | 2 MiB pad | stock Android Image

The boot ramdisk field stays the stock boot_b cpio. The BootShim patches the
ABL FDT /chosen initrd for the iOS/UEFI path to point at the kernel-field
PONGFAT; the magic/Android path leaves /chosen untouched and jumps to the
stock Android Image with x0=FDT, x1=x2=x3=0.

This script writes only under --out-dir. It does not call adb/fastboot and does
not touch a partition.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PARTITION = 201326592
PAGE = 4096
MAGIC_ADDR = 0xA7500000
MAGIC_WORD = 0x5241
ANDROID_HANDOFF = Path("Platforms/Xiaomi/zizhanPkg/Applications/AndroidHandoff/AndroidHandoff.c")
PONGFAT_MAGIC = b"PONGFAT1"
PONGFAT_HEADER = 4096


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def align(value, unit):
    return (value + unit - 1) // unit * unit


def run(command, cwd=None, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit("command failed: %r\n%s" % (command, result.stdout))
    return result.stdout


def which(name, fallback):
    found = shutil.which(name)
    if found:
        return found
    if Path(fallback).is_file():
        return fallback
    raise SystemExit("missing tool: %s" % name)


def parse_boot(path):
    data = Path(path).read_bytes()
    if data[:8] != b"ANDROID!" or struct.unpack_from("<I", data, 40)[0] != 4:
        raise SystemExit("stock boot is not Android v4: %s" % path)
    kernel_size, ramdisk_size = struct.unpack_from("<2I", data, 8)
    header_size = struct.unpack_from("<I", data, 20)[0]
    if header_size != 1584:
        raise SystemExit("unexpected boot header size %d" % header_size)
    kernel_at = PAGE
    ramdisk_at = PAGE + align(kernel_size, PAGE)
    signature_at = ramdisk_at + align(ramdisk_size, PAGE)
    vbmeta_at = signature_at + PAGE
    if vbmeta_at + 0x380 > len(data):
        raise SystemExit("stock boot is too short")
    return {
        "data": data,
        "header": data[:PAGE],
        "kernel_size": kernel_size,
        "ramdisk_size": ramdisk_size,
        "kernel": data[kernel_at:kernel_at + kernel_size],
        "ramdisk": data[ramdisk_at:ramdisk_at + ramdisk_size],
        "signature_at": signature_at,
        "vbmeta_at": vbmeta_at,
        "signature": data[signature_at:signature_at + PAGE],
        "vbmeta": data[vbmeta_at:vbmeta_at + 0x380],
    }


def parse_pongfat_ios(path):
    data = Path(path).read_bytes()
    if data[:8] != b"ANDROID!":
        raise SystemExit("iOS image is not Android boot: %s" % path)
    kernel_size, ramdisk_size = struct.unpack_from("<2I", data, 8)
    ramdisk_at = PAGE + align(kernel_size, PAGE)
    ramdisk = data[ramdisk_at:ramdisk_at + ramdisk_size]
    if ramdisk[:8] != PONGFAT_MAGIC:
        raise SystemExit("iOS ramdisk is not PONGFAT1")
    if len(ramdisk) < PONGFAT_HEADER:
        raise SystemExit("PONGFAT too short")
    fat = ramdisk[PONGFAT_HEADER:]
    size = struct.unpack_from("<Q", ramdisk, 16)[0]
    crc = struct.unpack_from("<I", ramdisk, 24)[0]
    if size != len(fat) or zlib.crc32(fat) & 0xffffffff != crc:
        raise SystemExit("input PONGFAT header does not match its FAT")
    return ramdisk


def build_bootshim(out, fdt_patch):
    make = which("make", "/usr/bin/make")
    ld = which("ld.lld", "/opt/homebrew/bin/ld.lld")
    objcopy = which("llvm-objcopy", "/opt/homebrew/opt/llvm/bin/llvm-objcopy")
    nm = which("llvm-nm", "/opt/homebrew/opt/llvm/bin/llvm-nm")
    objdump = which("llvm-objdump", "/opt/homebrew/opt/llvm/bin/llvm-objdump")
    run([make, "-C", str(ROOT / "BootShim"), "clean"])
    run([make, "-C", str(ROOT / "BootShim"), "REQUIRES_KERNEL_HEADER=1",
         "FD_BASE=0xA7100000", "FD_SIZE=0x300000", "ZIZHAN_FDT_PATCH=%d" % fdt_patch,
         "LD=" + ld, "OBJCOPY=" + objcopy])
    elf = ROOT / "BootShim/BootShim.elf"
    binary = (ROOT / "BootShim/BootShim.bin").read_bytes()
    symbols = {}
    for line in run([nm, "-n", str(elf)]).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    for name in ("_Head", "_StackSize", "_AndroidOffset", "_PongFatOffset",
                 "_PongFatSize", "_FdOffset", "_Payload", "_MagicHit", "_NoMagic"):
        if name not in symbols:
            raise SystemExit("BootShim symbol missing: %s" % name)
    if symbols["_Payload"] != len(binary):
        raise SystemExit("_Payload (%#x) != BootShim.bin length (%#x)" %
                         (symbols["_Payload"], len(binary)))
    (out / "bootshim.elf").write_bytes(elf.read_bytes())
    (out / "bootshim.bin").write_bytes(binary)
    (out / "bootshim.disasm.txt").write_text(run([objdump, "-d", str(elf)]))
    return binary, symbols


def build_handoff(out, handoff=None):
    efi = out / "AndroidHandoff.efi"
    if handoff:
        data = Path(handoff).read_bytes()
        efi.write_bytes(data)
        return data
    clang = which("clang", "/usr/bin/clang")
    lld_link = which("lld-link", "/opt/homebrew/bin/lld-link")
    includes = [
        "-I" + str(ROOT / "Mu_Basecore/MdePkg/Include"),
        "-I" + str(ROOT / "Mu_Basecore/MdePkg/Include/AArch64"),
    ]
    obj = out / "AndroidHandoff.obj"
    run([clang, "--target=aarch64-windows-msvc", "-ffreestanding", "-fshort-wchar",
         "-fno-stack-protector", "-mno-red-zone", *includes, "-c",
         str(ROOT / ANDROID_HANDOFF), "-o", str(obj)])
    run([lld_link, "/subsystem:efi_application", "/entry:AndroidHandoffEntry",
         "/machine:arm64", "/nodefaultlib", "/out:" + str(efi), str(obj)])
    data = efi.read_bytes()
    if data[:2] != b"MZ":
        raise SystemExit("AndroidHandoff.efi is not PE")
    return data


def update_pongfat(pongfat, app, config, out):
    fat = pongfat[PONGFAT_HEADER:]
    if len(fat) != 96 * 1024 * 1024:
        raise SystemExit("unexpected input FAT size %d" % len(fat))
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    with tempfile.TemporaryDirectory(prefix="m3-aprime-fat-") as temp:
        temp = Path(temp)
        fat_path = temp / "fat.img"
        fat_path.write_bytes(fat)
        app_path = out / "AndroidHandoff.efi"
        app_path.write_bytes(app)
        cfg_path = temp / "unixnu.toml"
        cfg_path.write_text(config)
        epoch = 1700000000
        os.utime(app_path, (epoch, epoch))
        os.utime(cfg_path, (epoch, epoch))
        run(["mcopy", "-m", "-i", str(fat_path), "-o", str(app_path),
             "::/EFI/UniXNU/AndroidHandoff.efi"], env=env)
        run(["mcopy", "-m", "-i", str(fat_path), "-o", str(cfg_path),
             "::/EFI/UniXNU/unixnu.toml"], env=env)
        fat = fat_path.read_bytes()
    if len(fat) != 96 * 1024 * 1024:
        raise SystemExit("FAT size changed to %d" % len(fat))
    header = bytearray(PONGFAT_HEADER)
    struct.pack_into("<8sIIQII", header, 0, PONGFAT_MAGIC, 1, PONGFAT_HEADER,
                     len(fat), zlib.crc32(fat) & 0xffffffff, 0)
    return bytes(header) + fat


def read_ios_config(ios_image, out):
    pongfat = parse_pongfat_ios(ios_image)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    with tempfile.TemporaryDirectory(prefix="m3-aprime-cfg-") as temp:
        temp = Path(temp)
        fat_path = temp / "fat.img"
        fat_path.write_bytes(pongfat[PONGFAT_HEADER:])
        cfg_path = temp / "unixnu.toml"
        run(["mcopy", "-i", str(fat_path), "-n", "::/EFI/UniXNU/unixnu.toml",
             str(cfg_path)], env=env)
        return cfg_path.read_text()


def default_entry(explicit=None):
    # Default boot entry: android (the user decision for M3). M3_DEFAULT=pong-xnu only for the
    # unattended fastboot-boot test that exercises the iPadOS path of the new BootShim.
    value = explicit if explicit is not None else os.environ.get("M3_DEFAULT", "android")
    if value not in ("android", "pong-xnu"):
        raise SystemExit("M3_DEFAULT must be android or pong-xnu")
    return value


def new_config(existing, default, android):
    match = re.search(r"\[\[entry\]\]\s*\nid = \"pong-xnu\".*?(?=\n\[\[|\Z)",
                      existing, re.S)
    if not match:
        raise SystemExit("existing unixnu.toml has no pong-xnu entry")
    ios = match.group(0).strip()
    return (
        "version = 1\n"
        "[boot]\n"
        "timeout = 10\n"
        "default = \"" + default + "\"\n"
        "[kits]\n"
        "autoload = false\n"
        "[[driver]]\n"
        "path = \"DarwinBoot24.efi\"\n"
        "connect = false\n"
        + (
            "[[entry]]\n"
            "id = \"android\"\n"
            "title = \"Android\"\n"
            "type = \"efi\"\n"
            "path = \"AndroidHandoff.efi\"\n"
            if android else ""
        )
        + ios + "\n"
    )


def patch_shim(shim, symbols, android_offset, pongfat_offset, pongfat_size,
               fd_offset, kernel_field_size):
    data = bytearray(shim)
    for name, value in (("_AndroidOffset", android_offset),
                        ("_PongFatOffset", pongfat_offset),
                        ("_PongFatSize", pongfat_size),
                        ("_FdOffset", fd_offset),
                        ("_StackSize", kernel_field_size)):
        offset = symbols[name]
        if offset + 8 > len(data):
            raise SystemExit("%s is outside BootShim.bin" % name)
        struct.pack_into("<Q", data, offset, value)
    return bytes(data)


def make_image(args, out):
    boot = parse_boot(args.stock_boot)
    default = default_entry(args.default_entry)
    if default == "android" and not args.android:
        raise SystemExit("default android requires --android 1")
    pongfat = update_pongfat(
        parse_pongfat_ios(args.ios_image),
        build_handoff(out, args.handoff),
        new_config(read_ios_config(args.ios_image, out), default, bool(args.android)),
        out,
    )
    (out / "pongfat.ramdisk").write_bytes(pongfat)
    shim, symbols = build_bootshim(out, args.fdt_patch)
    fd = Path(args.uefi_fd).read_bytes()
    dtb = Path(args.dtb).read_bytes()
    if len(fd) != 0x300000:
        raise SystemExit("UEFI FD is %d bytes, expected 0x300000" % len(fd))
    ramdisk = pongfat if args.ramdisk == "pongfat" else boot["ramdisk"]
    pongfat_in_field = args.layout != "b1"
    if args.layout == "b1":
        if args.fdt_patch or args.android or args.ramdisk != "pongfat":
            raise SystemExit("b1 requires --fdt-patch 0 --android 0 --ramdisk pongfat")
        field = shim + fd + dtb
        pongfat_offset = 0
        fd_offset = 0
        android_offset = 0
    elif args.layout == "pongfat-first":
        if args.fdt_patch != 1:
            raise SystemExit("pongfat-first requires --fdt-patch 1")
        pad = align(len(shim), PAGE) - len(shim)
        pongfat_offset = pad
        fd_offset = pad + len(pongfat)
        field = shim + bytes(pad) + pongfat + fd + dtb
        android_offset = align(len(field), 2 * 1024 * 1024) if args.android else 0
        if args.android:
            field += bytes(android_offset - len(field)) + boot["kernel"]
    else:
        if args.ramdisk != "stock":
            raise SystemExit("standard layout requires --ramdisk stock")
        field = shim + fd + dtb + pongfat
        pongfat_offset = len(fd) + len(dtb)
        fd_offset = 0
        android_offset = align(len(field), 2 * 1024 * 1024) if args.android else 0
        if args.android:
            field += bytes(android_offset - len(field)) + boot["kernel"]
    if not args.android:
        android_offset = symbols["_Dead"]  # trap if a stray magic ever arrives
    shim = patch_shim(shim, symbols, android_offset, pongfat_offset,
                      len(pongfat) if pongfat_in_field else 0, fd_offset, len(field))
    field = shim + field[len(shim):]
    if len(field) > 199364608:
        raise SystemExit("kernel field %d exceeds the 190.13 MiB budget" % len(field))
    if field[:8] == b"ANDROID!":
        raise SystemExit("kernel field unexpectedly starts with ANDROID!")
    if args.android:
        if field[android_offset:android_offset + 2] != b"MZ":
            raise SystemExit("Android Image does not start with MZ")
        if field[android_offset + 56:android_offset + 60] != b"ARMd":
            raise SystemExit("Android Image ARM64 magic missing")
    if pongfat_in_field:
        at = symbols["_Payload"] + pongfat_offset
        if field[at:at + 8] != PONGFAT_MAGIC:
            raise SystemExit("PONGFAT is not at _Payload + _PongFatOffset")
    elif ramdisk[:8] != PONGFAT_MAGIC:
        raise SystemExit("B1 ramdisk is not PONGFAT1")

    header = bytearray(boot["header"])
    struct.pack_into("<I", header, 8, len(field))
    struct.pack_into("<I", header, 12, len(ramdisk))
    struct.pack_into("<I", header, 1580, PAGE)

    image = bytearray(PARTITION)
    image[0:PAGE] = header
    image[PAGE:PAGE + len(field)] = field
    ramdisk_at = PAGE + align(len(field), PAGE)
    image[ramdisk_at:ramdisk_at + len(ramdisk)] = ramdisk
    signature_at = ramdisk_at + align(len(ramdisk), PAGE)
    image[signature_at:signature_at + PAGE] = boot["signature"]
    vbmeta_at = signature_at + PAGE
    image[vbmeta_at:vbmeta_at + 0x380] = boot["vbmeta"]
    original = signature_at + PAGE
    footer = struct.pack(">4sIIQQQ28s", b"AVBf", 1, 0, original, vbmeta_at,
                         0x380, b"")
    if len(footer) != 64:
        raise SystemExit("AVB footer size")
    image[PARTITION - 64:] = footer
    output = out / "pong-xnu-aprime.img"
    output.write_bytes(image)
    if len(image) != PARTITION:
        raise SystemExit("image size")
    return {
        "output": output,
        "field": field,
        "shim": shim,
        "symbols": symbols,
        "android_offset": android_offset,
        "pongfat_offset": pongfat_offset,
        "pongfat": pongfat,
        "layout": args.layout,
        "fdt_patch": args.fdt_patch,
        "android": 1 if args.android else 0,
        "ramdisk_kind": args.ramdisk,
        "pongfat_in_field": 1 if pongfat_in_field else 0,
        "fd_offset": fd_offset,
        "default_entry": default,
        "ramdisk_at": ramdisk_at,
        "signature_at": signature_at,
        "vbmeta_at": vbmeta_at,
        "original_image_size": original,
        "fd": fd,
        "dtb": dtb,
        "boot": boot,
        "ramdisk": ramdisk,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uefi-fd", default=str(ROOT / "Build/zizhanPkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd"))
    parser.add_argument("--dtb", default=str(ROOT / "Resources/DTBs/zizhan.dtb"))
    parser.add_argument("--ios-image", required=True)
    parser.add_argument("--stock-boot", required=True)
    parser.add_argument("--out-dir", required=True)
    parser.add_argument("--layout", choices=("standard", "pongfat-first", "b1"),
                        default="standard")
    parser.add_argument("--fdt-patch", type=int, choices=(0, 1), default=1)
    parser.add_argument("--android", type=int, choices=(0, 1), default=1)
    parser.add_argument("--ramdisk", choices=("stock", "pongfat"), default="stock")
    parser.add_argument("--default-entry", choices=("android", "pong-xnu"), default=None)
    parser.add_argument("--handoff", default=None,
                        help="reuse an existing AndroidHandoff.efi instead of building it")
    args = parser.parse_args()
    out = Path(args.out_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    result = make_image(args, out)
    image = result["output"].read_bytes()
    manifest = {
        "format": "zizhan-m3-aprime-boot-v1",
        "inputs": {
            "uefi_fd": {"path": str(Path(args.uefi_fd).resolve()), "bytes": len(result["fd"]), "sha256": sha256(result["fd"])},
            "dtb": {"path": str(Path(args.dtb).resolve()), "bytes": len(result["dtb"]), "sha256": sha256(result["dtb"])},
            "ios_image": {"path": str(Path(args.ios_image).resolve()), "bytes": Path(args.ios_image).stat().st_size, "sha256": sha256(Path(args.ios_image).read_bytes())},
            "stock_boot": {"path": str(Path(args.stock_boot).resolve()), "bytes": Path(args.stock_boot).stat().st_size, "sha256": sha256(Path(args.stock_boot).read_bytes())},
        },
        "bootshim": {"bytes": len(result["shim"]), "sha256": sha256(result["shim"]),
                     "symbols": {k: v for k, v in result["symbols"].items() if k in
                                 ("_Head", "_StackSize", "_AndroidOffset", "_PongFatOffset",
                                  "_PongFatSize", "_FdOffset", "_Payload", "_MagicHit",
                                  "_NoMagic", "_Dead")}},
        "android_handoff": {"bytes": (out / "AndroidHandoff.efi").stat().st_size,
                            "sha256": sha256((out / "AndroidHandoff.efi").read_bytes())},
        "pongfat": {"bytes": len(result["pongfat"]), "sha256": sha256(result["pongfat"])},
        "layout": {
            "kind": result["layout"],
            "fdt_patch": result["fdt_patch"],
            "android": result["android"],
            "ramdisk_kind": result["ramdisk_kind"],
            "pongfat_in_field": result["pongfat_in_field"],
            "fd_offset": result["fd_offset"],
            "default_entry": result["default_entry"],
            "kernel_field_bytes": len(result["field"]),
            "android_offset": result["android_offset"],
            "pongfat_offset": result["pongfat_offset"],
            "ramdisk_offset": result["ramdisk_at"],
            "ramdisk_bytes": len(result["ramdisk"]),
            "signature_offset": result["signature_at"],
            "vbmeta_offset": result["vbmeta_at"],
            "original_image_size": result["original_image_size"],
            "partition_bytes": PARTITION,
        },
        "output": {"path": str(result["output"]), "bytes": len(image), "sha256": sha256(image)},
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
