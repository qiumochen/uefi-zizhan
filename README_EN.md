# uefi-zizhan: Xiaomi MIX Fold 2

[简体中文](README.md) | **English**

UEFI firmware for the Xiaomi MIX Fold 2 (zizhan, SM8475), derived from [uefi-pong](https://github.com/Chengdong-Yu/uefi-pong) and used by [UniXNU-zizhan](https://github.com/qiumochen/UniXNU-zizhan). The board files are in `Platforms/Xiaomi/zizhanPkg`:

- the Qualcomm DXE binaries come from `Binaries/pong` (same SM8475);
- the memory map follows the stock `uefiplat.cfg`: PIL Reserved is `0x1DC00000`, Demura and QTEE are removed;
- `SimpleFbDxe` scans out the inner-panel splash the bootloader leaves at `0xb8000000`, 2160×1916;
- the FV embeds the phone's live device tree (`FdtBlob/sm8475-xiaomi-zizhan.dtb`, also `Resources/DTBs/zizhan.dtb`);
- `RamoopsView` and `RamoopsDump` read the ramoops console back after a reset (the latter writes it to the logfs partition).

The build actually used is UniXNU-zizhan's [`tools/zizhan/mac/build_fw.sh`](https://github.com/qiumochen/UniXNU-zizhan/blob/main/tools/zizhan/mac/build_fw.sh) (plain EDK II on `Platforms/Xiaomi/zizhanPkg/zizhan.dsc`) and `build_bootimg.py`. Boot only with `fastboot boot`; no partition is written.

The original uefi-pong README follows.

---

# Project Silicium UEFI for Nothing Phone (2)


UEFI firmware source for Nothing Phone (2), codename **Pong**, SoC **SM8475**. This repository contains the Mu-Silicium UEFI tree and the Pong board package used by UniXNU.

## Description

The Pong platform is ARM64, uses the Qualcomm SM8475 configuration, provides a 1080 × 2412 framebuffer, and uses the Pong device tree. Board files are under `Platforms/Nothing/pongPkg`. UniXNU and DarwinBoot application and driver integration is included in the Pong DSC/FDF configuration.

This repository contains firmware source and build scripts only. Apple restore assets and generated firmware or boot images are not checked in. The companion [UniXNU-pong device repository](https://github.com/Chengdong-Yu/UniXNU-pong) fetches the required iOS 16.6.1 / 20G81 components when needed.

## Resources

- [Build instructions](Building.md)
- [Pong status](Status.md)
- [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)
- [UniXNU documentation](https://unixnu.com)

## Frequently Asked Questions

### Is Pong supported?

The Pong board configuration and UEFI image packaging are included. Development boot records show the device reaching the UniXNU menu and handing off to DarwinBoot. This does not mean that every operating system or hardware feature is supported.

### How do I build the UEFI firmware?

Clone this repository and its submodules, and place it beside the UniXNU core checkout. To build the UniXNU-integrated Pong firmware:

```sh
git clone --recurse-submodules https://github.com/Chengdong-Yu/uefi-pong
git clone --recurse-submodules https://github.com/UniXNU/UniXNU
cd uefi-pong
python3 -m pip install -r pip-requirements.txt
./setup_env.sh -p apt
bash scripts/build-firmware.sh
```

Set `UNIXNU_ROOT` if the UniXNU core checkout is elsewhere. On Debian or Ubuntu, `setup_env.sh -p apt` installs build dependencies. To build the base UEFI target without UniXNU DarwinBoot integration, use the standard Mu-Silicium command:

```sh
python3 -m pip install -r pip-requirements.txt
python3 build_uefi.py -d pong
```

`scripts/build-bootimg.py` packages the firmware volume and Pong DTB in an Android boot image v4 container. It does not write to a device.

## Credits

This source tree is based on [Project Silicium / Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium), Project Mu, and the Qualcomm and ARM platform packages referenced by its submodules. Pong hardware configuration is based on the public Mu-Silicium Pong package and Nothing SM8475 device-tree sources.

## License

Mu-Silicium is licensed under BSD-2-Clause-Patent, with upstream GPL exceptions documented in its license. Individual source files retain their SPDX identifiers.
