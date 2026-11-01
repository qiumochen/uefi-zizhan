# uefi-zizhan：小米 MIX Fold 2

**简体中文** | [English](README_EN.md)

小米 MIX Fold 2（zizhan，SM8475）的 UEFI 固件，从 [uefi-pong](https://github.com/Chengdong-Yu/uefi-pong) 派生，供 [UniXNU-zizhan](https://github.com/qiumochen/UniXNU-zizhan) 使用。板级文件在 `Platforms/Xiaomi/zizhanPkg`：

- 高通 DXE 沿用 `Binaries/pong`（同为 SM8475）；
- 内存表按原厂 `uefiplat.cfg` 修改：PIL Reserved 为 `0x1DC00000`，去掉 Demura 和 QTEE；
- `SimpleFbDxe` 直接使用 bootloader 留下的内屏画面，framebuffer 在 `0xb8000000`，2160×1916；
- FV 内嵌手机的 live 设备树（`FdtBlob/sm8475-xiaomi-zizhan.dtb`，同一份也在 `Resources/DTBs/zizhan.dtb`）；
- `RamoopsView` 和 `RamoopsDump` 在复位后读回 ramoops 日志（后者写入 logfs 分区）。

实际使用的构建脚本是 UniXNU-zizhan 的 [`tools/zizhan/mac/build_fw.sh`](https://github.com/qiumochen/UniXNU-zizhan/blob/main/tools/zizhan/mac/build_fw.sh)（直接调用 EDK II，构建 `Platforms/Xiaomi/zizhanPkg/zizhan.dsc`）和 `build_bootimg.py`。只用 `fastboot boot` 临时启动，不写入任何分区。

以下为上游 uefi-pong 的原始说明。

---

# Project Silicium UEFI：Nothing Phone (2)


Nothing Phone (2)（代号 **Pong**，SoC **SM8475**）的 UEFI 固件源码。本仓库包含 Mu-Silicium UEFI 工程和供 UniXNU 使用的 Pong 板级配置。

## 说明

Pong 平台使用 ARM64 和高通 SM8475 配置，显示缓冲区为 1080 × 2412，并使用 Pong 设备树。板级文件位于 `Platforms/Nothing/pongPkg`。UniXNU 与 DarwinBoot 的应用和驱动集成已加入 Pong 的 DSC/FDF 配置。

仓库仅包含固件源码和构建脚本，不包含 Apple restore 组件或构建生成的固件、启动镜像。

## 资源

- [构建说明](Building.md)
- [Pong 状态](Status.md)
- [UniXNU-pong](https://github.com/Chengdong-Yu/UniXNU-pong)
- [UniXNU 文档](https://unixnu.com)

## 常见问题

### Pong 是否已支持？

仓库包含 Pong 板级配置和 UEFI 镜像打包流程。已有开发启动记录显示设备进入 UniXNU 菜单并将控制权交给 DarwinBoot；这不代表所有操作系统或硬件功能均已支持。

### 如何构建 UEFI 固件？

克隆本仓库及其子模块，并将其与 UniXNU 核心仓库放在同级目录。构建 UniXNU 集成版 Pong 固件：

```sh
git clone --recurse-submodules https://github.com/Chengdong-Yu/uefi-pong
git clone --recurse-submodules https://github.com/UniXNU/UniXNU
cd uefi-pong
python3 -m pip install -r pip-requirements.txt
./setup_env.sh -p apt
bash scripts/build-firmware.sh
```

如果 UniXNU 核心仓库位于其他位置，可设置 `UNIXNU_ROOT`。在 Debian/Ubuntu 上，`setup_env.sh -p apt` 用于安装构建依赖。若只需构建基础 UEFI 目标而不集成 UniXNU DarwinBoot，可使用 Mu-Silicium 构建命令：

```sh
python3 -m pip install -r pip-requirements.txt
python3 build_uefi.py -d pong
```

`scripts/build-bootimg.py` 可将固件卷和 Pong DTB 打包为 Android boot image v4 容器；该脚本不会向设备写入数据。

## 致谢

本源码基于 [Project Silicium / Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)、Project Mu，以及子模块所引用的 Qualcomm 和 ARM 平台包。Pong 硬件配置参考公开的 Mu-Silicium Pong 板级包和 Nothing SM8475 设备树。

## 许可证

Mu-Silicium 使用 BSD-2-Clause-Patent 许可证，上游 GPL 例外条款见许可证文件。各源码文件保留其 SPDX 标记。
