<div align="center">

# 🎵 Plasma 6 桌面歌词

**原生 Plasma 6 同步歌词部件 —— 跟随会话中任何 MPRIS 播放器，在桌面与面板上显示滚动歌词**

[![Release](https://img.shields.io/github/v/release/swim233/plasma-lyrics?include_prereleases&style=flat-square&logo=github&color=1D99F3)](https://github.com/swim233/plasma-lyrics/releases)
[![CI](https://img.shields.io/github/actions/workflow/status/swim233/plasma-lyrics/ci.yml?branch=main&style=flat-square&logo=githubactions&logoColor=white&label=CI)](https://github.com/swim233/plasma-lyrics/actions/workflows/ci.yml)
[![AUR](https://img.shields.io/aur/version/plasma-lyrics-git?style=flat-square&logo=archlinux&logoColor=white&label=AUR)](https://aur.archlinux.org/packages/plasma-lyrics-git)
[![License](https://img.shields.io/github/license/swim233/plasma-lyrics?style=flat-square&color=blue)](https://github.com/swim233/plasma-lyrics/blob/main/LICENSE)

[![Plasma](https://img.shields.io/badge/KDE_Plasma-6-1D99F3?style=flat-square&logo=kde&logoColor=white)](https://kde.org/plasma-desktop/)
[![Qt](https://img.shields.io/badge/Qt-6.6+-41CD52?style=flat-square&logo=qt&logoColor=white)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)

<img width="961" height="409" alt="桌面歌词部件截图" src="https://github.com/user-attachments/assets/e66dc90e-1889-4dab-9531-fb7821eb6000" />

[English](README.en.md) · [更新日志](CHANGELOG.md) · [报告问题](https://github.com/swim233/plasma-lyrics/issues)

</div>

---

监听当前会话中的 MPRIS 播放器并显示同步歌词。首个版本经
`plasma-browser-integration` 支持浏览器中的网易云音乐网页版；歌词源与前端均为
可扩展接口。歌词上方有一行常驻的曲目信息（标题 — 歌手），桌面默认开启、面板默认
关闭，两者可独立配置。

## ✨ 功能特性

- 🖥️ **桌面 + 面板双形态** —— 同一个部件，两种形态的外观配置各自独立互不影响
- 🎤 **同步滚动歌词** —— 按行高亮，支持翻译行显示
- 🪟 **全屏可见** —— 面板形态配合「窗口置于下方」，歌词在最大化窗口旁依然可见
- 🎨 **外观自由定制** —— 底板样式（主题 / 纯色 / 无）、文字描边、字号、六档字重、颜色
- 📏 **溢出策略** —— 自适应缩放 `fit` / 换行 `wrap` / 跑马灯 `marquee`
- 🎞️ **切行动画** —— 无 / 淡入淡出 / 滑动
- 💤 **自动隐藏** —— 播放空闲超过可配置缓冲时间后隐藏（桌面淡出、面板归还空间），曲目开始时恢复；默认关闭
- ⏱️ **时序微调** —— 右键菜单前后调整 0.5 s，默认按歌曲单独记录、同曲所有部件共享；可在「全局设置」改为所有歌曲共用一个偏移
- 📝 **手工覆盖歌词** —— 放入 `.lrc` 文件即可替换任意歌曲的歌词

## 📦 安装

### Arch Linux（AUR，推荐）

```sh
yay -S plasma-lyrics-git      # 或 paru -S plasma-lyrics-git
```

### Debian 13（.deb）

从 [GitHub Releases](https://github.com/swim233/plasma-lyrics/releases) 下载
`.deb` 与 `SHA256SUMS`，校验后安装：

```sh
sudo apt install ./plasma-lyrics_*_amd64.deb
```

Release 同时提供 Arch 的 `.pkg.tar.zst` 与源码 tarball。

### 从源码构建

需要 CMake 3.24+、Qt 6（≥ 6.6）、KDE Frameworks 6（ECM 与 KI18n）、Plasma 6、
Qt SQLite 驱动与 C++20 编译器。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

顶层 CMake 选项可分别关闭各部分：`BUILD_DAEMON`、`BUILD_PLASMOID`、
`BUILD_IMPORT_WAYLYRICS`、`ENABLE_PROVIDER_NETEASE`。

## 🚀 使用方式

### 1. 启动守护进程

安装完成后启用用户服务：

```sh
systemctl --user enable --now plasma-lyricsd.service
```

### 2. 添加部件

右键桌面或面板 →「添加部件」→ 找到 **「桌面歌词」** 拖入即可。

> [!TIP]
> **全屏可见**：将放置歌词的面板设为「窗口置于下方」可见性，是 Plasma 原生
> 保持歌词在最大化窗口周围可见的方式；桌面部件无法位于普通窗口之上。

### 3. 播放音乐

打开任意 MPRIS 播放器（如浏览器中的网易云音乐网页版），歌词即自动出现。

### 常用操作

| 操作                | 方式                                                                              |
| ------------------- | --------------------------------------------------------------------------------- |
| 歌词偏早 / 偏晚     | 右键部件 → 时序 **±0.5 s** 微调，默认按歌曲记录；「全局设置」可改为所有歌曲共用     |
| 修改外观            | 右键部件 →「配置」，桌面与面板形态的配置各自独立                                  |
| 替换某首歌的歌词    | 将 `.lrc` 放入 `~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc` |
| 迁移 waylyrics 缓存 | `plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics`                      |

## 🎵 支持的播放源

| 播放源            | 说明                                                                  |
| ----------------- | --------------------------------------------------------------------- |
| 网易云音乐网页版  | 经 `plasma-browser-integration`（浏览器扩展）接入，首个版本的主要音源 |
| 本地 MPRIS 播放器 | 任何实现了 MPRIS 接口的播放器                                         |
| KDE Connect 手机  | 支持，但默认忽略（可在守护进程配置中调整）                            |

## ⚙️ 配置与数据文件

| 路径                                              | 用途                                 |
| ------------------------------------------------- | ------------------------------------ |
| `~/.config/plasma-lyrics/plasma-lyricsd.conf`     | 守护进程配置（INI）                  |
| `~/.local/share/plasma-lyrics/overrides/`         | 手工 `.lrc` 覆盖目录                 |
| `~/.local/share/plasma-lyrics/plasma-lyricsd.log` | 可选日志文件（默认关闭）             |
| `$XDG_RUNTIME_DIR/plasma-lyricsd/state.json`      | 整曲歌词原子快照（前端唯一数据来源） |

部件的外观、文本等设置在该部件自身的配置页中修改，桌面与面板部件互不影响。

## 🔧 诊断

```sh
# 离线复现一首歌的匹配全过程（不依赖正在播放的音乐）
plasma-lyricsd --explain "歌名" "歌手"

# 跟随守护进程日志
journalctl --user -u plasma-lyricsd.service -f
```

## 🛠️ 开发检查

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint -I build/bin frontend/plasmoid/package/contents/ui/*.qml
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

CI 在 Arch Linux 与 Debian 13 双平台上构建、测试并打包（含 `ENABLE_PROVIDER_NETEASE=OFF`
配置），各版本变更见 [CHANGELOG.md](CHANGELOG.md)。

## 📄 许可证

[GPL-2.0-or-later](LICENSE)
