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

### 1.4 QtDBus 的阻塞调用（本机实测，Qt 6.11.2）

- **阻塞的 `QDBusInterface::call()` 不会旋转 Qt 事件循环**，`QDBus::AutoDetect`（默认）与显式
  `QDBus::Block` 行为一致，从 `QCoreApplication::exec()` 内部发起也一样。实测：让服务端在方法里
  睡 600ms，调用方耗时 601ms/600ms，期间 0ms 与 200ms 的 `QTimer` 都没触发，先前 pending 的
  `deleteLater()` 也没有执行。
- **因此阻塞期间不会有槽被重入，`this` 也不会在调用中途被销毁。** `MprisPlayer::pollPosition()`
  和 `getAll()` 里的阻塞调用不构成 use-after-free 窗口，不需要改成 `QDBusPendingCall`；
  同理，构造函数阻塞期间 `NameOwnerChanged` 也不会插进来，不会留下僵尸 `m_players` 条目。
- 会重入的是 `QDBus::BlockWithGui`（Qt 文档明写），本项目不使用。

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
    "globalOffsetEnabled": false,
    "switchingProvider": "",
    "lines": [
      { "startMs": 28630, "endMs": 31620, "text": "若能再相见", "translation": null, "words": null },
      { "startMs": 31620, "endMs": 35000, "text": "那条长街",   "translation": null, "words": null }
    ]
  }
}
```

- `lyric.state` ∈ `ok` | `searching` | `not-found` | `network-error` | `filtered`（被白名单/启发式判定为非音乐）| `no-lyric`（匹配成功但源站无词）
- **`anchorMonotonicNs` 必须取 `CLOCK_MONOTONIC`**，不能用墙钟——系统对时或休眠唤醒会让歌词瞬间跑飞。
- 前端推进：`当前位置 = positionUs + (now_monotonic - anchorMonotonicNs) * rate`，
  `status != "Playing"` 时不推进。
- `words` 字段现在恒为 `null`（见 1.2：`yrc` 拿不到），但**从第一天就存在于结构里**，
  这样将来实现加密调用后不必迁移缓存格式。
- **`lyric.offsetMs` 自决策 55 起是守护进程算好的生效值**：全局偏移开启时写全局值，关闭时写
  当前 `(provider, track_id)` 的单曲值；`globalOffsetEnabled` 只用于前端菜单文案与可用性判断。
  部件不再打开 SQLite，偏移变化也通过新快照原子地到达所有实例。

### 2.3 目录布局

```
plasma-lyrics/
├── CMakeLists.txt              顶层，-DBUILD_DAEMON / -DBUILD_PLASMOID 可分别关闭
├── CLAUDE.md  AGENTS.md
├── README.md  README.zh-CN.md
├── LICENSE                     GPL-2.0
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
| 4 | 曲库无必达项，查不到就算了；默认按本地 → 网易云 → AMLL 串行查询，首选源无可用歌词时回退，不跨源拼接正文与翻译 |
| 5 | 显示：**曲目信息（可关）+** 单行歌词 + 翻译；逐字为**可选远期目标** |
| 6 | 桌面上只显示歌词**与曲目信息**，其余功能全部放进设置菜单，**不做播放控制**——这条约束的是**交互**，不是信息密度 |
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
| 23 | provider 编译期扩展（每个一个 CMake option）；已实现本地、网易云与 AMLL TTML DB，接口允许每 provider 带自己的配置块；不引入运行时动态插件 ABI |
| 42 | **来源平台判定**：在决策 15 的 URL 白名单之上引入「来源平台」概念（`netease` / `apple`），由 D-Bus service 通配（`*.sidra`、`*.cider*`）与 `xesam:url` 前缀（`music.163.com`、`music.apple.com`、`classical.music.apple.com`）共同推导，**service 优先**——Sidra 的电台/古典条目没有 `xesam:url`，只有 service name 能认出它。`*.cider*` 点锚定（而非 `*cider*`）是为了不误吃 `org.mpris.MediaPlayer2.decider` 这类服务名里恰好含 `cider` 子串但并非 Cider 的播放器，和旁边 `*.sidra` 的锚定方式一致；真实 Cider（`ciderapp/Cider` 的 `src/main/plugins/mpris.ts`）注册的服务名就是字面量 `org.mpris.MediaPlayer2.cider`，收紧后仍然命中。`isMusic` 消费同一个判定：平台非空则只看是否在设置里勾选；**平台为空（未知来源）时沿用改动前的 URL 白名单 + 元数据启发式逐字节不变**，绝不能让升级当天所有本地播放器一起变哑。设置只列默认勾选的两个平台（网易云音乐、Apple Music），不列 YouTube Music / QQ音乐 / Spotify——列了又不勾会造成静默回归。`filter/musicUrlPrefixes` 原键原义保留（只装自定义 URL 前缀），新增 `filter/platforms` 装勾选结果，两者互不影响、零迁移 |
| 43 | **`transNames` 参与打分**：`Candidate` 新增 `alternateTitles`（网易云 `transNames`，storefront 本地化译名），`scoreCandidate` 对每个别名重算 `textSimilarity` 并取与主标题的 `max` 作为 `score.title`——只加证据、不降标准，权重（0.5/0.2/0.1/0.2）与阈值（0.55/0.58）不变。**只读 `transNames`，不读 `alias`**：实测 `alias=["TV动画《我推的孩子》片头曲"]` 是番剧挂钩语，`transNames=["偶像"]` 才是「アイドル」的译名；混进 `alias` 会把不相关的宣传文案当证据。字段缺失或为 `null` 都当空处理（`QJsonValue::toArray()` 对两者返回同一个空数组，天然安全）。`ScoreBreakdown` 新增 `titleViaAlternate`，`explainMatch` 打印 `titleVia=alias`/`titleVia=title`——否则诊断日志只会看到分数无端变高，看不出是哪条候选证据起的作用。本地 provider 同样构造 `Candidate`，但不使用 `alternateTitles` |
| 44 | **`cleanArtists` 拆分「主名 (括号内容)」的并列写法**：网易云把部分艺人名存成复合串，字面量如 `BTS (防弹少年团)`——实测 `--explain 春日 BTS` 之前 `artistSimilarity` 把整串当一个 token 算子串比例（`3/9≈0.333`），远低于本该有的精确匹配。`cleanArtists` 新增一条正则，仅当整条艺人字符串**以括号收尾**（`(...)`/`（...）`）时才把结尾这组拆成独立的一条，前后两段各自再走原有的斜杠/顿号/`&`/`feat.` 分隔逻辑；条件收紧到"结尾"是为了不误吃合作艺人本身名字里带括号的巧合写法（如 `(Sandy) Alex G`，括号在开头不在结尾，不拆）。**影响面不限于「同一实体的另一种文字写法」**：ACG 角色曲在网易云上常见 `神楽ひかり(CV:三森すずこ)` 这类写法，括号内是**扮演该角色的声优**，和角色本身是两个不同实体，同样会被拆成两条——这里拆开依然无害，理由和整个改动的正当性是同一条、同样是经验性的（见下文）：MPRIS 侧报的是裸名字（角色名或艺人名），拆开只是让任一段都有机会单独匹配上，而会降分的那种形状要求**对侧**给出没有分隔的合并写法，CV 场景同样没有观测到这样的来源。**这个改动在数学上没有任何一个方向是无条件单调的**：它把一个合并 token **替换**成拆开的若干 token（不是纯追加），而 `cleanArtists` 被 query、candidate 两侧共用，`artistSimilarity` 的分母又取 query 侧的 token 数——被替换掉的那个合并 token 本身往往就是最佳匹配，所以哪一侧被替换、分数都可能下降。实测反例（qa-2）：query 侧固定为 `["BTS 防弹少年团"]`（本身不含括号，逐字节不受这次改动影响），candidate 侧从未拆分的 `["BTS 防弹少年团"]` 变成拆分后的 `["BTS", "防弹少年团"]`，`artists` 从精确匹配 `1.000` 掉到 `0.556`。**"只增不减"的变体也救不了**：若改成保留原始合并串、再追加拆出的两段（candidate 侧从 1 个 token 变 3 个），的确不会在 candidate 侧丢信息，但同一改动应用在 query 侧时会撑大 `artistSimilarity` 的分母，同样能把分数往下拉——两个方向都堵死，不存在无条件安全的写法。**这个改动的正当性是经验性的，不是数学证明**：真实数据的形状是 MPRIS 报裸名字（如 `BTS`）、网易云存复合串（如 `BTS (防弹少年团)`），这个方向上是严格改善，实测 `0.333 → 1.000`（`--explain 春日 BTS`）；会降分的那些形状都要求**对侧**给出"没有分隔的合并写法"（如 qa-2 反例里 query 侧那个 `"BTS 防弹少年团"`），而目前观测到的各 MPRIS 来源都不会这样报艺人字段。测试 `compoundCandidateNameNoLongerUndercountsArtistSimilarity` 只验证了这一个具体实例（query 固定为裸 `"BTS"`，candidate 拆分前后对比），不代表任何通用结论。**副作用范围已按来源逐一核实**：`MprisPolicy::fingerprint` 只在 `state.mediaSrc` 为空时才退化到 `cleanArtists(...).join('/')` 参与的四元组哈希（决策 11 的兜底分支），而 `kde:mediaSrc` 是 **pbi 专属**的 KDE 扩展属性——实测 pbi 报网易云网页版时 `kde:mediaSrc` 是那个 mp3 直链（存在），Sidra 完全没有这个键。因此：**网易云网页版（pbi）不受影响**，走的是 `mediaSrc:` 分支，四元组哈希根本不参与；受影响的是**没有 `kde:mediaSrc` 的来源**——Sidra/Cider 这些本次才开始支持的 Apple 客户端（升级前本来就没有历史缓存，代价为零），以及本地播放器如 mpv/VLC（这类会真的经历一次 fingerprint 变化）。`fingerprint` 表（`fingerprint → provider, track_id`）与 `lyric`/`offset` 表（主键都是 `(provider, track_id)`）是三张独立的表，旧 fingerprint 行只是被孤立、不会级联删除任何东西。**用户可感知的影响**：受影响曲目升级后第一次播放会触发一次真实网络搜索（与任意一次正常的元数据变化触发重新解析毫无区别），期间可能短暂显示"匹配中"或"未找到"，随后恢复正常；不丢失已缓存的歌词或偏移；一次性代价，不会重复出现 |
| 45 | **本地化标题兜底 + 别名艺人闸**：决策 43 的 `transNames` 只救得了汉字化译名（`偶像`→`アイドル`），救不了 Apple us storefront 的罗马字形态（`Gunjou`→`群青`）——网易云从不把日文标题罗马字化，这类候选的 `transNames` 是 `None`，标题证据彻底缺失。新增 `chooseMatch(ranked, allowLocalizedFallback)` 取代 `Resolver` 里裸的 `ranked.first()` + `isAcceptableMatch`：第一优先级仍是 `isAcceptableMatch(ranked.first())`；不满足时，`allowLocalizedFallback`（**仅 `MprisState::platform == "apple"` 时置真**——这条路完全绕开标题证据，是刻意降低的标准，必须关在已知成因的场景里）才允许走第二判据：候选池 `>= 3`（池子只有一两条时"唯一"是白送的，恰是证据最弱的情形）+ 判据（艺人相似度 `>= 0.9` 且时长差 `<= 250ms` 且两侧时长都 `> 0`，**`scoreCandidate` 在任一侧缺失时长时把 `durationDifferenceMs` 置 0，`ScoreBreakdown` 因此新增 `durationComparable`** 供这一步单独判断）+ 按 `(normalizeSearchText(cleanTitle(title)), cleanArtists(artists))` 去重后**恰好一组**唯一候选（网易云同一首歌挂多个 id 是常态，不去重会把正确匹配当"歧义"拒掉），取组内时长差最小的一条。**这道时长闸的窗口是 250ms，不是 `scoreCandidate` 自己打分公式里"Δ<=2000ms 算最佳档"的 2000ms**——两者刻意不同，`scoreCandidate` 那个 2000ms 是主路径打分的既有行为，一个字节都没动；这里的 250ms 是兜底路径单独收紧的唯一性判据，理由见下。最初按 `scoreCandidate` 的 2000ms 抄来复用时，qa-2 指出一个结构性漏洞：**真曲的时长恰好落在闸外（源站与网易云对同一首歌的收录时长有出入，或网易云收的是不同剪辑版），而同一艺人的另一首不相干歌曲恰好落在闸内、且只有它一个存活**——这种情形下 D-8 的四道闸（艺人、时长、池子、去重唯一）全部通过，`chooseMatch` 会自信地返回错误的歌；`genuineAmbiguityIsRejected` 测的是"两首都活下来→按 D-9 分组不唯一→拒"，结构上覆盖不到"只有错的那首活下来→分组天然唯一→放行"这一类。收紧窗口是唯一能堵住这类洞的办法（唯一性判据本身没有"排除已被时长闸筛掉的候选"这个概念）。**收紧到 250ms 的依据是两组实测**：① 已知真命中的时长差全部是 `0～1ms`（群青 Δ=0、アイドル Δ=1）——Apple 与网易云对同一发行版的时长几乎逐毫秒一致，250ms 对真命中没有任何压缩空间；② 抽样 YOASOBI/Ado/IU/BTS/米津玄師 各 30 条搜索结果、按曲名去重后共 135 首，测了不同窗口下"至少一个同艺人邻居落在窗口内"的曲目占比与"两首不同曲都落在窗口内"的碰撞对占比：

