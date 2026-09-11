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

监听当前会话中的 MPRIS 播放器并显示同步歌词。经
`plasma-browser-integration` 支持浏览器中的网易云音乐网页版，也支持本地 MPRIS
播放器。

歌词默认依次从本地歌词目录、网易云、AMLL TTML DB 获取，并在首选源无可用歌词时自动回退。

歌词上方有一行常驻的曲目信息（标题 — 歌手），桌面默认开启、面板默认
关闭，两者可独立配置。

## ✨ 功能特性

- 🖥️ **桌面 + 面板双形态** —— 同一个部件，两种形态的外观配置各自独立互不影响
- 🎤 **同步滚动歌词** —— 按行高亮，支持翻译行显示
- 🔀 **多歌词源回退** —— 默认本地 → 网易云 → AMLL，可拖拽排序、逐源启停，也可为当前歌曲指定首选源或立即重搜
- 📁 **本地歌词源** —— 优先读取本地音频同级 `.lrc`，也可按统一匹配规则扫描自选歌词目录
- 📚 **AMLL TTML 支持** —— 保存逐词时间与来源信息；当前界面仍按行显示，不提供逐词高亮
- 🪟 **全屏可见** —— 面板形态配合「窗口置于下方」，歌词在最大化窗口旁依然可见
- 🎨 **外观自由定制** —— 底板样式（主题 / 纯色 / 无）、文字描边、字号、六档字重、颜色
- 📏 **溢出策略** —— 自适应缩放 `fit` / 换行 `wrap` / 跑马灯 `marquee`
- 🎞️ **切行动画** —— 无 / 淡入淡出 / 滑动
- 💤 **自动隐藏** —— 播放空闲超过可配置缓冲时间后隐藏（桌面淡出、面板归还空间），曲目开始时恢复；默认关闭
- ⏱️ **时序微调** —— 右键菜单前后调整 0.5 s，默认按歌曲单独记录、同曲所有部件共享；可在「全局设置」改为所有歌曲共用一个偏移
- 📝 **手工覆盖歌词** —— 放入 `.lrc` 文件即可替换任意歌曲的歌词

## 📦 安装

### Arch Linux（AUR，推荐）

预编译版本
```sh
yay -S plasma-lyrics      # 或 paru -S plasma-lyrics
```
也可选择自行编译
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
`BUILD_IMPORT_WAYLYRICS`、`ENABLE_PROVIDER_NETEASE`、`ENABLE_PROVIDER_AMLL`。

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
| 选择当前曲歌词源    | 右键部件 → 选择自动、首选本地文件、网易云或 AMLL；临时回退不会覆盖该选择          |
| 立即重新搜索        | 右键部件 →「重新搜索歌词」，绕过已有命中和负缓存                                  |
| 修改外观            | 右键部件 →「配置」，桌面与面板形态的配置各自独立                                  |
| 替换某首歌的歌词    | 将 `.lrc` 放入 `~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`（支持双语，见「歌词源」） |
| 迁移 waylyrics 缓存 | `plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics`                      |

## 🎵 支持的播放源

这里的“播放源”是提供 MPRIS 播放状态的播放器，不是获取歌词的 provider。

| 播放源            | 说明                                                                  |
| ----------------- | --------------------------------------------------------------------- |
| 网易云音乐网页版  | 经 `plasma-browser-integration`（浏览器扩展）接入，首个版本的主要音源 |
| 本地 MPRIS 播放器 | 任何实现了 MPRIS 接口的播放器                                         |
| KDE Connect 手机  | 支持，但默认忽略（可在守护进程配置中调整）                            |

## 🎤 歌词源

| 歌词源 | 说明 |
| ------ | ---- |
| 本地文件 | 默认首选；先找本地音频同级 `.lrc`，再递归扫描配置的歌词目录并按标题、歌手、专辑与时长匹配 |
| 网易云 | 在线检索 LRC 与翻译 |
| AMLL TTML DB | 默认补充源；缓存元数据索引后本地检索，按需下载 TTML；支持翻译和逐词时间的存储 |

