# 初音未来桌面宠物
这是一个初音未来桌面宠物项目，由copilot强力驱动（

详情请见[文档](DESIGN_QT.md)

## 应用图标（Windows / Linux / macOS）

- 将你想使用的图放到 `assets/app-icon.png`
- 配置阶段会自动生成：
	- Windows: `generated-icons/windows/mikupet.ico`
	- Linux: `generated-icons/linux/hicolor/*/apps/mikupet.png`
	- macOS: `generated-icons/macos/mikupet.icns`（若无 `iconutil` 则生成 `.iconset`）
- 若 `assets/app-icon.png` 缺失，将回退到 `drawable-hdpi/def01.png`

依赖：需要 Python 3 和 Pillow（`pip install Pillow`）。