| 窗口 | 有同艺人邻居占比 | 碰撞对占比 |
|---|---|---|
| ±2000ms | 60% | 3.91% |
| ±1000ms | 36% | 1.93% |
| ±250ms | 13% | 0.51% |
| ±100ms | 7% | 0.28% |

250ms 把碰撞率从 3.91% 压到 0.51%（降 4.6 倍），且不挡任何已知真命中，符合用户"宁可 `notFoundText` 也不要错歌词"的取向；100ms 再压一档收益已经很薄，250ms 是两头都够用的折中，**同样不是一个被精确校准出来的最优值**，往后如果实测数据要求更紧可以继续收。**qa-1 独立复现过这张表，指出两处方法论偏差，如实记下**：①"有同艺人邻居"这一列对取样量敏感——把 `limit` 从 30 提到 60（135→263 首）后，250ms 档从 13% 涨到 23%（"至少一个邻居"这种统计量会随候选池变大单调升高，13% 不能当成真实用户遇到误顶替的绝对概率，真实索引里同一艺人的曲目远不止 30 条，这一列还会继续升高）；"碰撞对占比"这一列稳健，250ms 档 0.51%→0.51%、2000ms 档 3.91%→3.83%，**上面"降 4.6 倍"引用的正是这一列**。②测量脚本按 trim+小写去重，比代码里实际用的 `normalizeSearchText`（NFKC 归一化）松——实例：YOASOBI「たぶん」在搜索结果里出现两次、时长完全相同，一个「ぶ」是预组合字符（U+3076）、另一个是基字符+浊点组合（U+3075 U+3099），脚本当成两首歌记了一次虚假碰撞，真实代码会正确合并；按 NFKC 重跑后 135→134 首，各档数字变化 0.05～1.2 个百分点（250ms 档：13.33%→11.94%、0.51%→0.46%），方向是把风险算高了一点，结论不变。**顺带发现（残留风险，不在本次范围）**：收紧只是把这类误顶替的概率降低，没有消除——真曲的时长恰好落在 250ms 窗外、且恰好有另一首同艺人歌曲落在窗内时，`chooseMatch` 仍会返回错误的歌，现有测试结构上覆盖不到这一类（能覆盖的只是"两者都在窗内"的双候选歧义）。**别名艺人闸**堵住决策 43 打开的口子：`transNames` 会广泛传播到同一首歌的翻唱/remix 条目上（实测 `偶像 YOASOBI`/`吵死了 Ado`/`大概 YOASOBI` 三组皆如此），一条错艺人的翻唱靠继承来的 `transNames` 就能把 `score.title` 顶到 1.0、总分越过两道阈值（0.55/0.58）——`titleViaAlternate` 为真的候选因此额外要求 `score.artists >= 0.5` 才算可接受。**这道闸只挂在 `chooseMatch` 第一个返回点（主路径）**，不挂在兜底路径（survivors 那一步）：兜底路径的 survivors 已经要求 `artists >= 0.9`，严于闸的 `0.5` 阈值，`0.9 >= 0.5` 恒真，再判一次是够不成立的死代码（qa-1 指出）。挂在 `chooseMatch` 而不是 `isAcceptableMatch`，是因为 `Resolver` 只评估 `ranked.first()`，翻唱一旦排第一会在 `isAcceptableMatch` 失败之前就返回，`isAcceptableMatch` 内部判不到。0.5 而非本条兜底判据的 0.9：这里标题证据本身存在且强（网易云自己声明了译名），艺人只需排除"完全不相干的人"。**但 0.5 这个具体数字本身没有被校准过，如实说清楚**：实测覆盖日/韩/英文原曲共 12 组本地化标题查询（YOASOBI×4、Ado、IU×4、BTS、《冰雪奇缘》原声、《爱乐之城》原声，决策 44 落地后用裸名字重新跑过一遍结论不变），正确原曲的 `artists` 分全部聚在 `1.000`；已知的错误候选（`YunFuCola remix`）`artists=0.143`。`(0.143, 1.0)` 这一整段区间**没有任何观测点**，0.5 只是这段空档里取的中间值——这批数据能证明的只是"0.5 落在安全区间内"，证明不了"0.5 本身有什么特别之处"：换成 0.6/0.7/0.8/0.9，在现有数据下会得到一模一样的结论。校准数据的作用是划出空档的两端边界，不是选定 0.5 这个点；往后如果出现新的错误候选样本落进这段空档，才需要真的收紧这个数字。**顺带发现（不在本次范围）**：`isAcceptableMatch` 本身从不检查 `score.artists`，"标题字面相同、艺人完全不相干"的候选在 `transNames` 特性引入之前就能被接受（`--explain Gunjou YOASOBI` 会选中 `GUNJOU (Cover)/Omnixor`，靠的是主标题、与 `transNames` 无关；但那是 `--explain` 不传时长的最坏情形——真实 daemon 路径下该候选 `cleanTitle` 先剥掉 `(Cover)` 使 `title=1.000`（不是子串比例），`Δ=13709 ms`，时长分 `0.9-(13709-2000)/15000=0.119`，总分 `1.0×0.5+0+0+0.119×0.2=0.524`，仍低于 0.58 不会被选中，只有 `--explain` 的时长恒缺失最坏情形才会显形）。别名艺人闸只堵住了 `transNames` 打开的新路径，这条更早存在的旧路径仍然敞着，修它会改变一条已经在工作的路径的行为，应当单独立项，此处仅记录、不顺手改 |

