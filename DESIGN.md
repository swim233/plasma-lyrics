# 桌面歌词 · Plasma 6 部件设计文档

> 本文档是 2026-09-03 一轮设计访谈的产出，记录 36 项已定决策、支撑它们的实测证据，以及明确不做的事。
> 实现开始前请先读第 1 节——那里的事实是本设计所有形状的来源。

## 0. 这个项目是什么

一个 KDE Plasma 6 小部件，在桌面上显示当前播放歌曲的歌词。

**为什么不用现成方案。** 本机已装 `waylyrics`（在用，缓存 3123 首、自定义主题 `new.css`）、
`osdlyrics`、以及 plasmoid `ink.chyk.lyricakde`（Lyrica）。诉求不是"没有能用的"，而是
**waylyrics 是一个贴在桌面上的独立 GTK 窗口，不是 Plasma 桌面的一部分**。本项目的目标是把
同等能力做成真正长在 Plasma 桌面里的东西，并最终替代 waylyrics。

**与两个参考实现的关系。** waylyrics（MIT）的 provider 与匹配思路可参考、可抄；它没有任何
可读取"当前歌词行"的对外接口（只有 `org.gtk.Actions` 控制动作 + ksni 托盘），所以"套壳复用"
这条捷径不存在——时间轴推进无论如何要自己做。Lyrica 的架构（Rust 后端 + QML 前端）与本项目
同构，但它没有偏移调节、后端配置全局共享导致多实例互踩、硬编码回环端口 15650、重启靠
`killall -9`。这些是本设计要避开的具体坑。

---

## 1. 已验证的环境事实

环境：Plasma 6.7.4 / Wayland / Qt 6.11.2 / Arch。以下每条都在本机实测或在上游源码中核对过。

### 1.1 MPRIS：音源的真实行为（全部为实测）

主音源是浏览器网页版网易云音乐，经 `plasma-browser-integration`（下称 pbi）暴露。

| 事实 | 证据 | 对设计的影响 |
|---|---|---|
| `Position` 真实推进 | 5.007s 墙钟内从 3.872s 走到 8.946s | 同步可行，这是项目最大风险已排除 |
| 元数据完整 | `title=老街北`, `artists=[...]`, `length=227.708s` | 可用于搜索匹配 |
| **`mpris:trackid` 恒为 `/org/kde/plasma/browser_integration/1337`** | 换歌前后不变；上游源码硬编码，注释 `// HACK this is needed or else SetPosition won't do anything` | **换歌检测不能用 trackid** |
| **pbi 从不发 `Seeked`** | 150s 监听：pbi 发 10 次 `PropertiesChanged`、0 次 `Seeked`；上游源码里是 `// FIXME actually invoke "Seeked" signal` | **拖进度条只能靠轮询 Position + 跳变检测** |
| `Metadata` 随 `PropertiesChanged` 主动推送 | 150s 内推送 16 次 | 换歌不需要轮询 |
| **同一份播放被两个服务同时暴露** | `chromium.instance3893`（pid 3893）与 pbi（pid 4330）并存 | 必须去重 |
| **chromium 自带服务把标签页标题当歌名** | `title='网易云音乐'`, `artist=['']`, `album=''` | 不去重会拿"网易云音乐"去搜歌词 |
| pbi 元数据含 `kde:pid` = 浏览器 pid | 实测 `kde:pid=3893` 恰为 `chromium.instance3893`；上游是 `getppid()` | 可用于去重，**但只有一个观测点**——匹配不上时降级为"只信 pbi" |
| **一个 pbi 服务在多个标签页之间切换** | 同一服务先报 `xesam:url=bilibili.com/video/BV1mkg36zEfX/`，后报 `xesam:url=music.163.com/st/webplayer` | 必须判断"这是音乐还是视频" |
| `xesam:artist` 是**单元素数组内塞斜杠拼接** | `['闹闹丶/FFF君/欧Ωhm/洛天依Official']`；源自 MediaSession 的单个 DOMString | 搜索前必须拆分清洗 |
| `xesam:url` 对网页版恒为 `https://music.163.com/st/webplayer` | 实测 | 可作音乐源白名单判据 |
| 真实音频 URL 在 `kde:mediaSrc`，含内容 hash | `.../c739729316bedb013393e5a6c543223f.mp3` | 最强的换歌指纹 |
| **拿不到网易云 song id** | MPRIS 里没有任何平台 id | 歌词只能靠"标题+歌手+时长"搜索匹配 |

### 1.2 网易云接口：能做什么、不能做什么（全部为实测）

- **明文 GET 搜索可用，无需 cookie、无需加密**：
  `GET https://music.163.com/api/search/get?s=<关键词>&type=1&limit=N` → `code=200`，
  返回 `songs[].{id,name,duration,artists[].name,album.name}`。**搜索结果里的 `artists` 是
  干净的数组**（脏的斜杠拼接只在查询侧）。