可在部件的「歌词服务」设置页拖拽调整全局顺序、勾选启用来源、选择本地歌词目录，
以及调整网络地址、超时和 AMLL 索引刷新间隔。同一页可设置网络代理：直连、系统代理或自定义地址
（`socks5://主机:端口` 或 `http://主机:端口`，可写用户名和密码，明文保存），作用于网易云与 AMLL，
不为回环地址旁路；地址非法时设置页无法保存，已保存的非法地址会使这两个来源在服务重启后不可用，
只剩本地文件源。
歌词目录及其子目录中的 `.lrc` 会建立可复用的搜索索引；
它不同于下面按 provider 与 track id 精确替换已有结果的覆盖目录。
本地目录与覆盖目录中的 `.lrc` 都支持双语：相邻两行时间戳相同时，第一行为原文、第二行为译文，
同一时间戳只取前两行，第三行起忽略；任一行像制作人员信息（如「作词：」）时两行原样保留。
AMLL 数据库以 CC0 提供；歌词原作及第三方内容权利仍由相应权利人持有。项目与贡献者信息见
[AMLL TTML DB](https://github.com/amll-dev/amll-ttml-db)。

## ⚙️ 配置与数据文件

| 路径                                              | 用途                                 |
| ------------------------------------------------- | ------------------------------------ |
| `~/.config/plasma-lyrics/plasma-lyricsd.ini`      | 守护进程配置（INI）                  |
| `~/.local/share/plasma-lyrics/lyrics/`            | 默认可搜索本地歌词目录               |
| `~/.local/share/plasma-lyrics/overrides/`         | 手工 `.lrc` 覆盖目录（支持双语，见「歌词源」） |
| `~/.local/share/plasma-lyrics/plasma-lyricsd.log` | 可选日志文件（默认关闭）             |
| `~/.cache/plasma-lyrics/amll-index.jsonl`          | AMLL 元数据索引缓存                   |
| `$XDG_RUNTIME_DIR/plasma-lyricsd/state.json`      | 整曲歌词原子快照（前端唯一数据来源） |

部件的外观、文本等设置在该部件自身的配置页中修改，桌面与面板部件互不影响。

## 🔧 诊断

```sh
# 离线复现一首歌的匹配全过程（不依赖正在播放的音乐）
plasma-lyricsd --explain "歌名" "歌手"

# 只诊断某个歌词源；也可显式给出播放平台以复现平台相关匹配策略
plasma-lyricsd --explain --provider amll --platform apple "歌名" "歌手"

# 跟随守护进程日志
journalctl --user -u plasma-lyricsd.service -f
```

诊断会打印当前构建和配置中的歌词源链、各源的版本匹配层级与拒绝原因；指定未编译或未配置的
歌词源时会列出可用来源。

「歌词服务」设置页「诊断」下的「记录调试信息」开关（配置项 `logging/debug`，默认关闭）为以下
六个分类打开 debug 级日志，重启服务后生效：

| 分类 | 内容 |
| --- | --- |
| `plasmalyrics.daemon` | 服务生命周期与控制调用 |
| `plasmalyrics.resolver` | 歌词解析流程 |
| `plasmalyrics.mpris` | MPRIS 播放器发现与状态 |
| `plasmalyrics.provider.netease` | 网易云歌词源请求 |
| `plasmalyrics.provider.amll` | AMLL TTML 数据库歌词源请求 |
| `plasmalyrics.provider.local` | 本地歌词目录 |

开启全部分类的调试信息用设置页勾选框即可；只想临时调试某一个分类时，可用 `QT_LOGGING_RULES`
环境变量单独控制，优先级高于该配置项。守护进程以 `systemd --user` 服务运行，环境变量要先经
`set-environment` 写入该用户实例再重启服务才会生效：

```sh
systemctl --user set-environment QT_LOGGING_RULES="plasmalyrics.mpris.debug=true"
systemctl --user restart plasma-lyricsd

# 用完还原
systemctl --user unset-environment QT_LOGGING_RULES
systemctl --user restart plasma-lyricsd
```

安装 KDE 版部件（`BUILD_PLASMOID=ON`）后，这六个分类会出现在 `kdebugsettings` 中，可按分类
单独开关，但仅在「记录调试信息」关闭时生效——该勾选框的优先级高于 kdebugsettings，开启时会
覆盖在那里对单个分类的 debug 开关。

**读日志**：每行格式为 `[时间] 级别 分类 内容`。每次歌词解析都以 `#编号` 开头关联同一次请求
的全部日志行；编号不连续属正常现象：守护进程发现当前没有可解析的播放内容时（服务启动时无播放器、
最后一个播放器退出、剩余来源都被过滤且未在播放）会取消进行中的解析，也消耗一个编号。info 级
默认只在解析的起点、经过的关键节点、终点各打一行：

```
#42 resolve: trigger=track-changed identity="Spotify" service=org.mpris.MediaPlayer2.spotify fingerprint="mediaSrc:0f3e…" platform=netease music=true title="劣等上等" artist="鏡音リン"
#42 search provider=netease candidates=1 selected=1294899572 score=0.900 elapsed=1488ms
#42 state=ok from=provider source=netease/1294899572 lines=59 elapsed=2061ms
```

起点行的 `trigger=` 说明触发这次解析的原因：

| 取值 | 含义 |
| --- | --- |
| `startup` | 服务启动后的首次解析 |
| `track-changed` | 同一播放器切到新曲目 |
| `player-changed` | 当前活动播放器发生变化 |
| `replay` | 同一曲目循环播放进入新一轮 |
| `research` | 部件菜单「重新搜索歌词」 |
| `set-preferred` | 为当前曲目指定了首选歌词源 |
| `clear-preferred` | 清除了当前曲目的首选歌词源 |

开启「记录调试信息」后还会打印候选打分明细、缓存查找细节等 debug 级行。

## 🛠️ 开发检查

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint --bare -I build/bin -I /usr/lib/qt6/qml \
  frontend/plasmoid/package/contents/ui/*.qml \
  frontend/plasmoid/package/contents/ui/config/*.qml
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

CI 在 Arch Linux 与 Debian 13 双平台上构建、测试并打包（含分别关闭网易云与 AMLL
provider 的配置），各版本变更见 [CHANGELOG.md](CHANGELOG.md)。

## 📄 许可证

[GPL-2.0-or-later](LICENSE)
