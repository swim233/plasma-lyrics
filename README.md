<div align="center">

# 🎵 Plasma 6 桌面歌词

**原生 Plasma 6 同步歌词部件 —— 跟随会话中任何 MPRIS 播放器，在桌面与面板上显示滚动歌词 支持逐字歌词**

[![Release](https://img.shields.io/github/v/release/swim233/plasma-lyrics?include_prereleases&style=flat-square&logo=github&color=1D99F3)](https://github.com/swim233/plasma-lyrics/releases)
[![CI](https://img.shields.io/github/actions/workflow/status/swim233/plasma-lyrics/ci.yml?branch=main&style=flat-square&logo=githubactions&logoColor=white&label=CI)](https://github.com/swim233/plasma-lyrics/actions/workflows/ci.yml)
[![AUR](https://img.shields.io/aur/version/plasma-lyrics-git?style=flat-square&logo=archlinux&logoColor=white&label=AUR)](https://aur.archlinux.org/packages/plasma-lyrics-git)
[![License](https://img.shields.io/github/license/swim233/plasma-lyrics?style=flat-square&color=blue)](https://github.com/swim233/plasma-lyrics/blob/main/LICENSE)

[![Plasma](https://img.shields.io/badge/KDE_Plasma-6-1D99F3?style=flat-square&logo=kde&logoColor=white)](https://kde.org/plasma-desktop/)
[![Qt](https://img.shields.io/badge/Qt-6.6+-41CD52?style=flat-square&logo=qt&logoColor=white)](https://www.qt.io/)
[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)

<img width="1672" height="941" alt="image" src="https://github.com/user-attachments/assets/b764b95a-2f49-4f84-abc9-baaea75e92e1" />


[English](README.en.md) · [更新日志](CHANGELOG.md) · [报告问题](https://github.com/swim233/plasma-lyrics/issues)

</div>

---

监听当前会话中的 MPRIS 播放器并显示同步歌词。经
`plasma-browser-integration` 支持浏览器中的网易云音乐网页版与 Apple Music 网页版，
也支持本地 MPRIS 播放器（包括 Apple Music 客户端 Cider 与 Sidra）。

歌词默认依次从本地歌词、网易云、AMLL TTML DB、QQ音乐获取，并在首选源无可用歌词时自动回退。

歌词上方有一行常驻的曲目信息（标题 — 歌手），桌面默认开启、面板默认
关闭，两者可独立配置。

## ✨ 功能特性

- 🖥️ **桌面 + 面板双形态** —— 同一个部件，两种形态的外观配置各自独立互不影响
- 🎤 **同步滚动歌词** —— 按行高亮，可显示副歌词（翻译或罗马音），字体可单独设置
- 🔀 **多歌词源回退** —— 默认本地 → 网易云 → AMLL → QQ音乐
- 📁 **本地歌词源** —— 优先读取本地音频同级 `.lrc`，也可按统一匹配规则扫描自选歌词目录
- 🎨 **外观自由定制** —— 背景样式、文字描边、字体、字号、字重、颜色
- 🌗 **亮暗两套外观** —— 可跟随 Plasma 外观样式的亮暗自动切换
- 💤 **自动隐藏** —— 播放空闲超过可配置缓冲时间后隐藏
- 📝 **手动覆盖歌词** —— 放入 `.lrc` 文件即可替换任意歌曲的歌词

## 📦 安装

### Arch Linux（AUR，推荐）

AUR 上有三个包，互相冲突，装其中一个即可：

| 包                  | 说明                                                   |
| ------------------- | ------------------------------------------------------ |
| `plasma-lyrics`     | 下载最新发布版的源码，在本地编译                       |
| `plasma-lyrics-bin` | 直接安装 GitHub Release 上预编译好的版本，无需本地编译 |
| `plasma-lyrics-git` | 在本地编译 `main` 分支的最新代码                       |

```sh
yay -S plasma-lyrics      # 或 paru -S plasma-lyrics；另外两个包把包名换掉即可
```

### Debian 13（.deb）

从 [GitHub Releases](https://github.com/swim233/plasma-lyrics/releases) 下载
`.deb` 与 `SHA256SUMS`，校验后安装：

```sh
sudo apt install ./plasma-lyrics_*_amd64.deb
```

Release 同时提供 Arch 的 `.pkg.tar.zst` 与源码 tarball。

### 从源码构建

构建需要 CMake 3.24+、Ninja、C++20 编译器、Qt 6（≥ 6.6，含 Qt Declarative）、
KDE Frameworks 6（ECM 与 KI18n）、Plasma 6（libplasma）、gettext（生成翻译文件），
以及 zlib 与 fontconfig 开发文件。运行时还需要 Qt SQLite 驱动（歌词缓存用它），部件另需
Kirigami、KSvg 与 KDeclarative（设置页的颜色按钮来自其中的 `org.kde.kquickcontrols`）。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
```

顶层 CMake 选项可分别关闭各部分：`BUILD_DAEMON`、`BUILD_PLASMOID`、
`BUILD_IMPORT_WAYLYRICS`。全部歌词源固定编译，在部件设置的「歌词服务」页启停和排序。
旧的 `ENABLE_PROVIDER_NETEASE`、`ENABLE_PROVIDER_AMLL`、`ENABLE_PROVIDER_QQ`
设为 `OFF` 会导致配置失败；请从构建参数和已有 CMake 缓存中移除这些选项
（可用 `cmake -S . -B build -U 'ENABLE_PROVIDER_*'` 清理缓存）。

## 🚀 使用方式

### 1. 启动守护进程

安装完成后启用用户服务：

```sh
systemctl --user enable --now plasma-lyricsd.service
```

### 2. 添加部件

右键桌面或面板 →「添加或管理小部件…」→ 找到 **「桌面歌词」** 拖入即可。

> [!TIP]
> **全屏可见**：把放置歌词的面板的「显示/隐藏」设为「覆盖窗口」，是 Plasma 原生
> 保持歌词在最大化窗口周围可见的方式；桌面部件无法位于普通窗口之上。

### 3. 播放音乐

打开任意 MPRIS 播放器（如浏览器中的网易云音乐网页版），歌词即自动出现。

### 常用操作

| 操作                | 方式                                                                              |
| ------------------- | --------------------------------------------------------------------------------- |
| 歌词偏早 / 偏晚     | 右键部件 → 时序 **±0.5 s** 微调本曲偏移；「全局设置」可加全局偏移、直接改本曲偏移 |
| 选择当前曲歌词源    | 右键部件 → 选择自动、首选本地文件、网易云、AMLL 或 QQ音乐；临时回退不会覆盖该选择 |
| 立即重新搜索        | 右键部件 →「重新搜索歌词」，绕过已有命中和负缓存                                  |
| 修改外观            | 右键部件 →「配置 桌面歌词...」，桌面与面板形态的配置各自独立                      |
| 替换某首歌的歌词    | 将 `.lrc` 放入 `~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`（支持双语，见下文「歌词源」一节） |
| 迁移 waylyrics 缓存 | `plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics`                      |

## 🎵 支持的播放源

这里的“播放源”是提供 MPRIS 播放状态的播放器，不是获取歌词的 provider。

| 播放源             | 说明                                                                  |
| ------------------ | --------------------------------------------------------------------- |
| 网易云音乐网页版   | 经 `plasma-browser-integration`（浏览器扩展）接入，首个版本的主要音源 |
| Apple Music 网页版 | 同样经 `plasma-browser-integration` 接入（`music.apple.com`）         |
| 本地 MPRIS 播放器  | 任何实现了 MPRIS 接口的播放器；Cider 与 Sidra 按 Apple Music 处理     |
| KDE Connect 手机   | 支持，但默认忽略（可在「歌词服务」页的「播放器黑名单」中调整）        |

网易云音乐与 Apple Music 两个平台可在「歌词服务」页的「启用以下平台的歌词服务」中分别关闭。

## 🎤 歌词源

| 歌词源 | 说明 |
| ------ | ---- |
| 本地文件 | 默认首选。先找音频同级的 `.lrc`，再递归扫描歌词目录，按文件名与 `[ti:]`、`[ar:]`、`[al:]`、`[length:]` 标签匹配 |
| 网易云 | 在线检索，提供 LRC 与翻译 |
| AMLL TTML DB | 缓存元数据索引后本地检索，按需下载 TTML；提供翻译与逐字歌词 |
| QQ音乐 | 默认排在最后；提供逐字歌词、翻译与罗马音（罗马音仅此源提供） |

在「歌词服务」设置页可拖拽排序、启停各源、选择歌词目录，以及调整网络地址、超时和 AMLL 索引刷新间隔。

**匹配说明**

- 标题带中文译名括号（如 `青さは止んだ (青春已逝)`）时，会再用去掉括号的标题试一次，要求时长相差 2 秒以内。歌词目录里的 `.lrc` 需带 `[length:mm:ss]` 标签才能比较时长；AMLL 索引不含时长，这类标题匹配不到。
- 本地 `.lrc` 与覆盖目录里的 `.lrc` 支持双语：同一时间戳的第一行是原文、第二行是译文，其余忽略；若像制作人员信息（如「作词：」）则原样保留。
- 覆盖目录按 `<provider>:<track-id>.lrc` 精确替换已有结果，不同于可搜索的歌词目录。

**网络代理**：「歌词服务」页可选直连、系统代理或自定义地址（`socks5://主机:端口` 或 `http://主机:端口`，可带用户名和密码，**明文保存**）。代理作用于网易云、AMLL 与 QQ音乐，不为回环地址旁路。地址非法时无法保存；已保存的非法地址会让这三个源在重启服务后不可用。

AMLL 数据库以 CC0 提供；歌词原作及第三方内容的权利仍归相应权利人。项目与贡献者信息见
[AMLL TTML DB](https://github.com/amll-dev/amll-ttml-db)。

## ⚙️ 配置与数据文件

| 路径                                              | 用途                                                 |
| ------------------------------------------------- | ---------------------------------------------------- |
| `~/.config/plasma-lyrics/plasma-lyricsd.ini`      | 守护进程配置（INI）                                  |
| `~/.local/share/plasma-lyrics/lyrics.db`          | 歌词缓存（SQLite），含每首歌的偏移、首选源与全局偏移 |
| `~/.local/share/plasma-lyrics/lyrics/`            | 默认的可搜索本地歌词目录                             |
| `~/.local/share/plasma-lyrics/overrides/`         | 手动 `.lrc` 覆盖目录                                 |
| `~/.local/share/plasma-lyrics/plasma-lyricsd.log` | 可选日志文件（默认关闭）                             |
| `~/.cache/plasma-lyrics/amll-index.jsonl`         | AMLL 元数据索引缓存                                  |
| `$XDG_RUNTIME_DIR/plasma-lyricsd/state.json`      | 整曲歌词原子快照，部件读取歌词与播放状态的唯一来源   |

部件的外观与文本设置保存在各部件自己的配置页，桌面与面板互不影响。

## 🔧 诊断

### 离线复现匹配

```sh
# 复现一首歌的完整匹配过程，不依赖正在播放的音乐
plasma-lyricsd --explain "歌名" "歌手"

# 只诊断某个歌词源，并指定播放平台
plasma-lyricsd --explain --provider amll --platform apple "歌名" "歌手"

# 给出曲目时长（毫秒），复现依赖时长的判定
plasma-lyricsd --explain --length-ms 215000 "歌名" "歌手"
```

输出包含歌词源链、各源的匹配过程与拒绝原因；`--provider` 指定的源不可用时，会列出可用来源。

### 查看日志

```sh
journalctl --user -u plasma-lyricsd.service -f   # 跟随守护进程日志
journalctl --user -u plasma-lyricsd -p 4         # 只看警告及以上
```

日志里的分类名省略 `plasmalyrics.` 前缀。info 级日志会记录关键解析行为，同一次解析共用一个 `#编号`：

```
#42 resolve: trigger=track-changed identity="Google Chrome" service=org.mpris.MediaPlayer2.plasma-browser-integration fingerprint="mediaSrc:0f3e…" platform=netease music=true title="劣等上等" artist="鏡音リン"
#42 cache miss
#42 search provider=local candidates=0 selected=none elapsed=3ms
#42 search provider=netease candidates=1 selected=1294899572 score=0.900 elapsed=1488ms
#42 fetched: netease/1294899572 lines=59 hasWords=false elapsed=560ms
#42 state=ok from=provider source=netease/1294899572 lines=59 tried=local elapsed=2061ms
```

`trigger=` 表示触发原因：`startup`（启动）、`track-changed`、`player-changed`、`replay`（循环重播）、`research`（手动重搜）、`set-preferred` / `clear-preferred`（指定 / 清除首选源）。

### 调试日志

在「歌词服务」→「诊断」打开「记录调试信息」（默认关闭，重启服务后生效），下列分类会额外输出 debug 级日志，如候选打分、缓存查找：

| 分类 | 内容 |
| --- | --- |
| `plasmalyrics.daemon` | 服务生命周期与控制调用 |
| `plasmalyrics.resolver` | 歌词解析流程 |
| `plasmalyrics.mpris` | MPRIS 播放器发现与状态 |
| `plasmalyrics.provider.netease` / `.amll` / `.local` / `.qq` | 各歌词源的请求与目录扫描 |

只想调试单个分类时，可用 `QT_LOGGING_RULES`（优先级更高）。守护进程由 `systemd --user` 管理，需先写入用户实例再重启服务：

```sh
systemctl --user set-environment QT_LOGGING_RULES="plasmalyrics.mpris.debug=true"
systemctl --user restart plasma-lyricsd

# 用完还原
systemctl --user unset-environment QT_LOGGING_RULES
systemctl --user restart plasma-lyricsd
```

`QT_LOGGING_RULES` 与 `kdebugsettings` 里要写完整分类名。`kdebugsettings` 也能单独开关这些分类，但勾选「记录调试信息」后以勾选为准。部件自己的 `plasmalyrics.config` 不受该开关影响，只能用 `kdebugsettings` 或 plasmashell 环境里的 `QT_LOGGING_RULES` 控制。

### 设置变更日志

修改设置时会记录变更，例如：

```
config changed store=db key=globalOffsetMs old="0" new="-300"
config changed store=applet applet=12 form=desktop key=desktopFontSize old="34" new="36"
```

部件侧的记录在 plasmashell 日志里，用 `journalctl --user QT_CATEGORY=plasmalyrics.config` 查看；守护进程也会收到同一条。代理地址记录时不带凭据。

## 🛠️ 开发检查

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint --bare -I build/bin -I /usr/lib/qt6/qml \
  --unqualified disable --max-warnings 0 \
  frontend/plasmoid/package/contents/config/config.qml \
  frontend/plasmoid/package/contents/ui/*.qml \
  frontend/plasmoid/package/contents/ui/config/*.qml
sh frontend/plasmoid/translations/check-pot-freshness.sh
sh frontend/plasmoid/translations/check-message-coverage.sh
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

- qmllint 要用 Qt 6 的 `/usr/lib/qt6/bin/qmllint`；`--bare` 让它不去解析本机已安装的本项目模块，只用 `build/bin` 里新构建的。
- 两个翻译脚本分别检查翻译模板是否过期、是否漏掉 `.qml` 文件。
- CI 在 Arch Linux 与 Debian 13 上构建并测试；Arch 另有 `-Werror` 构建、qmllint、翻译检查和 namcap，Debian 打 `.deb` 包。各版本变更见 [CHANGELOG.md](CHANGELOG.md)。

## 📄 许可证

[GPL-2.0-only](LICENSE)