- **`lrc` 字段开头夹着 JSON 行**，不是 LRC：
  ```
  {"t":0,"c":[{"tx":"作词: "},{"tx":"初繁言"}]}
  {"t":933,"c":[{"tx":"作曲: "},{"tx":"闹闹丶"}]}
  [00:02.80]编曲/伴奏混音：闹闹丶
  ```
  标准 LRC 解析器会在前两行翻车，必须单独处理。
- **provider 使用 `/api/song/lyric/v1`，不是 `/api/song/lyric`**。两者返回的"制作人员"形态不同，
  这个差异曾经让过滤功能整体失效（见 §6.1）：v1 把制作人员返回成**结构化条目**
  （`{"t":0,"c":[{"tx":"作词: "},{"tx":"爆音常安","li":"…"}]}`，带艺人链接），可以按结构识别；
  老端点把同一条制作人员**摊平成带时间轴的行**且给冒号加了留白：`[00:00.000] 作词 : 爆音常安`。
  v1 用 GET 即可（`id&cp=false&lv=0&tv=0&rv=0&kv=0`），`tlyric` 照常返回。
- **逐字歌词（`yrc`）拿不到**。对 `晴天`(2652820720)、`起风了`(1330348068)、`花海`(2659569861)
  POST `/api/song/lyric/v1` 并带足 `yv/ytv/yrv` 参数，响应顶层键里**根本没有
  `yrc`/`ytlrc`/`yromalrc`**，`klyric` 恒空；再加 `os=pc` / `os=android` / `os=ios` +
  `appver` cookie 伪装官方客户端，依然只有 `lrc`。Rust 侧同样不通：`ncmapi2` 的 `LyricResp`
  只有 `{lrc, klyric, tlyric}`，`ncmapi` v1.0.0 未实现歌词。
  **结论：逐字歌词的门槛是自行实现 weapi/eapi 加密调用，不是多写一个解析器。**
- **同人/V家音乐的歌词前段是制作人员名单**。`老街北`(1299289240) 的前 7 行带真实时间轴：
  `[00:02.80]编曲/伴奏混音：闹闹丶`、`[00:05.60]调教：FFF君`、`[00:08.20]混音：小欧Ω`、
  `[00:11.00]曲绘：偶尤大肥羊`、`[00:14.00]PV/封面设计：Ansa`、`[00:16.80]文案：铭言君，Ansa`、
  `[00:19.60]歌姬：洛天依`，第一句真歌词在 `[00:28.63]若能再相见`。
  waylyrics 的默认过滤正则只认 `^作词`/`^作曲`，**这 7 行全部漏掉**——而这正是本项目的主要曲库。

### 1.3 Plasma 6 的能力边界（源码/本机核对）

- **桌面小部件永远在窗口之下**，没有任何"置顶"开关。想要"全屏也可见"的唯一 KDE 原生路径是
  **放进面板并把面板可见性设为「窗口置于下方」**（`WindowsGoBelow`）。真 OSD 悬浮要
  LayerShellQt 独立窗口——那就不是 plasmoid 了。
- **桌面小部件做不到毛玻璃。** KWin 的模糊插件里 `BlurEffect::shouldBlur()` 有
  `if (w->isDesktop()) return false;` 无条件否决；更根本的是桌面小部件与壁纸共享同一个
  `DesktopView` 窗口，而该窗口最底层、壁纸本身由它绘制。`desktopview.cpp` 里
  `blur`/`WindowEffects` 一次都没出现，对比 `panelview.cpp` 的
  `KWindowEffects::enableBlurBehind(this, ...)`。面板的模糊由 KWin 对整条面板统一施加，
  **单个部件无权决定**。主题里的 `translucentbackground.svgz` 只有 alpha 渐变、没有模糊滤镜。
- **QML 没有 text stroke**。`MultiEffect` 无 outline 属性且只有一组 shadow 参数。实做方式是
  **把 Text 复制 8 份按八方向偏移垫在真文字后**（短单行成本可接受）。`Qt5Compat.GraphicalEffects`
  本机已装（`Glow`/`DropShadow` 可用），但 KDE 的 Plasma 6 移植指南导向 `MultiEffect`。
- **超长文本三种策略均有原生支持**：`fontSizeMode: Text.HorizontalFit` + `minimumPixelSize`
  （低于下限自动转 elide）；`wrapMode: WordWrap` + `maximumLineCount: 2` + `ElideRight`
  （Qt 6 确认可共存）；跑马灯用 `NumberAnimation` on `x` + `clip: true`（走场景图动画驱动，
  不需要每帧 JS 定时器）。
- **一个 plasmoid 包可同时支持桌面与面板**：`Plasmoid.formFactor === PlasmaCore.Types.Planar`
  判别，`preferredRepresentation` 切换 compact/full。