| 46 | **匹配诊断与实际决策一致**：`Resolver::resolve` 传入已判定的来源平台，`explainMatch` 的 `selected:` 必须与实际 `chooseMatch` 结果相同；仅有在反事实中开启本地化标题兜底才会改变结果时，额外输出 `would-select-with-fallback:`。搜索本身失败时不得调用 `explainMatch`、不得打印会被误读为成功搜索结果的 `selected: none`，而应只记录 provider 错误。`--explain` 默认遍历当前构建和配置中的实际 provider 链，也接受 `--provider` 限定来源、`--platform` 显式提供播放平台；逐源打印 provider、匹配策略、版本层级、选择或阈值/版本拒绝原因，搜索失败仍只输出该 provider 的具体错误。未编译网易云但启用 AMLL 的构建必须同样可用 |
| 47 | **provider 全链路异步**：网络 provider 以回调交付搜索与获取结果，不再用 `QEventLoop::exec()` 重入主事件循环；`Resolver::resolve` 发起工作后返回，完成时发 `resolved` 信号。daemon 在调用前本就发布 `searching` 快照，因此前端契约不变。每次解析带世代号，换曲或播放器消失后到达的旧回调不得写缓存、不得覆盖当前快照 |
| 48 | **网络重试与错误分类**：搜索与歌词获取最多尝试 3 次，网络传输错误可重试——既包括完全没收到 HTTP 响应，也包括已经收到 2xx 响应头、随后正文截断而产生的 `RemoteHostClosedError`；HTTP 非成功响应是服务端明确答复，JSON/业务解析错误发生在传输成功之后，二者均立即交付错误、不重试。provider 结果显式携带 `transportFailed`，Resolver 只把该标志归为 `network-error`；HTTP/解析错误归为 `not-found`，不能靠非空错误字符串冒充网络失败。`providers/netease/timeoutMs` 表示第一次尝试的超时，默认由 8000 ms 改为 4000 ms；后两次为该值的 1.5 倍和 2 倍，默认即 4/6/8 s。`network` 负缓存只保留 5 分钟，网络恢复后可重新尝试；`no-candidate` 仍保留 7 天 |
| 49 | **日志配置在单实例锁之前生效**：`Config` 的构造和日志文件安装有意放在 daemon 单实例锁检查之前，使第二个实例的「已在运行」失败原因也能写入用户配置的日志文件。日志 handler 在 `main()` 的所有正常返回路径退出前卸载并清空文件指针，必须早于函数局部静态 `QFile` 的析构，不能把悬垂风险推迟到静态析构期 |
| 50 | **新增文案开关必须迁移旧实例**：`notFoundText` 早于 `notFoundTextUseDefault` 存在，若只给新 Bool 配 `true` 默认值，升级后会遮蔽旧实例已有的非空自定义文案。每实例 kcfg 因此保存 `textConfigVersion`：版本 0 且旧文案非空时把伴生 Bool 置 `false`，空值（新安装）仍使用本地化默认；随后写版本 1。版本闸使迁移幂等，也保证迁移后用户重新选择默认值不会被下次启动覆盖。`noLyricText`/`networkErrorText` 与各自 Bool 同时引入，不参与这次迁移 |
| 51 | **服务重启结果必须可见且可重试**：`BackendConfig` 用自己持有的异步 `QProcess` 运行 `systemctl --user restart plasma-lyricsd`，分别处理启动失败、非零退出码、crash 与成功，配置页显示成功/失败 InlineMessage。运行中拒绝重复调用并禁用按钮；保存成功会清掉 `dirty`，但重启失败后按钮仍可直接再次执行重启，不要求用户制造一次无意义的配置编辑。对象销毁时终止仍在运行的子进程，避免完成回调访问失效对象 |
| 52 | **缓存诊断覆盖读写两端**：解析日志把「fingerprint 映射不存在」（`cache mapping missing`）与「映射存在但歌词正文不存在」（`cache lyric missing`）分开记录，均为 debug 级；provider 搜索失败记录 provider id 与原始错误文本，为 info 级。`putLyric`、`mapFingerprint`、`recordMiss` 的 Bool 结果都必须检查，失败分别写 warn 级日志；`record miss` 成功日志只能在负缓存真正落库后输出，不能把失败误报成成功。当前播放仍可使用刚获取的内存文档，原子整首快照契约不因缓存持久化失败而改变 |
| 53 | **空文案回退改为单一策略开关**：决策 14/50 的四个伴生 `*TextUseDefault` 开关在设置页合并为一个每实例 `emptyTextUseDefault`。非空自定义文案始终优先；只有字段为空时，该开关才决定显示本地化默认文案还是保持空白，因此编辑一个字段不会连带关闭其他字段的回退。旧四键保留在 kcfg 中只供迁移读取，不再参与版本 2 的运行时渲染。`textConfigVersion` 从 1 升至 2：从版本 0 直升时先执行决策 50 的 `notFoundText` 保护，再以四个旧 Bool 的逻辑或写入新键；只要旧实例任一状态仍使用默认文案就继续开启，只有四项全部明确关闭时才迁移为关闭。版本闸保持迁移幂等，迁移后用户对新开关的选择不会被旧键覆盖 |
| 54 | **多歌词源与 AMLL**：后端全局顺序默认 `local → netease → amll`，单曲首选按现有指纹持久化并移到链首，其余已配置源仍按全局顺序回退且每次只查询一次。缓存分为用户首选、实际命中、按 provider 失败冷却三种状态；旧 `fingerprint` 只迁移为实际结果，旧全局 `miss` 不抑制 AMLL。正常播放可先发布已有回退歌词；只有保留项是非空、状态为 `ok` 的有效歌词时，强制重搜失败才不清屏，否则必须发布 `not-found` 或具体错误终态。首选源冷却结束后仅在明确的播放轮次事件后台重试且一轮最多一次。MPRIS `Seeked` 到达时立即用目标位置和单调时钟重新锚定，通常作为手工跳转抑制轮次；考虑少数播放器也会在自然循环时发送该信号，只有同一套自然过尾证据独立成立时仍保留一轮事件。对 pbi 等不发 `Seeked` 的来源，曲尾→开头只有在 `(单调时间差 × Rate)` 已足以消耗上一采样点的曲尾剩余时长、且当前位置接近预期的循环后相位时才算新轮次；轮询、`Seeked` 与脏 `PropertiesChanged(Position)` 共用判据并在采样后统一重锚，普通回退仍只重新对轴且同一次回跳不会重复。协议极限是：若用户跳转发生在恰好与自然过尾相同的时刻且目标又与预期相位吻合，MPRIS 没有提供 seek 原因，客户端没有可观察信息区分两者；错误的曲长、Rate 或严重延迟也可能让非标准播放器越出容差。手动换源/恢复自动/重搜经带预期指纹的会话 D-Bus 控制接口执行，强制请求绕过映射与负缓存，并由世代号阻止旧回调提交。AMLL 缓存 JSONL 索引，24 小时后条件刷新，校验成功才原子替换；缓存版本纳入规范化索引与内容 URL，索引元数据记录来源，来源变化时强制无条件重验证并丢弃旧 ETag/Last-Modified。历史记录以关联为边做传递合并，稳定引用优先使用最早组；标题、艺人和已知 ISRC 冲突阻止误合并，具体 `rawLyricFile` 仅作为内容版本。AMLL 匹配保留 Live/Remix/Cover/重制等后缀：版本一致或双方无标记为正常层，仅单边标记为低层，明确冲突排除。TTML 在 `core/` 解析为现有整首模型，接受带单位或无单位秒时间、保留 `span` 间有效空白，并把官方 `<translations>/<translation>/<text for>` 按行 id 映射；翻译语言按 BCP-47 主语言、脚本和地区确定性选择。保存行/词时间及来源元数据；`x-bg` 与罗马音不拼入主歌词，前端首版仍按行显示。音乐播放平台（MPRIS 来源）与歌词 provider 是两个独立概念 |
| 55 | **部件是 SQLite 零接触的快照消费者**：`lyric.offsetMs` 是生效偏移，快照另带 `globalOffsetEnabled`；偏移调整经带预期指纹的 `AdjustOffset(deltaMs)` / `ResetOffset` 写入守护进程并立即重发快照。单曲模式必须有当前歌曲和歌词引用；全局模式在有当前歌曲时仍校验预期指纹，在确实没有当前歌曲且预期指纹也为空时允许从部件菜单调整。2 秒健康检查只做 daemon pid 存活检测。唯一例外是配置对话框中的 `GlobalConfig`：它为同步报告保存失败而直连 `LyricStore`，成功后以 `RefreshGlobalOffset` 通知 daemon 调用只写快照的窄重发布函数；该函数校验当前 MPRIS 指纹仍与已解析状态一致，绝不借偏移刷新触发完整解析。通知失败不改变已经成功落盘的设置，下次 daemon 启动会读取它。因此 `LyricStore` 只有守护进程与配置对话框两类调用方 |
| 56 | **MPRIS Position 与锚点永远同刻采样**：每次位置轮询都同时更新 `positionUs` 与 `anchorMonotonicNs`；跳变判据只决定是否发布状态，不得决定缓存配对是否自洽。媒体指纹和 `m_lastSample*` 跳变检测逻辑保持独立 |
| 57 | **歌词源配置是顺序与启用集合两个维度**：`providers/order` 保存全部源的稳定顺序，`providers/enabled` 保存勾选集合；设置页使用可拖拽、可勾选列表且至少保留一个可见源。守护进程的 `AvailableProviders` 经 D-Bus 提供当前构建支持的全部源（包括未启用源），与 Resolver 的启用解析链严格分离；否则重启后被禁用的源会从设置页消失、无法重新启用。停机时前端退回静态列表；保存必须原样保留未编译或未知 id。空顺序/空启用结果回退内置默认并写日志 |
| 58 | **本地歌词是可搜索 provider**：默认顺序为 `local → netease → amll`，升级旧顺序时自动前置并启用 `local`。先尝试本地音频同级 sidecar，再递归扫描配置的歌词目录；sidecar 无有效时间行时不得遮蔽目录内的有效匹配。本地路径优先使用有效且存在的 KDE 私有 `kde:mediaSrc`，否则回退到标准 MPRIS `xesam:url`，只影响查询输入而不改变指纹/曲目身份语义。只收录可读 `.lrc`，目录状态由相同文件集合的相对路径、大小和修改时间组成 `cacheVersion()`；版本变化时才重新解析文件名和 LRC `[ti:]`/`[ar:]`/`[al:]`/`[length:]` 建立内存候选索引，版本不变时复用，并像 AMLL 一样只把排名最前的 50 条交给 Resolver 复核。Resolver 的一次 provider 尝试只读取一次版本，异步获取后的竞态检查使用 provider 已知版本，不重复扫描本地目录。对本地音频的查询从搜索到获取、空歌词及过滤后为空的全部失败路径都不写 provider 负缓存，因为新增或原地修复 sidecar 不会改变目录版本，旧失败不得抑制新内容 |
| 59 | **手工覆盖不是 provider 能力**：`overrideFor` 从 `Provider` 接口移除，覆盖读取器位于 `core/store/` 并由 Resolver 在缓存正文之前独立调用。`overrides/<provider>:<track-id>.lrc` 的路径与行为保持不变（解析约定见决策 62） |
| 60 | **强制切源进度由快照表达**：`lyric.switchingProvider` 非空表示指定源的强制解析仍在进行；部件据此禁用所有来源命令并显示目标源，Resolver 成功或失败都清空该字段。旧歌词在有效时继续显示，不使用前端固定超时猜测后端状态 |
| 61 | **日志格式与优雅退出**：`[time] type category message` 是日志文件以及 stderr 被重定向到文件/管道时的形态，tty 与 journald 下的形态见决策 64。`Resolver` 的每次解析以 `#<generation> ` 前缀关联同一请求的全部日志行，取消（如播放器消失）也会消耗一个编号，不产生对应的 `resolve:` 行，因此编号本身不连续。字段一律 `key=value`；可能含空格的字符串值（`fingerprint`、`title`、路径、以及任何来自 D-Bus 调用方的字符串）经 `quoted()` 转义为带引号的值——D-Bus 调用方字符串可被劈行伪造出多余的 `key=value`，本地文件名可含空格，不转义会被当作多个字段误读。info 级只在一次解析/发现事件的起点、经过的关键节点、终点各打一行，且是用户能感知的动作（触发原因、候选数与选中结果、最终来源）；debug 级解释「为什么」（候选打分明细、原始标题/艺人、缓存查找细节、索引变化重试），默认关闭，`logging/debug` 或 `QT_LOGGING_RULES` 打开。同一原则下，Resolver 的 `resolve:` 起点行在 `music=false` 时不带 `title=`/`artist=`，只有确认是音乐才记录曲目信息；MPRIS 的 `filtered` 行同理在 info 级出现但不带标题——非音乐来源的标题多是浏览器里的视频名，不该默认写进 journal，标题只出现在 debug 级的 `metadata changed` 行。严重级按「是不是系统故障」判定，而不是按「是不是导致退出」：「另一个实例已在运行」是良性的启动竞争，记 warning——退出码仍为 2，`Restart=on-failure` 依据的是退出码而不是日志级别，两者不可混为一谈；`qCCritical` 只留给打不开歌词库、注册 D-Bus 接口失败这类真实故障，因为 critical 映射到 `LOG_CRIT`，会进入 `journalctl -p 3 -xb` 这种「系统哪里出问题了」的排查视图。daemon 收到 `SIGTERM`/`SIGINT` 时经 self-pipe 在 Qt 事件循环里优雅退出（打印 `stopping reason=SIGTERM`/`SIGINT` 后 `QCoreApplication::quit()`），不依赖默认信号处置；安装点在单实例锁获取成功后立即执行，早于 store/resolver/D-Bus 初始化，缩短「默认处置」窗口，且不在 `--explain` 路径安装 |
| 62 | **本地与手工覆盖 LRC 的双语约定**：本地歌词目录、sidecar 与 `overrides/<provider>:<track-id>.lrc` 都识别 LDDC / 163MusicLyrics 一类工具导出的双语 LRC——相邻两行时间戳相同时，第一行为原文、第二行为译文。解析先复用 `LrcParser::parse()` 得到已排序、已应用 `[offset]`、已确定结束时间的行序列，再按 `startMs` 切出连续 run；run 内前两行任一带 `credit` 标记或经 `looksLikeCredit()` 判定像制作人员署名时，整个 run 原样保留——这组行是否会被随后的「过滤开头制作人员」逻辑再处理掉，取决于 `lyrics/filterLeadingCredits` 开关（默认开）：关闭时同样原样保留，只是不再被该逻辑过滤。否则两行文本相同则只留第一行，不同则第二行并入第一行的 `translation`，run 中第三行起一律丢弃。`looksLikeCredit()` 从 `filterLeadingCredits` 的行内判定中抽出并导出，两处判据保持同一套，但不带决策 29 的「前奏区」上限——这里安全，因为守卫误命中的后果只是「不配对、原样保留」，永不删行，与决策 29 针对「正则误伤真歌词是无声的数据损坏」的场景不同。新入口 `LrcParser::parseBilingual()` 只用于 `LocalProvider::fetch()` 与 `LyricOverrideStore::lyric()`；`candidateForFile()` 与网易云路径（`parse()`/`merge()`）不受影响。丢弃的行数经可选出参回传，`core/` 无日志分类，由 `providers/local`（`lcLocal`）与 `daemon/src/resolver.cpp`（`lcResolver`）在各自调用处记 debug 日志。`[offset]` 为负把多条不同时间戳的行截到 0ms 时形成的人为 run 按普通 run 处理、不特判：这类输入本身病态（偏移量超过了开头几行的时间戳），配对前这组行本来就只显示 run 最后一条，配对后显示的变成第一条并带译文——两种行为都有损，只是丢的行不同，不值得为病态输入加特判 |
| 63 | **网络代理三档、daemon 全局生效、解析失败即熔断**：`network/proxyMode`（`none`/`system`/`manual`，未知值按 `none` 处理并 `qCWarning`）与 `network/proxyUrl` 是 daemon 全局设置，对网易云与 AMLL 两个网络 provider 生效（含 `--explain` 命令行路径），**不做回环地址例外**——`127.0.0.1` 目标同样走代理。地址解析放在 `core/config/proxyspec.{h,cpp}`（`ProxySpec`，仅依赖 `QUrl`/QtCore），前端校验（`BackendConfig::proxyUrlError`）与 daemon 解析共用同一份逻辑，避免「UI 判定合法、daemon 判定不合法」两套标准。`manual` 模式解析失败是 fail-closed：网易云与 AMLL 视为未选中（效果等同 `providers/enabled` 未勾选它们），本地 provider 不受影响。失败原因只记枚举名；原始地址经 `ProxySpec::redactedForLog()` 处理后才写日志——只有 `QUrl` 认出 authority（`scheme://host[:port]`）时才用 host/port 单独重拼一个新地址，从不读取原始输入的 path/query/fragment/userinfo，凭据无论落在其中哪一处都进不了日志（`QUrl::RemoveUserInfo` 单独用不够：地址漏写 `//` 时凭据会被 `QUrl` 当成 opaque path 的一部分，`RemoveUserInfo` 认不出它是 userinfo）；authority 缺失或重拼结果仍含 `@` 时返回空串，此时只记字符串长度，不回显任何原文。凭据（`user:password@host:port`）落盘为明文，配置页说明与 README 均需提示这一点。SOCKS5 一支使用 `QNetworkProxy::Socks5Proxy` 的默认 capabilities（含 `HostNameLookupCapability`，见 Qt 文档 `QNetworkProxy` 页），目标主机名交由代理端解析，不在本机查询。生效方式与其余后端设置一致——保存并重启服务，无热加载 |
| 64 | **日志输出形态按 stderr 的实际去向三分**：`isatty(2)` 为真走 ANSI 彩色并保留 `[时间] 级别 分类` 前缀；非 tty 且 `$JOURNAL_STREAM` 的 `device:inode` 与 `fstat(2)` 得到的 stderr 一致时判定为 journald，改为逐行加 syslog 优先级前缀 `<N>` 并去掉自带的时间戳与级别（journald 本身已记录这两项，重复渲染正是要消除的冗余）；两者都不满足（重定向到文件或管道）保持改动前的纯文本格式。判定用字符串相等比对 `device:inode`，不对环境变量做数值解析；`$JOURNAL_STREAM` 缺失、畸形或不匹配一律退回纯文本，不静默进入错误形态。**优先级映射直接取 Qt 自己 journald 后端的取值**——debug=7、info=6、warning=4、critical=2（`LOG_CRIT`）、fatal=1（`LOG_ALERT`），**不是直觉上的 `LOG_ERR(3)`**：`qInstallMessageHandler` 安装之前（`QCoreApplication` 构造期间，例如 Qt 自己的 locale 警告）产生的日志走 Qt 原生 sink、用的就是这套取值，自研映射若另选一套，同一份 journal 里同一严重级会分裂成两个优先级，`journalctl -p` 的结果将取决于日志发生在安装前还是安装后。这五个数字不得按「LOG_ERR 才对」的直觉改回。分类显示名在三种形态与日志文件里一律去掉 `plasmalyrics.` 前缀，但 `QLoggingCategory` 的真实名字不变——`QT_LOGGING_RULES`、`daemon/plasma-lyricsd.categories` 与用户既有 `qtlogging.ini` 都依赖真实名。`QT_MESSAGE_PATTERN` 被设置时消息主体改用 `qFormatLogMessage()` 渲染（stderr 三种形态与日志文件一致），此路径下 `%{category}` 保留**真实**名：那是 Qt 自己的语义，且真实名正是用户要粘进 `QT_LOGGING_RULES` 的字符串，在这条逃生舱上剥皮是主动做坏。journald 形态下多行消息的**每一行**都加前缀，不只首行——续行若无前缀会退回 journald 默认优先级，更要紧的是续行文本以 `<数字>` 开头时会被 journald 当作真的优先级标记解析（默认 `ForwardToWall=yes` + `MaxLevelWall=emerg` 下 `<0>` 会 wall 广播到该用户所有已登录终端），逐行占位从结构上封死这个注入口；`QT_MESSAGE_PATTERN` 用户可控，可达性不为零。消息处理器改为**无条件**安装（不再只在 `logging/fileEnabled` 为真时安装），代价是默认配置用户不再获得 Qt 原生 sink 的 `QT_CATEGORY` 结构化字段（`CODE_FILE`/`CODE_LINE`/`CODE_FUNC` 在 Release 构建下本来就不存在，项目未定义 `QT_MESSAGELOGCONTEXT`），换取的是跨发行版行为确定：链式调用 Qt 默认 handler 的方案会让日志形态取决于打包者有没有给 Qt 编 journald 支持，而 AUR 是唯一正式分发渠道、打包环境不由本项目控制。格式化逻辑抽到 `daemon/src/logmirrorformat.{h,cpp}` 的纯函数，`isatty`/`fstat` 探测、全局状态与 I/O 外壳留在 `main.cpp`；`mirrorMessage` 现在是**全体用户**默认路径上的代码，4 种目的地（tty/journald/纯文本 stderr/日志文件）× 2 种 `QT_MESSAGE_PATTERN` 状态的矩阵必须由单元测试覆盖，因为格式回归是静默的——处理器自己吞掉自己的输出，没有别的机制能发现。测试目标统一设置 `LC_ALL=C.UTF-8`：ctest 用管道收集输出，非 tty 下每个测试二进制的 Qt locale 警告都会经 `sd_journal_send()` 写进开发者真实的 journal，而 `sd_journal_send()` 不看 `DBUS_SESSION_BUS_ADDRESS` 也不看 XDG 目录，任何进程级隔离都拦不住它 |
| 65 | **译名括号靠查询侧变体处理，且只在精确命中时算数**：查询标题的尾部括号内**不含**版本标记词时，剥离括号后的主体作为**额外变体**参与打分，与候选侧（`candidate.title` + `alternateTitles`）取最大。判据是**反向**的——用现成的 `versionEvidence()` 词表 ∪ `cleanTitle` 正则的标记词（`伴奏|纯音乐|live|版|ver\.?|cover|remix`）取**并集**，两份任一认为是版本标记就不剥离。用并集而不是单独一份：`版`/`ver` 只在 `cleanTitle` 里有，`歌名 (2024版)`、`Song (Ver. 2)` 会被 `versionEvidence` 单独判成「无版本标记」而错剥，捅穿决策 54 的版本区分；守卫的正确方向是保守——宁可漏剥（退回 not-found）也不能错剥（选错版本）。**不得改 `versionEvidence()` 自身的词表**，那会连带改变 `VersionTier` 分类。**剥离变体只有相似度精确等于 1.0 时才参与取最大**，低于 1.0 一律不采纳：剥离后的主体更短，短串被长串包含的概率大得多，`textSimilarity` 的包含快路径按 `min/max` 给分，CJK 短标题很容易过 0.55（实测 `心跳 (跳动的心)` 对同歌手无关曲目 `心跳吧` 得 2/3=0.667 并被选中）。做成「不采纳」而不是「先抬分再靠闸门拒绝」：后者会改变排序，正是下面那条二阶效应的来源。**「括号内文本本身」不作为第三个变体**——实测 `Song (Intro)` 会和同专辑无关的 `Intro` 曲目在 `title=1.0` 打平，而 Intro/Interlude/Outro/Skit 正是专辑里最常见的短曲名。**靠剥离变体胜出的候选需要独立证据背书**：`ScoreBreakdown::titleViaGlossVariant` 标记之，`passesGlossVariantGate` 要求 `durationComparable && durationDifferenceMs <= 2000`（窗口值复用决策 12 的「2s 内判为最佳」，不新造常数），并 AND 进 `isAcceptableMatch`（而非像 `passesAliasArtistGate` 那样要求调用方自己记得加）。起因是「同艺人 + 剥离后主体字面撞车」能把 `total=0.7` 的错歌送过两道阈值，而时长差 185 秒也拦不住（`duration` 早已跌到地板 0，权重只有 0.2）。**时长未知时拒绝**：不放行，否则缺时长的候选就是绕过闸门的通道。曾评估用 album 做替代佐证并**否决**——最常见的碰撞形态恰恰是同专辑的 Intro/Outro/Skit 短曲，它们与正确答案**共享专辑**，album 在此不但不能鉴别还会反向背书；候选侧 `alternateTitles` 只对填 `transNames` 的网络源有意义，救不了本地场景；曲目编号要扩 `Candidate` schema；语种/字形判断本条明令不用（只覆盖日→中，韩中/英中/纯汉字日文标题全漏）。时长未知时判别所需的信息客观上不存在，此时按决策 45 的取向选择 not-found。代价：AMLL 索引不提供时长（`parseIndex` 结构上就没有赋值处），该源上这条修复完全失效；本地 `.lrc` 目录搜索路径的 `lengthMs` 来自可选的 `[length:]` 标签，缺失时同样失效（sidecar 路径不受影响，它直接拷贝 `query.lengthMs`）。因此拒绝原因分成 `gloss-duration-threshold`（时长已知但超窗口，不可操作）与 `gloss-duration-unknown`（任一侧缺时长，用户可补 `[length:]` 标签），两者对用户的含义与可操作性完全不同。`titleVia=` 相应扩为 `title`/`alias`/`gloss`/`alias+gloss`；平手时优先非剥离变体——闸门的语义是「这次匹配**依赖了**剥字符才成立」，存在等分的非剥离路径就说明它不依赖，闸门不该介入。**`chooseMatch` 的 Default 分支改为遍历，但只跳过被闸门取消资格的候选**：抬分会改变排序，而闸门是与「哪个变体获胜」绑定的硬拒绝，两者叠加后一个排第一、被闸门拒绝的候选会让整次匹配返回 none，把排第二的合格候选埋掉（该弱点本就存在于 `passesAliasArtistGate`，本条只是让它在一类常见输入上变得可达）。**跳过范围必须限于闸门，不能用 `isAcceptableMatch` 整体跳过**：排序键是 `total` 而 `title >= 0.55` 不是，首位候选可以总分最高却栽在标题阈值上，整体跳过会把「不给歌词」变成「给一首差 20 秒的错歌词」，作用于每一次 Default 匹配而不只是双语标题。阈值失败时用 `break` 而非 `return`——「主路径到此为止」不等于「整个 `chooseMatch` 到此为止」，本地化回退（决策 8）是独立判定，不得被连带截胡。**诊断必须跟得上**（决策 46）：`--explain` 此前写死 `lengthMs=0`，凡需剥离才能匹配的候选一律显示 `gloss-duration-unknown`，与守护进程实际行为系统性相反；新增 `--length-ms` 选项（具名而非第三个位置参数，因为 ARTIST 本就可选）同时修好这一类和本地化回退那一类（后者要求 `deltaMs <= 250`，同样从来无法在 `--explain` 中复现）。`clean title:` 行改为 `title variants:` 列出实际参与打分的全部查询侧变体。**性能**：`queryTitleVariants` 只依赖查询标题，必须在 `rankCandidates` 里算一次传入，不得放在 `scoreCandidate` 内每候选重算；`versionEvidence()` 的 16 个正则为 `static const`，否则每候选现场编译——用户真实 AMLL 索引（约三千条）实测 `PreserveVersions` 路径 1.4 秒降至 0.18 秒，该开销在本条之前就存在 |

