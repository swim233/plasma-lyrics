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
- 🔀 **多歌词源回退** —— 默认本地 → 网易云 → AMLL → QQ音乐，可拖拽排序、逐源启停，也可为当前歌曲指定首选源或立即重搜
- 📁 **本地歌词源** —— 优先读取本地音频同级 `.lrc`，也可按统一匹配规则扫描自选歌词目录
- 📚 **AMLL TTML DB 与 QQ音乐支持** —— 现支持逐字高亮与动画，唱到每个词时可从字上飘起粒子
- 🪟 **全屏可见** —— 面板形态配合「窗口置于下方」，歌词在最大化窗口旁依然可见
- 🎨 **外观自由定制** —— 底板样式（主题 / 纯色 / 无）、文字描边、字体（可搜索已安装字体，曲目信息可单独设置）、字号、字重、颜色
- 🌗 **亮暗两套外观** —— 亮色、暗色各存一套外观设置，跟随 Plasma 外观样式的亮暗自动切换并渐变过渡，也可固定使用其中一套
- 📏 **溢出策略** —— 自适应缩放 `fit` / 换行 `wrap` / 跑马灯 `marquee`
- 🎞️ **切行动画** —— 无 / 淡入淡出 / 滑动
- 💤 **自动隐藏** —— 播放空闲超过可配置缓冲时间后隐藏（桌面淡出、面板归还空间），曲目开始时恢复；默认关闭
- ⏱️ **时序微调** —— 右键菜单前后调整 0.5 s，按歌曲单独记录、同曲所有部件共享；「全局设置」可再开一个所有歌曲共用的全局偏移，与每首歌自己的偏移叠加，当前歌曲的偏移也能在那里直接修改
- 📝 **手工覆盖歌词** —— 放入 `.lrc` 文件即可替换任意歌曲的歌词

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

右键桌面或面板 →「添加部件」→ 找到 **「桌面歌词」** 拖入即可。

> [!TIP]
> **全屏可见**：将放置歌词的面板设为「窗口置于下方」可见性，是 Plasma 原生
> 保持歌词在最大化窗口周围可见的方式；桌面部件无法位于普通窗口之上。

### 3. 播放音乐

打开任意 MPRIS 播放器（如浏览器中的网易云音乐网页版），歌词即自动出现。

### 常用操作

| 操作                | 方式                                                                              |
| ------------------- | --------------------------------------------------------------------------------- |
| 歌词偏早 / 偏晚     | 右键部件 → 时序 **±0.5 s** 微调本曲偏移；「全局设置」可加全局偏移、直接改本曲偏移 |
| 选择当前曲歌词源    | 右键部件 → 选择自动、首选本地文件、网易云、AMLL 或 QQ音乐；临时回退不会覆盖该选择 |
| 立即重新搜索        | 右键部件 →「重新搜索歌词」，绕过已有命中和负缓存                                  |
| 修改外观            | 右键部件 →「配置」，桌面与面板形态的配置各自独立                                  |
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
| 本地文件 | 默认首选；先找本地音频同级 `.lrc`，再递归扫描配置的歌词目录，按文件名与 LRC 的 `[ti:]`、`[ar:]`、`[al:]`、`[length:]` 标签匹配标题、歌手、专辑与时长 |
| 网易云 | 在线检索 LRC 与翻译 |
| AMLL TTML DB | 默认补充源；缓存元数据索引后本地检索，按需下载 TTML；提供翻译与逐字歌词 |
| QQ音乐 | 默认排在最后；在线检索，提供逐字歌词、翻译与罗马音（罗马音目前只有该源提供） |

可在部件的「歌词服务」设置页拖拽调整全局顺序、勾选启用来源、选择本地歌词目录，
以及调整网易云与 AMLL 的网络地址和超时、AMLL 索引刷新间隔。歌词目录及其子目录中的 `.lrc`
会建立可复用的搜索索引，目录内容变化后才重新解析；它不同于下面按 provider 与 track id
精确替换已有结果的覆盖目录。