- **plasmoid 包不能内嵌进程内原生 QML 插件**，原生 QML 模块必须系统级安装到
  `/usr/lib/qt6/qml/...`。**QML 模块 URI 不能带连字符。**
- **`org.kde.plasma.private.mpris` 存在但属 private 命名空间**，不稳定，不采用。
- 开发期陷阱（沿用 `proc_net_monitor` 的既有经验）：plasmashell 缓存 QML，改动已摆放的部件需
  `systemctl --user restart plasma-plasmashell`；`~/.local` 副本会遮蔽 `/usr` 副本，排查前先确认
  是否装了两份；`plasmoidviewer -a <id> -f planar|horizontal` 可免注销预览指定形态。

---

## 2. 架构

### 2.1 三个组件与唯一接缝

```
会话 D-Bus (MPRIS)                       网易云 HTTP
        │                                     │
        ▼                                     ▼
┌─────────────────────────────────────────────────────┐
│ plasma-lyricsd   (C++/Qt6, systemd --user service)  │
│  监听 MPRIS → 去重/过滤 → 指纹 → 缓存/provider       │
│  → 原子写快照                                        │
└───────────────────────┬─────────────────────────────┘
                        │  $XDG_RUNTIME_DIR/plasma-lyricsd/state.json
                        │  （QSaveFile rename 就位 + 单调 seq）
        ┌───────────────┴───────────────┐
        ▼                               ▼
┌──────────────────┐            ┌──────────────────┐
│ 桌面上的部件实例  │            │ 面板里的部件实例  │
│ LyricSource 各一份│            │ LyricSource 各一份│
│ 本地推进时间轴    │            │ 本地推进时间轴    │
└──────────────────┘            └──────────────────┘
```

**唯一接缝是那个快照文件。** 快照只读、多前端无冲突，这正好满足"同时摆两个部件"。

### 2.2 数据契约：推整首 + 时间锚点

后端**每首歌写一次**完整歌词（外加暂停/跳转时更新锚点），**"现在该显示第几行"由前端本地计算**。

对比"推当前行"（Lyrica 的做法）：那需要每 2–5 秒写一次文件，且前端只有文本、无从做任何插值。
本方案写入频率降到每首一次，前端可逐帧插值，拖进度条本地即时重算，而且将来若拿到逐字时间轴，
**渲染侧不用改契约**。

```json
{
  "schema": 1,
  "seq": 1372,
  "player": {
    "service": "org.mpris.MediaPlayer2.plasma-browser-integration",
    "identity": "Google Chrome",
    "kdePid": 3893
  },
  "track": {
    "fingerprint": "mediaSrc:c739729316bedb013393e5a6c543223f",
    "title": "老街北",
    "artists": ["闹闹丶", "FFF君", "欧Ωhm", "洛天依Official"],
    "album": "老街北",
    "lengthUs": 227708345,
    "ref": { "provider": "netease", "trackId": "1299289240" }
  },
  "playback": {
    "status": "Playing",
    "positionUs": 3872211,
    "anchorMonotonicNs": 918273645000000,
    "rate": 1.0
  },
  "lyric": {
    "state": "ok",
    "offsetMs": 0,
    "lines": [
      { "startMs": 28630, "endMs": 31620, "text": "若能再相见", "translation": null, "words": null },
      { "startMs": 31620, "endMs": 35000, "text": "那条长街",   "translation": null, "words": null }
    ]
  }
}
```

- `lyric.state` ∈ `ok` | `searching` | `not-found` | `filtered`（被白名单/启发式判定为非音乐）| `no-lyric`（纯音乐）
- **`anchorMonotonicNs` 必须取 `CLOCK_MONOTONIC`**，不能用墙钟——系统对时或休眠唤醒会让歌词瞬间跑飞。
- 前端推进：`当前位置 = positionUs + (now_monotonic - anchorMonotonicNs) * rate`，
  `status != "Playing"` 时不推进。
- `words` 字段现在恒为 `null`（见 1.2：`yrc` 拿不到），但**从第一天就存在于结构里**，
  这样将来实现加密调用后不必迁移缓存格式。

### 2.3 目录布局

