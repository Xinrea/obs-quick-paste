## OBS Quick Paste 1.0.0

Paste a clipboard image into OBS as an independent, persistent native image source.

### Features

- Ctrl+V / Cmd+V in the main preview, Sources or Scenes area.
- Raw clipboard images, image MIME data and a single local static image file.
- Unique transparent PNG files, background encoding, safe failure rollback.
- Aspect-preserving layout, centering, selection, stacking and Undo / Redo.
- Studio Mode targets the Preview scene.
- Native text/source paste preservation and configurable interception.
- English / Simplified Chinese settings UI.

### Downloads

| Platform | Installer | Portable archive |
|---|---|---|
| Windows x64 | `obs-quick-paste-1.0.0-windows-x64.exe` | `obs-quick-paste-1.0.0-windows-x64.zip` |
| macOS Apple Silicon | `obs-quick-paste-1.0.0-macos-arm64.pkg` | `obs-quick-paste-1.0.0-macos-arm64.zip` |

Close OBS before installation. The Windows installer targets an existing OBS installation. The macOS installer places the plugin in the current user's `~/Library/Application Support/obs-studio/plugins/` directory; the ZIP supports manual installation to the same location. No OBS or Qt runtime is bundled. `SHA256SUMS.txt` covers all four downloads.

### Compatibility and known limits

- Build baseline: **OBS Studio 32.0.4 / Qt 6.8.3**. Windows x64 and macOS arm64 packages; no Intel macOS or Linux release package.
- Windows installer is **not Authenticode-signed**. macOS plugin is ad-hoc signed; installer is **not Developer ID-signed or notarized**. Operating-system download/install warnings may occur; do not disable security protections.
- This is the first release. Build/test success does **not** establish complete live OBS acceptance across chat applications, native Windows/macOS shortcuts, selection, Undo / Redo or Studio Mode. See `docs/testing.md` for the test matrix and actual validation record. The 200ms performance target has not been measured.
- No animation, multi-file paste or automatic image cleanup. Successfully used PNGs remain on disk for scenes, backups and undo. Uninstalling the plugin must not delete those images.
- Restart OBS after upgrades. Hot unloading is not supported.

---

首个正式版本提供剪贴板图片粘贴、持久化原生图片源、布局、选中、撤销重做及设置界面。安装前请退出 OBS。Windows 安装程序与 macOS PKG 尚未进行发行者签名/公证；版本号不代表所有实机验收已通过。图片文件会长期保留，请勿在卸载时误删仍被场景引用的图片。