### 工程结构
| # | 决策 |
|---|---|
| 13 | 内部歌词模型从第一天预留 `words: Option<Vec<Word>>`；**渲染层第一版不做逐字** |
| 16 | 数据契约：推整首 + 时间锚点（见 2.2） |
| 17 | 布局重新设计（见 2.3），不照抄 nethogs |
| 18 | 配置分两份：后端全局一份 + 前端每实例一份；后端配置的编辑界面仍在 plasmoid 设置里，但要视觉上分开并标注"影响所有歌词部件"。**存储落点**：后端那份是 `QSettings` INI（`~/.config/plasma-lyrics/plasma-lyricsd.ini`——`backendconfig.cpp:8-12` 与 `daemon/src/config.cpp:9-10` 构造的是同一个路径），前端那份是 kcfg（`<kcfgfile name=""/>`，落在 `plasma-org.kde.plasma.desktop-appletsrc` 里每实例自己的 `[Containments][…][Applets][…][Configuration][General]` 组）。**"每实例一份"是能力，不是缺陷**（2026-09-05 用户报"两个部件设置不互通"后补记）：同时摆一个桌面部件和一个面板部件是 §2.1 明写支持的用法（"快照只读、多前端无冲突，这正好满足『同时摆两个部件』"），此时两者的外观、文本、自动隐藏各自独立，四个 tab 里**只有「歌词服务」跨实例共享**。而恰好只有那一页带 InlineMessage 声明自己是全局的（`ConfigBackend.qml:16-21`），于是**其余三页的沉默被读成了"共享"**——设计是对的，界面失语。因此三个前端 tab（桌面外观 / 面板外观 / 文本）各加一条 InlineMessage："这些设置只作用于当前这个部件"。决策 40 把「文本」页那三个键叫作"全局文本键"，那里的"全局"指的是**跨 form factor**、不是跨实例，`ConfigText.qml` 的顶部注释已相应改写，免得它自己变成下一个误解源。**改做 form factor 感知**（桌面实例只显示「桌面外观」页，面板实例只显示「面板外观」页）：机制在 `config.qml` 一层——`ConfigModel` 上加 `readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar`，两个外观 `ConfigCategory` 各自加 `id`（`desktopAppearance`/`panelAppearance`），`ConfigModel` 的 `Component.onCompleted` 里 `configModel.removeCategory(onDesktop ? panelAppearance : desktopAppearance)` 把不适用的那一个整条移除（判据是「非 Planar」而不是「等于 Horizontal」，垂直面板同样算面板）。**不用 `ConfigCategory.visible`**（2026-09-11 qa-1 发现后改定）：`visible` 只过滤设置对话框侧栏的 `Repeater`，两处壳（`org.kde.plasma.desktop` 与 `plasmoidviewershell` 各自的 `AppletConfiguration.qml`）的 `Component.onCompleted` 决定初始显示哪一页时都直接读**未过滤**的 `configDialog.configModel.get(0)`，不检查 `visible`——只用 `visible` 会让面板实例每次打开设置都先落在被隐藏、侧栏无高亮的「桌面外观」页，需要用户自己点一下才能纠正。移除分类后 `get(0)` 在两种形态下天然就是各自对应的外观页：`Desktop appearance` 保持声明顺序第 0 位、`Panel appearance` 第 1 位不变，桌面落地页不变，面板落地页从「桌面外观」变为「面板外观」。原否决理由依然成立且与本次机制选择无关：`tst_appearance.qml:50,55` 直接实例化的是 `ConfigDesktopAppearance.qml`/`ConfigPanelAppearance.qml` 这两个页面文件本身，从不加载 `config.qml`，两个页面文件仍然不得引用 `plasmoid`/`Plasmoid`（`main.qml` 已有同一判据 `onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar`，此处是配置目录下新增的第二处）。**时序依据**：`PlasmaQuick::ConfigView` 在 C++ 里先 `create()` 出 `config.qml` 的 `ConfigModel`（其 `Component.onCompleted` 同步跑完、分类已移除），随后才加载壳的 `AppletConfiguration.qml`，壳读到的 `get(0)` 已经是移除之后的结果；本机没有 libplasma 源码，C++ 侧调用顺序无法本地核对，**结果已在 plasmoidviewer 的壳里实测**：`-f planar` 落「桌面外观」、`-f horizontal`/`-f vertical` 落「面板外观」，侧栏都只剩 4 个插件分类。`org.kde.plasma.desktop` 的壳未实测，其决定初始页的 `get(0)` 代码与 plasmoidviewer 壳逐字相同。**已接受的残留**：这条分支没有自动化测试覆盖（`config.qml` 引用 `Plasmoid`，`tst_appearance.qml` 不实例化它，原因同上），但侧栏分类与初始落地页已实测（方法：`frontend/plasmoid/package` 复制到临时目录，只在副本 `main.qml` 里加一个 `Timer` 调 `Plasmoid.internalAction("configure").trigger()` 让部件自己打开配置对话框，仓库不动，`plasmoidviewer -f planar/horizontal/vertical` 各起一次被动截图核对，不需要人工点击或输入注入）；另外，面板实例从此无法在设置页编辑 `desktop.*` 键、桌面实例无法编辑 `panel.*` 键：决策 31 的对称键仍然各自独立保存，只是这些键对该实例本就无效，在当前形态下不可见即不可改，部件在两种形态间移动后，另一形态沿用上次落盘的值。`Plasmoid`/`PlasmaCore` 的静态引用与限定调用 `configModel.removeCategory(...)` 的方法名已由 qmllint 覆盖（拼错方法名会报 `[missing-property]`），但 `desktopAppearance`/`panelAppearance` 这两个 `id` 参数本身不受 qmllint 覆盖（拼错 id 时 qmllint 仍然 exit 0；三元表达式只求值命中的那一支，所以运行时只在会取到该 id 的那种形态下抛 `ReferenceError` 并中断 `Component.onCompleted`，此时 `removeCategory` 不被调用、两个外观页都留在侧栏，另一种形态不受影响）。**另一处残留**：`removeCategory` 只在 `Component.onCompleted` 时对 `onDesktop` 采样一次，不是响应式绑定——对话框开着时若形态发生变化，需要关闭重开设置对话框才会生效；本插件同一部件实例的生命周期内 `formFactor` 不会变化，这里只是如实记录，不代表已知的用户可见问题。**明确不做跨实例共享外观**：那要把 46 个键搬去全局存储、再补一套变更通知（`BackendConfig` 全无通知路径，现有 UX 靠"改完重启 daemon"糊过去；仓库里唯一的跨实例活体同步是 `lyricsource.cpp:142-159` 每 2 秒轮询 SQLite 的共享偏移），换来的却是"两个部件必然长得一样"——而决策 31 的 `desktop.*`/`panel.*` 对称本来就是为"同一实例在两种形态下各有物理上合适的值"服务的，与跨实例无关；§0 也早把"后端配置全局共享导致多实例互踩"列为 Lyrica 的坑 |
| 21 | 命名：applet `io.github.swim233.plasma-lyrics` / QML URI `io.github.swim233.lyrics` / daemon `plasma-lyricsd` |
| 22 | `core/` 依赖 QtCore，**禁 QtNetwork 与 QtDBus**——这条禁令就是"可测"的定义边界 |
| 24 | daemon 用 `systemctl --user enable --now` 启动（不用 D-Bus 自动激活：daemon 需要在没有任何前端时也持续跟踪进度）；**plasmoid 里必须做"服务没跑"的引导界面** |
| 31 | 桌面与面板**各一套完整外观配置**（kcfg 开 `desktop.*` / `panel.*` 两组） |