```
desktop_lyrics/
├── CMakeLists.txt              顶层，-DBUILD_DAEMON / -DBUILD_PLASMOID 可分别关闭
├── CLAUDE.md  AGENTS.md
├── README.md  README.zh-CN.md
├── LICENSE                     GPL-2.0-or-later
├── DESIGN.md                   本文档
│
├── core/                       纯逻辑：依赖 QtCore，禁 QtNetwork / QtDBus
│   ├── lyric/                  LRC 解析（含 JSON 制作信息行）、逐字模型、
│   │                           「给定 position 求当前行」
│   ├── match/                  关键词清洗、时长容差打分、候选排序
│   ├── store/                  SQLite 缓存、指纹映射、负缓存
│   └── tests/                  测试重心
│
├── providers/                  横向扩展点 1
│   ├── provider.h              抽象接口 + 每 provider 独立配置块
│   ├── netease/                第一版唯一在线源
│   ├── local/                  .lrc 文件、音频内嵌标签、overrides/ 目录
│   ├── lrclib/                 占位，接口先留好
│   └── tests/fixtures/         录制的真实 API 响应（含脏数据）
│
├── daemon/
│   ├── src/
│   │   ├── mpris/              会话总线监听、去重、黑白名单、指纹、锚点采样
│   │   ├── resolver.cpp        编排：指纹 → 缓存 → provider → 写快照
│   │   ├── snapshot.cpp        QSaveFile 原子写 + seq
│   │   └── config.cpp          全局配置
│   └── tests/                  假 MPRIS 播放器，重放实测脏数据
│
├── frontend/                   横向扩展点 2
│   ├── qmlmodule/              io.github.swim233.lyrics
│   │   ├── LyricSource.{h,cpp} 读快照 + 持有时间轴 + 边界唤醒
│   │   └── tests/
│   └── plasmoid/
│       ├── package/{metadata.json, contents/{ui,config}/}
│       ├── autotests/
│       └── translations/
│
├── tools/import-waylyrics/     一次性导入 3123 首，独立可执行，不进 daemon
├── systemd/                    **user** 单元
└── packaging/{arch,aur}/
```

**为什么不照抄 `proc_net_monitor` 的布局**：nethogs 的难点在内核侧采集、业务逻辑很薄；本项目
的难点是"匹配对不对、时间轴准不准"，而这两件事完全可以离线单测——所以它们必须住在独立的
`core/` 层，而不是埋在 `daemon/src` 里。另外时间轴归前端（见 2.2），所以前端不再是哑管道，
需要自己的测试；provider 与 frontend 是两个真实的横向扩展点，必须是目录级接缝。

**与 nethogsd 的三个结构性差异**：本 daemon **无需任何权限**（不要 `AmbientCapabilities`）；
它**必须看得见会话 D-Bus**（MPRIS 在 session bus 上，system 服务看不到）；快照写
`$XDG_RUNTIME_DIR/`，不是 `/run/`。

**沿用 nethogs 已验证的**：`QSaveFile` rename 就位 + 前端同时监听文件与目录、每次事件后重新
`addPath`、用 `seq` 区分真更新（`QFileSystemWatcher` 在 rename 后会丢监听）；
`qmllint` / `xmllint --noout main.xml` / `qmltestrunner` / "安装到 staging 树再用
`qml -a core` 探测模块能否加载" 四道检查；`packaging/aur` 的发布路径。

---

## 3. 决策清单

### 定位与范围
| # | 决策 |
|---|---|
| 1 | 目标：把 waylyrics 的能力做成真正长在 Plasma 桌面里的部件，**最终替代 waylyrics** |
| 2 | 一等音源：浏览器网页版网易云（pbi）；本地播放器二等；kdeconnect 手机源**支持但默认忽略** |
| 3 | 形态：一个 plasmoid，桌面 + 面板双形态自适应；"全屏可见"用「面板 + 窗口置于下方」满足 |
| 4 | 曲库无必达项，查不到就算了；网易云单源即够（实测 waylyrics 单开网易云基本都能查到） |
| 5 | 显示：单行 + 翻译；逐字为**可选远期目标** |
| 6 | 桌面上只显示歌词，其余功能全部放进设置菜单，不做播放控制 |
| 7 | 第一版只为自己；未来考虑发布，但代码结构从第一天就切开 provider 与 UI |

