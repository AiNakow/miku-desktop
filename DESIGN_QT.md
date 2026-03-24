# MikuPet — Qt 6 C++ 跨平台桌面宠物设计方案

## 一、项目概述

以初音未来精灵帧图（`drawable-hdpi/`，240×240 RGBA PNG）为资源，
使用 **Qt 6 + CMake** 构建一个透明、无边框、始终置顶的桌面宠物，
可编译为以下所有平台与架构的原生应用：

| 平台    | 架构                          | 产物                               |
|---------|-------------------------------|------------------------------------|
| Linux   | x86_64 / aarch64             | AppImage / `.deb` / `.rpm`         |
| Windows | x86_64 / aarch64             | NSIS 安装程序 / 绿色 ZIP           |
| macOS   | x86_64 / aarch64 / Universal | `.app` Bundle / `.dmg`             |

---

## 二、技术选型

### 为什么选 Qt 6 C++？

| 方案         | 包体    | 原生  | 透明窗口 | 系统托盘 | shaped 窗口 | 跨平台编译 |
|--------------|---------|-------|----------|----------|-------------|------------|
| **Qt 6 C++** | ~8 MB   | ✅    | ✅       | ✅       | ✅          | ✅         |
| Tauri 2      | ~5 MB   | ✅    | ✅       | ✅       | JS 模拟     | ✅         |
| Electron     | ~80 MB  | ❌    | ✅       | ✅       | JS 模拟     | ✅         |
| SDL2         | ~3 MB   | ✅    | ✅       | ❌       | 平台相关    | ✅         |
| wxWidgets    | ~8 MB   | ✅    | 复杂     | ✅       | 部分支持    | ✅         |

Qt 6 的关键优势：
- **`QWidget::setMask(QBitmap)`**：像素级 shaped window，鼠标事件自动穿透透明像素，无需任何平台特定代码
- **`Qt::WA_TranslucentBackground`**：一行代码实现窗口背景透明
- **`QSystemTrayIcon`**：原生系统托盘，三平台统一 API
- **`QResources`**：将 285 帧精灵嵌入可执行文件，单文件分发
- **CMake + CPack**：原生支持 DEB / RPM / NSIS / DragNDrop 各格式打包

---

## 三、资源清单（drawable-hdpi/）

240×240 RGBA PNG，帧编号从 01 开始（两位补零）：

| 动画            | 帧数 | FPS | 循环  | 触发方式         |
|-----------------|------|-----|-------|------------------|
| `def`           | 18   | 8   | ✅    | 默认待机         |
| `hello`         | 10   | 8   |       | 左键点击         |
| `bye`           | 14   | 8   |       | 托盘菜单/保留    |
| `excited`       | 8    | 10  |       | 随机             |
| `shy`           | 16   | 8   |       | 右键单击         |
| `angry`         | 5    | 8   |       | 随机             |
| `cry`           | 12   | 8   |       | 随机             |
| `heart`         | 10   | 8   |       | 随机             |
| `question`      | 7    | 8   |       | 右键单击         |
| `dizzy`         | 19   | 8   |       | 随机             |
| `jump`          | 8    | 10  |       | 随机             |
| `rolling`       | 15   | 8   |       | 托盘「跳舞」     |
| `rotate`        | 10   | 8   |       | 随机             |
| `hairflip`      | 5    | 8   |       | 随机             |
| `sleep`         | 14   | 6   |       | 托盘「睡觉」     |
| `watch`         | 8    | 8   |       | 随机             |
| `drink`         | 33   | 10  |       | 随机             |
| `eatcake`       | 15   | 8   |       | 托盘「喂食」     |
| `listenmusic`   | 4    | 6   |       | 随机             |
| `notlistenmusic`| 23   | 8   |       | 随机             |
| `punching`      | 10   | 10  |       | 随机             |
| `lift`          | 12   | 8   | ✅    | 拖拽开始         |
| `fall`          | 9    | 12  |       | 拖拽结束         |

---

## 四、系统架构

```
┌────────────────────────────────────────────────┐
│                MikuPet 进程                    │
│                                                │
│  main.cpp                                      │
│    └── QApplication                           │
│          └── PetWindow (QWidget)               │
│                ├── AnimationEngine (QObject)   │
│                │     ├── QTimer (frame clock)  │
│                │     └── QMap<QVector<QPixmap>>│
│                │         (内嵌于 Qt Resources) │
│                ├── QSystemTrayIcon + QMenu     │
│                └── QSettings (位置持久化)      │
└────────────────────────────────────────────────┘
         ↕ OS native APIs
   Window Manager / Shell / SystemTray
```

### 类职责

| 类                 | 职责                                                  |
|--------------------|-------------------------------------------------------|
| `AnimationEngine`  | 精灵预加载、帧计时（QTimer）、状态通知（signals）    |
| `PetWindow`        | 透明窗口、绘制、拖拽、鼠标事件、托盘集成、位置持久化 |

---

## 五、透明像素穿透 — `setMask(QBitmap)` 方案

Qt 提供开箱即用的像素级 Shaped Window API：

```cpp
// 每帧更新一次 mask
QBitmap mask = pixmap.createMaskFromColor(Qt::transparent, Qt::MaskOutColor);
//   MaskOutColor: bit=1 where pixel != transparent  →  可接收鼠标事件
//   bit=0         →  鼠标事件穿透到下层窗口
setMask(mask);
```

