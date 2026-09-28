# FermiSurfer Enhanced

基于 [FermiSurfer 2.4.0](https://github.com/mitsuaki1987/fermisurfer) 的独立增强版，
增加三维中心翻转、二维整幅画面旋转，以及 PDF / SVG 真矢量和高清 PNG 导出。
主要运行与验证环境为 **Ubuntu 22.04 + WSLg**；这是源码发行，不包含 Windows 原生 `.exe`。

## 新增功能

| 控件 | 功能 |
| --- | --- |
| Left / Right / Up / Down | 三维视角旋转，Step 可选 90°或 180° |
| Cell center | 保持中央胞体的三维几何中心及深度固定；第一 BZ 以 Gamma 为中心，primitive cell 以三基矢之和的一半为中心 |
| Origin | 保留围绕 Gamma 原点的旋转方式 |
| 2D image rotation | Clockwise / Counterclockwise：整幅绘图绕画布中心顺/逆时针旋转 90°或 180°，不改三维姿态和光照 |
| Reset image | 只清除二维画面旋转 |
| Reset view | 重置三维旋转、缩放、位移及二维旋转 |
| Export... | PDF、SVG 或 PNG，指定尺寸、DPI 和 PNG 超采样 |

二维旋转包含色条和坐标标记，不旋转控制面板。屏幕和导出共享渲染路径，
SVG/PDF 保留矢量几何，不是截图包进矢量文件。`CCW` 表示累计逆时针角度，
因此顺时针 90°显示为 `CCW: 270 deg`。

## 构建

Ubuntu 22.04 已验证的依赖安装方式（需要管理员权限，仅在缺少依赖时执行）：

```bash
sudo apt update
sudo apt install build-essential libwxgtk3.0-gtk3-dev libgl1-mesa-dev libglu1-mesa-dev zlib1g-dev
git clone https://github.com/yuanyi5453-oss/Fermisurfer-enhanced.git
cd Fermisurfer-enhanced
bash build.sh
```

其他发行版请安装对应的 wxWidgets GTK/OpenGL 开发包，并确保 `wx-config` 可用。
GL2PS 源码和头文件已随附，无需安装系统 `libgl2ps-dev`。
产物为 `build/fermisurfer-enhanced` 和 `build/lib/libgl2ps_enhanced.so`，请保留其相对位置。
构建可能显示上游 `nesting.cpp` 的两个字符串常量警告；不影响本环境编译。

## 使用与导出

```bash
./build/fermisurfer-enhanced /path/to/input.frmsf
./build/fermisurfer-enhanced /path/to/input.bxsf
./build/fermisurfer-enhanced --export exports/view.pdf --width 3600 --height 3600 --dpi 600 /path/to/input.frmsf
./build/fermisurfer-enhanced --export exports/view.png --width 3600 --height 3600 --dpi 600 --supersample 2 /path/to/input.frmsf
```

GUI 和命令行导出都需要可用的 OpenGL 图形环境，例如 WSLg 或 Linux 桌面。
程序不会自动筛选穿越费米能的能带；请准备正确输入和费米能设置，或在界面选择显示的能带。
已有导出文件默认不会覆盖，明确需要替换时才加 `--overwrite`。

PNG 默认宽 3600 像素、600 DPI、2x 超采样；GUI 默认按当前画布比例确定高度。
PDF/SVG 是真矢量，但大型网格可能产生大文件，部分 SVG 查看器仍可能显示三角形接缝。
提高 DPI 不会增加原始能带/k 网格的物理信息；请在最终排版尺寸检查图片。

本仓库不自动修改 PATH 或替换系统 `fermisurfer`；可将本仓库 `build` 目录加入自己的 PATH，
以 `fermisurfer-enhanced` 启动并与原版并存。用户机器上的默认命令切换是独立安装配置，不随仓库执行。

## 可复现测试

```bash
bash tests/run_checks.sh --math-only
# 完整测试需要已构建的程序、python3 和图形环境：
bash tests/run_checks.sh
```

测试生成独立的合成三能带 FRMSF，不含个人计算数据，每次在 `test-output/run_*` 新目录输出结果。
覆盖数学旋转、真实 wx 按钮事件、三维中心固定、90°/180°往返、二维旋转不改三维状态、
PNG/SVG 导出、复位精确恢复、OpenGL 状态恢复及覆盖/扩展名/尺寸保护。
若当前 Python 装有 `numpy` 和 `Pillow`，还会比较旋转后的像素及 SVG 填充几何；
否则明确显示 `SKIP`，可在已有这些库的 Python 环境运行 `tests/check_pixels.py <输出目录>`。

发布前验证结果见 [docs/VALIDATION.md](docs/VALIDATION.md)。

## 已知边界

- 二维旋转不自动交换画布宽高或缩放；长方形画布转 90°可能裁切。必要时缩小视图或采用方形导出。
- 二维角度仅保留在当前窗口，尚未写入视角配置文件，也没有专用 CLI 参数；重启归零。
- 三维按钮和鼠标维持原三维控制逻辑，再叠加二维旋转；旋转不是改变手性的镜像反射。
- 主要验证不透明固定管线场景；立体图、截面、箭头等全部组合尚未全面回归。
- 最新二维功能验证 PNG 和 SVG；没有为该功能单独重做 PDF 视觉回归，PDF 复用共享矢量路径。

## 目录与许可

- `source/src`：应用源码；`source/tests`：C++ 数学及 GUI 集成测试。
- `source/third_party/gl2ps-1.4.2`：修改版 GL2PS 源码及原许可证。
- `tests`：不依赖个人路径的合成样例和测试脚本。
- `build.sh`：Linux/WSL 构建入口；生成文件不纳入版本管理。

应用遵循 MIT License；GL2PS 仍遵循其原有许可，见 [LICENSE](LICENSE) 和
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。保留原作者署名；本项目不是上游官方发行。