### 数据获取
| # | 决策 |
|---|---|
| 9 | 不复用 waylyrics 缓存文件；用 `tools/import-waylyrics/` 一次性导入那 3123 首（格式已知 `{olyric, tlyric, offset}`） |
| 37 | **播放器发现只能直接匹配 `NameOwnerChanged`，不能用 `QDBusServiceWatcher`**（实测 Qt 6.11.2）：该类只对 `addWatchedService()` 登记过的名字发信号，**且不接受通配符**——`org.mpris.MediaPlayer2.*` 一样收不到。而能登记的名字只有构造时已在总线上的那些，于是形成闭环：新播放器不在监听表里 → 不发 `serviceRegistered` → 永远进不了监听表。daemon 随会话启动、那时一个播放器都还没开，所以这个洞覆盖的是**全部**播放器，表现为"放着歌，部件却一直显示未在播放"，重启 daemon 才好。改为连 `org.freedesktop.DBus` 的 `NameOwnerChanged` 并在槽里按前缀过滤；名字易主时（新旧 owner 都非空）必须先 remove 再 add |
| 10 | 播放器选择：自动跟随"最近变为 Playing 的"，外加可编辑黑名单（默认含 `org.mpris.MediaPlayer2.kdeconnect.*`）。去重用 `kde:pid` **定向**压制：只删掉服务名后缀为 `.instance<该 pid>` 的那一个，因此"集成代理着另一个浏览器、同时本机浏览器在原生播放"时后者仍然可选；集成未上报可用 pid 时才降级为"只信 pbi" |
| 11 | 换歌指纹：有 `kde:mediaSrc` 用其路径段，否则用 `(title, artist, album, length)` 四元组；**另加"Position 倒退 > 3s 视为重新对轴"**（覆盖单曲循环与拖动） |
| 12 | 匹配照 waylyrics 的思路：标题+歌手+专辑搜索 → 时长容差打分（2s 内判为最佳）→ 记录清洗前后关键词与候选打分 |
| 15 | 非音乐过滤：URL 白名单 + 元数据启发式 + "搜不到就算了"，三者均可在设置中配置 |
| 19/20 | 单个 SQLite；主键为 `(provider, track_id)` 复合键；`overrides/*.lrc` 目录承载手工修正 |
| 23 | provider 编译期扩展（每个一个 CMake option）；阵容照 waylyrics（网易云 / QQ音乐 / LRCLIB），**第一版只实现网易云**；接口允许每 provider 带自己的配置块 |

### 工程结构
| # | 决策 |
|---|---|
| 13 | 内部歌词模型从第一天预留 `words: Option<Vec<Word>>`；**渲染层第一版不做逐字** |
| 16 | 数据契约：推整首 + 时间锚点（见 2.2） |
| 17 | 布局重新设计（见 2.3），不照抄 nethogs |
| 18 | 配置分两份：后端全局一份 + 前端每实例一份；后端配置的编辑界面仍在 plasmoid 设置里，但要视觉上分开并标注"影响所有歌词部件" |
| 21 | 命名：applet `io.github.swim233.plasma-lyrics` / QML URI `io.github.swim233.lyrics` / daemon `plasma-lyricsd` |
| 22 | `core/` 依赖 QtCore，**禁 QtNetwork 与 QtDBus**——这条禁令就是"可测"的定义边界 |
| 24 | daemon 用 `systemctl --user enable --now` 启动（不用 D-Bus 自动激活：daemon 需要在没有任何前端时也持续跟踪进度）；**plasmoid 里必须做"服务没跑"的引导界面** |
| 31 | 桌面与面板**各一套完整外观配置**（kcfg 开 `desktop.*` / `panel.*` 两组） |

### 外观
| # | 决策 |
|---|---|
| 25/32/36 | 可读性是**两个独立维度**：底板 `plate ∈ {none, ksvg, solid}`（**默认 `ksvg`**）× 描边 `stroke: bool`（**默认 `false`**——默认底板已足够压住壁纸，描边要 8 份 Text 副本，不该默认开）。毛玻璃**删除**（1.3：结构上不可能），第三档换成纯半透明色块。**`ksvg` 档的底板由 plasmashell 画**（`Plasmoid.backgroundHints: DefaultBackground`），不能自己摆 `KSvg.FrameSvgItem`：手画的框是创建时的一次性快照，主题切换后不跟随，还会绕过框的内边距。其余两档 shell 必须让位（`NoBackground`），由部件自己画 |
| 26 | 翻译行"等号弱化"：同字号，译行降透明度（CJK 曲库下缩字号会造成字面大小突变） |
| 27 | 超长歌词三种全做：`fit`（HorizontalFit + 下限 60%）/ `wrap`（≤2 行）/ `marquee`，**默认 `fit`** |
| 28 | 切行动画三种全做：无 / 淡入淡出 180ms / 上移推入 260ms，**默认上移推入** |
| 29 | 制作人员行用**启发式过滤**（详见 §6.1，含实测边界）：自开头连续扫描，命中 `^[^\s：:]{1,12}[：:]\s*.+$` 的行视为制作人员行，遇到第一个不命中的行即停止（该行即"首句真歌词"）。**限定在前奏区 + 冒号结构双条件**，不用无底洞式关键词表——正则误伤真歌词是无声的数据损坏 |
| 30 | **字族**跟随 Plasma 通用字体设置（`Kirigami.Theme.defaultFont.family`）；**字号、字重、颜色是部件自己的配置，不跟随主题**——颜色尤其必须脱钩（歌词压在壁纸上，`Kirigami.Theme.textColor` 在浅色主题下会彻底翻车）。颜色两种编辑方式并列：Plasma 取色器（`org.kde.kquickcontrols.ColorButton`）+ 十六进制输入框，共用同一个 `#RRGGBB` / `#AARRGGBB` 字符串——QML 颜色的 `toString()` 保留 alpha 字节，所以取色器不会把半透明默认值（`#99000000`、`#cc000000`）变成不透明。字重给 6 档（Light / Regular / Medium / DemiBold / Bold / Black，**默认 Regular = 400**）；字族没有的档位 Qt 会吸附到最近的实有档位，所以不铺满 9 档 |
| 14 | 五种非歌词状态：① 未播放 → 可自定义文案，默认「当前未在播放」，可置空；② 查不到 → 独立文案，默认空；③ 间奏 → 空；④ 暂停 → 保留最后一行不动（靠"非 Playing 不推进位置"实现，不特判）；⑤ **搜索中 → 显示「搜索中…」**。⑤ 是实现期补充的第五态：抓取有网络往返，完全无反馈时会让人以为部件坏了；文案走 `i18n()`，msgid 为 `Searching for lyrics…`，zh_CN 译作「搜索中…」 |
| 38 | **前端不轮询，按边界唤醒**：位置是解析式的（锚点 + `CLOCK_MONOTONIC`，见 2.2），换行时刻因此可以精确算出来——`nextBoundaryMs()` 求下一个能改变行号的时刻，单次 `Qt::PreciseTimer` 直接打过去。原实现是 33ms 固定轮询：一首 4 分钟 60 行的歌要醒 7200 次，其中约 60 次真的换了行，而且换行还被量化在 33ms 网格上（`QTimer` 默认 `Qt::CoarseTimer`，再叠最多 5% 抖动）；改后约 120 次唤醒、误差进入 1ms 内。三条实现约束：① 边界取所有 `startMs`/`endMs` 的**超集**——行尾可能晚于下一行行首，不能只扫到第一个更晚的行首；② 无锚点或 `rate <= 0` 时不武装，位置根本不会自己走，武装只会空转；③ 单次间隔封顶 60s，把畸形时间戳（`[9999999:00.00]`）挡在定时器的 `int` 之外。**将来做逐字扫词（决策 13）时驱动应是 QML `FrameAnimation` 逐帧拉取，不是把定时器调回 16ms**——帧驱动在窗口不渲染时会自己停，定时器不会 |

