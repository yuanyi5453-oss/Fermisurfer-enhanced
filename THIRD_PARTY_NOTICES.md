# 来源与第三方许可证

## FermiSurfer

应用基于 [Mitsuaki Kawamura 的 FermiSurfer](https://github.com/mitsuaki1987/fermisurfer)，MIT License。
本地增强版基线为 2.4.0，对应上游提交 `4690a55a84268c8bda4a2a1200607263956b465d`。
原作者版权声明保留在源码中，根目录 `LICENSE` 包含许可条款。

此仓库是独立增强版本，不代表上游官方发行。保留的 `docs/upstream_readme.md`
是上游原文参考，其中旧版目录、预编译 Windows 程序和构建说明不适用于本仓库；
请以根目录 README 的构建方法为准。

## GL2PS 1.4.2

来源：[GL2PS](https://geuz.org/gl2ps/)，Copyright (C) 1999–2020 C. Geuzaine。
原版归档 SHA256：`8d1c00c1018f96b4b97655482e57dcb0ce42ae2f1d349cd6d4191e7848d9ffe9`。
本仓库分发构建所需的 `gl2ps.c`、`gl2ps.h` 和两个完整许可证文件。
GL2PS 按其原有双重许可提供：GNU Library GPL version 2 or later，或 GL2PS License version 2 or later；不改为 MIT。

随附修改源码公开可用，修改仍遵守 GL2PS 原有许可：SVG 三角形使用较高精度坐标，
并增加 0.01 pt 同色接缝描边，减轻矢量查看器中的三角形缝隙。
动态库单独构建到 `build/lib`，不替换系统 GL2PS。

## 构建与运行依赖

wxWidgets、OpenGL/GLU 和 zlib 使用系统提供的库，未将其二进制打包进仓库。
公开测试样例由 `tests/make_fixture.py` 生成，仅为合成数学场，不是实际材料计算结果。
