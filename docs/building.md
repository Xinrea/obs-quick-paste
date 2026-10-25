# 构建与安装

## 工具链

- CMake 3.28+、C++17。
- macOS：Xcode 16+ / macOS SDK 15+，首期 arm64；Intel 需单独构建 SDK 和插件。CI 使用 `macos-15`，避免旧 runner 默认 Xcode 15 的 SDK 不满足 OBS 32 要求。
- Windows：Visual Studio 2022，Desktop development with C++，x64。
- 插件：OBS 32.0.4 的公开 libobs/frontend SDK，以及 OBS 官方对应 Qt 6.8.3 依赖。
- 测试：Qt 6 Core/Gui/Widgets/Test 和 Ninja，不需要 OBS。

不要使用系统上碰巧找到的 Qt 5，也不要把另一套 Qt 动态库放进 OBS。下面的插件构建只生成插件及资源，不打包 Qt、libobs 或 frontend API 动态库。

## 独立构建插件

在仓库根目录执行。SDK bootstrap 会联网下载固定 SHA256 的官方 OBS 源码，并使用该版本 `buildspec.json` 中锁定的官方依赖，在仓库的 `.deps/` 中构建公开 SDK；不会修改已安装的 OBS。源码首次构建时间和下载量较大。

### macOS Apple Silicon

```sh
cmake -P cmake/BootstrapObs.cmake
cmake --preset macos-arm64 -C .deps/sdk-prefix.cmake
cmake --build --preset macos-arm64 --parallel
cmake --install build/macos-arm64 --config Release
cd build/macos-arm64
cpack -C Release -G ZIP -B packages
cpack -C Release -G productbuild -B packages
```

结果：

- 安装 staging：`build/macos-arm64/install/obs-quick-paste.plugin`
- ZIP：`build/macos-arm64/packages/obs-quick-paste-1.0.0-macos-arm64.zip`
- 安装程序：`build/macos-arm64/packages/obs-quick-paste-1.0.0-macos-arm64.pkg`

双击 PKG，通过系统安装器安装到当前用户的 `~/Library/Application Support/obs-studio/plugins/`，不需要管理员权限。OBS 32 arm64 从这个用户目录发现 `.plugin`，不能假设同名系统目录也被扫描。安装程序不包含另一份 OBS/Qt，不修改用户的配置和图片目录。ZIP 也可手动安装到同一用户插件目录。

`cmake --install` 默认仅写入构建目录，**不会安装到用户的 OBS 配置目录**。退出 OBS 后，手动将 `.plugin` bundle 复制到：

```text
~/Library/Application Support/obs-studio/plugins/
```

重新启动 OBS。工具菜单应出现“快速粘贴设置”。macOS 构建使用 ad-hoc 签名，不是 Developer ID 签名或 Apple 公证；不能视为面向公众的已公证发行包。不要为加载插件关闭系统安全机制。

验证 bundle：

```sh
codesign --verify --strict --verbose=2 build/macos-arm64/install/obs-quick-paste.plugin
otool -L build/macos-arm64/install/obs-quick-paste.plugin/Contents/MacOS/obs-quick-paste
```

插件在签名前使用宿主相对 RPATH，安装/打包不再重写二进制，避免破坏签名。运行库应指向 `@rpath` 或系统路径，不得指向开发者的 `.deps/`、Homebrew 或 Anaconda 路径。

### Windows x64