### 质量与发布
| # | 决策 |
|---|---|
| 33 | 诊断三件套：**① journal**——daemon 是 systemd user service，消息处理器无条件写 stderr，直通 journal（`journalctl --user -u plasma-lyricsd -f`），零实现成本；**② 可配置日志文件**——`logging/fileEnabled` 默认 `false`，路径 `logging/filePath` 默认 `~/.local/share/plasma-lyrics/plasma-lyricsd.log`，开启后与 journal 并行写；**③ `plasma-lyricsd --explain "<标题>" "<歌手>"`**——离线打印匹配全过程，调匹配逻辑时的主力工具。**不做设置里的日志面板**（实现要两天、半年用两次，`--explain` 覆盖同一需求且离线可重复）|
| 34 | 测试必须项见第 6 节 |
| 35 | i18n 第一天就做；AUR 为唯一正式分发渠道；**不上 KDE Store** |

---

## 4. 缓存 schema

存 `~/.local/share/plasma-lyrics/lyrics.db`——**不是 `~/.cache`**，因为库内含 `offset` 这类
不可再生数据（waylyrics 把 per-track offset 放在 `~/.cache` 里，有被系统清理的隐患）。

```sql
-- 歌词正文（可再生）
CREATE TABLE lyric (
  provider    TEXT    NOT NULL,          -- 'netease' | 'lrclib' | 'local'
  track_id    TEXT    NOT NULL,          -- 平台内 id；local 用 sha1(绝对路径)
  fetched_at  INTEGER NOT NULL,
  origin      TEXT,                      -- 规范化后的行数组 JSON
  translation TEXT,
  has_words   INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (provider, track_id)
);

-- 指纹 → TrackRef。指纹规则将来若改，重建此表即可，不必重抓歌词
CREATE TABLE fingerprint (
  fingerprint TEXT    PRIMARY KEY,
  provider    TEXT    NOT NULL,
  track_id    TEXT    NOT NULL,
  matched_at  INTEGER NOT NULL,
  score       REAL                       -- 当时的匹配得分，便于事后复盘
);

-- 负缓存：查过了没有。TTL 7 天
CREATE TABLE miss (
  fingerprint TEXT    PRIMARY KEY,
  tried_at    INTEGER NOT NULL,
  reason      TEXT                       -- 'no-candidate' | 'network' | 'not-music'
);

-- 用户手工偏移（不可再生）
CREATE TABLE offset (
  provider  TEXT    NOT NULL,
  track_id  TEXT    NOT NULL,
  offset_ms INTEGER NOT NULL,
  PRIMARY KEY (provider, track_id)
);
```

**负缓存是必需的**，不是优化：不做它，每看一个 B 站视频都会打一次网易云搜索接口。