### 外观
| # | 决策 |
|---|---|
| 25/32/36 | 可读性是**两个独立维度**：底板 `plate ∈ {none, ksvg, solid}`（**默认 `ksvg`**）× 描边 `stroke: bool`（**默认 `false`**——默认底板已足够压住壁纸，描边要 8 份 Text 副本，不该默认开）。毛玻璃**删除**（1.3：结构上不可能），第三档换成纯半透明色块。**`ksvg` 档的底板由 plasmashell 画**（`Plasmoid.backgroundHints: DefaultBackground`），不能自己摆 `KSvg.FrameSvgItem`：手画的框是创建时的一次性快照，主题切换后不跟随，还会绕过框的内边距。其余两档 shell 必须让位（`NoBackground`），由部件自己画。**⚠️ 本条「不能自己摆 `KSvg.FrameSvgItem`」的技术论断已由决策 40 证伪**（实测 Plasma 6.7.4 / KSvg 6.29）：shell 画的底板**本身就是**普通 QML 里的 `KSvg.FrameSvgItem { imagePath: "widgets/background" }`（`BasicAppletContainer.qml:86-105`）——不存在什么 shell 特权机制；「主题切换后不跟随」假：`framesvgitem.cpp` 构造函数 `connect(m_frameSvg, &FrameSvg::repaintNeeded, this, &FrameSvgItem::doUpdate)`，`componentComplete()` 再接 `ImageSet::imageSetChanged`，`imageset.h:214-225` 把契约写在头文件里，配色方案切换由 `svg.h` 的 `colorsChanged()` 单独覆盖；「绕过框的内边距」假：`margins` 带 `marginsChanged` 信号（现成写法见 `ConfigOverlay.qml:75-92`、`Menu.qml:34-37`），`Menu`/`Popup`/`Dialog`/`DialogBackground` 整个 Plasma 对话框栈都是自绘、无一靠重建跟随主题。该论断对 Plasma 5 的 `PlasmaCore.FrameSvgItem` 是否曾成立**未查证**。**现状（决策 40 起）**：桌面档 `ksvg` **仅在自动隐藏开启时**改由 `LyricsView` 自绘、`Plasmoid.backgroundHints` 相应切 `NoBackground`——因为 shell 底板是 applet item 的**兄弟**节点（`itemcontainer.cpp`），`opacity` 无论挂哪都淡不掉它；但 shell 画的那份严格更好（见决策 40 的毛玻璃条款），所以只在真需要它淡出时才接管；**面板档 `ksvg` 继续什么都不画**（面板容器只画整条面板的 `panel-background`，其 `AppletContainer.qml` 是无边框裸 `Loader`——「Plasma 主题」在面板里本来就名不副实，此处保持存量行为不变） |
| 26 | 翻译行"等号弱化"：同字号，译行降透明度（CJK 曲库下缩字号会造成字面大小突变） |
| 27 | 超长歌词三种全做：`fit`（HorizontalFit + 下限 60%）/ `wrap`（≤2 行）/ `marquee`，**默认 `fit`** |
| 28 | 切行动画三种全做：无 / 淡入淡出 180ms / 上移推入 260ms，**默认上移推入** |
| 29 | 制作人员行用**启发式过滤**（详见 §6.1，含实测边界）：自开头连续扫描，命中 `^[^\s：:]{1,12}[：:]\s*.+$` 的行视为制作人员行，遇到第一个不命中的行即停止（该行即"首句真歌词"）。**限定在前奏区 + 冒号结构双条件**，不用无底洞式关键词表——正则误伤真歌词是无声的数据损坏 |
| 30 | **字族**跟随 Plasma 通用字体设置（`Kirigami.Theme.defaultFont.family`）；**字号、字重、颜色是部件自己的配置，不跟随主题**——颜色尤其必须脱钩（歌词压在壁纸上，`Kirigami.Theme.textColor` 在浅色主题下会彻底翻车）。颜色两种编辑方式并列：Plasma 取色器（`org.kde.kquickcontrols.ColorButton`）+ 十六进制输入框，共用同一个 `#RRGGBB` / `#AARRGGBB` 字符串——QML 颜色的 `toString()` 保留 alpha 字节，所以取色器不会把半透明默认值（`#99000000`、`#cc000000`）变成不透明。字重给 6 档（Light / Regular / Medium / DemiBold / Bold / Black，**默认 Regular = 400**）；字族没有的档位 Qt 会吸附到最近的实有档位，所以不铺满 9 档 |
| 14 | 八种非歌词显示情况：① 未播放 → 可自定义文案，默认「当前未在播放」；② 查不到 → 默认「暂无歌词」；③ 间奏 → 空；④ 暂停 → 保留最后一行不动（靠"非 Playing 不推进位置"实现，不特判）；⑤ 搜索中 → 显示「搜索中…」；⑥ 非音乐过滤 → 空；⑦ `no-lyric`（匹配成功但源站无词）→ 默认「此曲无歌词」；⑧ `network-error` → 默认「网络错误，暂时无法获取」。②的旧默认为空，其隐含前提是决策 4 的「查不到就算了」和「waylyrics 单开网易云基本都能查到」；2026-09-07 实测 22 次解析有 12 次 `selected: none`（主要是 Vocaloid / 独立曲目），该前提已被数据推翻，因此改为非空默认。未播放、查不到、源站无词和网络错误文案都沿用 `idleTextUseDefault` 的伴生 `*UseDefault` Bool 模式，用户仍可显式设为空文案 |
| 39 | **曲目信息行**：歌词区上方一行常驻「标题 — 歌手」，桌面默认开 / 面板默认关（面板 `gridUnit*2` 高度塞不下第三行文字）。不显示专辑：CJK 单曲专辑名常与标题相同，`core/match/matcher.cpp:92` 已经因为这个把同名专辑排除出搜索关键词。不显示封面：全链路未采集 `mpris:artUrl`；远程图要么让每个部件实例各自发网络请求、破坏「前端只是快照只读消费者」，要么给 daemon 加一套下载缓存——那是独立功能。样式完全独立于歌词（字号/字重/颜色/描边/描边色/溢出/布局各一套 kcfg，桌面面板对称，共 16 键），因为它是另一个语义层；决策 26 的「同字号」先例只管同一条歌词的译文。唯一硬编码的是两行模式歌手行 alpha ×0.75（排版层级，不是用户会调的东西）。溢出只给 `fit`/`elide`，不给 marquee——曲目一整首歌不变，滚动只抢走歌词的注意力。`trackTitle` 为空时整行不占高度；`searching`/`not-found`/`filtered`/`no-lyric`/`network-error` 时照常显示，这正是常驻的价值（那些状态下歌词区本来不显示实时歌词）。本功能零 daemon 改动、零 schema 改动——快照早就带着 `title`/`artists`，这是决策 16 数据契约的红利 |
| 38 | **前端不轮询，按边界唤醒**：位置是解析式的（锚点 + `CLOCK_MONOTONIC`，见 2.2），换行时刻因此可以精确算出来——`nextBoundaryMs()` 求下一个能改变行号的时刻，单次 `Qt::PreciseTimer` 直接打过去。原实现是 33ms 固定轮询：一首 4 分钟 60 行的歌要醒 7200 次，其中约 60 次真的换了行，而且换行还被量化在 33ms 网格上（`QTimer` 默认 `Qt::CoarseTimer`，再叠最多 5% 抖动）；改后约 120 次唤醒、误差进入 1ms 内。三条实现约束：① 边界取所有 `startMs`/`endMs` 的**超集**——行尾可能晚于下一行行首，不能只扫到第一个更晚的行首；② 无锚点或 `rate <= 0` 时不武装，位置根本不会自己走，武装只会空转；③ 单次间隔封顶 60s，把畸形时间戳（`[9999999:00.00]`）挡在定时器的 `int` 之外。**将来做逐字扫词（决策 13）时驱动应是 QML `FrameAnimation` 逐帧拉取，不是把定时器调回 16ms**——帧驱动在窗口不渲染时会自己停，定时器不会 |
| 40 | **无歌曲时自动隐藏**：独立开关（**默认关**——本功能有一条硬代价见下，默默开给存量用户会变成一堆「壁纸上有块点不动的区域」的 bug 报告），语义是「整个部件不可见」，与底板 `plate` **完全解耦**——不复用「`plate = none`」，那一档只是不画底板、文字照旧渲染（默认还会显示 `idleText`）。判据 = `determined && serviceAvailable && !stale && (无播放器/`Stopped` ‖ (`filtered` ∧ 开了「非音乐媒体也隐藏」，默认勾选))`。**`Paused` 不算无歌曲**（暂停十秒去接水不该把歌词淡掉，且暂停本身意味着还想接着听）；**已知风险**：若播放器队列放完停在 `Paused`（metadata 仍挂着最后一首）而非 `Stopped`，退场永不触发、部件会一直挂着最后一行——需实测目标播放器的 MPRIS 收尾状态。**`!serviceAvailable ‖ stale` 时绝不隐藏，且不可配置**：`LyricsView.qml` 那个 `Loader` 是唯一告知「daemon 挂了、执行 `systemctl --user restart`」的 surface，朴素判据（「`Stopped` 或无标题就隐藏」）会在 daemon 崩溃时把部件永久静默隐藏——用户既看不到歌词也看不到原因。退场有可配置缓冲（默认 5 s，0–120 s，**0 = 立即开始淡出**；关功能是主开关的事，不让 0 兼任第二个开关），缓冲计的是「**判据成立至今多久**」而非「配置变更至今多久」，所以空闲时勾上主开关会立刻淡出；入场无缓冲。`filtered` 走**同一个**缓冲键（视频/音乐边界的穿越比队列放空频繁得多，无缓冲会闪成频闪灯）。**桌面**：`opacity` 1↔0，时长可配（默认 1000 ms，0–3000，步长 50，**0 = 无动画**），双向**共用一个键**（两方向不同时长会让「从当前值反向」失去定义）、`Easing.OutCubic`（先快后慢；KDE 单向减速事实标准，装机栈内 146 处；`OutQuad` 偏线性、`OutExpo` 尾部像卡住）。配置时长是**绝对值**、不受 `[KDE] AnimationDurationFactor` 缩放（否则 SpinBox 会说谎），但 `Kirigami.Units.longDuration <= 1`（用户全局关了动画）时**整体跳过**动画。**首次判定两个方向都不播动画**（`Behavior { enabled: 已过首次判定 }`）——动画只属于运行期的状态转移，否则每次登录都会淡入一次，daemon 没跑时还会变成「诊断文案淡入」。淡出中来新歌**从当前不透明度反向**（`Behavior` 天然语义，永不跳变）；缓冲期内来新歌只取消定时器、不播任何动画。**面板**：无动画，直接切 `Plasmoid.status = HiddenStatus`（真正把容器从 `GridLayout` 摘掉、邻居重排，见 `LayoutManager.js:14-26`；`opacity: 0` 会留 `gridUnit*14 ≈ 252 px` 空洞，而 `Layout.preferredWidth: 0` **无效**——面板 `main.qml` 的 `findPositive` 把 0 当「未设置」并替换成面板厚度）。此举对面板自动隐藏/闪避**零影响**（`panelview.cpp:1028` 有专门的 `!= HiddenStatus` 守卫，正因 Hidden 数值最高）；逃生口是 KDE 自带的 `‖ (!Plasmoid.immutable && Plasmoid.userConfiguring) ‖ Containment.corona.editMode`。`Plasmoid.status` **必须命令式重申、不能写成绑定**：`expanded` 可由每个 applet 都有的全局快捷键（`applet.cpp:760-766` + `ConfigurationShortcuts.qml:21`）和键盘 Space/Enter（`CompactApplet.qml:95-104`）驱动，**不需要任何 `MouseArea`**，而 `CompactApplet.qml:230` 会 `Plasmoid.status = RequiresAttentionStatus` 摧毁绑定（`Binding {}` 元素也救不了）→ 在自身条件 handler 与 `onExpandedChanged` 变 false 时各写一次；且 `shouldBeVisible` 为假时**强制 `expanded = false`**，禁止弹出一个锚定在不可见 item 上、内容为空歌词区的弹窗。用 `ActiveStatus` 而非 `PassiveStatus`（`containment_p.cpp:90-110`：Passive 会触发容器状态重算，可能在任意时刻把焦点抢回上一个窗口）。**绝不写自身根上的 `visible`**（`LayoutManager.js:26-28` 已装 shell 的绑定），但可**只读**它得知面板已把我们隐藏。**桌面隐藏期间会隐形拦截左键，这是已接受的代价**：`ItemContainer` 构造函数 `setAcceptedMouseButtons(Qt::LeftButton)` + `setFiltersChildMouseEvents(true)`，吃点击的是容器不是我们，从 applet 内部**无解**——`opacity` 按 Qt 规定不影响输入事件，`visible: false` 被 shell 覆写（`setContentItem()` 的 `item->setVisible(true)`、`CompactApplet.qml:35-43`）且不缩容器几何，`HiddenStatus` 在 Planar 上 **no-op**（desktopcontainment 与 `libcontainmentlayoutmanagerplugin.so` 都不提这个枚举）。右键仍出上下文菜单、长按仍进编辑模式，**不会把部件锁死**。**tooltip 无需任何处理**：面板侧 `HiddenStatus` 已把链路上两个 item 置 `visible: false`（`AppletContainer.qml:24` 的容器 + `LayoutManager.js:26-28` 直接绑我们的 `PlasmoidItem` 根），而 hover 投递跳过不可见子树（`qquickdeliveryagent.cpp:1229-1230`）；桌面侧**根本不存在 tooltip 通路**（`main.qml` 在 Planar 下设 `preferredRepresentation: fullRepresentation` → `appletShouldBeExpanded()` 为真 → 走 full 分支，`CompactApplet.qml`（栈内唯一消费 `toolTipMainText` 处）的 expander 从不创建；`BasicAppletContainer.qml` 也无 `ToolTipArea`）。且抑制本身不可靠：`tooltiparea.cpp:118-120` 用 C++ setter 直塞共享默认 item、绕过 QML 绑定，**弹过一次后 `isValid()` 永久为真**，清空两个文本只会得到空气泡。**状态机在编译型 QML 类** `frontend/qmlmodule/visibilitypolicy.{h,cpp}`（`QML_ELEMENT`，同 `LyricSource`/`BackendConfig`），吃**六个离散输入属性**（`serviceAvailable`/`stale`/`playbackStatus`/`trackTitle`/`lyricState`/`determined`）而非整个 `LyricSource` 对象——这样单测直接 setter 灌值跑真值表，不必构造快照文件，且对 `LyricSource` 零编译期依赖。`main.qml` **单实例挂 `PlasmoidItem` 根**，配置用 `onDesktop ? desktop* : panel*` 三元喂（沿用 `activePlateMode` 先例）；放 representation 内部会因 `Loader` 生死**静默重置正在跑的缓冲计时**（把部件从桌面拖进面板，歌词会莫名重新出现）。缓冲用 C++ `QTimer` 而非 QML `Timer`——后者是 `QPauseAnimationJob` 驱动（16 ms 分辨率、无补偿），窗口不渲染时可能不推进；注意 C++ `QTimer` 同样是单调时钟、不补偿休眠，规避的是动画驱动停摆而非抗休眠。新增 `LyricSource::determined`（首次 `reload()` 结束时置真，无论成败）**必须自带 `determinedChanged` 信号**：`setUnavailable()` 的 `const bool changed = m_serviceAvailable ‖ m_stale != staleValue;` 配上初值 `m_serviceAvailable=false, m_stale=false`，在「daemon 没跑的冷启动」这条路径上 `changed == false`、**恰好不发 `statusChanged`**——复用会让绑定永不更新、未定态永久驻留、诊断文案永不出现，即本条钉死要防的那个失败，且只在 daemon 挂了时才显形。之所以需要 `determined`：初始状态与「首次读取失败」后的状态是**完全相同的五个值**（`serviceAvailable=false, stale=false, lyricState="filtered", playbackStatus="Stopped", trackTitle=""`），五输入无法区分「尚未判定」与「已确认不可用」，而两者行为相反。**「未定」态行为等同隐藏**（面板 `HiddenStatus`、桌面 `opacity: 0`，不播动画，尚不能显示诊断文案），只持续**一轮事件循环**（`lyricsource.cpp:67` 的 `QTimer::singleShot(0, ..., &LyricSource::reload)`；`m_retryTimer`/`m_healthTimer` 虽都是 2000 ms 但**都不参与首次判定**，后者只做 pid 存活检测与共享偏移同步）——面板做不到「既不显示也不隐藏」的中间态，因为 applet 默认的 `UnknownStatus` 是**可见**的。**但主开关关闭时 `shouldBeVisible` 恒为真，且该判断先于「未定」分支与其余一切判据**——「未定等同隐藏」只服务于「自动隐藏开启时别闪出马上要淡掉的内容」这一个目的，功能关闭时必须逐字节保持改动前行为。漏掉这一层会让**每个**面板实例（含从未启用本功能的存量用户）开机瞬间走一次 `HiddenStatus`、邻居重排一次再翻回来，恰好违背本条「默认关 = 存量零观感变化」的初衷；成因是 `Component.onCompleted` 同步执行、早于 `LyricSource` 构造函数里那个 `singleShot(0)` 的首次 `reload()`。**7 个 kcfg 键，故意不对称**（桌面 `desktopAutoHide`/`desktopHideDelaySec`/`desktopHideAnimationMs`/`desktopHideNonMusic`，面板 `panelAutoHide`/`panelHideDelaySec`/`panelHideNonMusic`）：砍掉 `panelHideAnimationMs`,因为面板不做动画，留着就是一个转起来毫无效果的 SpinBox——决策 31 的对称是为了「同一实例在两种 form factor 下都有物理上合适的值」（字号 34 vs 16），而面板压根没有「动画时长」这个概念可言，不是同一个东西缺了一半。设置页因此**拆成四个 tab**（桌面外观 / 面板外观 / 文本 / 歌词服务）：三个全局文本键（`idleText`/`idleTextUseDefault`/`notFoundText`）进「文本」——不能在两个外观 tab 各放一份（同键两处编辑，用户会以为它们独立），也不能塞进「歌词服务」（那页明说「影响每个实例、改完要重启 daemon」，而文本键是 per-instance 且即时生效）。`AppearanceSection` 拆掉 `Kirigami.Card` 与 `title`（tab 名已承担标题职责，留着卡片标题会和 tab 名重复出现），自动隐藏分节放**页面顶层第二个 `FormLayout` + `twinFormLayouts`**（`FormLayout.qml:84`）而非塞进 `AppearanceSection`——两个 tab 的自动隐藏区块内容已经不同（桌面 4 控件 / 面板 3 控件），共用子组件会退化成带 `visible: isDesktop` 条件的错抽象，且顶层可用朴素 `property int cfg_x` 省掉仓库里那套「隐藏控件 + `property alias` + `required property var xControl`」的绕法。子控件用 `visible:` 跟随主开关（先例 `AppearanceSection.qml:69`、`ConfigBackend.qml:69`）。拆 tab **无数据迁移**（kcfg `<group name="General">` 与键名全不变）。**分两次提交**：① 底板搬家（纯行为保持，验证=切主题/切配色/对比截图，可独立回滚）② 自动隐藏——混成一次改动的话，桌面外观若出偏差将无法区分是哪一边引入的。**测试**：`VisibilityPolicy` 的 C++ 单测覆盖六输入真值表（含「未定」行与服务不可用例外）、缓冲期内取消、淡出中反向、冷启动不播动画；**不加** QML 端到端（测动画中途的 `opacity` 数值要靠 `qWait` 卡时序，是典型 flaky 来源，而 QML 侧只剩「把 bool 绑到 `Behavior`」一行）。**毛玻璃：自绘只在自动隐藏开启时进行**（2026-09-05 实测更正）。此前依据「默认主题下毛玻璃是死代码」采纳了无条件自绘，那个结论抽错了样本——只查了 `air`/`default`/`oxygen`/`breeze-*`，而用户实际在用的 ChromeOS 主题的 `widgets/background.svg` 带 **26 个 `blurred-*` 元素**。shell 在 `BasicAppletContainer.qml` 判断 `hasElementPrefix("blurred")` 为真时，会把 frame 切到 `prefix: "blurred"` 并叠一个 `MultiEffect` **在 QML 里采样壁纸自行模糊**、用主题的 `blurred-mask` 裁切（不是 KWin：KWin 对桌面窗口 `shouldBlur()` 返回 false，见 §末「毛玻璃底板」一行）。该效果依赖 `appletContainer.Window.window` 与容器内部结构，**applet 内部无法复制**——无条件自绘会把带毛玻璃的主题底板变成一块实心暗块。**因此底板在动画边界换手**：静止可见时由 shell 持有（毛玻璃完好，也就是用户实际注视它的每一刻）；退场**先**转交自绘再开始淡出（否则文字会在一块不会淡的底板里淡）；隐藏中与淡入中由自绘持有；淡入结束后一个与淡入等长的定时器把它交回 shell。两个消费方（`Plasmoid.backgroundHints` 与 `LyricsView.ownsPlate`）读**同一个属性**，换手因此是一次绑定求值，不会出现两者相差一帧导致底板叠加或消失。「动画实际会不会播、播多久」也一并收敛到 `main.qml` 的 `effectiveFadeMs` 一处——定时器必须与淡入等长，两处各自判断动画状态正是会把底板卡在错误一方的成因。自绘时用 `prefix: "blurred"`（即 shell 会选的同一套帧图元；实测 ChromeOS 上朴素帧中心像素 alpha 244、blurred 帧 152，margins 两者同为 24），所以换手不产生任何位移，**唯一可见的差异是透过底板的壁纸从清晰变模糊**——已与用户确认可接受。关闭自动隐藏（默认）时 shell 全程持有。**未被测试覆盖**：换手逻辑在 `main.qml`，而它是 `PlasmoidItem`，QML 测试套件无法实例化；该文件唯一的静态检查是 Qt6 的 qmllint（见 CLAUDE.md——`PATH` 上那个是 Qt5 的，对 Qt6 QML 空转）。**`Paused` 的实测后果**（同日）：Chrome + plasma-browser-integration 暂停与队列放完都停在 `Paused` 且 metadata 完整，所以在该播放器上自动隐藏几乎只有关掉整个 Chrome 才触发。已向用户确认后**维持 `Paused` 不算无歌曲**，不做逃生口。**顺带发现（不在本次范围）**：面板里「背景 - Plasma 主题」实际什么都不画，观感等同 `none`，此处仅记录、不顺手改 |
| 41 | **全局歌词进度调整**：新增独立开关（**默认关**——关闭时逐字节保持 per-track 行为不变），开启后所有歌曲共用一个全局偏移，而非每首歌各自一份。开关与偏移值全局一份、跨所有部件实例共享，符号沿用既有约定（正值 = 歌词延后）。**存储落点选 `LyricStore`（SQLite）而非后端 INI 或前端 kcfg**：INI 那份的契约是「保存后要重启 daemon」（决策 18），kcfg 那份是「每实例各自一份」（同样是决策 18），而这个值必须**立即**跨所有实例生效——只有 SQLite 已经具备这个能力：`lyricsource.cpp` 每 2 秒轮询它同步 per-track 共享偏移（决策 18 认定这是仓库里**唯一**的跨实例活体同步通路），本功能直接复用同一条通路，不另开一条。**快照 `lyric.offsetMs` 的语义因此固定为「per-track 原始值」**（见 §2.2）：daemon 不感知这个开关，照旧只读写 per-track 偏移；全局模式下前端本地忽略快照里的这个字段，改用轮询到的全局值——`LyricSource` 内部因此拆成 `m_trackOffsetMs`（快照/per-track 表来的原始值，随时保持更新）与 `m_offsetMs`（`advance()` 实际用的"生效值"，全局模式下等于全局值），关闭开关时 `m_trackOffsetMs` 一直没被覆盖，读数**原样弹回**，per-track 数据本身从未被删除或改写。**±10000 ms 截断是存储层的不变量、读写两侧都夹**：只在写路径夹会让绕过 setter 的写入（脏改库、未来的迁移脚本、bug）读出界外值，此时前端 SpinBox 的 `from`/`to` 已经用 `LyricStore::maximumGlobalOffsetMs()` 钉死了范围，两边必须说同一个数字，因此 QML 里**不重复写字面量** 10000，一律读这个静态方法。**配置页 `ConfigGlobal.qml` 不像其余四个 tab 那样用 kcfg 的 `cfg_` 自动绑定**（这个值不落 kcfg），而是走 KDE 官方为非 kcfg 页面准备的钩子：`AppletConfiguration.qml` 的 Apply/OK 路径调用当前页的 `saveConfig()`，并把页面的 `unsavedChanges` 属性接进 Apply 按钮的可用性判断（实测 Plasma 6.7.4 `AppletConfiguration.qml:51-56,104-105,160-161,197-199`）——`ConfigBackend.qml` 那个自制的「保存」按钮是因为它写的是 daemon 的 INI、天然独立于对话框生命周期，这次的值必须能被 Apply/OK 一并提交，不能照抄。**测试**：`LyricStore` 的全局键读写/截断/持久化覆盖见提交①；`LyricSource` 的 C++ 单测新增一个仅供测试使用的 store 路径注入口（不 Q_INVOKABLE、不 Q_PROPERTY，QML 侧不可见），覆盖开关开/关时 `offsetMs` 取值、菜单调整写向哪张表、2 秒轮询把另一实例的改动同步过来、关闭开关后 per-track 值原样恢复、全局模式下 `canAdjustOffset` 放宽为仅需 `serviceAvailable`。新配置页不进 `tst_appearance.qml`（同一 QML 测试套件无法安全接触真实 SQLite 路径，`ConfigBackend.qml` 已是先例）。**已接受的残留：保存失败的可见反馈只在 Apply 路径成立**——`ConfigGlobal.qml` 的 `saveConfig()` 接住 `GlobalConfig::save()` 的返回值、失败时置一条 `Kirigami.MessageType.Error` 的 `InlineMessage`，但官方 `unsavedChanges` + `saveConfig()` 钩子本身不含失败回传通道：`AppletConfiguration.qml` 的 OK 按钮（`:454-459`，回车键 `:480` 走同一路径）调用 `applyAction.trigger()` 后**无条件** `close()`；而 `applyAction`（`:462-468`）在 `:467` **无条件**把 `applyButton.enabled` 置 `false`，早于这次 `saveConfig()` 的返回值能被看到，于是 `closing()`（`:42-47`）那条"有未保存改动就不许关"的护栏必然放行——OK/回车路径下用户点下去、对话框已经在关闭，看不见那条错误提示。彻底堵住这条路径需要一条生命周期独立于配置对话框的通知通道（如系统通知），那要新增依赖、`.notifyrc`、一整套文案，而触发前提仅是磁盘满/权限损坏/库损坏这类低概率场景，判定超出本次范围、不做；Apply 路径的错误条保留（零成本、严格优于没有）。**决策 55 已替代本条关于前端轮询 SQLite 与快照偏移语义的实现方式；本条的存储选择、范围夹取与同步保存失败反馈仍有效。** |