- **Windows**：底层调用 `SetWindowRgn()`
- **Linux X11**：底层调用 `XShapeInput` / `XShapeCombineMask()`
- **macOS**：底层调用 `NSWindow contentView` 不规则形状

无需任何平台判断代码，天然跨平台。

---

## 六、动画状态机

```
        ┌── 随机等待(30~120s) ──► [随机动画] ──► ┐
        │                                       │
 ┌──────▼──────┐   onFinished   ┌───────────────┴──────┐
 │    Idle     │◄───────────────│     Animating        │
 │  (def 循环) │                │  (一次性动画播毕)    │
 └──────┬──────┘                └──────────────────────┘
        │ mousePressEvent                ▲
        │ (LeftButton)                  │ fall → onFinished
        ▼                               │
 ┌──────────────┐   mouseRelease ──► fall ──►
 │   Dragging   │   (moved)
 │  (lift 循环) │   mouseRelease ──► hello ──► idle
 └──────────────┘   (click, <300ms, no move)

 托盘触发:
   「喂食」  → eatcake → idle
   「跳舞」  → rolling → idle
   「睡觉」  → sleep   → idle
```

---

## 七、项目文件结构

```
pet/
├── DESIGN_QT.md
├── CMakeLists.txt              # 构建 + CPack 打包配置
├── .gitignore
├── src/
│   ├── main.cpp                # QApplication 入口
│   ├── AnimationEngine.h       # 精灵加载 + 帧计时
│   ├── AnimationEngine.cpp
│   ├── PetWindow.h             # 主窗口（透明/拖拽/托盘）
│   └── PetWindow.cpp
├── drawable-hdpi/              # 原始精灵资源（由 CMake 嵌入 QRC）
│   └── def01.png … watch08.png
└── .github/
    └── workflows/
        └── build-qt.yml        # 5 平台矩阵 CI/CD
```

---

## 八、构建系统设计

### 资源嵌入

使用 `qt_add_resources()` 将所有精灵直接嵌入可执行文件（零外部依赖运行）：

```cmake
file(GLOB SPRITE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/drawable-hdpi/angry*.png"
    ... # 每个动画名单独 GLOB 确保只收录动画帧
)
qt_add_resources(MikuPet "sprites"
    PREFIX "/sprites"
    BASE   "${CMAKE_SOURCE_DIR}/drawable-hdpi"
    FILES  ${SPRITE_FILES}
)
```

访问路径：`QPixmap(":/sprites/def01.png")`

### 打包（CPack）

| 平台    | 使用的 CPack 生成器    | 工具链                         |
|---------|------------------------|--------------------------------|
| Linux   | DEB / RPM / TXZ        | `linuxdeploy --plugin qt`      |
| Windows | NSIS / ZIP             | `windeployqt` + NSIS           |
| macOS   | DragNDrop              | `macdeployqt`                  |

### Qt 库部署

Qt 6.3+ 提供 CMake 原生部署助手：

```cmake
qt_generate_deploy_app_script(
    TARGET MikuPet
    OUTPUT_SCRIPT deploy_script)
install(SCRIPT ${deploy_script})
```

---

## 九、本地构建步骤

### 安装依赖

**Linux (Ubuntu 22.04+)**
```bash
sudo apt install build-essential cmake qt6-base-dev libqt6widgets6 \
     libgl1-mesa-dev
```

**macOS**
```bash
brew install cmake qt6
export CMAKE_PREFIX_PATH=$(brew --prefix qt6)
```

**Windows (MSVC)**
- 安装 Qt 6（Online Installer），选 MSVC 2022 x64 组件
- 安装 CMake 3.21+、Visual Studio 2022

### 编译

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### 打包

```bash
# 当前平台打包
cmake --install build --prefix install_dir
cpack --config build/CPackConfig.cmake

# macOS: 生成 .dmg（需先 macdeployqt）
macdeployqt build/MikuPet.app -dmg

# Linux: 生成 AppImage（需 linuxdeploy）
linuxdeploy --appdir AppDir -e build/MikuPet --plugin qt --output appimage
```

---

## 十、多平台 CI/CD（GitHub Actions）

`.github/workflows/build-qt.yml` 矩阵策略：

| Runner           | 目标                           | Qt 安装方式       |
|------------------|--------------------------------|-------------------|
| ubuntu-22.04     | x86_64-linux-gnu               | apt               |
| ubuntu-22.04     | aarch64-linux-gnu (cross)      | apt + crossbuild  |
| windows-latest   | x86_64-windows-msvc            | jurplel/install-qt-action |
| windows-latest   | aarch64-windows-msvc (cross)   | jurplel/install-qt-action |
| macos-latest     | universal (x86_64 + arm64)     | brew              |

---

## 十一、平台注意事项

| 平台    | 说明                                                       |
|---------|------------------------------------------------------------|
| Linux   | `setMask` 依赖 X11 Shape 扩展；Wayland 下需 `QT_QPA_PLATFORM=xcb` |
| Windows | `WA_TranslucentBackground` 需 Win10+；DWM 合成必须开启    |
| macOS   | `.app` bundle 需要 macdeployqt 复制 Qt 库；公证需 Apple 账号 |

---

## 十二、可扩展方向

- [ ] 设置对话框（缩放比例、动画速度）
- [ ] 开机自启（`QSettings(HKEY_CURRENT_USER/Run)`）
- [ ] 贴边吸附（检测屏幕边界）
- [ ] 多皮肤支持（运行时切换资源路径）
- [ ] 时间触发器（早晨 hello，深夜 sleep）
- [ ] 音效（通过 `QSoundEffect`）