**手工改歌词不改数据库**，走 `~/.local/share/plasma-lyrics/overrides/<provider>:<id>.lrc`，
由 `local` provider 以最高优先级读取。这样缓存保持"纯粹可再生"的语义，覆盖目录是"你的数据"，
备份时只需备份后者。

---

## 5. Provider 接口

```cpp
struct TrackQuery { QString title; QStringList artists; QString album; qint64 lengthMs; };
struct Candidate  { QString trackId; QString title; QStringList artists;
                    QString album; qint64 lengthMs; };
struct LyricDoc   { LyricLines origin; LyricLines translation; bool hasWords; };

class Provider {
public:
    virtual ~Provider() = default;
    virtual QString id() const = 0;                          // "netease"
    virtual bool    isConfigured() const = 0;                // 未配置则跳过
    virtual QList<Candidate>        search(const TrackQuery &) = 0;
    virtual std::optional<LyricDoc> fetch(const QString &trackId) = 0;
};
```

`isConfigured()` 不是多余的：waylyrics 的 QQ音乐 provider 并非直连，而是要用户自行运行一个
`QQMusicApi` 桥接服务（`api_base_url = "http://127.0.0.1:3300"` + cookie 字符串）。所以接口
从第一天就不能假设"provider 都是无状态直连"，每个 provider 需要自己的配置块
（base URL / cookie / timeout）。

---

## 6. 测试清单

**必须有：**

1. **LRC 解析** — JSON 制作信息行、三位毫秒 `[00:29.638]`、一行多时间戳、`[offset:]` 标签
2. **「给定 position 求当前行」的边界** — 第一行之前、最后一行之后、间奏、同时间戳多行
3. **匹配打分** — 用真实候选（`魔法厨娘` 的三个同名不同版本：423776453 / 1418713342 / 33497601，
   时长 286066 / 286289 / 284328 ms）验证时长容差选对了
4. **缓存** — 指纹→TrackRef 映射、负缓存 TTL 过期
5. **假 MPRIS 重放实测脏数据** — trackid 恒定、永不发 `Seeked`、标签页标题当歌名、
   B站/网易云在同一服务内互切、Position 跳变。**这是本项目最独特的测试资产**：
   第 1.1 节那些行为三个月后不会记得，只有写进测试才不会在某次重构里悄悄回归
7. **快照契约** — `seq` 单调、rename 后监听不丢、stale 判定
9. **播放器发现**（`tst_mprisdiscovery`）— 在 `dbus-run-session` 起的私有总线上，先建 `MprisManager`、
   **后**注册假播放器，断言它被发现；再断言它离开总线后不会把最后一句歌词留在屏幕上。
   必须是真总线上的集成测试：决策 37 那个洞就长在 D-Bus 接线里，纯函数测试看不见它

**其他：** 第 6 项"时间锚点推进（含休眠唤醒后 monotonic 的行为）"通过**可注入时钟**的接口来测
（否则要真的休眠一次）；第 8 项构建期检查沿用 nethogs 的四道命令。

`providers/tests/fixtures/` 存录制的真实 API 响应，必须包含：空 `yrc`、恒空 `klyric`、
开头的 JSON 制作信息行、同名不同版本的候选列表。

### 6.1 制作人员行启发式：已实测的边界

**过滤分两条路径，结构优先于形状：**

1. **provider 标记**（权威）。v1 端点的结构化制作人员条目在解析时就被打上
   `LyricLine::credit`，过滤时无条件信任，不做任何形状猜测。
2. **形状启发式**（兜底），用于带时间轴的制作人员行：先把冒号周围的留白折叠
   （`\s*([：:])\s*` → `\1`），再套 `^[^\s：:]{1,12}[：:]\s*.+$`。自歌词开头连续扫描，
   **遇到第一个不命中的行即停止**，并以 30 秒为前奏区上限。

**已实测命中：**
- `老街北`(1299289240) 的带时间轴制作人员行：`编曲/伴奏混音：闹闹丶`、`调教：FFF君`、
  `混音：小欧Ω`、`曲绘：偶尤大肥羊`、`PV/封面设计：Ansa`、`文案：铭言君，Ansa`、`歌姬：洛天依`，
  第 8 行 `若能再相见` 正确终止扫描
- `春风漫野绿`(2699991455) 经老端点返回的 `作词 : 爆音常安`、`作曲 : 爆音常安`（**折叠留白后**才命中）
- 结构化条目：不论文本形状一律过滤（如 `Mix&Mastering by Foo Bar`）