### 质量与发布
| # | 决策 |
|---|---|
| 33 | 诊断四件套：**① journal**——daemon 是 systemd user service，消息处理器无条件安装并写 stderr，由 systemd 收进 journal（`journalctl --user -u plasma-lyricsd -f`），输出形态按 stderr 的实际去向三分，见决策 64。「直通 journal、零实现成本」的原始设想不成立：Qt6 若链接 `libsystemd`（Arch 的 qt6-base 即如此），其默认消息处理器在非 tty 时走 `sd_journal_send()` 而非 stderr，且 journald 只按 syslog 优先级着色、不认应用自己打的级别文字，因此原生着色与去重都需要显式实现；**② 可配置日志文件**——`logging/fileEnabled` 默认 `false`，路径 `logging/filePath` 默认 `~/.local/share/plasma-lyrics/plasma-lyricsd.log`，开启后与 journal 并行写；**③ `plasma-lyricsd --explain "<标题>" "<歌手>"`**——离线打印匹配全过程，调匹配逻辑时的主力工具；**④ 分类日志与调试开关**——六个 `QLoggingCategory`（`plasmalyrics.daemon`/`.resolver`/`.mpris`/`.provider.netease`/`.provider.amll`/`.provider.local`），默认 info；`logging/debug` 打开对应 debug 级输出；三层过滤规则叠加、优先级依次升高——kdebugsettings 写入的 `qtlogging.ini` < 该开关调用的 `QLoggingCategory::setFilterRules` < `QT_LOGGING_RULES` 环境变量，因此开关开启时在 kdebugsettings 里单独关闭某个分类不生效（仅指 debug 开关；kdebugsettings 设的 info/warning 阈值仍生效）。`daemon/plasma-lyricsd.categories` 随 `BUILD_PLASMOID=ON` 安装，六个分类因此在 kdebugsettings 中可见。**不做设置里的日志面板**（实现要两天、半年用两次，`--explain` 覆盖同一需求且离线可重复）|
| 34 | 测试必须项见第 6 节 |
| 35 | i18n 第一天就做；AUR 为唯一正式分发渠道；**不上 KDE Store** |