在 Visual Studio 2022 开发者 PowerShell 中。生成 EXE 安装程序还需要 [NSIS 3](https://nsis.sourceforge.io/)（`makensis` 可发现）；GitHub Windows runner 会按需通过 Chocolatey 安装：

```powershell
cmake -P cmake/BootstrapObs.cmake
cmake --preset windows-x64 -C .deps/sdk-prefix.cmake
cmake --build --preset windows-x64 --parallel
cmake --install build/windows-x64 --config Release
Push-Location build/windows-x64
cpack -C Release -G ZIP -B packages
cpack -C Release -G NSIS -B packages
Pop-Location
```

EXE 安装程序默认选择 `C:\Program Files\obs-studio`，允许选择已有 OBS 根目录并校验 `bin\64bit\obs64.exe`。退出 OBS 后运行安装；不会安装 OBS 本身。Windows EXE 尚未 Authenticode 签名。

便携 ZIP 包含：

```text
obs-plugins/64bit/obs-quick-paste.dll
data/obs-plugins/obs-quick-paste/locale/en-US.ini
data/obs-plugins/obs-quick-paste/locale/zh-CN.ini
```

退出 OBS，将这两个顶层目录合并到目标 OBS 安装根目录。不要将 DLL 单独复制到任意目录而遗漏本地化资源。需要安装目录写入权限；安装到便携 OBS 时应选择该便携实例的根目录。重新启动 OBS。

### 已有 SDK、Intel 与 Linux

已有公开 SDK、无 CMake 导出的 SDK fallback，以及 Intel 架构设置详见 [cmake/BUILDING.md](../cmake/BUILDING.md)。不要将 `libobs_DIR` 指向 framework 的真实 `Versions/A/Resources/cmake` 路径；OBS 32 导出文件的相对路径要求使用未展开的 `libobs.framework/Resources/cmake` 路径，或让 CMake 从 `Frameworks` 前缀自动查找。

Linux 当前提供 Qt 测试和通用安装规则，不承诺 Linux 插件发行及桌面剪贴板兼容性。

## 仅构建和运行测试

```sh
cmake --preset tests -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/platform
cmake --build --preset tests --parallel
ctest --preset tests
```

Ubuntu 24.04 可安装 `qt6-base-dev` 和 `ninja-build` 后省略前缀。CTest 设置 `QT_QPA_PLATFORM=offscreen`，不启动 OBS，不接触桌面真实剪贴板。

OBS 官方 Qt SDK 不包含 QtTest。在本次 macOS 27 / Xcode 27 验证环境中，完整 Qt 6.8.3 SDK 的 `FindWrapOpenGL.cmake` 仍请求系统已经移除的 AGL.framework，因此直接使用该 SDK 链接测试失败。实际通过的配置为：Core/Gui/Widgets 使用 OBS 官方 Qt 6.8.3，仅补充同版本 QtTest：

```sh
cmake --fresh --preset tests \
  -DCMAKE_PREFIX_PATH=/path/to/obs-deps-qt6-2025-08-23-universal \
  -DQt6Test_DIR=/path/to/full-Qt/6.8.3/macos/lib/cmake/Qt6Test
cmake --build --preset tests --parallel
ctest --preset tests
```

这是**测试程序的依赖配置**，不适用于在 OBS 中混装运行库，也未把 QtTest 加入插件发布包。旧版系统/CI 若能使用完整 SDK，无需此处理。

## 卸载与升级

退出 OBS 后移除或替换插件 bundle / DLL 及其本地化资源，然后重启。Windows EXE 安装会注册“OBS Quick Paste”卸载项，卸载程序为 `<OBS 根目录>\uninstall-obs-quick-paste.exe`，不覆盖 OBS 自己的卸载程序，仅删除插件列明的文件。macOS 删除 `~/Library/Application Support/obs-studio/plugins/obs-quick-paste.plugin` 即可卸载。

不要删除插件配置目录下的 `images/` 或自定义图片目录：即使不再安装插件，OBS 原生图片源仍通过这些文件工作。热卸载不受支持，因为 OBS 撤销历史可能保留插件回调。

## CI 与验收

`.github/workflows/build.yml` 在 branch push / PR 上执行 Linux Qt 测试、Windows x64 和 macOS arm64 插件及安装包构建。`workflow_dispatch` 仅构建，不发布。

推送 `v*` tag 会额外执行发布任务，所有版本校验、测试和平台构建通过后才创建 GitHub Release：

1. 修改 `CMakeLists.txt` 的版本，并更新 `RELEASE_NOTES.md` 和文档。
2. 提交并推送代码，等待分支 CI 通过。
3. 创建与工程版本一致的 tag，例如 `git tag -a v1.0.0 -m "Release 1.0.0"`，再 `git push origin v1.0.0`。
4. 发布任务核对 tag 与工程版本严格一致、四份资产齐全且非空，生成 `SHA256SUMS.txt`。
5. 上传 Windows EXE/ZIP、macOS PKG/ZIP 和校验和到 draft，上传成功才将 draft 转为公开的正式 release。

发布任务只在 tag push 时拥有 `contents: write`；普通构建仅有只读权限。失败时不会发布缺少某个平台的 release。可重跑失败任务并继续上传尚未发布的 draft；已经发布的 release 不会被重跑覆盖。不要移动已发布 tag，应修复后递增版本。macOS ad-hoc 签名会在 CI 检查，但没有自动使用 Developer ID/Authenticode 证书或公证凭据。

实际执行结果及尚未通过的实机验收见 [testing.md](testing.md)。