> ⚠️ **本节曾经写错，代价是功能整体失效。** 初版只列了"老街北 的真实前 7 行"作为已验证集，
> 而那批样本是用 **v1 端点**抓的（全角冒号、无留白）。实现选了老端点，它把同一条制作人员写成
> `作词 : 爆音常安`——冒号前有空格，被 `[^\s：:]` 的头部拒绝。再叠上"遇第一个不命中即停止"，
> **一行格式不符 = 69 行一条都不过滤**，桌面上会挂 8 行制作人员名单。
> 教训有两条，都已落到代码和测试里：
> - **fixture 必须来自实现真正调用的那个端点。** 全绿的测试套件之所以掩盖了生产故障，
>   就是因为 fixture 全是 v1 形状。`providers/tests/fixtures/netease-lyric-old-endpoint.json`
>   现在保留了老端点的真实响应作为回归防线。
> - **"遇第一个不命中即停止"让窄失配变成灾难性失败。** 这是启发式本身的脆弱点：
>   它不会少过滤一行，而是一行都不过滤。若将来再遇到新的制作人员形态，优先加结构化信号
>   （路径 1），而不是继续放宽正则。

**已知漏判**（不会被过滤，会显示在桌面上）：
- 冒号前超过 12 个字符，例如 `Mix&Mastering: Foo Bar`（13 字符）
- 冒号前含空格且**没有** provider 标记，例如老端点未来若返回 `Vocal 调整：某某`
  （折叠只处理冒号紧邻的留白，不处理头部内部的空格）

这两种写法在同人音乐里可能出现，但本机曲库尚未抓到实例。收到实例后优先走路径 1；
放宽正则头部会同时提高误判真歌词的概率。

**已知误判风险**：形如 `我说：你听` 的真歌词会命中路径 2。风险由"仅扫描前奏区 + 遇第一个
不命中即停止"约束——只有当这类句子恰好出现在**第一句真歌词之前**时才会被吞掉。接受此风险；
替代方案（关键词白名单）的失效模式更糟：它会在歌曲中段任意位置吞掉歌词，且无声。

## 7. 打包与发布

- **AUR 为唯一正式渠道**（`packaging/aur`）。
- **默认启用 user service**：装
  `/usr/lib/systemd/user-preset/90-plasma-lyricsd.preset`，内容 `enable plasma-lyricsd.service`
  （Arch 打包不允许在 `.install` 里直接 `systemctl enable`，preset 是官方认可做法）。
  ⚠️ preset 只对新用户或执行 `preset-all` 时生效，**不追溯已有用户**——所以 plasmoid 里
  "服务没跑"的引导界面仍然必须做。
- **不上 KDE Store**：Store 分发纯 plasmoid 包（zip 解到 `~/.local/share/plasma/plasmoids/`），
  而本部件依赖装在 `/usr` 的系统级 QML 模块 + 一个 user service，从 Store 装上去会直接报
  `module is not installed`。这是分发模型的错配，硬上只会收到"装了没反应"的差评。
- **i18n 从第一天做**：所有字符串走 `i18n()`，`frontend/plasmoid/translations/` + build 脚本。

### 7.1 上线第一天会遇到的事

本设计的目标是**替代** waylyrics（#1），而现有的 3123 首缓存要靠 `tools/import-waylyrics/`
一次性导入——**那个工具在第一版里还不存在**。所以在它写出来并跑过之前，第一次使用是
**冷缓存对着一个非官方、会限流的接口**：每首新歌都要现搜现抓，偶发失败会比稳定期明显得多。
这不是设计缺陷，但足以让第一次试用感觉"这东西不好使"。建议把导入工具排在第一版范围内，
或至少在第一次运行时不要同时评判匹配质量。

---

## 8. 明确不做 / 做不到

| 项 | 原因 |
|---|---|
| 毛玻璃底板 | **做不到**。KWin `shouldBlur()` 里 `if (w->isDesktop()) return false;`；桌面部件与壁纸共享最底层窗口 |
| 逐字歌词渲染 | **拿不到数据**。明文接口不返回 `yrc`（多首歌 + 多种 cookie 伪装均已验证），门槛是自实现 weapi/eapi 加密。数据结构已预留 |
| 播放控制 | 范围外（Q6）。本机已有 4 个音乐 plasmoid 在做 |
| KDE Store 上架 | 分发模型与架构不兼容（见第 7 节） |
| 运行时 provider 插件（.so） | 过度设计。稳定 ABI、版本协商、加载失败处理的代价换不到收益；接口定好了将来要改也不必推翻 |
| 读 waylyrics 的运行时缓存 | 耦合他人私有格式且要求 waylyrics 常驻；只做一次性导入 |
| 设置里的"最近匹配记录"面板 | 实现要两天、半年用两次。`--explain` 子命令覆盖同一需求且离线可重复 |
| 日志文件的轮转 | 有意不做。日志文件以 append 打开，**没有大小上限、不会自动轮转**——它是为"排查一次问题"准备的开关（默认关闭），不是常开设施。日常诊断走 journal，那边由 systemd 负责限额与轮转。若哪天需要长期开着，再补一个按大小截断的处理，而不是现在预先造 |