---

## 4. 缓存 schema

存 `~/.local/share/plasma-lyrics/lyrics.db`——**不是 `~/.cache`**，因为库内含 `offset` 这类
不可再生数据（waylyrics 把 per-track offset 放在 `~/.cache` 里，有被系统清理的隐患）。

```sql
-- 歌词正文（可再生）
CREATE TABLE lyric (
  provider    TEXT    NOT NULL,          -- 'netease' | 'amll' | 'waylyrics'
  track_id    TEXT    NOT NULL,          -- provider 内稳定引用
  fetched_at  INTEGER NOT NULL,
  origin      TEXT,                      -- 规范化后的行数组 JSON
  translation TEXT,
  has_words   INTEGER NOT NULL DEFAULT 0,
  metadata    TEXT,                      -- 来源、作者、内容版本等 JSON
  PRIMARY KEY (provider, track_id)
);

-- 指纹 → 当前实际采用的 TrackRef；保留旧表供兼容
CREATE TABLE fingerprint (
  fingerprint TEXT    PRIMARY KEY,
  provider    TEXT    NOT NULL,
  track_id    TEXT    NOT NULL,
  matched_at  INTEGER NOT NULL,
  score       REAL                       -- 当时的匹配得分，便于事后复盘
);

-- 同一指纹可保留每个 provider 各自的成功映射
CREATE TABLE provider_fingerprint (
  fingerprint TEXT NOT NULL,
  provider    TEXT NOT NULL,
  track_id    TEXT NOT NULL,
  matched_at  INTEGER NOT NULL,
  score       REAL,
  PRIMARY KEY (fingerprint, provider)
);

-- 用户明确指定的单曲首选；不存在即按全局顺序自动选择
CREATE TABLE track_preference (
  fingerprint TEXT PRIMARY KEY,
  provider     TEXT NOT NULL,
  updated_at   INTEGER NOT NULL
);

-- provider 级负缓存；网络 5 分钟，其他失败默认 7 天
CREATE TABLE provider_miss (
  fingerprint   TEXT NOT NULL,
  provider      TEXT NOT NULL,
  tried_at      INTEGER NOT NULL,
  reason        TEXT NOT NULL,
  cache_version TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (fingerprint, provider)
);

-- 旧全局负缓存仅为迁移兼容保留；新解析流程不读取它
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

**按 provider 的负缓存是必需的**，不是优化：不做它，每看一个 B 站视频都会向所有在线源
重复搜索；若继续使用旧全局 miss，又会让升级前的网易云失败错误抑制新加入的 AMLL。

**手工改歌词不改数据库**，走 `~/.local/share/plasma-lyrics/overrides/<provider>:<id>.lrc`，
由 `core/store/` 的覆盖读取器在 provider 结果之后读取。这样缓存保持"纯粹可再生"的语义，覆盖目录是"你的数据"，
备份时只需备份后者。

---

## 5. Provider 接口

```cpp
struct TrackQuery { QString title; QStringList artists; QString album; qint64 lengthMs;
                    QHash<QString, QStringList> platformIds; QString mediaSrc; };