播放器报出的标题若带中文译名括号（如 `青さは止んだ (青春已逝)`），匹配会额外用剥掉括号后的
标题再试一次；这类靠剥离才匹配上的结果需要两侧时长相差 2 秒以内才会被采用，避免撞上同名的无关
曲目。本地歌词目录里的 `.lrc` 若没有 `[length:mm:ss]` 标签就没有时长可比，这类标题会匹配不到，
补上该标签即可（音频同级的 sidecar 不受影响，它直接沿用播放器报的时长）。AMLL 的索引不提供
时长，该源上这类标题始终匹配不到。

本地歌词（音频同级的 `.lrc` 与歌词目录）和覆盖目录中的 `.lrc` 都支持双语：同一时间戳有多行时，
第一行为原文、第二行为译文，第三行起忽略；前两行中任一行像制作人员信息（如「作词：」）时，
该时间戳的全部行原样保留。

AMLL 数据库以 CC0 提供；歌词原作及第三方内容权利仍由相应权利人持有。项目与贡献者信息见
[AMLL TTML DB](https://github.com/amll-dev/amll-ttml-db)。

「歌词服务」设置页还可设置网络代理：直连、系统代理或自定义地址
（`socks5://主机:端口` 或 `http://主机:端口`，可写用户名和密码，明文保存），作用于网易云、AMLL 与
QQ音乐三个网络源，不为回环地址旁路；地址非法时设置页无法保存，已保存的非法地址会使这三个来源在服务
重启后不可用，只剩本地文件源。

## ⚙️ 配置与数据文件

| 路径                                              | 用途                                                   |
| ------------------------------------------------- | ------------------------------------------------------ |
| `~/.config/plasma-lyrics/plasma-lyricsd.ini`      | 守护进程配置（INI）                                    |
| `~/.local/share/plasma-lyrics/lyrics.db`          | 歌词缓存（SQLite），含每首歌的偏移、首选源与全局偏移   |
| `~/.local/share/plasma-lyrics/lyrics/`            | 默认可搜索本地歌词目录                                 |
| `~/.local/share/plasma-lyrics/overrides/`         | 手工 `.lrc` 覆盖目录（支持双语，见上文「歌词源」一节） |
| `~/.local/share/plasma-lyrics/plasma-lyricsd.log` | 可选日志文件（默认关闭）                               |
| `~/.cache/plasma-lyrics/amll-index.jsonl`         | AMLL 元数据索引缓存                                    |
| `$XDG_RUNTIME_DIR/plasma-lyricsd/state.json`      | 整曲歌词原子快照（部件读取歌词与播放状态的唯一来源）   |

部件的外观、文本等设置在该部件自身的配置页中修改，桌面与面板部件互不影响。

## 🔧 诊断

```sh
# 离线复现一首歌的匹配全过程（不依赖正在播放的音乐）
plasma-lyricsd --explain "歌名" "歌手"

# 只诊断某个歌词源；也可显式给出播放平台以复现平台相关匹配策略
plasma-lyricsd --explain --provider amll --platform apple "歌名" "歌手"

# 给出曲目时长（毫秒），复现依赖时长的判定，如上文带译名括号的标题
plasma-lyricsd --explain --length-ms 215000 "歌名" "歌手"

# 跟随守护进程日志
journalctl --user -u plasma-lyricsd.service -f
```

诊断会打印当前配置中启用的歌词源链、各源的版本匹配层级与拒绝原因；`--provider` 指定未启用或
未配置的歌词源时会列出可用来源。

「歌词服务」设置页「诊断」下的「记录调试信息」开关（配置项 `logging/debug`，默认关闭）为以下
七个分类打开 debug 级日志，重启服务后生效：

| 分类 | 内容 |
| --- | --- |
| `plasmalyrics.daemon` | 服务生命周期与控制调用 |
| `plasmalyrics.resolver` | 歌词解析流程 |
| `plasmalyrics.mpris` | MPRIS 播放器发现与状态 |
| `plasmalyrics.provider.netease` | 网易云歌词源请求 |
| `plasmalyrics.provider.amll` | AMLL TTML 数据库歌词源请求 |
| `plasmalyrics.provider.local` | 本地歌词目录 |
| `plasmalyrics.provider.qq` | QQ音乐歌词源请求 |

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

安装 KDE 版部件（`BUILD_PLASMOID=ON`）后，`kdebugsettings` 中会出现八个分类——上表七个加上
部件自己的 `plasmalyrics.config`（见下文「设置变更日志」）。「记录调试信息」只管上表这七个，
可按分类单独开关，但仅在该勾选框关闭时生效——勾选框的优先级高于 kdebugsettings，开启时会
覆盖在那里对单个分类的 debug 开关。`plasmalyrics.config` 不受这个开关影响，只能在
`kdebugsettings` 里单独开关，或对 plasmashell 所在的环境设置 `QT_LOGGING_RULES`。

**读日志**：输出形态取决于日志的去向。systemd journal 下每行是 `分类 内容`——时间与级别由 journald 自己记录，`journalctl` 因此按级别着色，`journalctl --user -u plasma-lyricsd -p 4` 可只看警告及以上；在终端直接运行时是 `[时间] 级别 分类 内容` 并按级别着色；重定向到文件或管道时同样是 `[时间] 级别 分类 内容`，不带颜色。三种形态与日志文件里分类名都省掉 `plasmalyrics.` 前缀（上表的 `plasmalyrics.mpris` 在日志里显示为 `mpris`），但 `QT_LOGGING_RULES` 与 `kdebugsettings` 仍须写完整名。每次歌词解析都以 `#编号` 开头关联同一次请求
的全部日志行；编号不连续属正常现象：守护进程发现当前没有可解析的播放内容时（服务启动时无播放器、
最后一个播放器退出、剩余来源都被过滤且未在播放）会取消进行中的解析，也消耗一个编号。info 级
默认只在解析的起点、经过的关键节点、终点各打一行：

```
#42 resolve: trigger=track-changed identity="Google Chrome" service=org.mpris.MediaPlayer2.plasma-browser-integration fingerprint="mediaSrc:0f3e…" platform=netease music=true title="劣等上等" artist="鏡音リン"
#42 cache miss
#42 search provider=local candidates=0 selected=none elapsed=3ms
#42 search provider=netease candidates=1 selected=1294899572 score=0.900 elapsed=1488ms
#42 fetched: netease/1294899572 lines=59 hasWords=false elapsed=560ms
#42 state=ok from=provider source=netease/1294899572 lines=59 tried=local elapsed=2061ms
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

**设置变更日志**：「歌词服务」「全局设置」两页保存、桌面外观/面板外观/文本三页的每个设置项、
以及「保存并重启服务」的请求与结果，都会各记一行，例如：

```
config changed store=db key=globalOffsetMs old="0" new="-300"
config changed store=applet applet=12 form=desktop key=desktopFontSize old="34" new="36"
```

部件这一份落在 plasmashell 的日志里，用 `journalctl --user QT_CATEGORY=plasmalyrics.config`
查看；守护进程会收到相同的一行并计入 `plasmalyrics.daemon`，因此同时出现在
`journalctl --user -u plasma-lyricsd` 与可选的日志文件里——服务未运行时守护进程这一份不存在，
只有部件那份。`network/proxyUrl` 一项记录时不带凭据。

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

qmllint 要用 Qt 6 的 `/usr/lib/qt6/bin/qmllint`，参数与 CI 相同，只多了 `--bare -I /usr/lib/qt6/qml`：
本机装有本项目的软件包时，不加它会先解析到系统里已安装的 QML 模块，而不是 `build/bin` 里新构建的那份。
两个翻译脚本分别检查提交的 `.pot` 是否与源码里的界面文字同步、`messages.sh` 是否覆盖了全部 `.qml` 文件。

CI 在 Arch Linux 与 Debian 13 上分别构建并运行测试。Arch 上还会检查翻译文件、以
`-Wall -Wextra -Wpedantic -Werror` 另行构建并测试、运行 qmllint，并构建 Arch 包、用 namcap
检查；Debian 上打出 `.deb` 包。各版本变更见 [CHANGELOG.md](CHANGELOG.md)。

## 📄 许可证

[GPL-2.0-only](LICENSE)