struct Candidate  { QString trackId; QString title; QStringList artists;
                    QString album; qint64 lengthMs; QStringList alternateTitles;
                    QString contentId; QHash<QString, QStringList> platformIds;
                    QStringList authors; };
struct LyricDoc   { LyricLines lines; int offsetMs; bool hasWords; QJsonObject metadata; };

struct ProviderSearchResult { QList<Candidate> candidates; QString error; bool transportFailed;
                              QString cacheVersion; bool cacheableMiss; };
struct ProviderFetchResult  { std::optional<LyricDoc> document; QString error; bool transportFailed; };

class Provider {
public:
    using SearchCallback = std::function<void(ProviderSearchResult)>;
    using FetchCallback = std::function<void(ProviderFetchResult)>;
    virtual ~Provider() = default;
    virtual QString id() const = 0;                          // "netease"
    virtual bool    isConfigured() const = 0;                // 未配置则跳过
    virtual MatchPolicy matchPolicy() const;                 // AMLL 保留版本后缀
    virtual QString cacheVersion() const;                    // 负缓存失效键
    virtual void search(const TrackQuery &, SearchCallback) = 0;
    virtual void fetch(const QString &trackId, FetchCallback) = 0;
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
10. **播放器生命周期**（`tst_mprisplayer_lifecycle`）— 同样在私有总线上：让 `changed` 的处理链
    在嵌套事件循环里销毁 MprisPlayer，断言 `apply()` 与 `pollPosition()` 发出信号之后不再解引用
    `this`。`apply()` 那条必须用**只改 `PlaybackStatus`** 的 `PropertiesChanged`、且经真总线投递：
    换成 `Metadata` 变更会让 `metadataChanged || oldStatus != m_state.playbackStatus` 短路掉
    那次成员读，剩下的野指针解引用落在 libQt6Core 里，而 ASan 只插桩本项目自己的编译单元，
    测试会在坏代码上照样全绿
11. **AMLL 匹配与 TTML** — 版本正常/单边/冲突三层、别名后缀、平台 ID、历史修订去重；
    行/词时间、空白、行内与关联翻译、多语言优先级、背景声隔离、重叠与非法时间轴
12. **多 provider 编排** — 首选与回退、空/全制作信息歌词继续、按源冷却及版本失效、旧全局
    miss 不抑制新源、强制重搜、缓存先显示、换曲/换源/重复重搜的旧回调隔离
13. **控制与快照** — 私有会话总线验证指纹不符拒绝和偏好落库；快照与前端往返验证首选、
    实际来源、临时回退、翻译、词时间和来源元数据

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
