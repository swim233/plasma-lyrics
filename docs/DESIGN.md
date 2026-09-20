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
| 23 | provider 通过源码模块扩展；本地、网易云、AMLL TTML DB 与 QQ音乐固定编译，启停和排序由运行时配置控制，接口允许每 provider 带自己的配置块；不引入运行时动态插件 ABI。移除按源裁剪的 CMake option 及四组裁剪 CI 构建，保留全部功能测试；旧 `ENABLE_PROVIDER_*=OFF` 参数或缓存值使配置失败，迁移时需移除这些参数并清理缓存。QtNetwork 与 zlib 为固定构建依赖 |
| 42 | **来源平台判定**：在决策 15 的 URL 白名单之上引入「来源平台」概念（`netease` / `apple`），由 D-Bus service 通配（`*.sidra`、`*.cider*`）与 `xesam:url` 前缀（`music.163.com`、`music.apple.com`、`classical.music.apple.com`）共同推导，**service 优先**——Sidra 的电台/古典条目没有 `xesam:url`，只有 service name 能认出它。`*.cider*` 点锚定（而非 `*cider*`）是为了不误吃 `org.mpris.MediaPlayer2.decider` 这类服务名里恰好含 `cider` 子串但并非 Cider 的播放器，和旁边 `*.sidra` 的锚定方式一致；真实 Cider（`ciderapp/Cider` 的 `src/main/plugins/mpris.ts`）注册的服务名就是字面量 `org.mpris.MediaPlayer2.cider`，收紧后仍然命中。`isMusic` 消费同一个判定：平台非空则只看是否在设置里勾选；**平台为空（未知来源）时沿用改动前的 URL 白名单 + 元数据启发式逐字节不变**，绝不能让升级当天所有本地播放器一起变哑。设置只列默认勾选的两个平台（网易云音乐、Apple Music），不列 YouTube Music / QQ音乐 / Spotify——列了又不勾会造成静默回归。`filter/musicUrlPrefixes` 原键原义保留（只装自定义 URL 前缀），新增 `filter/platforms` 装勾选结果，两者互不影响、零迁移 |
| 43 | **`transNames` 参与打分**：`Candidate` 新增 `alternateTitles`（网易云 `transNames`，storefront 本地化译名），`scoreCandidate` 对每个别名重算 `textSimilarity` 并取与主标题的 `max` 作为 `score.title`——只加证据、不降标准，权重（0.5/0.2/0.1/0.2）与阈值（0.55/0.58）不变。**只读 `transNames`，不读 `alias`**：实测 `alias=["TV动画《我推的孩子》片头曲"]` 是番剧挂钩语，`transNames=["偶像"]` 才是「アイドル」的译名；混进 `alias` 会把不相关的宣传文案当证据。字段缺失或为 `null` 都当空处理（`QJsonValue::toArray()` 对两者返回同一个空数组，天然安全）。`ScoreBreakdown` 新增 `titleViaAlternate`，`explainMatch` 打印 `titleVia=alias`/`titleVia=title`——否则诊断日志只会看到分数无端变高，看不出是哪条候选证据起的作用。本地 provider 同样构造 `Candidate`，但不使用 `alternateTitles` |
| 44 | **`cleanArtists` 拆分「主名 (括号内容)」的并列写法**：网易云把部分艺人名存成复合串，字面量如 `BTS (防弹少年团)`——实测 `--explain 春日 BTS` 之前 `artistSimilarity` 把整串当一个 token 算子串比例（`3/9≈0.333`），远低于本该有的精确匹配。`cleanArtists` 新增一条正则，仅当整条艺人字符串**以括号收尾**（`(...)`/`（...）`）时才把结尾这组拆成独立的一条，前后两段各自再走原有的斜杠/顿号/`&`/`feat.` 分隔逻辑；条件收紧到"结尾"是为了不误吃合作艺人本身名字里带括号的巧合写法（如 `(Sandy) Alex G`，括号在开头不在结尾，不拆）。**影响面不限于「同一实体的另一种文字写法」**：ACG 角色曲在网易云上常见 `神楽ひかり(CV:三森すずこ)` 这类写法，括号内是**扮演该角色的声优**，和角色本身是两个不同实体，同样会被拆成两条——这里拆开依然无害，理由和整个改动的正当性是同一条、同样是经验性的（见下文）：MPRIS 侧报的是裸名字（角色名或艺人名），拆开只是让任一段都有机会单独匹配上，而会降分的那种形状要求**对侧**给出没有分隔的合并写法，CV 场景同样没有观测到这样的来源。**这个改动在数学上没有任何一个方向是无条件单调的**：它把一个合并 token **替换**成拆开的若干 token（不是纯追加），而 `cleanArtists` 被 query、candidate 两侧共用，`artistSimilarity` 的分母又取 query 侧的 token 数——被替换掉的那个合并 token 本身往往就是最佳匹配，所以哪一侧被替换、分数都可能下降。实测反例（qa-2）：query 侧固定为 `["BTS 防弹少年团"]`（本身不含括号，逐字节不受这次改动影响），candidate 侧从未拆分的 `["BTS 防弹少年团"]` 变成拆分后的 `["BTS", "防弹少年团"]`，`artists` 从精确匹配 `1.000` 掉到 `0.556`。**"只增不减"的变体也救不了**：若改成保留原始合并串、再追加拆出的两段（candidate 侧从 1 个 token 变 3 个），的确不会在 candidate 侧丢信息，但同一改动应用在 query 侧时会撑大 `artistSimilarity` 的分母，同样能把分数往下拉——两个方向都堵死，不存在无条件安全的写法。**这个改动的正当性是经验性的，不是数学证明**：真实数据的形状是 MPRIS 报裸名字（如 `BTS`）、网易云存复合串（如 `BTS (防弹少年团)`），这个方向上是严格改善，实测 `0.333 → 1.000`（`--explain 春日 BTS`）；会降分的那些形状都要求**对侧**给出"没有分隔的合并写法"（如 qa-2 反例里 query 侧那个 `"BTS 防弹少年团"`），而目前观测到的各 MPRIS 来源都不会这样报艺人字段。测试 `compoundCandidateNameNoLongerUndercountsArtistSimilarity` 只验证了这一个具体实例（query 固定为裸 `"BTS"`，candidate 拆分前后对比），不代表任何通用结论。**副作用范围已按来源逐一核实**：`MprisPolicy::fingerprint` 只在 `state.mediaSrc` 为空时才退化到 `cleanArtists(...).join('/')` 参与的四元组哈希（决策 11 的兜底分支），而 `kde:mediaSrc` 是 **pbi 专属**的 KDE 扩展属性——实测 pbi 报网易云网页版时 `kde:mediaSrc` 是那个 mp3 直链（存在），Sidra 完全没有这个键。因此：**网易云网页版（pbi）不受影响**，走的是 `mediaSrc:` 分支，四元组哈希根本不参与；受影响的是**没有 `kde:mediaSrc` 的来源**——Sidra/Cider 这些本次才开始支持的 Apple 客户端（升级前本来就没有历史缓存，代价为零），以及本地播放器如 mpv/VLC（这类会真的经历一次 fingerprint 变化）。`fingerprint` 表（`fingerprint → provider, track_id`）与 `lyric`/`offset` 表（主键都是 `(provider, track_id)`）是三张独立的表，旧 fingerprint 行只是被孤立、不会级联删除任何东西。**用户可感知的影响**：受影响曲目升级后第一次播放会触发一次真实网络搜索（与任意一次正常的元数据变化触发重新解析毫无区别），期间可能短暂显示"匹配中"或"未找到"，随后恢复正常；不丢失已缓存的歌词或偏移；一次性代价，不会重复出现 |
| 45 | **本地化标题兜底 + 别名艺人闸**：决策 43 的 `transNames` 只救得了汉字化译名（`偶像`→`アイドル`），救不了 Apple us storefront 的罗马字形态（`Gunjou`→`群青`）——网易云从不把日文标题罗马字化，这类候选的 `transNames` 是 `None`，标题证据彻底缺失。新增 `chooseMatch(ranked, allowLocalizedFallback)` 取代 `Resolver` 里裸的 `ranked.first()` + `isAcceptableMatch`：第一优先级仍是 `isAcceptableMatch(ranked.first())`；不满足时，`allowLocalizedFallback`（**仅 `MprisState::platform == "apple"` 时置真**——这条路完全绕开标题证据，是刻意降低的标准，必须关在已知成因的场景里）才允许走第二判据：候选池 `>= 3`（池子只有一两条时"唯一"是白送的，恰是证据最弱的情形）+ 判据（艺人相似度 `>= 0.9` 且时长差 `<= 250ms` 且两侧时长都 `> 0`，**`scoreCandidate` 在任一侧缺失时长时把 `durationDifferenceMs` 置 0，`ScoreBreakdown` 因此新增 `durationComparable`** 供这一步单独判断）+ 按 `(normalizeSearchText(cleanTitle(title)), cleanArtists(artists))` 去重后**恰好一组**唯一候选（网易云同一首歌挂多个 id 是常态，不去重会把正确匹配当"歧义"拒掉），取组内时长差最小的一条。**这道时长闸的窗口是 250ms，不是 `scoreCandidate` 自己打分公式里"Δ<=2000ms 算最佳档"的 2000ms**——两者刻意不同，`scoreCandidate` 那个 2000ms 是主路径打分的既有行为，一个字节都没动；这里的 250ms 是兜底路径单独收紧的唯一性判据，理由见下。最初按 `scoreCandidate` 的 2000ms 抄来复用时，qa-2 指出一个结构性漏洞：**真曲的时长恰好落在闸外（源站与网易云对同一首歌的收录时长有出入，或网易云收的是不同剪辑版），而同一艺人的另一首不相干歌曲恰好落在闸内、且只有它一个存活**——这种情形下 D-8 的四道闸（艺人、时长、池子、去重唯一）全部通过，`chooseMatch` 会自信地返回错误的歌；`genuineAmbiguityIsRejected` 测的是"两首都活下来→按 D-9 分组不唯一→拒"，结构上覆盖不到"只有错的那首活下来→分组天然唯一→放行"这一类。收紧窗口是唯一能堵住这类洞的办法（唯一性判据本身没有"排除已被时长闸筛掉的候选"这个概念）。**收紧到 250ms 的依据是两组实测**：① 已知真命中的时长差全部是 `0～1ms`（群青 Δ=0、アイドル Δ=1）——Apple 与网易云对同一发行版的时长几乎逐毫秒一致，250ms 对真命中没有任何压缩空间；② 抽样 YOASOBI/Ado/IU/BTS/米津玄師 各 30 条搜索结果、按曲名去重后共 135 首，测了不同窗口下"至少一个同艺人邻居落在窗口内"的曲目占比与"两首不同曲都落在窗口内"的碰撞对占比：`±2000ms` 同艺人邻居 60% / 碰撞对 3.91%；`±1000ms` 36% / 1.93%；`±250ms` 13% / 0.51%；`±100ms` 7% / 0.28%。250ms 把碰撞率从 3.91% 压到 0.51%（降 4.6 倍），且不挡任何已知真命中，符合用户"宁可 `notFoundText` 也不要错歌词"的取向；100ms 再压一档收益已经很薄，250ms 是两头都够用的折中，**同样不是一个被精确校准出来的最优值**，往后如果实测数据要求更紧可以继续收。**qa-1 独立复现过这张表，指出两处方法论偏差，如实记下**：①"有同艺人邻居"这一列对取样量敏感——把 `limit` 从 30 提到 60（135→263 首）后，250ms 档从 13% 涨到 23%（"至少一个邻居"这种统计量会随候选池变大单调升高，13% 不能当成真实用户遇到误顶替的绝对概率，真实索引里同一艺人的曲目远不止 30 条，这一列还会继续升高）；"碰撞对占比"这一列稳健，250ms 档 0.51%→0.51%、2000ms 档 3.91%→3.83%，**上面"降 4.6 倍"引用的正是这一列**。②测量脚本按 trim+小写去重，比代码里实际用的 `normalizeSearchText`（NFKC 归一化）松——实例：YOASOBI「たぶん」在搜索结果里出现两次、时长完全相同，一个「ぶ」是预组合字符（U+3076）、另一个是基字符+浊点组合（U+3075 U+3099），脚本当成两首歌记了一次虚假碰撞，真实代码会正确合并；按 NFKC 重跑后 135→134 首，各档数字变化 0.05～1.2 个百分点（250ms 档：13.33%→11.94%、0.51%→0.46%），方向是把风险算高了一点，结论不变。**顺带发现（残留风险，不在本次范围）**：收紧只是把这类误顶替的概率降低，没有消除——真曲的时长恰好落在 250ms 窗外、且恰好有另一首同艺人歌曲落在窗内时，`chooseMatch` 仍会返回错误的歌，现有测试结构上覆盖不到这一类（能覆盖的只是"两者都在窗内"的双候选歧义）。**别名艺人闸**堵住决策 43 打开的口子：`transNames` 会广泛传播到同一首歌的翻唱/remix 条目上（实测 `偶像 YOASOBI`/`吵死了 Ado`/`大概 YOASOBI` 三组皆如此），一条错艺人的翻唱靠继承来的 `transNames` 就能把 `score.title` 顶到 1.0、总分越过两道阈值（0.55/0.58）——`titleViaAlternate` 为真的候选因此额外要求 `score.artists >= 0.5` 才算可接受。**这道闸只挂在 `chooseMatch` 第一个返回点（主路径）**，不挂在兜底路径（survivors 那一步）：兜底路径的 survivors 已经要求 `artists >= 0.9`，严于闸的 `0.5` 阈值，`0.9 >= 0.5` 恒真，再判一次是够不成立的死代码（qa-1 指出）。挂在 `chooseMatch` 而不是 `isAcceptableMatch`，是因为 `Resolver` 只评估 `ranked.first()`，翻唱一旦排第一会在 `isAcceptableMatch` 失败之前就返回，`isAcceptableMatch` 内部判不到。0.5 而非本条兜底判据的 0.9：这里标题证据本身存在且强（网易云自己声明了译名），艺人只需排除"完全不相干的人"。**但 0.5 这个具体数字本身没有被校准过，如实说清楚**：实测覆盖日/韩/英文原曲共 12 组本地化标题查询（YOASOBI×4、Ado、IU×4、BTS、《冰雪奇缘》原声、《爱乐之城》原声，决策 44 落地后用裸名字重新跑过一遍结论不变），正确原曲的 `artists` 分全部聚在 `1.000`；已知的错误候选（`YunFuCola remix`）`artists=0.143`。`(0.143, 1.0)` 这一整段区间**没有任何观测点**，0.5 只是这段空档里取的中间值——这批数据能证明的只是"0.5 落在安全区间内"，证明不了"0.5 本身有什么特别之处"：换成 0.6/0.7/0.8/0.9，在现有数据下会得到一模一样的结论。校准数据的作用是划出空档的两端边界，不是选定 0.5 这个点；往后如果出现新的错误候选样本落进这段空档，才需要真的收紧这个数字。**顺带发现（不在本次范围）**：`isAcceptableMatch` 本身从不检查 `score.artists`，"标题字面相同、艺人完全不相干"的候选在 `transNames` 特性引入之前就能被接受（`--explain Gunjou YOASOBI` 会选中 `GUNJOU (Cover)/Omnixor`，靠的是主标题、与 `transNames` 无关；但那是 `--explain` 不传时长的最坏情形——真实 daemon 路径下该候选 `cleanTitle` 先剥掉 `(Cover)` 使 `title=1.000`（不是子串比例），`Δ=13709 ms`，时长分 `0.9-(13709-2000)/15000=0.119`，总分 `1.0×0.5+0+0+0.119×0.2=0.524`，仍低于 0.58 不会被选中，只有 `--explain` 的时长恒缺失最坏情形才会显形）。别名艺人闸只堵住了 `transNames` 打开的新路径，这条更早存在的旧路径仍然敞着，修它会改变一条已经在工作的路径的行为，应当单独立项，此处仅记录、不顺手改 |
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
| 65 | **译名括号靠查询侧变体处理，且只在精确命中时算数**：查询标题的尾部括号内**不含**版本标记词时，剥离括号后的主体作为**额外变体**参与打分，与候选侧（`candidate.title` + `alternateTitles`）取最大。判据是**反向**的——用现成的 `versionEvidence()` 词表 ∪ `cleanTitle` 正则的标记词（`伴奏\|纯音乐\|live\|版\|ver\.?\|cover\|remix`）取**并集**，两份任一认为是版本标记就不剥离。用并集而不是单独一份：`版`/`ver` 只在 `cleanTitle` 里有，`歌名 (2024版)`、`Song (Ver. 2)` 会被 `versionEvidence` 单独判成「无版本标记」而错剥，捅穿决策 54 的版本区分；守卫的正确方向是保守——宁可漏剥（退回 not-found）也不能错剥（选错版本）。**不得改 `versionEvidence()` 自身的词表**，那会连带改变 `VersionTier` 分类。**剥离变体只有相似度精确等于 1.0 时才参与取最大**，低于 1.0 一律不采纳：剥离后的主体更短，短串被长串包含的概率大得多，`textSimilarity` 的包含快路径按 `min/max` 给分，CJK 短标题很容易过 0.55（实测 `心跳 (跳动的心)` 对同歌手无关曲目 `心跳吧` 得 2/3=0.667 并被选中）。做成「不采纳」而不是「先抬分再靠闸门拒绝」：后者会改变排序，正是下面那条二阶效应的来源。**「括号内文本本身」不作为第三个变体**——实测 `Song (Intro)` 会和同专辑无关的 `Intro` 曲目在 `title=1.0` 打平，而 Intro/Interlude/Outro/Skit 正是专辑里最常见的短曲名。**时长可比较时一律以时长为准；只有在时长不可比较时，才看非剥离路径本身是否已经够格**（2026-09-12 经 B.3b、B.3c 两次订正后的最终形态，完整经过见决策 66 的同名两段，本条末尾的 B.3c 补记记有被推翻的原表述）：`ScoreBreakdown::titleViaGlossVariant` 标记「哪个变体胜出」，`passesGlossVariantGate` 把判定交给与另外两道闸门共享的 `strippedVariantDurationGateOutcome`——**先看 `durationComparable`**，为真则只按 `durationDifferenceMs <= 2000` 判（窗口值复用决策 12 的「2s 内判为最佳」，不新造常数），**为假才**轮到 `nonStrippedMatchAloneIsAcceptable`（决策 66）的逃生口：非剥离路径自身已跨过两道验收线就放行，否则拒绝。整体 AND 进 `isAcceptableMatch`（而非像 `passesAliasArtistGate` 那样要求调用方自己记得加）。起因是「同艺人 + 剥离后主体字面撞车」能把 `total=0.7` 的错歌送过两道阈值，而时长差 185 秒也拦不住（`duration` 早已跌到地板 0，权重只有 0.2）。**时长未知时不得无条件放行**：单凭「查不到时长」不能放行，否则缺时长的候选就是绕过闸门的通道；B.3c 之下的唯一出口是上面那道逃生口——非剥离路径**自身**已跨过两道验收线的候选才在时长未知时被接受，因为这类匹配根本不依赖剥离。原表述为「时长未知时拒绝：不放行」，B.3c 之后为假，见本条末尾补记。曾评估用 album 做替代佐证并**否决**——最常见的碰撞形态恰恰是同专辑的 Intro/Outro/Skit 短曲，它们与正确答案**共享专辑**，album 在此不但不能鉴别还会反向背书；候选侧 `alternateTitles` 只对填 `transNames` 的网络源有意义，救不了本地场景；曲目编号要扩 `Candidate` schema；语种/字形判断本条明令不用（只覆盖日→中，韩中/英中/纯汉字日文标题全漏）。时长未知时判别所需的信息客观上不存在，此时按决策 45 的取向选择 not-found。代价：AMLL 索引不提供时长（`parseIndex` 结构上就没有赋值处），该源上这条修复完全失效；本地 `.lrc` 目录搜索路径的 `lengthMs` 来自可选的 `[length:]` 标签，缺失时同样失效（sidecar 路径不受影响，它直接拷贝 `query.lengthMs`）。**B.3c 补记（决策 66）**：本条正文原有两处表述已被 B.3c 推翻并于同日改写——① 原「只有当没有任何非剥离变体本身已经跨过两道验收阈值时，靠剥离变体才匹配上的候选才需要独立证据背书」（B.3b 的优先级：逃生口在外、时长在内）；② 原加粗规则「时长未知时拒绝：不放行」。二者在 B.3c 之下均为假，正文现为 B.3c 的成文形态，此处仅存档。`durationComparable` 在任一侧未知时即为假，而 AMLL 与缺 `[length:]` 的本地 `.lrc` 恰好总是这个情况，因此这两处的剥离候选永远落在「不可比较」分支——门在构造上无法凭时长生效，只能被决策 66 的逃生口绕过或直接拒绝，等同把这两个来源永久钉在决策 65 之前（v0.3.1）的语义。因此拒绝原因分成 `gloss-duration-threshold`（时长已知但超窗口，不可操作）与 `gloss-duration-unknown`（任一侧缺时长，用户可补 `[length:]` 标签），两者对用户的含义与可操作性完全不同。`titleVia=` 相应扩为 `title`/`alias`/`gloss`/`alias+gloss`；平手时优先非剥离变体——闸门的语义是「这次匹配**依赖了**剥字符才成立」，存在等分的非剥离路径就说明它不依赖，闸门不该介入。**`chooseMatch` 的 Default 分支改为遍历，但只跳过被闸门取消资格的候选**：抬分会改变排序，而闸门是与「哪个变体获胜」绑定的硬拒绝，两者叠加后一个排第一、被闸门拒绝的候选会让整次匹配返回 none，把排第二的合格候选埋掉（该弱点本就存在于 `passesAliasArtistGate`，本条只是让它在一类常见输入上变得可达）。**跳过范围必须限于闸门，不能用 `isAcceptableMatch` 整体跳过**：排序键是 `total` 而 `title >= 0.55` 不是，首位候选可以总分最高却栽在标题阈值上，整体跳过会把「不给歌词」变成「给一首差 20 秒的错歌词」，作用于每一次 Default 匹配而不只是双语标题。阈值失败时用 `break` 而非 `return`——「主路径到此为止」不等于「整个 `chooseMatch` 到此为止」，本地化回退（决策 8）是独立判定，不得被连带截胡。**诊断必须跟得上**（决策 46）：`--explain` 此前写死 `lengthMs=0`，凡需剥离才能匹配的候选一律显示 `gloss-duration-unknown`，与守护进程实际行为系统性相反；新增 `--length-ms` 选项（具名而非第三个位置参数，因为 ARTIST 本就可选）同时修好这一类和本地化回退那一类（后者要求 `deltaMs <= 250`，同样从来无法在 `--explain` 中复现）。`clean title:` 行改为 `title variants:` 列出实际参与打分的全部查询侧变体。**性能**：`queryTitleVariants` 只依赖查询标题，必须在 `rankCandidates` 里算一次传入，不得放在 `scoreCandidate` 内每候选重算；`versionEvidence()` 的 16 个正则为 `static const`，否则每候选现场编译——用户真实 AMLL 索引（约三千条）实测 `PreserveVersions` 路径 1.4 秒降至 0.18 秒，该开销在本条之前就存在。**决策 66 补记**：`normalizeSearchText` 自己的 `QRegularExpression` 当时漏做了同样的处理，每次调用都现场构造——它是候选标题、每个艺人、专辑三处共用的高频函数，main 上已是每候选约 4 次调用，决策 66 的剥离变体又让调用数翻倍；qa-b-2 在用户真实索引上实测约 +75%、改为 `static const` 后反而比 main 快约 70%（按单次查找计：main 约 230ms，本分支加剧后约 401ms，修复后约 67ms；早期记录曾把 3 次查找的总耗时 691ms/200ms 误当成单次数字，此处更正）。dev 在合成的约 3275 条数据集与本机上独立复测，方向与量级一致但数字不同（约 207ms→约 150ms），差异来自数据集构成与硬件，不是同一份测量。 |
| 66 | **艺人拼进标题靠候选相关的第三个标题变体处理，门槛复用决策 65**：播放源偶尔把艺人名拼进标题字段（实测 `--explain`：查询 `title="浴火者 被遗忘者的哀伤" artists=["北山薇"]`，候选 `title="浴火者" artists=["被遗忘者的哀伤","北山薇"]`，剥离前 `title=0.273` 被 title-threshold 拒绝；网易云真实候选 `2733786721` 复现同一形态，`--length-ms` 传入其真实时长 207369ms 后 `deltaMs=0` 被选中，验证见提交记录）。**与决策 65 的两个变体不同，这第三个变体是候选相关的**：查询标题的尾部连续 token 只有在能与某个候选的艺人字段完整匹配（`normalizeSearchText` 后相等，不接受包含/部分匹配，否则「浴火」会误配「浴火者乐队」）时才被剥离，因此不能像前两个变体那样在 `queryTitleVariants()` 里一次算好塞进 `rankCandidates` 的共享列表，只能放进 `scoreCandidateWithVariants` 内部按候选现算——`title variants:` 那一行因此**不会**列出这个变体，`--explain` 只能在每个候选自己的 `titleVia=artist-strip` 里看到它，这是该诊断行注释里显式记下的例外。**分隔符是本条自己的一套**：空白、`/`、`-`（`titleStripTokens`），故意不是 `cleanArtists` 拆分艺人字段用的那一套（后者认 `、`/`;`/`&`/`feat.`/`ft.` 但不认 `-`）——两套分隔符服务不同的字符串（标题 vs 艺人字段），没有理由绑在一起。**只剥尾部、不挖中间**，且返回每一种能让尾部整体匹配上某个艺人的切法（不只是最长的那种）：查询 `A B C` 对候选标题 `A B`、艺人 `["C","B C"]`，只试最长尾缀 `B C` 会剥出 `A`（对不上候选标题）而漏掉真正对的切法（尾缀 `C`，剥出 `A B`）——因为**剥离变体只在与候选标题精确相等（`textSimilarity=1.0`）时才参与取最大**（原样沿用决策 65 对包含快路径的顾虑：剥离后的主体更短，短串被长串包含的概率更大），多算的错误切法只是被判为不精确而自然出局，不需要额外过滤。**平手时优先非剥离路径**的判据从决策 65 的「非 gloss」推广为「非任一剥离变体（gloss 或 artist-strip）」：候选的主标题只靠 artist-strip 打平、但某条 `alternateTitles` 与查询原标题精确相等时，后者才是不依赖剥离就成立的证据，应当胜出——查询标题本身已等于候选标题时同样不触发这条剥离（此时剥离变体只是候选*完整*标题的真子集，连 `textSimilarity` 的包含快路径都过不了 1.0，不需要与「相等」特判）。**独立证据背书复用决策 65 原封不动的常量与窗口**（原 `kGlossVariantDurationWindowMs`，因两个闸门共享同一个数字和同一条理由——两者都是「这次匹配只因替入了候选相关变体才成立」——而改名为 `kStrippedVariantDurationWindowMs`），新增 `ScoreBreakdown::titleViaArtistStrip` 与 `passesArtistStripGate`，AND 进 `isAcceptableMatch`；`chooseMatch` 的 Default 分支的「只跳过闸门、不跳过阈值」范围相应扩大到这个新闸门。拒绝原因按决策 65 的先例分裂为 `artist-strip-duration-threshold`（时长已知但超窗口）与 `artist-strip-duration-unknown`（任一侧缺时长），`--explain` 的时长未知提示同时覆盖这些 `-unknown` 原因（决策 70 加入候选侧闸门后共三个，代码覆盖三个：`matcher.cpp:1215-1217`、`:1229-1232`）。`titleVia=` 对应扩出 `artist-strip`/`alias+artist-strip` 两档。 **B.3b 返工（2026-09-12，qa-b-1 发现，同一提交内一并修复）**：两道闸门最初的写法是「哪个变体胜出决定要不要查时长」，但契约其实是「靠剥离才匹配上才要查时长」，两者在「存在一个分数更低但自身已经够格的非剥离变体」时不等价——qa-b-1 双分支探针实测：查询 `title="Bohemian Rhapsody Queen"`（或等价的 `"Bohemian Rhapsody (Queen)"`）、候选 `title="Bohemian Rhapsody" artists=["Queen"]`，双侧时长未知，非剥离变体本身已经 `title=17/23≈0.739`（`textSimilarity` 包含快路径）、`total≈0.6696`，早已跨过两道验收线，main 上一直被接受；剥离变体（艺人剥离或译名括号，此处等价）得分更高（`1.0`）反而把整个候选拖进时长门，因未知被拒——**这是 v0.3.2 随决策 65 一起引入、由本条修复的既有回归**，不是本条新引入的问题，修复因两道闸门形状完全相同而一并做。新增 `ScoreBreakdown::titleWithoutStrip`：considerTitle 内部并行跟踪「只用非剥离变体能打到的最高分」（与实际胜出分数并存，不覆盖），`nonStrippedMatchAloneIsAcceptable(score)` 判定它是否独立跨过 `kTitleAcceptanceThreshold`（0.55）与 `kTotalAcceptanceThreshold`（0.58，均从 `isAcceptableMatch` 抽出命名，避免第三处硬编码 0.55/0.58）；两道闸门都在其对应的 `titleViaXxx` 为真时先查这个函数，为真则直接放行，不再要求时长佐证。**闸门收紧的范围，不是闸门本身**：一旦真去查时长（非剥离路径本身也不够格），判据与窗口逐字节不变，`glossVariantDurationGateRejectsATooShortSameArtistTrack`/`artistAppendedToTitleRejectedWhenDurationUnknown` 两个既有强拒绝原样保留。**「非剥离变体够格」不能只看分数，还要看是否靠 `alternateTitles` 撑起来**：`titleWithoutStrip` 单独达标不足以放行——若这个够格分数是通过某条 `alternateTitles` 打平的（新增 `ScoreBreakdown::titleWithoutStripViaAlternate` 并行跟踪，与 `titleViaAlternate` 分开记，因为实际胜出的变体可能是候选*主标题*的剥离命中，与够格的非剥离路径不是同一条），且该候选 `artists < kAliasArtistThreshold`（从 `passesAliasArtistGate` 抽出命名的 0.5），闸门仍必须要求时长：否则一个艺人完全不对的翻唱可以靠主标题剥离把 `titleViaAlternate` 从真变假，从而绕开本应拦住它的 D-11 别名闸门——探针 `"Song Artist Name"`/`["Nobody"]` 对候选 `"Song"`/`artists=["Artist Name"]`、`alternateTitles=["Song Artist Nam"]`（缺尾字母的近似完整匹配，`15/16=0.9375` 经包含快路径够格）复现，剥离变体对主标题得 `1.0` 胜出、把 `titleViaAlternate` 翻成假，若无此检查会被误判为「非剥离路径已经够格」而放行，验证见 `artistStripEscapeHatchDoesNotResurrectAWrongArtistAlternateTitleCover`/`glossEscapeHatchDoesNotResurrectAWrongArtistAlternateTitleCover` 两个 `chooseMatch` 级用例。`score.total` 与 `nonStrippedMatchAloneIsAcceptable` 的四个权重（0.5/0.2/0.1/0.2）同时抽成 `kTitleWeight`/`kArtistsWeight`/`kAlbumWeight`/`kDurationWeight` 共用，避免两处各自硬编码同一组数字。新增回归用例覆盖「已够格不进时长门」与「不够格仍进时长门」各两例（gloss、artist-strip 各一），外加上述别名漏洞两例。**诊断可读性**（决策 46，lead 裁定并入本条而非另立）：`titleVia=gloss`/`artist-strip` 一行此前只有 `duration=`/`deltaMs=`，没有任何字段说明时长门到底有没有被触发——一个 `titleVia=artist-strip duration=0.500` 且无 `rejected=` 的候选行，读者无法判断它是本来就够格还是恰好躲过了未知时长的拒绝。`--explain` 的候选行因此新增 `plainTitle=`（即 `titleWithoutStrip`）与 `durationGate=`（B.3c 收窄后为四态 `within-window`/`outside-window`/`bypassed`/`required`，直接调用与两道闸门共用的 `strippedVariantDurationGateOutcome`，不是另写一套推导，不会与真实判定漂移；早期版本曾直接打印 `nonStrippedMatchAloneIsAcceptable` 本身，qa-b-2 指出 B.3c 收窄后那已经不是决定闸门是否生效的条件，会在时长已知且在窗口内的候选上打印 `required`、在时长已知且超窗口的候选上打印 `bypassed`，与实际判定相反），仅在 `titleVia` 为 gloss/artist-strip（含 `alias+` 组合）时出现。**B.3c 收窄（2026-09-12，qa-b-2 发现，更正 lead 自己的 B.3b 裁定）**：B.3b 的逃生口把「佐证不可得」与「佐证可得且为负」当成同一回事，撤掉了一道真实保护。qa-b-2 用跨版本差分回放（v0.3.1／main／B.3b 分支，replay 用户真实日志 82 次解析、470 候选）实测：查询 `ARC Raiders (II)`（170567ms）对两个 `ARC Raiders` 候选（分别 `+27911ms`／`+26754ms`，用户事后确认是纯乐器版、没有歌词），main 以 `gloss-duration-threshold` 拒绝，B.3b 分支接受——本次选择未受影响（正确曲目排第一），但候选池缺它时（网易云 `limit=10` 的常规情形）会从 `none` 变成选中一首长 27.9 秒的录音版本，正是决策 45 的反目标。v0.3.1（决策 65 之前）同样接受这两个候选，证明 qa-b-1 报的「既有回归」与决策 65 原始保护是同一机制，只是在这个输入上该机制原本是对的。**更正后的判据**（两道闸门共用，见 `strippedVariantDurationGateOutcome`）：`durationComparable` 为真时只看 `durationDifferenceMs <= 窗口`，与非剥离路径是否够格无关；`durationComparable` 为假时才轮到 `nonStrippedMatchAloneIsAcceptable` 的逃生口。已用真实历史 153 blocks／692 候选验证与 main 逐字节一致（B.3b 分支上的 3 处接受性翻转全部恢复），Bohemian 两种形状仍接受、`浴火者` 仍接受、`Intro` 反例仍拒。**在 AMLL 与缺 `[length:]` 的本地 `.lrc` 上，这条收窄就是整道门**（`durationComparable` 在这两处恒为假，见决策 65 的代价说明——门在构造上永远走逃生口这一支，不可能凭时长本身生效或拒绝）。新增回归用例固定 ARC Raiders 这一形状，gloss、artist-strip 各一例，均验证非剥离变体单独已经够格（若沿用 B.3b 逃生口会被误放行）而时长已知超窗口时仍需拒绝。**不做**：不碰 `cleanTitle`/`normalizeSearchText`/`splitTrailingGloss` 的既有行为；不引入编辑距离或 LCS 之外的模糊匹配（本条只认 token 级完整相等）；不动 AMLL 的 `MatchPolicy::PreserveVersions` 分层 |
| 67 | **Q24：`players/blacklist` 与 `filter/musicUrlPrefixes` 用伴生布尔键表示「显式设为空」，不靠值本身区分**。**当前规则（下面的版本记录是它没有半途而废的原因，不是必读前提）**：`players/blacklist` 迁成「未设置」——个案证据；`filter/musicUrlPrefixes` 也迁成「未设置」——但这个选择不可观测，只是跟着分组；`filter/platforms` 迁成「明确的空」——依据是起源，不是严重程度。起因（详见 `SOURCE_FIXES_PLAN.md` D4/Q24 与 2026-09-12 `preflight` 对真实 journal 的核实）：`QSettings` 的 INI 后端把空 `QStringList` 写盘序列化成字面量 `@Invalid()`（对着真实 Qt 6.11.2 实测，不是猜测），读回时 `contains(key)` 为真但 `value(key)` 是**无效** `QVariant`——「用户存了空列表」与「值损坏/从未真正写入」在 INI 里是同一个字面量，无法靠值区分，`contains()` 也帮不上忙（它对 `@Invalid()`同样返回真）。默认值 `org.mpris.MediaPlayer2.kdeconnect.*` 因此在这条件下被静默清空，kdeconnect 转发的手机播放绕过了黑名单。修法是 `core/config/stringlistsetting.{h,cpp}` 的 `StringListSetting{key, explicitEmptyKey}`：`explicitEmptyKey`（`players/blacklistEmpty` / `filter/musicUrlPrefixesEmpty`）为真时读作显式空，否则 `key` 存在且有效时用该值，都不满足时退回内置默认——**优先级是「`key` 有效值 > marker > 默认」**，不是 marker 优先：人手改了 `key` 却忘了删旧 marker 时，尊重刚打的字面值。**读取时会丢弃空字符串／纯空白条目**（2026-09-12，qa-a-2 发现）：手写裸 `key=` 解析出的是「一个有效的空字符串元素」而非 `@Invalid()`，会走「`key` 有效值」分支而不是 marker 分支；`mprispolicy.cpp` 对 `musicUrlPrefixes` 用 `startsWith(prefix)` 判断，`startsWith("")` 对任何字符串都为真，于是不丢弃的话，裸 `key=` 的实际效果不是「没有自定义前缀」而是「把每一个带 URL 的播放器无条件判定为音乐」——与用户想清空列表的原意正相反。**表达「空」仍然只认 marker**——`key=` 不是本模块会写出的形式，`writeStringListOrEmpty` 永远只用 marker 表达空；丢弃空白条目只是让这个手写状态读起来安全，下次经本模块保存时会被规范化成 marker 形式。`players/blacklist` 上此前就是良性的（`wildcardToRegularExpression("")` 有锚定，只匹配空字符串本身），这条改动对它不可观察；`filter/musicUrlPrefixes` 上是修正。写入侧（`writeStringListOrEmpty`）对空值一律 `remove(key)` 改设 marker，绝不再写 `QStringList()`，因此往返稳定且人类可读可编辑（marker 就是一行 `xxxEmpty=true`）。**标记比键本身更黏**（2026-09-12 记录，A.3.1 的可手编要求下这一点必须写明）：`xxxEmpty=true` 一旦写下，之后哪怕手工删掉 `key` 本身，读取仍然是「显式设为空」而不是回落到「未设置→内置默认」——读取优先级里 marker 分支就在默认值之前，删 `key` 并不触碰它。想手工恢复默认，删的必须是 `xxxEmpty` 这一行，而不是 `key` 那一行；只删 `key` 是自洽但常见的误操作。迁移（`migrateLegacyInvalidEntry`；`BackendConfig::load()` 里也调用一次以防设置对话框先于 daemon 重启打开）只处理**字面上仍无效**的残留 `key`——一次性挪走它、不打 marker，让内置默认恢复生效；不动任何有效值或已缺失的键，天然幂等。**`Config::migrateLegacySettings()` 在 `main.cpp` 里的调用点不是「构造 `Config` 后立即调用」**（2026-09-12，qa-a-2 两轮发现后确定）：它紧贴 `config.policy()` 唯一的调用点——`MprisManager manager(config.policy())`——放在 `QLockFile` 抢锁成功之后、`qInstallMessageHandler(mirrorMessage)`（决策 64 的 stderr/journald 分流）安装之后。两个前置条件缺一不可：① `--explain` 的全部出口都在锁之前 `return`，且它从不读 `config.policy()`，若迁移仍在锁之前，一个只读诊断命令会写盘且绕开单实例锁，与正在运行的守护进程竞争；② 这也让 `policy()` 是全二进制唯一的迁移消费者这件事对读者是自明的，不必单靠注释交代顺序。**不是理由**：迁移内部（`migrateLegacyInvalidEntry`/`migrateLegacyInvalidEntryToExplicitEmpty`/`migrateLegacySettings()`）从不调用 `qCWarning` 或任何日志函数，配置目录只读导致迁移失败时同样静默无输出（qa-a-2 实测），因此消息处理器的装配顺序与本条放置位置无关，此前的版本把这一条当作第三个理由是错的。**并发迁移的竞态因此从结构上不可能发生**：只有抢到 `QLockFile` 的那个实例才会走到这一行，另一个实例在锁检查处就已经 `return`，根本不存在第二个写者；**这一条只覆盖两个 daemon 实例之间**——`BackendConfig::load()`（`frontend/qmlmodule/backendconfig.cpp`）独立调用同一对迁移函数，不持有任何锁（该文件与 `frontend/` 全目录都不引用 `QLockFile`），daemon-vs-设置对话框、设置对话框-vs-设置对话框仍是 SPEC.md §A.3d 那条「已知未压测但可自愈」的情形，未受这次改动影响。`providers/order`／`providers/enabled` 的既有「空即回退内置默认」语义**不受影响、不加 marker**（对这两个键「空」从来不是合法意图），但写入侧同样不能再产生 `@Invalid()` 字面量，且两者读取逻辑对「键缺失」的处理并不相同：`providers/order` 缺失与存在但空回退到同一个内置默认（`writeStringListNoInvalid` 用 `RemoveKey`——直接删键，等价于「缺失」）——**代价是日志里的一条区分消失**（2026-09-12 记录）：旧代码对 `providers/order=@Invalid()`会打 `qCWarning`「providers/order is empty; using the built-in provider order」，键被删掉后 `contains()` 为假，这条 warning 不再触发；返回值经实测两种写法逐字节相同，行为不受影响。**接受这一点**：journal 里「用户主动清空过 order」与「从未配置过」从此不可区分，但这不是用户能据此采取行动的信息——补一条迁移期日志能找回这一行，却分辨不出任何用户能用上的东西，不值得为此再加日志。**另一方面**，`providers/enabled` 缺失时的语义是「`providers/order` 全部启用」，与「存在但耗尽后回退内置默认」是两个不同分支，删键会误入前者，因此改用 `KeepPresentAsBlank`——写入单个空字符串元素（`enabled=`，纯文本非 `@Invalid()`），保持 `contains()` 为真，且现有读取逻辑本就会过滤掉空白条目，观测结果不变。`filter/platforms`（两个平台复选框都取消时同样会写出 `@Invalid()`）**2026-09-12 由 lead 追加纳入范围（SPEC.md §A.3a）**：同一类 `@Invalid()` 泄漏症状，三个同类键只修两个是日后必然重新踩的不一致，因此用完全相同的伴生 marker 机制处理（`filter/platformsEmpty`），验收同样是三态。**迁移语义按键区分**：`migrateLegacyInvalidEntry`「把残留 `@Invalid()` 当未设置」这条判断并非对三个键都成立，`filter/platforms` 走的是另一个函数——`migrateLegacyInvalidEntryToExplicitEmpty`（移除 `key` 的同时把 `explicitEmptyKey` 置 true），另外两键不变。这条区分**在同一任务里被四轮用有缺陷的理由论证，四轮都被实测或复审推翻，结论最终都保住了（尽管第四版一度让 `filter/platforms` 的结论也悬而未决，见下）**，记录如下以防第六次：<br>1. 第一版理由：「自由文本框未预填默认值，所以清空必是意外；复选框没有这一步」。**已推翻**——`ConfigBackend.qml:251-262` 对 `backend.serviceBlacklist`/`musicUrlPrefixes` 是无条件绑定，`load()` 在键缺失时确实回填内置默认（`blacklistUnsetUsesBuiltInDefault()` 直接验证），文本框同样会被预填。<br>2. 第二版理由：机制分两段——①**纯净→首次损坏**：字段/复选框显示预填的默认状态，必须主动清空/取消勾选才会写出空值，这一步两种界面形态相同；②**已损坏→持续损坏**：`@Invalid()` 一旦存在，`value(key, default)` 对现有键返回空而非默认（`contains()` 为真时默认参数不生效，见本条开头实测），读回**空白/全不勾选**，此后保存任何无关设置都会静默重写 `@Invalid()`——这是棘轮，不是快照，因此不能假定存储值代表用户意图。**这一版仍然错**：qa-a-1 直接探针实测，`filter/platforms=@Invalid()` 经旧的 `.value(key, default)` 读回同样是空列表，两个复选框同样会读回未勾选，同样对任何无关保存静默重写——**棘轮对三个键完全相同，不是区分依据**，第②段本身正确但不能用来区分三个键。<br>3. **第三版理由：区分依据是个案证据，不是界面形态或棘轮结构**：`players/blacklist`／`filter/musicUrlPrefixes` 有**针对这一次损坏**的具体证据——`preflight` 对真实 journal 的抓取，以及用户在访谈中明确要求恢复 kdeconnect 过滤；这份证据本身才是「迁成未设置」的依据，不是任何结构性质。`filter/platforms` 不存在这样的证据，在没有证据时保留观测到的状态（迁成「明确的空」）比替用户猜测并抹掉它更稳妥。可恢复性的方向不对称——猜错「未设置」，用户再清空一次，且因标记机制此后持久化；猜错「明确的空」，用户手上留着的正是他要求修掉的缺陷，界面不会告诉他原因——仍作为次要支撑成立，但不是主依据。**这一版同样不完整**（qa-a-2 复审发现）：「有针对性证据」这一判断只对 `players/blacklist` 成立——`preflight` 抓到的真实 journal 与用户明确提出的恢复请求都只指向这一个键；`filter/musicUrlPrefixes` 从未在 A.1、本条或 `SOURCE_FIXES_PLAN.md:280` 里获得过针对这次损坏的具体证据，只是被 `SOURCE_FIXES_PLAN.md:280` 的「这两个字段『空』都不是合法意图」捆绑在一起——而那正是第 1、2 版里已经被推翻两次的「界面形态」推理的残留，不是这一版自己重新导出的结论。<br>4. **第四版理由：两层规则，不是单一依据**：①**第一层——个案证据，只适用于 `players/blacklist`**：`preflight` 对真实 journal 的抓取与用户在访谈中明确要求恢复 kdeconnect 过滤，这份针对性证据仅覆盖这一个键，必须独立存在，不能被「猜错代价」类推理取代——单看代价，`players/blacklist` 的方向并不像 `filter/musicUrlPrefixes` 那样一边倒：迁成「未设置」如果猜错，代价不是「什么都没发生」，而是拿走一项此前意外可用的能力——kdeconnect 转发的手机内容此前能被当作候选去网易云搜索，恢复默认黑名单会让这条路径彻底消失，是「移除功能」而不是单纯「恢复默认」；正因为这个方向本身有真实代价，才需要靠这份独立、针对性的证据来定夺，代价推理单独顶不上。②**第二层——猜错的严重程度，`filter/musicUrlPrefixes` 没有针对性证据时改用这条**：`MprisPolicy::musicRejectReason`（`mprispolicy.cpp:105-126`）里，`musicUrlPrefixes` 为空时前缀循环一次都不会命中，`plasma-browser-integration` 的每一条带 URL 播放全部落到 `return "browser-non-music"`——包括本该被前缀匹配放行的网易云网页版本身——浏览器歌词因此对所有网站静默失效，且没有 platform 检查兜底（浏览器不是已知 platform，见本条前段的 `platformFor`）。迁成「未设置」的代价只是恢复两条内置的 `music.163.com` 前缀；猜错了，用户能看见网易云网页版歌词重新出现、手动清空一次即可，此后因标记机制而持久化。这条论证比「猜错可恢复」更强，因为它可用 `MprisPolicy::musicRejectReason` 直接复现验证，不是泛泛的成本比较。③**`filter/platforms` 维持「迁成明确的空」不变**：两个方向猜错的后果在打开设置对话框那一刻就能从复选框状态直接看见，且「明确的空」只让带 platform 标记的曲目落回 `metadataHeuristic`（仍然照常运行），不是「整类功能死掉」——两个方向都不严重，不需要引入第一层或第二层的论证。**重新讨论 `filter/platforms` 的迁移目标，门槛是一份带明确来源的 `platforms=@Invalid()` 实测记录，不是一条新论证**——这条区分本身已经在这个任务里被讨论了四轮。**本轮差点第五次论证出错的记录**：lead 一度倾向把 `platforms` 也改成「未设置」，理由是「`@Invalid()` 已知是缺陷产物」——**这是错的**，第 2 版理由①段（纯净→首次损坏）已经把这条路堵死：`platforms=@Invalid()` 唯一的产生方式是用户主动取消勾选两个平台，棘轮解释的是它此后如何自我维持，不解释它最初从哪来；把「此后自我维持」误读成「起源也是缺陷产物」，是这条区分第五次可能被重新论证的最可能形式，记于此以备下次核对。**②③两段与「重开门槛」都需要更正**（2026-09-12，qa-a-2 探针实测 + lead 复核确认）：②把「猜错的严重程度」这张牌打给了 `filter/musicUrlPrefixes`，但 `platformRules()`（`mprispolicy.cpp:24-30`）里 `netease` 的 `urlPrefixes` 与 `musicUrlPrefixesSetting()` 的内置默认逐字节相同，而 `musicRejectReason`（`mprispolicy.cpp:105-126`）先查 `platformFor`（`:110`）再查前缀循环（`:118`）——命中这两条内置前缀的 URL 早在 `platformFor` 就被判定成 `netease` 并返回，前缀循环对内置默认值根本不可达；qa-a-2 对网易云网页版、Apple、bilibili、bandcamp 四种场景实测「未设置」与「明确的空」结果逐字节相同。**这个键没有猜错代价可比较，因为两个迁移目标观测不到区别**（裸 `key=` 让 `startsWith("")` 命中一切的发现仍然成立，那是另一件独立的事，不受这条更正影响）。③反过来把「两个方向都不严重」派给了 `filter/platforms`，但 `mprispolicy.cpp:115` 的 `platform-disabled` 是**无条件**返回，`:112-114` 的注释「回落到 heuristic」只适用于**未列入**的平台，不适用于**已知但被取消勾选**的平台——`enabledPlatforms=[]` 时（其余条件满足、`useMetadataHeuristic` 开启，探针实测确认）netease/apple 的全部曲目直接判 `platform-disabled`，不会落到 heuristic。**真实的严重程度排序是反过来的**：`filter/platforms` 为空是重的一侧，`filter/musicUrlPrefixes` 因为②的更正，严重程度根本无从谈起。**这一版的错误比第 1-3 版更危险**：若真按③错误的严重度论证走下去「修正」结论，会把 `filter/platforms` 判给「未设置」——这正是本轮 lead 一度倾向的方向，是错的，见第 5 版。<br>5. **第五版（当前）理由：不靠严重程度，各自独立成立**：`players/blacklist` 迁成「未设置」——个案证据不变，仍是第 4 版①段的理由，仍必须独立于任何代价/严重程度论证。`filter/musicUrlPrefixes` 迁成「未设置」——**这个选择不可观测**：与上文更正一致，`platformFor` 抢先判定令内置默认前缀在实测中不可达，两个迁移目标结果相同；它被划进「未设置」只是因为 Q24 最初把它和 `players/blacklist` 分在一组，不是这里给出的独立论证。`filter/platforms` 迁成「明确的空」——**依据是起源，不是严重程度**：A.3c①已确立 `platforms=@Invalid()` 唯一的产生方式是用户主动取消勾选两个平台，是真实用户状态，不是缺陷产物；**严重程度在这里不是论据，且方向是反的**——netease/apple 一旦被禁用是无条件拒绝，比 `musicUrlPrefixes`（无代价可比）严重得多，任何想用严重程度重新论证这条区分的尝试都用错了判据。**这条依据有失效边界，且边界只在一个方向**：**新增**一个 platform id（比如追加 `qq`）不影响这条论证——新 id 从未被任何用户配置过，不可能凭空出现在已存的配置里。**改名或移除一个已存 id** 才会失效：`platformRules()` 里的 id 一旦被改名，或已存的 id 不再匹配，两个复选框会读回「都未勾选」而用户从未碰过它们，下一次任何无关设置的保存都会重写出 `@Invalid()`——此时 A.3c①不再成立，需要重新评估。**目前不适用**：`netease`/`apple` 这两个 id 在 `45dfba2` 一次性引入后从未改过名。**重开这条区分的门槛是新的测量，不是新的论证——但有一条例外**：论证不能单靠自己推翻已经实测确认过的判断——本轮②③段就是反例，它们是被一次新测量（qa-a-2 的四场景探针）推翻的，不是被又一条论证推翻；一份带明确来源的新 `platforms=@Invalid()` 实测记录，或者上面失效边界真的发生，都足以重开，不算破例。**例外**：指出「已写下的理由推不出已写下的结论」是记录一致性问题，不受这条门槛约束——第 3 版与第 5 版都是这一类，不是新测量：第 3 版指出「有个案证据」被同时错套在两个键上，第 5 版指出「严重程度」论证反而支持相反结论，两者都是指出旧论证内部不自洽，不是又提出一套新说法，随时可以提出。**测试方法论踩坑记录**：在同一进程内，写者用 `QSettings::setValue(key, QStringList())` 之后，即使换一个新的 `QSettings` 实例读，也**不会**读到 `@Invalid()`——Qt 的进程内 `QConfFile` 缓存会把序列化前的、有效的空 `QStringList` 变体直接交给同进程的后续读者，只有真正跨进程（或绕开 `QSettings` 直接写裸文本）才会经历「文本化再解析」从而产出无效变体；单测里复现「残留 `@Invalid()`」必须用 `QFile` 直接写 `key=@Invalid()` 字面量，不能用 `setValue(key, QStringList())` 再指望同进程读出无效值（`daemon/tests/tst_config.cpp` 与 `core/tests/tst_stringlistsetting.cpp` 都按此写法）。 |
| 70 | **候选侧译名括号，镜像决策 65 但作用在候选标题上**（**跳过集合**：本条新增的第四道闸门与前三道一样进入 `chooseMatch` 的跳过集合，`matcher.cpp:964-966` 四道闸门同在 `continue` 分支里——被 `candidate-gloss-artist-threshold` 拒绝的候选是「跳到下一个候选」，不是「结束整个循环」，决策 65 论证过这个区别正是「不给歌词」与「给错歌词」的分界。注意 `-threshold` 后缀本身定不了这件事：它同时用于走 `continue` 的闸门拒绝与会 `break` 的阈值失败，分组见 `core/match/matcher.h` 的原因清单注释）：决策 65／66 只处理查询侧的两种剥离变体；QQ 会把中译名拼进非中文曲目的候选 `title`（如候选 `惑星ループ (行星循环)` 对查询 `惑星ループ`），候选侧无人处理时 `title=0.500` 卡在 0.55 阈值下被拒（`--length-ms` 复现真实响应：`deltaMs=504`，落在 2000ms 窗口内 1496ms 余量，非临界修复）。新变体在 `considerTitle` 内部对每个候选标题／`alternateTitles` 字符串都尝试 `splitTrailingGloss`（原样复用，不改其谓词与词表），产生的剥离形式与既有查询侧变体做笛卡尔积打分；**只有精确等于 1.0 才参与取最大**（同一包含快路径顾虑），**平手优先非剥离路径**的判据从「非 gloss/artist-strip」再推广为「查询侧与候选侧均未剥离」。`titleWithoutStrip`/`titleWithoutStripViaAlternate` 的排除范围同步扩大，确保其「完全不剥离时的分数」语义不因新增的候选侧维度而改变。独立证据背书**原封不动复用** `strippedVariantDurationGateOutcome`（同一常数、同一窗口，决策 65／66 已明令不得新造）。`titleVia=` 从「按情形硬编码 6 种字符串」改写为拼接式实现（有则加入 `alias`/`gloss`/`artist-strip`/`candidate-gloss` 分量、用 `+` 连接，无则为 `title`），在候选侧维度恒假时逐字节复现原 6 种输出，同时无组合爆炸地扩展到新增维度；`--explain` 的 `plainTitle=`/`durationGate=` 触发条件同步纳入 `titleViaCandidateGloss`。拒绝原因按先例分裂为 `candidate-gloss-duration-threshold`/`candidate-gloss-duration-unknown`。**E.4a（dev-b 实测发现，lead 裁定采纳）：`passesCandidateGlossGate` 额外要求 `artists >= kAliasArtistThreshold`（复用 D-11 的 0.5，不新造第二个）。** 起因是 dev-b 按 §E.7 跑既有全量套件时翻出两条真实回归：候选 `Gunjou (Originally Performed by YOASOBI)`（`artists=["Backing Business"]`）对查询 `Gunjou`/`["YOASOBI"]`——`Originally Performed by YOASOBI` 既非译名也不在任何版本标记词表里（翻唱／伴奏带署名短语），被剥出精确标题命中；候选真实时长与查询相差仅 880ms，**合法落在窗口内，未经逃生口**，但真实艺人（`Backing Business`）与 `YOASOBI` 相似度只有 0.125。这与决策 65/66 允许的残余风险不同类：`Karaoke`／`Inst.`／`feat. X` 是同曲同艺人同时长，危害上限是「同一录音的另一剪辑」；这一个是**另一位艺人的另一份录音**，只是时长凑巧接近，时长门天然无法分辨这两种情形。依据不是新原则，是 `matcher.cpp` 对 D-11 `passesAliasArtistGate` 已写下的——候选侧标题证据（那里是 `transNames`，这里是候选侧剥离）会传播到无关艺人身上，这道低门槛只需拦住「无关的人」；决策 66 的 `titleWithoutStripViaAlternate` 是同一顾虑的第二次应用，本条是第三次，不是新标准。**这道艺人下限有一条与决策 65 披露时长未知同样必须写明的前提**：`artistSimilarity` 在查询侧或候选侧艺人列表任一为空时直接返回 0.0，所以「艺人下限」也意味着「缺艺人元数据时这条修复必然失效」——AMLL 索引本身干净（3274 条无一缺艺人），暴露面在查询侧的 MPRIS 元数据缺艺人字段，以及其他来源里本就缺艺人信息的候选。**该艺人下限没有 B.3b／B.3c 那样的逃生口，这是刻意的、已披露的验收口径变化，不是缺陷**：查询 `Bohemian Rhapsody`／`["Queen"]`（355000ms）对候选 `Bohemian Rhapsody (BR)`／`["Nobody At All"]`（355500ms）——`main`（`b22d041`）只靠候选未剥离的原标题（`title=0.850`，包含关系）就已经过验收（`total=0.620`）；本条把 `(BR)` 剥成精确命中后，同一候选反而因 `artists=0.0` 被 `candidate-gloss-artist-threshold` 拒绝，是否触发这道艺人下限取决于候选标题是否带括号，这一点不对称被有意保留。裁定：不补逃生口——艺人真的不对（`artistSimilarity("Queen", "Nobody At All")=0.0`），B.3c「证据存在且为负」不等于「证据缺失」的原则同样适用于艺人证据，不只适用于时长；给这道闸门加逃生口会让一个错误艺人的候选仅仅因为标题带括号就被放行，正好颠倒了这道下限存在的意义。曾评估把逃生口也扩展到不带括号的候选、消除这处不对称，**已否决**：那会改变每个候选在每个来源上的匹配结果，远超 §E 的范围，且 `alternateTitle` 路径本就由 D-11 单独把关。另一个更贴近的补救方案也被评估并否决（qa-e-2 提出并给出了否决的机制性理由）：`artists >= kAliasArtistThreshold \|\| nonStrippedMatchAloneIsAcceptable(score)`——这个写法确实能在不撤销 E.4a 本身的前提下恢复 `main` 对这类候选的验收（Gunjou 案例的 `titleWithoutStrip=0.158`，逃生口对它不会触发，E.4a 要拦住的那个案例仍然被拦住）。否决理由：这道逃生口的触发条件是 `titleWithoutStrip`——只有当**括号相对标题短**时它才会高，也就是说它恰好会优先救活艺人下限本来要拦住的那一类「括号短、剥离后精确命中」的候选，是括号本身在替它们背书，而不是独立证据，方向被整个反过来了。**此门槛只加在候选侧、不加在查询侧**（`passesGlossVariantGate`／`passesArtistStripGate` 保持不动）：方向不对称——用户播放伴奏带时查询艺人是 `Backing Business`、候选是 `YOASOBI`，给出原曲歌词正是用户想要的，查询侧剥离后艺人不符通常无害甚至正是目的；候选侧剥离后艺人不符，是拿另一位艺人的录音冒充查询请求的那首。拒绝原因因此三分：先查艺人下限（不足则 `candidate-gloss-artist-threshold`，与 `alias-artist-threshold` 同构），艺人达标后才轮到时长两态，避免把「时长其实在窗口内、真正原因是艺人不符」的候选误报成时长问题。`romanizedTitleRescuedByLocalizedFallback`／`dedupeGuardTreatsSameSongUnderDifferentIdsAsOne` 两个既有用例因此翻转又修复：修复后不是逐字节回到本任务之前的内部状态（该候选现在 `title=1.0` 经 `titleViaCandidateGloss`，拒绝原因是新的 `candidate-gloss-artist-threshold`，而非任务前的 `title-threshold`），但两个用例实际断言的外部行为（首位候选不可接受、`chooseMatch` 最终仍解出正确曲目）不变。**E.4 残余风险（lead 裁定：接受，须钉死为显式用例）**：`Karaoke`／`Inst.`／`feat. X` 均非版本标记，会被剥离；平手优先非剥离把风险收窄到「候选池恰好缺失精确曲目」这一种情形。`(Inst.)` 的实际危害**被 C2 的 `isInstrumentalPlaceholder` 部分削减，不是消化掉**（2026-09-12 dev-c 更正 lead 的原始表述）：该判定只在单行正文含「纯音乐」且走行级路径（`scroll=0` 或非 hex 回退）时命中；不覆盖纯音乐轨自带人声版歌词的情形；且发生在 fetch 阶段、错配已经写入指纹映射之后，命中时用户看到「没有歌词」而非错词，但缓存的错误 track id 要等用户手动「重新搜索歌词」才清掉。三条回归用例只断言 matcher 自身的接受/拒绝与原因，不依赖也不验证 C2 的占位符处理，因为后者是另一层、不总是生效。**该残余风险的实测代价（qa-e-2，真实索引，`PreserveVersions`）**：用「每条候选真实的剥离主体作为查询、且查询艺人取该候选自身真实艺人」这种贴近实际的回放方式重放 3274 条真实索引里所有带尾部括号的候选，`selected` 集合逐条比对：**0 条流失、0 条新增、0 条切换**——E.4 的平手判据在真实数据上成立。同一批数据也量出了 `splitTrailingGloss` 谓词本身有多保守：90 条候选剥离后与索引里另一条候选的标题精确相同，但谓词实际只剥了其中 7 条——`(Live)`／`(Cover)`／`(Taylor's Version)`／`(…ver.)`／`(和声伴奏)`／`(…版)` 都被版本标记词表拦下。风险确实可触发，但需要「艺人证据偏向括号那一侧」这个附加条件，普通 MPRIS 元数据不构造这种偏向；一版更早、构造粗糙的合成回放（查询艺人错取自括号侧候选）曾测出 2 例 `(Reprise)` 选型变差、1 例 `海阔天空`→`海阔天空（国语）`变好，已确认是回放脚本自身构造失真，不是真实成本，**已撤回，不作为结论依据**。QQ 录制样本（28 行）同样 0 条流失，§E.1 自身样本从 not-found 变为在真实时长下命中；`Default` 策略下 `ドラマツルギー` 集合有 2 处选型变化，均为 `(Live Film Ver.)`→`(拟剧论)`（录音室版优先于现场版，对用户有利），且 `(Live Film Ver.)` 在 `Default` 下能到 `title=1.0` 是 `cleanTitle` 既有行为，与本任务无关。**跟进项已考虑并否决——现在不做**：`splitTrailingGloss` 同时被 `searchKeywords` 用来构造发给 provider 的 `s=` 查询串，收紧谓词会改变**哪些候选能被召回**，不只是改变打分，而这个效应是差分回放这种方法**结构性看不见的**（回放只能对着已录制的固定候选池重放，§E.8 禁止联网抓取新样本验证新查询串召回了什么）——这正是本任务全程依赖的验证方法在这个改动上会失灵，是搁置的首要原因；次要原因是 `splitTrailingGloss` 同时被决策 65／66 的查询侧闸门共用，收紧会牵动 §E 范围外的场景。若日后要收紧，可辩护的子集只有 `Reprise` 和 `翻自` 两种——真实索引里当前被剥离的 602 条标题／专辑名中，dev-c 枚举的 11 种残余风险形状占 135 条（22.4%），其中 117 条是 `feat.`／`ft.`（还应排除，因为 `X (feat. Y)` 与 `X` 通常就是同一次录音，剥离通常是对的）、6 条 `with`、4 条 `Reprise`、4 条 `Explicit`、3 条 `翻自`、1 条 `Demo`——全部 11 种一起收紧大概率是净回归，只加 `Reprise`／`翻自` 才有把握不伤及 `feat.` 这个多数、通常正确的情形；即便如此，验证仍需要新查询串对应的已录制候选池，目前不存在。**性能（qa-e-1 实测发现，dev-b 复测并修复一部分）**：`considerTitle` 现在对每个候选标题与每条 `alternateTitles` 都调用一次 `splitTrailingGloss`（此前只按查询调用一次），qa-e-1 在约 3275 候选的合成基准上实测 `Default` 策略 +37～42%、`PreserveVersions` +24～34%。根因是 `splitTrailingGloss` 的锚定正则以惰性 `.*?` 开头，对**不含括号的绝大多数标题**也要跑一遍完整的回溯匹配才能确认「不匹配」。**修复**：在跑正则前加一个 O(1) 前置判断——去掉尾部空白后，若最后一个字符不是 `)`／`）`／`]`／`】` 之一，直接返回 `false`，因为正则本身要求 `[\)）\]】]\s*$` 收尾，这是匹配的**必要**条件（非充分条件，仍含括号的标题照常跑正则）。**等价性未止于推理，做了差分验证**：77 组用例（含空串、纯空白、多种右括号无正文、跨中间括号、NBSP／表意空格等 Unicode 空白结尾）逐条比对新旧判断结果，0 处不一致；专门验证了 Unicode 空白的方向性——本机 Qt 正则引擎的 `\s` 在这个上下文下不识别 NBSP／表意空格（PCRE 默认窄集合），而 `QChar::isSpace()` 识别，因此新判断的空白裁剪只会**裁得比正则更多、不会更少**，不存在把正则本会匹配的标题误判为不匹配的方向；ASCII 六种空白字符（空格/`\t`/`\n`/`\v`/`\f`/`\r`）逐一验证一致。**dev-b 的复测过程本身出过一次方法论错误，如实记录**：第一次比较把「不带前置判断、单独 `-O2` 编译」的对照跟「带前置判断、走 `build-dev` 的 Debug 库」相比，得到「加了前置判断反而慢数倍」的荒谬结论；改用 `perf stat` 看 retired instructions（不受调度抢占与本机当时高负载——`plasmashell`、多个 Chromium 实例等并发占用 CPU——影响）才发现两者用了不同优化级别，重新用同一 `-O2`、脱离 `build-dev` 的独立对象文件比较后异常消失。**权威数字测自用户真实的 AMLL 索引缓存**（`~/.cache/plasma-lyrics/amll-index.jsonl`，决策 65／66 的性能数字同样测自这份索引；本条只读读取，从未写回；3274 条有效候选，字段与 `amllprovider.cpp` 的 `metadata` 解析逐字节一致：`musicName`/`artists`/`album`，`lengthMs=0`——AMLL 从不提供时长，决策 65 已记载），`MatchPolicy::PreserveVersions`（AMLL 实际使用的策略），`perf stat -r 3` 三次重复取硬件计数器（三次重复标准差均为 0，不受 CPU 频率调度与本机当时的高负载影响）。**两组独立测量各自标注了构建配置、二进制与回放规模，互不覆盖，互不取代**：（一）本条自己的测量——对 `matcher.cpp` 单独以 `-O2` 编译出的对照目标文件（与 `tst_matcher` 共享同一份评分代码路径），回放约 166 次真实标题查询、每次 1 个查询变体（查询标题不含尾部括号），对整份 3274 条候选评分：任务 E 之前 590.19 亿指令／190.10 亿周期；任务 E、未修前置判断 623.94 亿指令（**+5.72%**）／193.18 亿周期（+1.63%）；任务 E、修完前置判断 615.30 亿指令（**+4.25%**）／193.44 亿周期（+1.76%，与未修判断相比在噪声范围内不可区分）——前置判断收回约 1.47 个百分点。（二）qa-e-2 的测量——Release `-O3` 完整构建，回放 630 次真实标题查询，按查询侧变体数分两组量：变体数为 1（查询标题不含尾部括号，真实索引里的多数情形）的一组基准为 224.555 亿指令，未修前置判断 +4.20%、修完 +2.99%，收回约 1.22 个百分点；变体数为 2（查询标题含尾部括号，触发 gloss 剥离）的一组基准为 351.235 亿指令，未修 +3.14%、修完 +2.31%，收回约 0.83 个百分点。**查询变体数越多，绝对开销越高，但回归的百分比反而越低**（351.235 亿 > 224.555 亿，+3.14% < +4.20%）——总评分工作量随变体数增长得比前置判断能省下的绝对开销更快，两组数字都成立，不是矛盾。真实用户的查询标题多数不带尾部括号（决策 65 已记录的真实索引括号占比同样适用于查询侧的典型形状），因此**变体数为 1 的一组才是贴近现实的数字：+4.20%／+2.99% 是应当引用的现实区间，+3.14%／+2.31% 是偏乐观的上限，不是典型值**。本条与 qa-e-2 的测量彼此吻合、互不取代：基准指令数之比 224.555 亿／59.019 亿 ≈ 3.8，与两者回放的查询次数之比 630／166 ≈ 3.8 一致；前置判断带来的绝对收回量之比也在同一量级（≈2.8），方向一致——两份数字均据实保留，各自标注了回放规模，规模不同不构成互相取代的理由。**qa-e-2 的微基准进一步验证了前置判断本身的净收益**：对 `splitTrailingGloss` 单次调用计数，不带前置判断时平均 1649.7 条指令／次，带前置判断时 130.9 条指令／次，省下 1519 条／次；真实索引里前置判断能够避免完整回溯匹配的调用次数为 1,812,424 次，预测总收回量 1,812,424 × 1519 ≈ 27.5 亿指令，与该组宏观测量的收回量 27.36 亿指令相差不到 0.5%，两种粒度的测量彼此印证。**指令数是本条采信的度量**：硬件计数器直接读取，不受调度噪声影响；qa-e-2 同批测量里的实际耗时反而出现「已修版（17.584s±7.53%）比去掉前置判断的版本（16.123s±0.61%）更慢」这种逻辑上不可能的结果，证明本机高负载下实际耗时噪声大到无法据此下结论——**该实际耗时数字已撤回，不作为本条任何结论的依据，也不得在别处引用**。**该索引里尾部带右括号的标题占比是这个结果的直接成因**：3274 条候选里恰好以 `)`/`）`/`]`/`】` 结尾（去除尾部空白后）的有 397 条，占 12.13%（`album` 字段占比 10.64%）——前置判断能对其余 87.87% 免掉一次完整回溯匹配，但 `splitTrailingGloss` 只是 `considerTitle` 总开销的一部分（还有候选形式 × 查询变体的 `textSimilarity` 逐一打分等），因此这条尾部字符检查省下的工作量换算到总指令数只是个位数百分点，量级吻合。**qa-e-2 最初报过的一版数字已完全撤回**：回归 3～4 倍于本条、且「前置判断毫无收回」，已确认是其回放脚本自身的 bug——`.trimmed()` 在按 tab 分列之前就先吃掉了候选行开头的空列，导致 3274 条候选里只有 397 条（恰好全部尾部带括号，集中度 100%）真正进入统计，其余 88% 被静默丢弃；该版本不作为本条的任何依据，以上（二）的两组数字是修复脚本后的重测结果。**同一方法论下还留了两份合成基准供交叉验证，均不是权威数字**：本条自己造的约 3275 候选合成集测得 +14.6%／+13.8%（未修／修完前置判断，`-O2`，硬件计数器）；qa-e-1 的合成集测得 +37～42%（`Default`）／+24～34%（`PreserveVersions`）。两份合成集都比真实索引悲观得多——`alternateTitles` 与短标题的人为占比显著高于真实索引的自然分布（12.13% 尾部带括号，不是合成集里那种更高的比例），这正是几份数字彼此不一致的原因，不是测量误差。前置判断带来的净收益方向在多份数据里一致（真实索引两组测量、本条合成集都收回正的百分点），残余部分是结构性的——`considerTitle` 需要遍历的（候选形式 × 查询变体）组合数本就因这条新维度而增加，不是单靠这一处正则优化能吸收完的，但按用户实际会遇到的数据规模衡量，残余已经很小（现实区间 +2.99%～+4.25%）。修复保留（零风险、有实测正收益）；各方数据的回归数字均据实披露并标注各自数据集／构建配置／二进制／回放规模，真实索引数字（本条与 qa-e-2 两组独立测量）是本条的权威数字，两份合成数字作为交叉验证保留、不删除。**不做**：不碰 `splitTrailingGloss`／`versionEvidence()`／`cleanTitle`／`normalizeSearchText` 的既有行为与词表；不新造时长窗口常数，不放宽 2000ms；不碰 `providers/` 或 `frontend/`（C2／D2 并行评审开发中）；不联网抓取新样本，测试全部使用 `scratchpad/c0/` 已录制的真实响应 |
| 68 | **QQ音乐是第四个独立歌词源，不是补词层**：自己的 trackId、自己的 `(provider, trackId)` 偏移、走既有匹配器、参与既有串行回退链，不跨源拼接正文与翻译（决策 4）。沿用决策 23 的源码模块扩展：固定编译，通过运行时配置启停，无运行时插件 ABI。**搜索的门槛是请求形状，不是 TLS、也不是设备身份**——实测必须同时满足 Mobile 方法 `DoSearchForQQMusicMobile` + 完整 mobile `comm` 块（`ct=11`、`cv=v=13020508`、`uid`、`tmeAppID`、`format`/`inCharset`/`outCharset`）+ 一个**存在且非空**的 `QIMEI36`；三者缺一都只回空结果而非错误。`QIMEI36` 的**值从不被校验**（`"x"`、`"abc"`、36 个 `0` 全部成功，空串与缺键才失败），因此**不实现 QIMEI 设备注册**，用固定占位常量即可，也**不做任何 TLS 指纹伪装**——普通 `QNetworkAccessManager` 全程可用，两者都是用户明确排除项。`searchid` **不是自由 nonce**：它是打包的三段字段 `<1..20> * 2^54 + <22 位随机> * 2^32 + <当日毫秒>`，高位段为 0 时服务端直接拒绝（`is_filter=-9`），一个计数器或裸时间戳正好落进这个洞；**固定值本身是可接受的**，服务端不要求每次不同，因此请求体对给定 `searchid` 逐字节可复现。**失败判别看 `meta.is_filter` 而不是结果数**：`0` 表示请求被接受，此时 `sum=0` 才真的是「没有这首歌」；`-12`（配 `req_code=2001`）是瞬时限流，必须重试——实测 3 秒间隔 12 次中 3 次触发、**4 秒及以上**间隔 16 次中 0 次，但这组测量并不单调（0.5 秒 12%、3 秒 25%），Fisher 单侧 p=0.082 不显著，**因此退避取实测中最保守的间隔，而不是据此编造速率模型**；其余任何负值（已见 `-2` 请求形状不被接受、`-9` 字段畸形）是基础设施故障，必须**单独记 warning** 并原样上报。把它们和「没有结果」合并会让**钉死的客户端版本被停用时与冷门歌曲完全同形**，故障在日常使用中不可诊断。**QRC 解密必须自行实现**：非标准三重 DES 后接 zlib，偏离标准 DES **至少四处**，任何现成密码学库都做不了。实现细节、移植保真度的验证证据及其边界见**决策 71**。解密留在 `providers/qq/`，解析器 `QrcParser` 放 `core/lyric/`（纯解析、无网络无密码学）。**QRC 正文不能用 XML 解析器读**：正文在 `LyricContent` 属性里而属性值规范化会把换行全部变成空格（XML 1.0 §3.3.3），整首歌会塌成一行；且整个歌词响应包在 XML **注释**里，符合规范的解析器根本看不到元素。手工取属性再反转义五个预定义实体。`contentts` 是**明文** LRC，`content` 与 `contentroma` **都是**加密 QRC。**罗马音按时间对齐、不能按下标**：`contentroma` 是逐音拍的（`惑` 一个字对 `wa `+`ku ` 两拍），token 数与正文不等；归属规则是：取 token **中点**，落到「`startMs` 不大于它的最后一个词」上——注意这不是 `[start,end)` 区间包含判定，中点若落进两词之间的空隙会归给**前一个**词，而不是无人认领；实测该曲 715 个 token 的中点**无一**落在空隙里（0/715），因此两种说法在现有数据上同解，但代码实现的是前者。用中点而不用 start 的收益是实测出来的：改用 start 会有 **283/715（约 40%）** 的 token 归到另一个词上，根源是两份载荷各自 ±1ms 取整（如某拍 start=232 而对应词 start=233）。行对齐用 ±20ms 的双指针而非下标配对。翻译对齐要 ±15ms 容差：LRC 时间戳最细到厘秒而 QRC 到毫秒，实测最大差 9ms，`LrcParser::merge` 的精确相等只能对上 68 行中的 8 行；`//` 是 QQ 的「本行无翻译」占位符，必须丢弃。**罗马音存词级也存整行**（用户 2026-09-12 决定）：`LyricWord::romanization` 与 `LyricLine::romanization`，后者由前者依次拼成，因此二者不可能不一致。两者在 JSON 里**缺省不写键**（不像 `translation` 写显式 null）：词数组每首上百条，几乎没有源填这个字段，恒定写出会让每次快照与每行缓存平白涨半倍；缺键与 null 读回同为未设置，下游无需区分。**不需要 schema 迁移**：`lyric` 表把行存成 `origin` 里的 JSON blob，新字段随 `lineToJson`/`lineFromJson` 直接贯通 SQLite、快照与 QML，旧行只是没有该键。**曲名行与制作人员行是带时间的正文行**（与网易云同款问题，实测同一首歌两边都出现）：曲名行按文档自带的 `[ti:]`/`[ar:]` 判定而不是形状——它常无冒号，形状判据永远抓不到；且**不能拿两半去逐字比对标签**，`[ti:]` 常带正文行没有的副标题（`万物有灵 (剑灵八职业同人曲)` 对 `万物有灵 - 洛天依`），因此艺人半必须精确相等、标题半只要求互为前缀。其后的署名行按形状标记，但**只在开头连续段内**，且判据比 `looksLikeCredit()` 宽（允许头部带空格、放宽到 24 字符）——QQ 写的是 `Lyrics by：MSR Studio`，头部有空格，现有共享判据结构上抓不到；**不放宽那个共享判据**，那会改变其余每个源的过滤结果，宽判据只留在这里，格式已知且范围有界。**匹配不得引入包含式判据**：C0 实测包含式匹配在该源上贡献 1 次错配（`万物有灵` 匹到无关的 `万物有灵人有情`）且零次正确命中，交给既有匹配器即可。`scroll` 属性在 22/22 上准确预判 QRC 是否存在，但它只是预测、不是保证，**因此判定由正文本身做出，两个方向都不得因预测出错而丢掉整首歌**（C2.3.10）：正文全为十六进制即按加密 QRC 处理，否则按明文 LRC 处理；`scroll` 只在与正文不一致时记一条 debug 日志。这样做不损失什么——扫一遍是否全为十六进制比它所要避免的那次解密还便宜，而单看 `scroll` 会有两种丢歌方式：说有 QRC 而正文是明文时解密失败，说没有而正文是十六进制时会解析出零行 LRC 并**静默**丢掉（无任何错误）。退回整行歌词**是常态而非异常**，日志用 debug。纯音乐曲目该源返回一行「此歌曲为没有填词的纯音乐」，必须按「没有歌词」处理，否则它会作为整首歌唯一一行显示出来。曲目 id 用搜索结果的**数字 `id`** 而非 `mid`：歌词接口收到 songmid 会回 `musicid="0"`。**版本停用风险**：请求钉死客户端版本常量（`cv`/`v`=13020508、`ct`=11、UA `QQMusic 14.8.0.8`），腾讯停用该版本时预期表现为 `is_filter` 变成某个负值，上面的判别正是为此而设 |
| 71 | **QRC 解密的实现与验证证据**（本条从决策 68 拆出，便于单独查阅；使用这套解密的歌词源本身见**决策 68**）。**当前结论先行**：本仓库的实现与参考实现在三份真实载荷上逐字节一致；错表在结构上几乎不可能静默产出乱码，失败是响亮的解析失败；**唯一未被任何证据覆盖的保留是——这证明的是移植保真，不是这套表就是腾讯在用的表**。以下是依据、四条界限，以及已被更正的早期说法。**QRC 解密必须自行实现**：非标准三重 DES（密钥 `!@#)(*$%123ZXC!@!@#)(NHL`，顺序 key3 解 / key2 加 / key1 解）后接 zlib，偏离标准 DES **五处**——`S2[23]=15`（标准 14）、`S4[53]=10`（标准 1）、PC-2 的 D 半区取 `pos-27`（标准 `pos-28`）、密钥按两个**小端**字读入，以及非标准的 IP 与逆 IP 表。任何现成密码学库都做不了，而**每一处单独改错都不会在 DES 这一层报错**——错误要到其后的 zlib 解压才暴露，且那里只会是响亮的失败而非乱码（见下），因此测试仍必须把密钥编排（覆盖小端读入与 PC-2 偏移）与单块密文（覆盖 S-box 与 IP）**分开钉死**：端到端只能告诉你「有一处错了」，分开钉死才能告诉你**是哪一处**；单块向量要挑对 `S2[23]` 敏感的输入，`0x0001020304050607` 恰好不敏感。**移植保真度已独立验证，但验证有明确边界**：QA 从录制的 XML 里取出 `content`／`contentroma` 的密文，当场用 `scratchpad/c0/qrc.py` 解密并与 C++ 的期望值逐字节比对，三份真实载荷（3401／12456／11300 字节，共约 27 KB）**零字节差异**。但 `qrc.py` 是独立的**实现**、不是对格式的独立**推导**——它移植自 repoB 的 Rust，本项目的 C++ 又移植自它，因此**上游共有的错误能同时躲过这两道比对**，「逐字节一致」证明的是移植无误，不是这套表就是腾讯在用的表。**本条早先说「错表会安静地产出看似合理的乱码」，这是错的，而且错在最要紧的地方**——整个任务的风险叙事就建立在那句话上。真相是：密文之下不是被消费的字节流，而是一个 **zlib 流**，于是只有两种结局——要么解压成功且明文**逐位正确**，要么根本解不开；「略有出入的明文」在结构上没有容身之处。**部分输出也不可能**：解压循环只在 `Z_STREAM_END` 时退出，流提前结束会命中 `Z_BUF_ERROR && avail_in == 0` 并返回 `the zlib stream ends mid-payload`，没有任何路径会把「已经解出来的一截」交出去。**约束在于 zlib 的 2 字节头部校验与其后的 Huffman 码合法性，不是 adler32**——860 次变异实测中 adler32 与截断分支**一次都没有触发过**，它只是从未被用上的兜底；约半数变异当场死在头部，其余死在第一个非法 Huffman 码上，**在产生任何输出之前**（40 次单条目变异的「失败前已输出字节数」全部为 0，而明文有 3401 字节）。证据不止于已知的五处偏离，且**结构化的错误被抓得更狠**（均针对最小载荷布道人、245 块，即最不利情形）：随机单条目 S-box 变异 300 次 + 120 次、两条目对调 120 次、整行置换 120 次，合计 **860 次试验、零次静默存活**；整行置换扰动的查表更多，120 次里有 117 次死在第一个块。覆盖面也已核实：四份载荷**各自**都把 512/512 个 S-box 条目读过一遍，而 IP／逆 IP 每块都走满 64 个位置、密钥编排由三把固定密钥算一次，均与数据无关，不存在「错在某个从没被用到的条目上」的缝隙。**一处真正的限定必须写明**：密钥编排变异一度看起来有 14/120 的「存活者」，改为比对输出字节后（N=200）真相是——14 次变异后的 48 个轮密钥**逐位未变**（密钥固定，对调两个选中相同密钥位的表项不改变结果），186 次真正改变了编排并全部被拒，**0 次产出了不同的明文**。因此本条证据钉住的是**轮密钥**，不是密钥编排表本身：一张与腾讯不同、但对这三把固定密钥产生相同编排的 PC-2 与之无法区分，也无害。`pos-27` 被钉死为**一张正确的表**，而不是**那张表**。**四条界限**（它们是「实测结论」与本任务反复出现的过度推断之间的分界）：① 0/300 不等于 0%，按 rule of three，单次试验存活率的 95% 上界约 1%——**统计是最弱的一环，真正承重的是上面的结构性论证**，试验只是佐证；② 所有扫描只用了**一份密文**（布道人），已知五处偏离的 20/20 across 四份载荷可部分抵消，但那是五个错误实例，不是一个分布；③ 只变异了**表**，`sBoxIndex`／`applyPBox`／E-box／分块与尾部不足一块的丢弃都没有被变异——它们同样位于 zlib 之前、理应同样响亮地失败，但这一点未经实测，而移植的面积不止于常量；④ 用的是**最小**载荷，因此结论偏保守，这是唯一值得放心的偏差方向。在结构性论证之外，还有三条旁证：解密结果是良构的 UTF-8 QRC XML；能解析成连贯歌词；罗马音在语义上正确（`銀河の隅で 惑星はグルグル周る` → `gin ga no sumi de waku sei ha gu ru gu ru mawa ru`）。**曾把「词 token 数 159 与另一套实现的记录吻合」列为第四条旁证，这是错的**：`qq_census.py` 自己 `import qrc` 并调用 `qrc.decrypt_qrc`，与逐字节比对用的是同一份实现，对「表是否正确」零信息量。census 中唯一触及密码学的独立量是 **`clen=3920`**（另有 `scroll=4`，属响应形状），与 committed fixture 逐字相符，它证明录制的是真实响应，不证明表是腾讯在用的表。（**`trans_len` 在任何解读下都不构成密码学证据**：`contentts` 是明文、根本不经过 `QrcCipher`；且 census 的 1097 是原始**十六进制**长度（与 `content_len` 同一度量），而 `qq-qrc-budaoren-trans.lrc` 的 1097 是解密后的**字节**数，两者是无关的量，数值相同纯属巧合——fixture 的 `trans_len` 实为 631，两次抓取之间 QQ 修订过该曲译文。）**唯一未被这 860 次试验触动的，是本条原有的核心保留**：逐字节一致证明的是移植保真，不是这套表就是腾讯在用的表——一张正确但并非腾讯那张的表是自洽的，会干干净净地解压通过。**五处偏离的覆盖已逐一证伪确认**：每处单独改回标准 DES，**四份载荷各自独立地都能捕获全部五处改动（4 × 5 = 20/20）**，含两处被改的 S-box 行。这不是运气——最小的一份（布道人正文）密文 1960 字节 = **245 块** × 3 把密钥 × 16 轮 × 8 次查表 ≈ **94,080** 次 S-box 查询，改动过的行在这个量级上躲不掉。（早先此处写「约 425 块 ≈ 16 万次」，那是把**明文** 3401 字节除以 8 得到的块数；DES 作用于密文，与明文长度无关。）该 20/20 起初只能在 Python 中确认，因为 `decryptsRealPayloadsExactly` 用循环加 `QVERIFY2`，而 `QVERIFY2` 失败即从测试函数返回，第一份载荷之后的都不会执行；现已改为 data-driven（每份载荷一行），四行各自独立失败，20/20 在 C++ 测试中直接可验。单块向量则相反：`S2[23]` 改回 14 时 `cryptBlock(0..7)` 与 `tripleDes(0..7)` **都仍然通过**，`tripleDes(全零块)` 是**本套测试中**唯一能捕获该行的单块向量——全零块本身并不特殊，实测 2000 个随机块中约半数（1018 个）都能捕获它，只是 `0x0001020304050607` 恰好不在其中，`cryptBlock(全零块, key1-decrypt)` 同样不在其中，只有三重那一遍才捕获。删掉它会失去对一处被改 S-box 的单块覆盖。 |

### 工程结构
| # | 决策 |
|---|---|
| 13 | 内部歌词模型从第一天预留 `words: Option<Vec<Word>>`；**渲染层第一版不做逐字**（后续实现见决策 69）。当初"拿不到词级数据"的判定只针对**网易云明文接口**（1.2 节：不返回 `yrc`），在那个范围内依然成立；词级时间轴改由 **QQ 音乐**（`core/lyric/qrcparser.cpp` 解出的 QRC）与 **AMLL 的 TTML**（`core/lyric/ttmlparser.cpp`）提供，是新增的这两个数据源绕开了网易云那道限制，不是限制本身不成立 |
| 16 | 数据契约：推整首 + 时间锚点（见 2.2） |
| 17 | 布局重新设计（见 2.3），不照抄 nethogs |
| 18 | 配置分两份：后端全局一份 + 前端每实例一份；后端配置的编辑界面仍在 plasmoid 设置里，但要视觉上分开并标注"影响所有歌词部件"。**存储落点**：后端那份是 `QSettings` INI（`~/.config/plasma-lyrics/plasma-lyricsd.ini`——`backendconfig.cpp:8-12` 与 `daemon/src/config.cpp:9-10` 构造的是同一个路径），前端那份是 kcfg（`<kcfgfile name=""/>`，落在 `plasma-org.kde.plasma.desktop-appletsrc` 里每实例自己的 `[Containments][…][Applets][…][Configuration][General]` 组）。**"每实例一份"是能力，不是缺陷**（2026-09-05 用户报"两个部件设置不互通"后补记）：同时摆一个桌面部件和一个面板部件是 §2.1 明写支持的用法（"快照只读、多前端无冲突，这正好满足『同时摆两个部件』"），此时两者的外观、文本、自动隐藏各自独立，四个 tab 里**只有「歌词服务」跨实例共享**。而恰好只有那一页带 InlineMessage 声明自己是全局的（`ConfigBackend.qml:16-21`），于是**其余三页的沉默被读成了"共享"**——设计是对的，界面失语。因此三个前端 tab（桌面外观 / 面板外观 / 文本）各加一条 InlineMessage："这些设置只影响当前部件，其余部件不受影响"。决策 40 把「文本」页那三个键叫作"全局文本键"，那里的"全局"指的是**跨 form factor**、不是跨实例，`ConfigText.qml` 的顶部注释已相应改写，免得它自己变成下一个误解源。**改做 form factor 感知**（桌面实例只显示「桌面外观」页，面板实例只显示「面板外观」页）：机制在 `config.qml` 一层——`ConfigModel` 上加 `readonly property bool onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar`，两个外观 `ConfigCategory` 各自加 `id`（`desktopAppearance`/`panelAppearance`），`ConfigModel` 的 `Component.onCompleted` 里 `configModel.removeCategory(onDesktop ? panelAppearance : desktopAppearance)` 把不适用的那一个整条移除（判据是「非 Planar」而不是「等于 Horizontal」，垂直面板同样算面板）。**不用 `ConfigCategory.visible`**（2026-09-11 qa-1 发现后改定）：`visible` 只过滤设置对话框侧栏的 `Repeater`，两处壳（`org.kde.plasma.desktop` 与 `plasmoidviewershell` 各自的 `AppletConfiguration.qml`）的 `Component.onCompleted` 决定初始显示哪一页时都直接读**未过滤**的 `configDialog.configModel.get(0)`，不检查 `visible`——只用 `visible` 会让面板实例每次打开设置都先落在被隐藏、侧栏无高亮的「桌面外观」页，需要用户自己点一下才能纠正。移除分类后 `get(0)` 在两种形态下天然就是各自对应的外观页：`Desktop appearance` 保持声明顺序第 0 位、`Panel appearance` 第 1 位不变，桌面落地页不变，面板落地页从「桌面外观」变为「面板外观」。原否决理由依然成立且与本次机制选择无关：`tst_appearance.qml:50,55` 直接实例化的是 `ConfigDesktopAppearance.qml`/`ConfigPanelAppearance.qml` 这两个页面文件本身，从不加载 `config.qml`，两个页面文件仍然不得引用 `plasmoid`/`Plasmoid`（`main.qml` 已有同一判据 `onDesktop: Plasmoid.formFactor === PlasmaCore.Types.Planar`，此处是配置目录下新增的第二处）。**时序依据**：`PlasmaQuick::ConfigView` 在 C++ 里先 `create()` 出 `config.qml` 的 `ConfigModel`（其 `Component.onCompleted` 同步跑完、分类已移除），随后才加载壳的 `AppletConfiguration.qml`，壳读到的 `get(0)` 已经是移除之后的结果；本机没有 libplasma 源码，C++ 侧调用顺序无法本地核对，**结果已在 plasmoidviewer 的壳里实测**：`-f planar` 落「桌面外观」、`-f horizontal`/`-f vertical` 落「面板外观」，侧栏都只剩 4 个插件分类。`org.kde.plasma.desktop` 的壳未实测，其决定初始页的 `get(0)` 代码与 plasmoidviewer 壳逐字相同。**已接受的残留**：这条分支没有自动化测试覆盖（`config.qml` 引用 `Plasmoid`，`tst_appearance.qml` 不实例化它，原因同上），但侧栏分类与初始落地页已实测（方法：`frontend/plasmoid/package` 复制到临时目录，只在副本 `main.qml` 里加一个 `Timer` 调 `Plasmoid.internalAction("configure").trigger()` 让部件自己打开配置对话框，仓库不动，`plasmoidviewer -f planar/horizontal/vertical` 各起一次被动截图核对，不需要人工点击或输入注入）；另外，面板实例从此无法在设置页编辑 `desktop.*` 键、桌面实例无法编辑 `panel.*` 键：决策 31 的对称键仍然各自独立保存，只是这些键对该实例本就无效，在当前形态下不可见即不可改，部件在两种形态间移动后，另一形态沿用上次落盘的值。`Plasmoid`/`PlasmaCore` 的静态引用与限定调用 `configModel.removeCategory(...)` 的方法名已由 qmllint 覆盖（拼错方法名会报 `[missing-property]`），但 `desktopAppearance`/`panelAppearance` 这两个 `id` 参数本身不受 qmllint 覆盖（拼错 id 时 qmllint 仍然 exit 0；三元表达式只求值命中的那一支，所以运行时只在会取到该 id 的那种形态下抛 `ReferenceError` 并中断 `Component.onCompleted`，此时 `removeCategory` 不被调用、两个外观页都留在侧栏，另一种形态不受影响）。**另一处残留**：`removeCategory` 只在 `Component.onCompleted` 时对 `onDesktop` 采样一次，不是响应式绑定——对话框开着时若形态发生变化，需要关闭重开设置对话框才会生效；本插件同一部件实例的生命周期内 `formFactor` 不会变化，这里只是如实记录，不代表已知的用户可见问题。**明确不做跨实例共享外观**：那要把 46 个键搬去全局存储、再补一套变更通知（`BackendConfig` 全无通知路径，现有 UX 靠"改完重启 daemon"糊过去；仓库里唯一的跨实例活体同步是 `lyricsource.cpp:142-159` 每 2 秒轮询 SQLite 的共享偏移），换来的却是"两个部件必然长得一样"——而决策 31 的 `desktop.*`/`panel.*` 对称本来就是为"同一实例在两种形态下各有物理上合适的值"服务的，与跨实例无关；§0 也早把"后端配置全局共享导致多实例互踩"列为 Lyrica 的坑 |
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
| 14 | 八种非歌词显示情况：① 未播放 → 可自定义文案，默认「当前未在播放」；② 查不到 → 默认「暂无歌词」；③ 间奏 → 空；④ 暂停 → 保留最后一行不动（靠"非 Playing 不推进位置"实现，不特判）；⑤ 搜索中 → 显示「搜索中…」；⑥ 非音乐过滤 → 空；⑦ `no-lyric`（匹配成功但源站无词）→ 默认「纯音乐，请欣赏」；⑧ `network-error` → 默认「网络错误，暂时无法获取」。②的旧默认为空，其隐含前提是决策 4 的「查不到就算了」和「waylyrics 单开网易云基本都能查到」；2026-09-07 实测 22 次解析有 12 次 `selected: none`（主要是 Vocaloid / 独立曲目），该前提已被数据推翻，因此改为非空默认。未播放、查不到、源站无词和网络错误文案都沿用 `idleTextUseDefault` 的伴生 `*UseDefault` Bool 模式，用户仍可显式设为空文案 |
| 39 | **曲目信息行**：歌词区上方一行常驻「标题 — 歌手」，桌面默认开 / 面板默认关（面板 `gridUnit*2` 高度塞不下第三行文字）。不显示专辑：CJK 单曲专辑名常与标题相同，`core/match/matcher.cpp:92` 已经因为这个把同名专辑排除出搜索关键词。不显示封面：全链路未采集 `mpris:artUrl`；远程图要么让每个部件实例各自发网络请求、破坏「前端只是快照只读消费者」，要么给 daemon 加一套下载缓存——那是独立功能。样式完全独立于歌词（字号/字重/颜色/描边/描边色/溢出/布局各一套 kcfg，桌面面板对称，共 16 键），因为它是另一个语义层；决策 26 的「同字号」先例只管同一条歌词的译文。唯一硬编码的是两行模式歌手行 alpha ×0.75（排版层级，不是用户会调的东西）。溢出只给 `fit`/`elide`，不给 marquee——曲目一整首歌不变，滚动只抢走歌词的注意力。`trackTitle` 为空时整行不占高度；`searching`/`not-found`/`filtered`/`no-lyric`/`network-error` 时照常显示，这正是常驻的价值（那些状态下歌词区本来不显示实时歌词）。本功能零 daemon 改动、零 schema 改动——快照早就带着 `title`/`artists`，这是决策 16 数据契约的红利 |
| 38 | **前端不轮询，按边界唤醒**：位置是解析式的（锚点 + `CLOCK_MONOTONIC`，见 2.2），换行时刻因此可以精确算出来——`nextBoundaryMs()` 求下一个能改变行号的时刻，单次 `Qt::PreciseTimer` 直接打过去。原实现是 33ms 固定轮询：一首 4 分钟 60 行的歌要醒 7200 次，其中约 60 次真的换了行，而且换行还被量化在 33ms 网格上（`QTimer` 默认 `Qt::CoarseTimer`，再叠最多 5% 抖动）；改后约 120 次唤醒、误差进入 1ms 内。三条实现约束：① 边界取所有 `startMs`/`endMs` 的**超集**——行尾可能晚于下一行行首，不能只扫到第一个更晚的行首；② 无锚点或 `rate <= 0` 时不武装，位置根本不会自己走，武装只会空转；③ 单次间隔封顶 60s，把畸形时间戳（`[9999999:00.00]`）挡在定时器的 `int` 之外。**将来做逐字扫词（决策 13）时驱动应是 QML `FrameAnimation` 逐帧拉取，不是把定时器调回 16ms**——帧驱动在窗口不渲染时会自己停，定时器不会 |
| 40 | **无歌曲时自动隐藏**：独立开关（**默认关**——本功能有一条硬代价见下，默默开给存量用户会变成一堆「壁纸上有块点不动的区域」的 bug 报告），语义是「整个部件不可见」，与底板 `plate` **完全解耦**——不复用「`plate = none`」，那一档只是不画底板、文字照旧渲染（默认还会显示 `idleText`）。判据 = `determined && serviceAvailable && !stale && (无播放器/`Stopped` ‖ (`filtered` ∧ 开了「非音乐媒体也隐藏」，默认勾选))`。**`Paused` 不算无歌曲**（暂停十秒去接水不该把歌词淡掉，且暂停本身意味着还想接着听）；**已知风险**：若播放器队列放完停在 `Paused`（metadata 仍挂着最后一首）而非 `Stopped`，退场永不触发、部件会一直挂着最后一行——需实测目标播放器的 MPRIS 收尾状态。**`!serviceAvailable ‖ stale` 时绝不隐藏，且不可配置**：`LyricsView.qml` 那个 `Loader` 是唯一告知「daemon 挂了、执行 `systemctl --user restart`」的 surface，朴素判据（「`Stopped` 或无标题就隐藏」）会在 daemon 崩溃时把部件永久静默隐藏——用户既看不到歌词也看不到原因。退场有可配置缓冲（默认 5 s，0–120 s，**0 = 立即开始淡出**；关功能是主开关的事，不让 0 兼任第二个开关），缓冲计的是「**判据成立至今多久**」而非「配置变更至今多久」，所以空闲时勾上主开关会立刻淡出；入场无缓冲。`filtered` 走**同一个**缓冲键（视频/音乐边界的穿越比队列放空频繁得多，无缓冲会闪成频闪灯）。**桌面**：`opacity` 1↔0，时长可配（默认 1000 ms，0–3000，步长 50，**0 = 无动画**），双向**共用一个键**（两方向不同时长会让「从当前值反向」失去定义）、`Easing.OutCubic`（先快后慢；KDE 单向减速事实标准，装机栈内 146 处；`OutQuad` 偏线性、`OutExpo` 尾部像卡住）。配置时长是**绝对值**、不受 `[KDE] AnimationDurationFactor` 缩放（否则 SpinBox 会说谎），但 `Kirigami.Units.longDuration <= 1`（用户全局关了动画）时**整体跳过**动画。**首次判定两个方向都不播动画**（`Behavior { enabled: 已过首次判定 }`）——动画只属于运行期的状态转移，否则每次登录都会淡入一次，daemon 没跑时还会变成「诊断文案淡入」。淡出中来新歌**从当前不透明度反向**（`Behavior` 天然语义，永不跳变）；缓冲期内来新歌只取消定时器、不播任何动画。**面板**：无动画，直接切 `Plasmoid.status = HiddenStatus`（真正把容器从 `GridLayout` 摘掉、邻居重排，见 `LayoutManager.js:14-26`；`opacity: 0` 会留 `gridUnit*14 ≈ 252 px` 空洞，而 `Layout.preferredWidth: 0` **无效**——面板 `main.qml` 的 `findPositive` 把 0 当「未设置」并替换成面板厚度）。此举对面板自动隐藏/闪避**零影响**（`panelview.cpp:1028` 有专门的 `!= HiddenStatus` 守卫，正因 Hidden 数值最高）；逃生口是 KDE 自带的 `‖ (!Plasmoid.immutable && Plasmoid.userConfiguring) ‖ Containment.corona.editMode`。`Plasmoid.status` **必须命令式重申、不能写成绑定**：`expanded` 可由每个 applet 都有的全局快捷键（`applet.cpp:760-766` + `ConfigurationShortcuts.qml:21`）和键盘 Space/Enter（`CompactApplet.qml:95-104`）驱动，**不需要任何 `MouseArea`**，而 `CompactApplet.qml:230` 会 `Plasmoid.status = RequiresAttentionStatus` 摧毁绑定（`Binding {}` 元素也救不了）→ 在自身条件 handler 与 `onExpandedChanged` 变 false 时各写一次；且 `shouldBeVisible` 为假时**强制 `expanded = false`**，禁止弹出一个锚定在不可见 item 上、内容为空歌词区的弹窗。用 `ActiveStatus` 而非 `PassiveStatus`（`containment_p.cpp:90-110`：Passive 会触发容器状态重算，可能在任意时刻把焦点抢回上一个窗口）。**绝不写自身根上的 `visible`**（`LayoutManager.js:26-28` 已装 shell 的绑定），但可**只读**它得知面板已把我们隐藏。**桌面隐藏期间会隐形拦截左键，这是已接受的代价**：`ItemContainer` 构造函数 `setAcceptedMouseButtons(Qt::LeftButton)` + `setFiltersChildMouseEvents(true)`，吃点击的是容器不是我们，从 applet 内部**无解**——`opacity` 按 Qt 规定不影响输入事件，`visible: false` 被 shell 覆写（`setContentItem()` 的 `item->setVisible(true)`、`CompactApplet.qml:35-43`）且不缩容器几何，`HiddenStatus` 在 Planar 上 **no-op**（desktopcontainment 与 `libcontainmentlayoutmanagerplugin.so` 都不提这个枚举）。右键仍出上下文菜单、长按仍进编辑模式，**不会把部件锁死**。**tooltip 无需任何处理**：面板侧 `HiddenStatus` 已把链路上两个 item 置 `visible: false`（`AppletContainer.qml:24` 的容器 + `LayoutManager.js:26-28` 直接绑我们的 `PlasmoidItem` 根），而 hover 投递跳过不可见子树（`qquickdeliveryagent.cpp:1229-1230`）；桌面侧**根本不存在 tooltip 通路**（`main.qml` 在 Planar 下设 `preferredRepresentation: fullRepresentation` → `appletShouldBeExpanded()` 为真 → 走 full 分支，`CompactApplet.qml`（栈内唯一消费 `toolTipMainText` 处）的 expander 从不创建；`BasicAppletContainer.qml` 也无 `ToolTipArea`）。且抑制本身不可靠：`tooltiparea.cpp:118-120` 用 C++ setter 直塞共享默认 item、绕过 QML 绑定，**弹过一次后 `isValid()` 永久为真**，清空两个文本只会得到空气泡。**状态机在编译型 QML 类** `frontend/qmlmodule/visibilitypolicy.{h,cpp}`（`QML_ELEMENT`，同 `LyricSource`/`BackendConfig`），吃**六个离散输入属性**（`serviceAvailable`/`stale`/`playbackStatus`/`trackTitle`/`lyricState`/`determined`）而非整个 `LyricSource` 对象——这样单测直接 setter 灌值跑真值表，不必构造快照文件，且对 `LyricSource` 零编译期依赖。`main.qml` **单实例挂 `PlasmoidItem` 根**，配置用 `onDesktop ? desktop* : panel*` 三元喂（沿用 `activePlateMode` 先例）；放 representation 内部会因 `Loader` 生死**静默重置正在跑的缓冲计时**（把部件从桌面拖进面板，歌词会莫名重新出现）。缓冲用 C++ `QTimer` 而非 QML `Timer`——后者是 `QPauseAnimationJob` 驱动（16 ms 分辨率、无补偿），窗口不渲染时可能不推进；注意 C++ `QTimer` 同样是单调时钟、不补偿休眠，规避的是动画驱动停摆而非抗休眠。新增 `LyricSource::determined`（首次 `reload()` 结束时置真，无论成败）**必须自带 `determinedChanged` 信号**：`setUnavailable()` 的 `const bool changed = m_serviceAvailable ‖ m_stale != staleValue;` 配上初值 `m_serviceAvailable=false, m_stale=false`，在「daemon 没跑的冷启动」这条路径上 `changed == false`、**恰好不发 `statusChanged`**——复用会让绑定永不更新、未定态永久驻留、诊断文案永不出现，即本条钉死要防的那个失败，且只在 daemon 挂了时才显形。之所以需要 `determined`：初始状态与「首次读取失败」后的状态是**完全相同的五个值**（`serviceAvailable=false, stale=false, lyricState="filtered", playbackStatus="Stopped", trackTitle=""`），五输入无法区分「尚未判定」与「已确认不可用」，而两者行为相反。**「未定」态行为等同隐藏**（面板 `HiddenStatus`、桌面 `opacity: 0`，不播动画，尚不能显示诊断文案），只持续**一轮事件循环**（`lyricsource.cpp:67` 的 `QTimer::singleShot(0, ..., &LyricSource::reload)`；`m_retryTimer`/`m_healthTimer` 虽都是 2000 ms 但**都不参与首次判定**，后者只做 pid 存活检测与共享偏移同步）——面板做不到「既不显示也不隐藏」的中间态，因为 applet 默认的 `UnknownStatus` 是**可见**的。**但主开关关闭时 `shouldBeVisible` 恒为真，且该判断先于「未定」分支与其余一切判据**——「未定等同隐藏」只服务于「自动隐藏开启时别闪出马上要淡掉的内容」这一个目的，功能关闭时必须逐字节保持改动前行为。漏掉这一层会让**每个**面板实例（含从未启用本功能的存量用户）开机瞬间走一次 `HiddenStatus`、邻居重排一次再翻回来，恰好违背本条「默认关 = 存量零观感变化」的初衷；成因是 `Component.onCompleted` 同步执行、早于 `LyricSource` 构造函数里那个 `singleShot(0)` 的首次 `reload()`。**7 个 kcfg 键，故意不对称**（桌面 `desktopAutoHide`/`desktopHideDelaySec`/`desktopHideAnimationMs`/`desktopHideNonMusic`，面板 `panelAutoHide`/`panelHideDelaySec`/`panelHideNonMusic`）：砍掉 `panelHideAnimationMs`,因为面板不做动画，留着就是一个转起来毫无效果的 SpinBox——决策 31 的对称是为了「同一实例在两种 form factor 下都有物理上合适的值」（字号 34 vs 16），而面板压根没有「动画时长」这个概念可言，不是同一个东西缺了一半。设置页因此**拆成四个 tab**（桌面外观 / 面板外观 / 文本 / 歌词服务）：三个全局文本键（`idleText`/`idleTextUseDefault`/`notFoundText`）进「文本」——不能在两个外观 tab 各放一份（同键两处编辑，用户会以为它们独立），也不能塞进「歌词服务」（那页明说「影响每个实例、改完要重启 daemon」，而文本键是 per-instance 且即时生效）。`AppearanceSection` 拆掉 `Kirigami.Card` 与 `title`（tab 名已承担标题职责，留着卡片标题会和 tab 名重复出现），自动隐藏分节放**页面顶层第二个 `FormLayout` + `twinFormLayouts`**（`FormLayout.qml:84`）而非塞进 `AppearanceSection`——两个 tab 的自动隐藏区块内容已经不同（桌面 4 控件 / 面板 3 控件），共用子组件会退化成带 `visible: isDesktop` 条件的错抽象，且顶层可用朴素 `property int cfg_x` 省掉仓库里那套「隐藏控件 + `property alias` + `required property var xControl`」的绕法。子控件用 `visible:` 跟随主开关（先例 `AppearanceSection.qml:69`、`ConfigBackend.qml:69`）。拆 tab **无数据迁移**（kcfg `<group name="General">` 与键名全不变）。**分两次提交**：① 底板搬家（纯行为保持，验证=切主题/切配色/对比截图，可独立回滚）② 自动隐藏——混成一次改动的话，桌面外观若出偏差将无法区分是哪一边引入的。**测试**：`VisibilityPolicy` 的 C++ 单测覆盖六输入真值表（含「未定」行与服务不可用例外）、缓冲期内取消、淡出中反向、冷启动不播动画；**不加** QML 端到端（测动画中途的 `opacity` 数值要靠 `qWait` 卡时序，是典型 flaky 来源，而 QML 侧只剩「把 bool 绑到 `Behavior`」一行）。**毛玻璃：自绘只在自动隐藏开启时进行**（2026-09-05 实测更正）。此前依据「默认主题下毛玻璃是死代码」采纳了无条件自绘，那个结论抽错了样本——只查了 `air`/`default`/`oxygen`/`breeze-*`，而用户实际在用的 ChromeOS 主题的 `widgets/background.svg` 带 **26 个 `blurred-*` 元素**。shell 在 `BasicAppletContainer.qml` 判断 `hasElementPrefix("blurred")` 为真时，会把 frame 切到 `prefix: "blurred"` 并叠一个 `MultiEffect` **在 QML 里采样壁纸自行模糊**、用主题的 `blurred-mask` 裁切（不是 KWin：KWin 对桌面窗口 `shouldBlur()` 返回 false，见 §末「毛玻璃底板」一行）。该效果依赖 `appletContainer.Window.window` 与容器内部结构，**applet 内部无法复制**——无条件自绘会把带毛玻璃的主题底板变成一块实心暗块。**因此底板在动画边界换手**：静止可见时由 shell 持有（毛玻璃完好，也就是用户实际注视它的每一刻）；退场**先**转交自绘再开始淡出（否则文字会在一块不会淡的底板里淡）；隐藏中与淡入中由自绘持有；淡入结束后一个与淡入等长的定时器把它交回 shell。两个消费方（`Plasmoid.backgroundHints` 与 `LyricsView.ownsPlate`）读**同一个属性**，换手因此是一次绑定求值，不会出现两者相差一帧导致底板叠加或消失。「动画实际会不会播、播多久」也一并收敛到 `main.qml` 的 `effectiveFadeMs` 一处——定时器必须与淡入等长，两处各自判断动画状态正是会把底板卡在错误一方的成因。自绘时用 `prefix: "blurred"`（即 shell 会选的同一套帧图元；实测 ChromeOS 上朴素帧中心像素 alpha 244、blurred 帧 152，margins 两者同为 24），所以换手不产生任何位移，**唯一可见的差异是透过底板的壁纸从清晰变模糊**——已与用户确认可接受。关闭自动隐藏（默认）时 shell 全程持有。**未被测试覆盖**：换手逻辑在 `main.qml`，而它是 `PlasmoidItem`，QML 测试套件无法实例化；该文件唯一的静态检查是 Qt6 的 qmllint（见 CLAUDE.md——`PATH` 上那个是 Qt5 的，对 Qt6 QML 空转）。**`Paused` 的实测后果**（同日）：Chrome + plasma-browser-integration 暂停与队列放完都停在 `Paused` 且 metadata 完整，所以在该播放器上自动隐藏几乎只有关掉整个 Chrome 才触发。已向用户确认后**维持 `Paused` 不算无歌曲**，不做逃生口。**顺带发现（不在本次范围）**：面板里「背景 - Plasma 主题」实际什么都不画，观感等同 `none`，此处仅记录、不顺手改 |
| 41 | **全局歌词进度调整**：新增独立开关（**默认关**——关闭时逐字节保持 per-track 行为不变），开启后所有歌曲共用一个全局偏移，而非每首歌各自一份。开关与偏移值全局一份、跨所有部件实例共享，符号沿用既有约定（正值 = 歌词延后）。**存储落点选 `LyricStore`（SQLite）而非后端 INI 或前端 kcfg**：INI 那份的契约是「保存后要重启 daemon」（决策 18），kcfg 那份是「每实例各自一份」（同样是决策 18），而这个值必须**立即**跨所有实例生效——只有 SQLite 已经具备这个能力：`lyricsource.cpp` 每 2 秒轮询它同步 per-track 共享偏移（决策 18 认定这是仓库里**唯一**的跨实例活体同步通路），本功能直接复用同一条通路，不另开一条。**快照 `lyric.offsetMs` 的语义因此固定为「per-track 原始值」**（见 §2.2）：daemon 不感知这个开关，照旧只读写 per-track 偏移；全局模式下前端本地忽略快照里的这个字段，改用轮询到的全局值——`LyricSource` 内部因此拆成 `m_trackOffsetMs`（快照/per-track 表来的原始值，随时保持更新）与 `m_offsetMs`（`advance()` 实际用的"生效值"，全局模式下等于全局值），关闭开关时 `m_trackOffsetMs` 一直没被覆盖，读数**原样弹回**，per-track 数据本身从未被删除或改写。**±10000 ms 截断是存储层的不变量、读写两侧都夹**：只在写路径夹会让绕过 setter 的写入（脏改库、未来的迁移脚本、bug）读出界外值，此时前端 SpinBox 的 `from`/`to` 已经用 `LyricStore::maximumGlobalOffsetMs()` 钉死了范围，两边必须说同一个数字，因此 QML 里**不重复写字面量** 10000，一律读这个静态方法。**配置页 `ConfigGlobal.qml` 不像其余四个 tab 那样用 kcfg 的 `cfg_` 自动绑定**（这个值不落 kcfg），而是走 KDE 官方为非 kcfg 页面准备的钩子：`AppletConfiguration.qml` 的 Apply/OK 路径调用当前页的 `saveConfig()`，并把页面的 `unsavedChanges` 属性接进 Apply 按钮的可用性判断（实测 Plasma 6.7.4 `AppletConfiguration.qml:51-56,104-105,160-161,197-199`）——`ConfigBackend.qml` 那个自制的「保存」按钮是因为它写的是 daemon 的 INI、天然独立于对话框生命周期，这次的值必须能被 Apply/OK 一并提交，不能照抄。**测试**：`LyricStore` 的全局键读写/截断/持久化覆盖见提交①；`LyricSource` 的 C++ 单测新增一个仅供测试使用的 store 路径注入口（不 Q_INVOKABLE、不 Q_PROPERTY，QML 侧不可见），覆盖开关开/关时 `offsetMs` 取值、菜单调整写向哪张表、2 秒轮询把另一实例的改动同步过来、关闭开关后 per-track 值原样恢复、全局模式下 `canAdjustOffset` 放宽为仅需 `serviceAvailable`。新配置页不进 `tst_appearance.qml`（同一 QML 测试套件无法安全接触真实 SQLite 路径，`ConfigBackend.qml` 已是先例）。**已接受的残留：保存失败的可见反馈只在 Apply 路径成立**——`ConfigGlobal.qml` 的 `saveConfig()` 接住 `GlobalConfig::save()` 的返回值、失败时置一条 `Kirigami.MessageType.Error` 的 `InlineMessage`，但官方 `unsavedChanges` + `saveConfig()` 钩子本身不含失败回传通道：`AppletConfiguration.qml` 的 OK 按钮（`:454-459`，回车键 `:480` 走同一路径）调用 `applyAction.trigger()` 后**无条件** `close()`；而 `applyAction`（`:462-468`）在 `:467` **无条件**把 `applyButton.enabled` 置 `false`，早于这次 `saveConfig()` 的返回值能被看到，于是 `closing()`（`:42-47`）那条"有未保存改动就不许关"的护栏必然放行——OK/回车路径下用户点下去、对话框已经在关闭，看不见那条错误提示。彻底堵住这条路径需要一条生命周期独立于配置对话框的通知通道（如系统通知），那要新增依赖、`.notifyrc`、一整套文案，而触发前提仅是磁盘满/权限损坏/库损坏这类低概率场景，判定超出本次范围、不做；Apply 路径的错误条保留（零成本、严格优于没有）。**决策 55 已替代本条关于前端轮询 SQLite 与快照偏移语义的实现方式；本条的存储选择、范围夹取与同步保存失败反馈仍有效。** |
| 69 | **逐字渲染「抬升发光」**：颜色**整词切换**（词到自己的 `startMs` 整词换色，词内不插值），**抬升与提亮做词内插值**（决策 73 起按绝对时间走，并延续到词结束之后），包络 `sin(p·π)`（两端为零、词中最大，两个边界都不跳变；**2026-09-13 被决策 73 的时间基弹簧取代**）。三档颜色（未唱／正在唱／已唱）+ 第二行颜色各自独立可配。**数据通路**：`LyricSource` 新增只读的 `currentWords`（`[{startMs,endMs,text}]`）、`currentRomanization` 与 `Q_INVOKABLE lyricPositionMs()`（**已扣掉偏移**，可直接与词的时间比较）；**驱动是 QML `FrameAnimation` 逐帧拉取，不加 16 ms 定时器**——这正是决策 38 末尾定下的结论，帧驱动在窗口不渲染时自停。该动画仅在「本行有词 ∧ `Playing` ∧（面板 ‖ 未被自动隐藏）」时运行，暂停即冻结在最后一帧。**顺带修掉一个既有缺陷**：`advance()` 只在**行号**变化时发 `currentLineChanged`，于是切歌后新歌当前行号与旧歌相同（两首都停在第 0 行）时一个信号都不发，QML 绑定继续显示上一首——getter 早已返回新内容，所以轮询 getter 的测试永远抓不到它；改为 `m_lines` 内容变化也通知。**提亮不能用 `Qt.lighter()`**：它抬的是 HSV value，而这些颜色本就压在壁纸上、value 已是 1.0，实测那样做出来的开关**肉眼毫无变化**；改为向不透明白插值，RGB 与 alpha 同时移动，三档颜色的默认值（`#8cfffaf5`／`#e6fffaf5`／`#c4fffaf5`）也据此留出余量。**抬升自带行高余量**：字体自身内容盒（ascent + descent + leading，`FontMetrics.height`）比 `fontSize × lineHeightFactor` 算出的行盒更高——fontSize 34、1.25 行盒下，`AlignVCenter` 把这份差额平分到行盒上下两侧，上方落入空白，可以被抬升借用，但这份自然余量与抬升本身是两回事：0.14em 抬升约 4.8 px，所以行盒在这份自然余量之外**另外**预留 `ceil(fontSize × liftEm)`（决策 73 起含回弹系数：`× (1 + liftOvershoot)`）；下方那半差额此前一直被 `root` 的 `clip: true` 直接裁掉，是决策 73 记录的「100% 行高下压被裁，已接受」背后的真实机制，且与是否开启抬升无关——详见决策 73 2026-09-13 的第二次修复：裁剪改由内层 `clipper` 按字体实测余量承担，`root` 不再直接 `clip: true`。该 `liftHeadroom` 余量**只看抬升开关，不看本曲有没有词**——否则播放在有词源与无词源之间移动时文字基线会上下跳。行高系数另做可配项（100%–200%，默认 125%），同时也是光晕的活动空间。**2026-09-13 第三次修订：桌面下限抬到 125%，面板不变**：决策 73 的 clipper 修复解决了字形被裁的问题，但 100% 行高下上一行的下伸部会伸进下一行的墨迹带，是另一件事——**这个重叠量与 `glyphSpill` 不是同一个量**：`glyphSpill` 是 clipper 单侧放宽给字体自身内容盒的余量，这里说的是原文行下伸部墨迹的最低点与第二行墨迹最高点之间的竖直重叠，两者刚好都在个位数像素级但互不相关，别把二者配对着看。实测（离屏渲染 `LyricBlock`，fontSize 39、`lineHeightFactor` 1.00、`liftEnabled` 开、`liftEm` 0.15、`positionMs` 取全部词已落定之后，原文 `"your own?"`——`y` 提供下伸部——第二行 `"其实我和你也一样"`，按两行各自的颜色区分像素后取各自墨迹的行范围：`root.height` 46、`glyphSpill` 9、`liftHeadroom` 7，原文墨迹 y 12→50、第二行墨迹 y 48→84）：竖直重叠约 **3 px**。水平方向是否真的撞上，取决于那一列恰好落在哪个字上——竖直有重叠不等于画面上一定压字。这个数字对抗锯齿阈值与渲染后端敏感：三次独立测量分别得到 3、3、4 px（前两次 offscreen 软件后端，qa-2 用 `xvfb-run` 真实 GL 测得 4），差异来自取墨迹边界时用的阈值与后端本身，±1 px 视为正常，不代表测量矛盾。用户裁定：只抬控件下限 + 渲染时 clamp 兜底，不做配置迁移、不动 `main.xml`（`desktopLineHeight`／`panelLineHeight` 的默认值本来就是 125）。**机制**：`LyricsView` 新增 `lineHeightMinPercent`（默认 100），`lineHeightFactor` 改为 `Math.max(root.lineHeightMinPercent, root.lineHeightPercent) / 100`；`main.qml` 的桌面实例（`fullRepresentation`）设 `lineHeightMinPercent: 125`，面板实例（`compactRepresentation`）不设、沿用默认 100。clamp 没有写进 `main.qml` 本身——它是 `PlasmoidItem`，QML 测试套件无法实例化（CLAUDE.md 已记），写在可实例化的 `LyricsView` 里才可测。控件下限同步抬高：`AppearanceSection` 的行高 SpinBox 新增 `lineHeightMin` 属性（默认 100，面板页不覆盖），`ConfigDesktopAppearance.qml` 传 `lineHeightMin: 125`。**两处 125 是两个字面量，不共享常量**（2026-09-14 用户裁定：曾短暂引入 `ui/LineHeightPolicy.js` 共享导出 `desktopLineHeightMinPercent`，配置页跨目录 `import "../LineHeightPolicy.js"` 在本仓库没有先例——`TextPolicy.js` 只被部件本体同目录导入过——为验证这条路径在配置对话框的宿主进程里能否解析，三个 agent 各自重复测过一遍，成本和「共享一个整数」这件事本身不成比例，已撤销）。两处并不对等，这是不共享也没关系的原因：`LyricsView` 的 clamp 是**执行**，SpinBox 的 `from` 是**UI 挡板**，拦住用户去选一个反正会被忽略的值。单侧漂移不会静默失败——SpinBox 停在 100、clamp 在 125 时，用户选 110 会立刻看到渲染成 125，两个数字对不上这件事本身就会被看见；SpinBox 在 125、clamp 缺失时，新配置本来就选不到 125 以下，同样不会有「选了 110 却按 110 渲染」这种悄悄错位。只有两处**同时**被改错才会让下限静默消失，而那种情形（把两处一起删掉）共享常量同样救不了——它只防得住「改了一处忘了另一处」，防不住「两处都被拿掉」。两个字面量各自写一条互指注释（哪一处对应哪一处），比引入一条本仓库没有先例的跨目录脚本导入更相称。**存量配置不会自愈，这是核实过的**（2026-09-14，qa-2 证伪了「打开设置对话框保存一次就会写回 125」这条最初的猜测）：`cfg_desktopLineHeight` 是普通属性，不是 `property alias`；`AppearanceSection.qml` 里只有 `SpinBox.onValueModified` 才会写回 `cfg_*`，而 `onValueModified` 按 Qt 的定义只在用户真正交互（拖动、输入、点箭头）时触发；`value: root.lineHeightPercent` 是配置到控件的单向绑定，QQC2 把越界值钳制到 125 只影响**显示**，不会反向写回绑定源；真实 Plasma 壳（`AppletConfiguration.qml`）的 `saveConfig()` 保存的是 `cfg_*` 属性当前持有的值，不是控件当下显示的值。实测：把 `cfg_desktopLineHeight` 设成 100、完全不碰这个 SpinBox，等任意多个事件循环后仍是 100，从未变成 125。**真相**：只要用户没有亲手调整过这一项，存量值就会一直停在旧值——哪怕同一次对话框里改了别的设置并点了 Apply，这个没被碰过的 `cfg_*` 也会原样写回。**不影响渲染**：`LyricsView` 的 `Math.max(lineHeightMinPercent, lineHeightPercent)` clamp 是独立生效的，存量值多低都不会让画面回到 100% 行高；这也正是为什么下限不需要配置迁移代码——迁移要解决的是「存量值本身要不要改」，而这里改的是「存量值不管多低都不再影响渲染」。**警告**：不要以「存量值反正会自愈到 125」为由移除这处渲染期 clamp——上面已经证明存量值并不会自愈，移除 clamp 会在从未手动碰过这个控件的用户身上重新引入决策 69 本节开头说的那种行间重叠。**不对称是有意的**：面板的高度不归这个部件管，抬下限是在向外长，风险落在面板窗口边界上；而面板本就强制关闭抬升（见下文），100% 行高下从来没有为抬升留出余量的需求。**未被任何测试覆盖：`main.qml` 里这两行接线本身**（`fullRepresentation` 传 `lineHeightMinPercent: 125`、`compactRepresentation` 不传）。`main.qml` 是 `PlasmoidItem`，QML 测试套件无法实例化，唯一覆盖它的是 qmllint，而 qmllint 只挡得住拼写错误，挡不住整行接线被删——**在当前代码（`LineHeightPolicy.js` 已拆除、两处都是字面量）上重新实测**：把 `main.qml` 这行 `lineHeightMinPercent: 125` **整行删掉**，`ctest` 依旧 **31/31 全过**（`LyricsView` 自己的 clamp 测试测的是它自身的默认值与显式传入值是否正确参与 `Math.max`，不检查 `main.qml` 有没有把这个属性接上去）；仓库 CI 实际跑的那条命令（`.github/workflows/ci.yml:146-152`，`qmllint --unqualified disable --max-warnings 0 -I build/bin …`）同样 **exit 0**，且**没有任何输出**——早前「删掉这行会留下一条 Info 级 `[unused-imports]`」的证据是在共享常量、`main.qml` 还 `import` 着 `LineHeightPolicy.js` 时测的，那条 import 现在已经不存在，没有导入可以变得没人用，那条 Info 诊断也就不会再出现，纯粹的 exit 0 静默放行。对照组：把属性名拼错成 `lineHeightMinPercen:`，同一条 CI 命令改为 **exit 255**，报 `[missing-property]`（`Could not find property "lineHeightMinPercen"`）。边界因此划在这里：**属性名拼错会被抓到，整行接线被静默删掉不会**——这个 125% 下限可以被悄悄移除而 `ctest` 与 CI 双双绿灯，没有任何自动化信号能发现。**面板强制不抬升且不设面板键**（沿用决策 40 砍掉 `panelHideAnimationMs` 的同一条理由：面板高度不归部件管，留着就是一个转起来毫无效果的开关），面板外观页那一行显示为**禁用并写明原因**。**`wrap` 不进逐字**：词是一条横排，positioner 叠两排复现不了 `Text.WordWrap` 那个居中两行块，该档退回整行——与「无词级数据退回整行」同一形状。**`fit`／`elide` 在「怎么缩都放不下」时退回整行**（2026-09-12 qa-d-1 实测，用户裁定）：`wordPixelSize` 以 `minimumPixelSize`（`round(fontSize×0.6)`）为下限，所以整行在最小字号下仍比部件宽时，`wordRow` 比 item 宽、`x` 钉在 0、`clip` 直接切掉尾巴——**正在唱的词就这么没了，既没有省略号也没有滚动把它带回来**，而它替代的整行路径在同样输入下会缩小并省略。触发条件与宽度无关，是「整行原始宽度 > 约 1.7 倍可用宽度」（`fontSize / round(fontSize×0.6)`），所以桌面宽部件上罕见、面板里常见。**裁定是退回整行**：`fit` 的契约是缩小、`elide` 的契约是截断，两者都没承诺「会动」，为了逐字而让字跑出可视区是拿一个模式去兑现另一个模式的承诺。**已接受的代价**：长行在这两档下失去逐字，主要影响面板；`marquee` 是想在长行上要逐字的人的逃生口，它本来就会滚动，任何长度都能把当前词带到眼前。判据写成只读 `metrics.width × minimumPixelSize / fontSize <= width`，**刻意只用「任何 delegate 存在之前就已经有值」的量**——写成 `wordRow.width > root.width` 会死循环，因为 Repeater 的 model 依赖这个属性。**`marqueeRunning` 拆成 `marqueeWanted && visible`**（同日，同一轮）：`visible` 是祖先合并的，在整个 QML 测试套件里恒为 false，所以任何含它的条件在那里**不可观测**——实测把 `marqueeApplies`、`!wordMode`、`!restarting`、`longDuration` 全部满足后 `marqueeRunning` 仍是 false，于是断言它的测试删掉条件里的任意一项都照样通过（`!wordMode` 与 `visible` 两项都被证实不承重）。可测的部分收进 `marqueeWanted` 由测试断言，`visible` 另用一个真正 show 出来的 `Window` 覆盖。**`fit` 用 `TextMetrics` 整行测量后算统一 pixelSize**，不能每词各自 `Text.HorizontalFit`（那是每词按自己的宽度缩，且 `Text` 不提供读回整行实际字号的接口）。**描边分两套实现**：整行仍是 8 份偏移副本（决策 25/32/36 不变），**逐字改用 `Text` 自带的 `style: Text.Outline`**。实测分两轮，35 token 的 CJK 行、offscreen 软件后端、每帧改位置。**只隔离描边变量的对照组件**：逐帧 8 副本/词 0.151 ms、`Text.Outline` 0.086 ms、无描边 0.044 ms；换行重建 9.5 / 2.7 / 2.08 ms。**真实 `LyricLine` 上复测**（开描边）：换行重建 8 副本/词 **15.0 ms**、`Text.Outline` **6.9 ms**（**省下 8.1 ms**，比对照组件暗示的 6.8 ms 更多），Text 项 326 对 38。真实组件比对照组件重约 3 倍（每词多一层 delegate、Loader 与状态属性，还要重排 `TextMetrics`），**但两轮的排序与量级结论一致**，定案依据是换行那一组——尖峰恰好落在换行那一帧，也就是眼睛正看着的那一帧。顺带的对照：整行路径开描边时换行重建本身就是 **9.3 ms**（8 份副本要为新文本重新排版整行），所以逐字用 `Text.Outline` 在换行开销上**反而低于今天的整行描边**。代价是逐字描边比 8 副本略细，同屏的第二行（整行路径）仍是 8 副本。**跑马灯逐字改为跟随当前词**：新增 `wordScrollOffset`（绑定 + `Behavior`），与 `marqueeOffset`（动画写、`x` 绑定读）**分开两个属性**——一个属性不能既被绑定又被动画拥有，而 `LyricLine.qml` 注释里那两个坑（动画直写属性毁绑定、重启不能用 `restart()`）一条都不绕。`activeWordItem` 必须依赖 **`Repeater.count`** 而非 `words.length`：`itemAt()` 是普通函数，只随同读的属性重算，依赖 `words.length` 时若首次求值早于 delegate 创建就永远停在 `null`（当前词不是首词的行必然命中）。**`MultiEffect` 的两条实测事实**（模糊光晕，默认关，UI 上写明开销）：① 它**隐藏 source**，所以清晰字形要另画一份；② `autoPaddingEnabled` 与 `paddingRect` 都是把 source **缩放进自身几何**而不是扩大采样区，实测字形被缩小；正确做法是让 **source 自己带 padding**、effect 与之同尺寸且 `autoPaddingEnabled: false`。③ 开销实测（10 词行，`xvfb-run` 的 llvmpipe 软件 GL，整帧含回读）：关 **16.6／17.1 ms**、开 **20.9／21.8 ms**，即单个高亮词的光晕约 **+4.5 ms/帧（+26%）**（决策 73 起光晕随包络存活、并发无上限，快唱段会同时多个）；真实 GPU 上会低得多，但该数字足以支撑「默认关 + UI 写明开销」。另：`QT_QPA_PLATFORM=offscreen` 走的是**软件后端**（`Loading backend software`），着色器根本不跑，光晕这类效果只能在有 GL 上下文的环境（如 `xvfb-run`）验证。**逐字设一个总开关，每形态一个键，默认开**（默认开的理由不是「这是本项目的目标」，而是可核查的结构性事实：`wordClockRunning` 以 `effectiveWords.length > 0` 为前提，**没有词级时间轴的曲目上这个时钟根本不武装**，所以默认开在今天零成本，日后也只在开了这个开关的那些曲目上才有成本）：关掉它**清空词 Repeater 的 model，而这同时就是停掉那个逐帧 `FrameAnimation` 的条件**——一个闸门同时关掉两件事，不是「看不见但还在跑」。理由不是每帧耗时（真实组件上关掉全部视觉开关仍要 0.063 ms/帧、换行 6.5 ms，对整行的 0.016 ms 与 1.1 ms；绝对值不威胁帧预算），而是**唤醒次数**：**决策 38** 把四分钟 60 行的歌从 7200 次定时器唤醒压到约 120 次，并把这一点记为该设计的目的；而 `FrameAnimation` 是**跟着显示器刷新率走**的，所以这个数字是一个区间而不是一个值：60 Hz 上约 **14400 帧**，144 Hz 上实测 **144 次/秒**、同一首歌约 **34560 次**——**是决策 38 要拿掉的 7200 次的 4.8 倍**，不是 2 倍。只记 60 Hz 那个下限会让高刷屏上的读者低估整条理由，而用户本机正是高刷。**关键在于既有的开关一个都替代不了它**：把抬升、提亮关掉并把三档颜色设成同色只是让观感归于平静，时钟照跑——代码无从得知三个颜色相等。只有清空 model 才能退回决策 38 的唤醒曲线。**唤醒这条理由不依赖描边决定**（`Text.Outline` 省的是换行那一帧的 CPU，实测两种描边下唤醒都是 144 次/秒，动不到「每秒一个刷新率」这条轴）；反过来，若日后把逐字描边改回每词 8 副本，本开关会**再多一条独立理由**——届时的换行尖峰 15.0 ms 相对今天整行描边的 9.3 ms 多出约 **5.5 ms**（不是 15.0 ms，那条基线就记在本条前面几句），而关掉逐字是躲开它的现成办法之一；直接的补救是**不做那个改动**，本分支用 `Text.Outline` 正是如此。面板尤甚：面板高度不归部件管，换行尖峰在窄面板上最扎眼，而本开关正是每形态一个键。**第二行三选一用两个键**（既有的 `*ShowTranslation` 保持「显示第二行」原义 + 新增 `*SecondLineSource`），不做迁移——存量实例的开关值原样沿用。**选了罗马音而该行没有，第二行即为空，不回落翻译**：绝大多数源本就不提供罗马音，回落会让同一个设置在不同曲目上含义不同 |
| 73 | **逐字抬升改为时间基弹簧，取代决策 69 的 `sin(p·π)` 包络**（2026-09-13，四轮访谈裁定；试验台与结论页 https://claude.ai/code/artifact/5abbaec6-1970-41a7-b85e-756fec79bd1b ）。**起因**是反馈「桌面形态字升起时阻尼感不足」。**数据**（本机 `lyrics.db`，22 首带词级时间轴的曲目，qq 16／amll 6，8 996 词、1 022 行；访谈当日上午为 8 982／1 008，缓存随播放增长，中位数与各占比不变）：词时长中位数 **230 ms**（p25 172、p75 380、p5 50、p95 1000），**55% 的词短于 250 ms**、14.7% 短于 150 ms；**92.5% 的相邻词无空隙**（有空隙者 p95 也只有 81 ms）；34 px 字号下默认 14% 抬升只有 **4.8 px**。`sin(p·π)` 以词内进度为变量、两端对称、词中最高，230 ms 的词就是升 115 ms 再落 115 ms，字从不「到位停住」——抽动感来自短词而非长词，所以只改进度基曲线的形状治不了病。**裁定：包络改为二阶阻尼弹簧的阶跃响应，以绝对毫秒为变量。**自 `startMs` 起向 `liftEm` 升起，**到位时间 150 ms**（首个峰值时刻），**回弹 10%**（峰值 1.10 × liftEm，反推 ζ ≈ 0.59），到位后**保持**至 `endMs`；自 `endMs` 起从当时高度以同一 ζ 回落，**回落时间 300 ms**（最低点时刻，基线下约 10% × 放下时高度，默认下 0.48 px），约 630 ms 后包络在 1% 以内并**硬置为 0**——不置零则每个已唱词每帧都带一个亚像素 y 变化，整行永远在重绘。回落**不延续上升段的速度**：是「放下时高度 × (1 − 阶跃响应)」而非两个阶跃响应叠加，短词才不会被跑得比上升还快的回落压到基线以下。**短词接受部分上升**：50 ms 的词升到约 47% 即放下，不设阈值、不延长（延长会让 50 ms 的词动 650 ms，与歌声脱节）。三个常量 `liftArriveMs`／`liftOvershoot`／`liftReleaseMs` 是 `LyricLine` 的属性、写死默认值、**不进配置骨架**——桌面外观页已有五项逐字设置，用户调过一次后先按这组出货，若反馈只争回弹再加一个滑块。**仍是 `positionMs` 的纯函数**：闭式解由决策 69 的 `FrameAnimation` 逐帧拉取，暂停即冻结、拖动即时正确、唤醒闸门不变；**不用 QML `SpringAnimation`**——它带自己的定时器与状态，三条全失守，且 `Behavior` 与动画直写属性正是 `LyricLine.qml` 注释里反复踩过的坑。**余量**：顶部改为 `ceil(fontSize × liftEm × (1 + liftOvershoot))`（默认 6 px，此前 5 px；仍只看抬升开关、不看本曲有无词），否则峰值被 `clip` 削平。**底部**当初裁定为不预留（用户裁定，非推荐项），理由写的是「125% 行高的行盒下方约有 1.5 px 自然余量且随字号等比放大，100% 行高时下压会被裁掉，已接受」——**这条理由本身是错的，2026-09-13 第二次修复时证伪**（实测 Noto Sans CJK SC：`contentHeight ≈ 1.471 × fontSize`，不是假设的 `1.25 × fontSize`）：真正决定是否被裁的不是回落下压（默认仅 0.48 px），而是字体自身内容盒（ascent + descent + leading）比行盒高出的部分——`AlignVCenter` 把这份差额平分到行盒上下，上方落入空白，下方是实打实的字形墨迹（主要是 descent），**125% 行高下这份自然余量同样是负的**（fontSize 39 实测上下各溢出约 4.5 px），并非 1.5 px 正余量；这与回落下压是两件互不相关的事，关掉抬升、下压归零，底部一样被裁。**修复**：把 `clip` 从 `root` 移到 `LyricLine.qml` 新增的内层 `Item { id: clipper }`，其上下各撑出 `glyphSpill = max(0, (ceil(FontMetrics.height) − (root.height − liftHeadroom)) / 2)`——按字体实测的 `FontMetrics.height` 推导，不依赖某个假设的行高系数，因此与具体字体无关；横向裁剪范围（`[0, root.width]`）不变，marquee/fit/elide 依赖的像素级裁剪不受影响。回落下压（0.48 px 默认）仍不单独预留，因为它相对 `glyphSpill` 是可忽略的量。**代价：光晕随 clipper 放宽一并渗出**（2026-09-13 第二次修复的副作用，qa-1 用 `xvfb-run` + 真实 GL 逐像素比对 `LyricBlock`——`offscreen` 是软件后端、着色器不跑，测不出这个）：`blurGlow` 的模糊源 `haloSource` 与清晰字形 `glyph` 是同一 delegate 下的兄弟节点，`clipper` 的裁剪边界上下各放宽 `glyphSpill` 是为了放行字形墨迹，光晕跟着获得了同一份余量。fontSize 39、100% 行高、抬升／提亮／光晕全开下实测：原文行旧裁剪线之下、第二行墨迹开始之前那条空白带（画布 y=66–68），修复前逐像素 max brightness = 0（纯黑，裁得干净），修复后 max = 177/765（约 23%），逐行均值 10–20；该配置下实测可见渗入约 3 px，理论上限即当帧的 `glyphSpill`——qa-1 这次渲染环境的字体读数约 9 px（该环境 `FontMetrics.height ≈ 57`，`(57 − 39) / 2`），量级上与 Noto Sans CJK SC 的公式值 9.5 px（下段引用的那个数）一致，但不是同一次测量，读数出自不同字体；实际渗入量随词的抬升相位变化。**裁定：接受**——渗出的是渐隐的模糊辉光而非字形墨迹本身；要单独堵住它，需要把光晕层整个搬出 `wordRow`、脱离 `glyphSpill` 的裁剪范围另起一套，为一个默认关闭的装饰性开关加这套机械不相称；旧行为保留的代价是模糊光晕被一条笔直硬边齐平切断，同样不美观。**范围有限**：下一个提交把桌面行高下限抬到 125% 后，Noto Sans CJK SC 的 `glyphSpill` 公式值从 100% 行高下的 9.5 px 降到 125% 下约 4.5 px、原文行与第二行间距另增约 10 px，这条代价基本自消；**面板仍维持 100% 行高下限且有独立的 `panelWordBlurGlow` 开关，这条代价在面板上照旧成立**。（qa-1 同时测到原文行上方、以及第二行自身下边界处的差异，那是第二行下伸部同样不再被裁的正面效果，不计入这条代价。）**光晕在词模式下按满字号推导余量，比实际需要的更宽**（qa-2 在本次上一轮终审中提出）：`glyphSpill` 由 `root.fontSize` 经 `FontMetrics` 推导，而词模式 `fit` 档的 `wordPixelSize` 会把实际渲染字号压得更小（决策 69：判据是「整行原始宽度 > 约 1.7 倍可用宽度」，桌面宽部件上罕见、面板里常见）——字缩小了，`glyphSpill` 却仍按满字号算，光晕因此会比「恰好够用」多渗出一些。方向上安全（多留的是余量，不会导致裁字），但面板正是上面这条光晕渗入代价唯一不随本次「桌面下限抬到 125%」自消的地方，二者叠加在一起，如实记为已知代价，不做修正。**提亮取同一包络截到 0–1**：峰值不过亮、下压不变暗；面板不抬升，这是面板唯一的变化——亮度从「一闪」变为「亮着直到词结束、300 ms 内暗回」。**光晕随包络存活，但窗口比字形短**：`Loader` 的存活条件由「正在唱」改为「起唱后至 `endMs + liftReleaseMs`」（`haloAlive`），按**时间窗**而非包络阈值判断，因为回落会多次过零，按值判断会在每次过零处拆掉再重建 `MultiEffect`；窗口取 `liftReleaseMs` 而非 `liftSettleMs`，是因为回落在约 `0.7 × liftReleaseMs`（210 ms）首次穿零，此后截到 0–1 的 glow 不再超过约 1%，活到落定点的光晕后半生是看不见的 `MultiEffect`（qa-2 2026-09-13 量得：628 ms 窗口里 glow > 0.02 只占 32%）。取整到 `liftReleaseMs` 而不是精确的 `0.7 × liftReleaseMs`，是为了不引入第四个系数，代价是窗口最后约 90 ms（穿零到 300 ms）glow 恒为 0 却仍有实例，已接受。**并发没有上限，最初写的「同屏最多约三个」是错的**（qa-2 用本机缓存证伪；旧 `LyricLine.qml` 注释「Only ever one instance」同时作废）：对本机 1022 行逐行取峰值，300 ms 光晕窗口下中位峰值 3、85.9% 的行 ≥ 3、20.2% 的行 ≥ 4、4.6% 的行 ≥ 5，超过 3 个的时间占比约 1%；上界出现在一行 22 ms 均分时长的退化时间轴（`夢(シッポ)を忘れちゃいないかね！？`，QQ 359097617），15 个同时存活；字形自身的 628 ms 在飞窗口下中位峰值 4、上界 18。换行淡出那一帧再加上一行仍在回落的词（各一个实例，约 260 ms 后随 `previous` 块拆掉）。零长 token（1.4% 的词，多为尾随标点）包络恒为 0，被排除在 `inFlight` 之外，否则每个会白养一个 opacity 为 0 的实例整个窗口。配置页文案改为「快唱段会同时出现多个」，不给数字上界；默认关不变。是否加并发上限（按 `startMs` 取最近 N 个）留待反馈，其代价是词掉出前 N 时一次确定性的拆建。**回落也回弹是用户裁定的非推荐项**（推荐是临界阻尼、不压过基线）。**颜色仍在 `endMs` 整词切换**（决策 69 不变），所以已唱色的字还在回落约 600 ms，与下一词的升起重叠——这是设计意图；`AnimatedLyric` 里「换行时上一行没有任何东西在动」的注释随之改写。**否决的方案**：B 进度基非对称包络（改动最小，但 50 ms 的词照样抽动）；C 时间基上升、锚定词尾回落（切色时已落回，但 170 ms 以下的词被回落门压成小鼓包，而那正是问题所在的一段）；D 击发后自由衰减（不看 `endMs`、不保持，长词唱到一半字已静止）。**测试**：`test_brighteningMovesTheColorOfTheWordBeingSung` 原本硬编码 sin 在词中点为 1、词尾回基色，改为词尾仍满亮、最低点处颜色**恰等于**已唱色、落定后不变；新增 `test_liftEnvelopeIsATimeBasedSpringNotAProgressCurve`（起唱前 0、峰值 1 + overshoot、500 ms 内稳定、`envelope(150, 0, 5000) === envelope(150, 0, 1000)` 钉住时间基、最低点 −overshoot、落定处**恰为 0**、短词部分上升与按份额下压、字形 y 跟随）；`test_liftReservesItsOwnHeadroomAndOnlyWhenEnabled` 的 `ceil(40 × liftEm)` 改为含回弹系数即 7；光晕用例改名 `test_blurredGlowIsOffByDefaultAndFollowsEachWordUntilItSettles`，1200 ms 处两个实例、各自窗口关闭后递减为 0，零长 token 不建实例。qa-2 破坏矩阵补的三个缺口：峰值处断言提亮截到 1（不截为 0.8947）、50 ms 处断言 0.4734 钉住 ζ/√(1−ζ²) 系数（去掉系数读 0.530）、三个常量与 `liftSettleMs ≈ 628` 直接 `compare`。减动画路径（`envelopesAnimate` 为假时包络置平、光晕不创建）沿用决策 69 的未覆盖说明 |
| 74 | **模拟逐字**：歌词源整篇不提供词级时间轴时，可选把每行按字符切成 token、按字符权重均分该行时长，合成与真逐字同形的 `LyricWord` 数组，走决策 69/73 的完整渲染链（不做扫光遮罩、不做短语级切分）。桌面与面板各一个 Bool 开关（`desktopWordByWordSynthetic`／`panelWordByWordSynthetic`，默认关），不把既有 `*WordByWord` 改成三态。**判定是文档级，不是行级**：依据是用户缓存里 204 个 `hasWords=1` 的文档全部 204/204 全行覆盖、0 个混合文档——真实数据里不存在"部分行带词、部分行不带"的情况，所以没有必要在行级做判断，`LyricSource` 在快照应用后缓存一个 `m_documentHasWords`（= 是否存在任意一行带非空 `words`），只要文档里有一行带真词，同一文档的其余行永远不合成，即使那一行自己没有词（纯配乐间奏行）。**行时长直接用 `endMs - startMs`，不做每字上限、不做启发式修正**：依据是实测每字时长 p50 288 ms（真词中位 230 ms），旧的 10 秒/行封顶（`finalizeEndTimes`）撞线的行占 6.94%，但按撞线后的行时长与该行字符数换算，最差约 1000 ms/字，真正 ≥2000 ms/字 的只占 0.88%——不足以为每字设一个封顶，写在这里是防止下一个人看到 6.94% 这个数字又重提一次。**分词**：CJK（`QChar::script()` 归为 Han／Hiragana／Katakana／Hangul／Bopomofo）逐字自成一个 token；其余非空白字符（拉丁、数字、标点）累积成一个 token，遇空白或 CJK 收口；空白永不单独成 token，整段并入前一个 token 的尾部（行首空白防御性地并入下一个 token 头部，实测语料 0 行命中）。**权重全部为 1，空白也算**：现有的抬升/提亮/光晕本就不看字宽（拉丁 `i` 和汉字都算 1），只挑空白说它窄是双标；`QChar::isSpace()` 覆盖全角空格 U+3000（语料 4.49% 的行含有）与 NBSP U+00A0（0.44%），已用单测钉住而非凭记忆。**前缀和取整分配时长**（`startMs + llround(total × 累计权重 / 总权重)`），保证最后一个 token 的 `endMs` 精确等于行 `endMs`、相邻 token 首尾相接，不做逐词累加以免误差累积。**不特判**：credit 行（worded 文档里 625 条 credit 行 625 条全带真词，说明特判没有必要）、lift/brightness/halo 三个既有开关（CJK 真词 100% 单字，合成词与真词形态相同）均照常处理。**实现落点**：core 新增纯函数 `PlasmaLyrics::synthesizeWords(const LyricLine &)`（`core/lyric/wordsynthesis.{h,cpp}`，不引入 QtNetwork/QtDBus/QtGui）；`LyricSource` 加**第二个只读属性** `currentSyntheticWords`（`READ`+`NOTIFY currentLineChanged`，与既有 `currentWords` 同形），**不能做成 LyricSource 上的可写属性**——`main.qml` 的 `LyricSource { id: lyricSource }` 是单例，被桌面与面板两个 representation 共用，存不下两份开关状态，开关状态只能留在各自的 `LyricsView` 里。`LyricsView.qml` 新增 `syntheticWordByWord`，`effectiveWords` 在既有 gate（含 `wordByWord`）为真时，`root.syntheticWordByWord && root.source.currentWords.length === 0 ? root.source.currentSyntheticWords : root.source.currentWords`——开关关闭时合成属性根本不会被求值，不会因为读取而产生任何唤醒成本。**接受功耗代价**：一个此前因"整篇无词、`effectiveWords` 恒空"而零成本的曲目，开启后会像真逐字一样武装决策 38 的逐帧 `FrameAnimation`，配置页在开关下方写明"会让该动画在所有曲目上运行，播放时耗电和占用更高"。**三条边界，前两条有实测代价、第三条无语料背书**（qa-1 复核指出）：分词逐 `QChar`（UTF-16 code unit）扫描而非逐 Unicode code point，一个 astral 平面字符（超出 BMP 的生僻扩展汉字、多数 emoji）编码成代理对即两个 `QChar`，`isCjkChar()` 对孤立的半个代理对调用 `QChar::script()` 永远解不出 CJK script，两半都会落进"非空白非 CJK"的累积分支——**不会**因此拦腰拆开产生半个代理对独立成 token 的乱码（两半走同一分支、该分支只会累加不会从字符中间切开），但有两处偏差：一是连续多个 astral 平面 CJK 表意文字会合并成一个 token 而不是像 BMP CJK 那样逐字拆开；二是 astral 字符按 `QChar` 计权重算成 2，比同等 BMP 字符多分到约一倍时长，是精度偏差、不是正确性问题。两条都接受不修：支撑决策 74 的语料（204 个 worded 文档、1000+ 行）里没有 astral 平面歌词，尚未观察到实际影响；解除条件是遇到真实命中该情况的歌词行时，把逐 `QChar` 扫描改成先拼合代理对再分类的逐 code point 扫描。**Hangul 归入 CJK 集合是本条里唯一没有语料验证的规则**——决策 8/74 的其余每条分词规则都能在语料里对出百分比，但语料里没有韩文样本可供验证"韩文逐字符自成 token"这条判断是否成立，只是按"CJK"的常规外延纳入，留待拿到韩文样本后再确认或推翻。 |

### 质量与发布
| # | 决策 |
|---|---|
| 33 | 诊断四件套：**① journal**——daemon 是 systemd user service，消息处理器无条件安装并写 stderr，由 systemd 收进 journal（`journalctl --user -u plasma-lyricsd -f`），输出形态按 stderr 的实际去向三分，见决策 64。「直通 journal、零实现成本」的原始设想不成立：Qt6 若链接 `libsystemd`（Arch 的 qt6-base 即如此），其默认消息处理器在非 tty 时走 `sd_journal_send()` 而非 stderr，且 journald 只按 syslog 优先级着色、不认应用自己打的级别文字，因此原生着色与去重都需要显式实现；**② 可配置日志文件**——`logging/fileEnabled` 默认 `false`，路径 `logging/filePath` 默认 `~/.local/share/plasma-lyrics/plasma-lyricsd.log`，开启后与 journal 并行写；**③ `plasma-lyricsd --explain "<标题>" "<歌手>"`**——离线打印匹配全过程，调匹配逻辑时的主力工具；**④ 分类日志与调试开关**——七个 `QLoggingCategory`（`plasmalyrics.daemon`/`.resolver`/`.mpris`/`.provider.netease`/`.provider.amll`/`.provider.local`/`.provider.qq`），默认 info；`logging/debug` 打开对应 debug 级输出；三层过滤规则叠加、优先级依次升高——kdebugsettings 写入的 `qtlogging.ini` < 该开关调用的 `QLoggingCategory::setFilterRules` < `QT_LOGGING_RULES` 环境变量，因此开关开启时在 kdebugsettings 里单独关闭某个分类不生效（仅指 debug 开关；kdebugsettings 设的 info/warning 阈值仍生效）。`daemon/plasma-lyricsd.categories` 随 `BUILD_PLASMOID=ON` 安装，七个分类因此在 kdebugsettings 中可见。**不做设置里的日志面板**（实现要两天、半年用两次，`--explain` 覆盖同一需求且离线可重复）|
| 34 | 测试必须项见第 6 节 |
| 35 | i18n 第一天就做；AUR 为唯一正式分发渠道；**不上 KDE Store** |
| 72 | **`-Werror` 闸门与具名初始化——保护强度分三档，不能压成一句话**：CI 的 **Arch job** 新增 `-Wall -Wextra -Wpedantic -Werror` 的构建与 `ctest`；**Debian 13 job 未接入**；项目的**默认构建仍然不设任何警告选项**。接入前，全量 `-Wall -Wextra -Wpedantic` 构建有 **929 条警告，全部是 `-Wmissing-field-initializers`，没有第二个类别**：生产代码 40 条、测试 889 条（`tst_matcher.cpp` 独占 671）；测试目标因此单独加了 `-Wno-missing-field-initializers`，生产目标没有。共有 **41 处**聚合初始化改为具名形式，**这个数字连计数规则一起记**：按「构造站点」计，`mprispolicy.cpp` 的 `PlatformRule` 静态表按两条记录计为 2；本轮曾用三种计数法得到 38/41/42 三个不同的数，分歧全部来自计数方法本身（多行构造的首个 designator 不在开花括号那一行、带续行的构造被切成两处、初始化列表里的多条记录算一处还是多处），不写清规则下一个人重数必然对不上。三档保护（均已实测）：**① 始终生效**——具名初始化的 designator 必须按声明顺序书写，否则是 C++20 语言规则层面的硬编译错误，与任何警告开关无关，默认构建里也成立；**② 仅在 `-Werror` 构建里生效**——少写一个成员会报错，默认构建（也就是实际打包发布的那个）不受益；**③ 完全没有保护**——designator 都写对、只把两个同类型成员的**值**互换，在满警告下也编译通过、零诊断。所以准确的表述是：**这批改动挡住的是「按位置错位赋值」，挡不住「换位赋值」**，不能写「编译期不可能写反」。`-Wmissing-field-initializers` 的真实触发规则（2×2 最小复现实测）：**只取决于被省略的成员有没有类内默认初值（in-class default member initializer），与位置初始化/具名初始化无关**，成员类型自带默认构造函数（如 `std::optional`、`QString`）**不**豁免——曾有「该警告只盯具名初始化」的推断，与最初那 929 条警告（全部来自位置初始化）直接矛盾，是错的，不写进文档。`matcher.cpp` 里那两个 `switch` 的穷尽性由 **`-Wswitch`** 强制，**不是** `-Wmissing-field-initializers`；已实测：给枚举加第六个值后，默认构建**编译通过**（静默），`-Werror` 构建在两个 switch 上都失败。**强类型化（单字段 wrapper）明确不做**：它是唯一能让「换位赋值」变成编译期错误的办法，连纯位置构造站点也能保护到，但要动 QML `Q_PROPERTY`、JSON 序列化、`Q_DECLARE_METATYPE` 三条边界，属独立立项。一次穷尽扫描（不依赖编译器警告，改为枚举「存在相邻同型或可隐式互转成员」的聚合结构体再找构造点）发现风险横跨 7 个结构体（口径是「既有相邻同型成员、又确实存在纯位置构造点」两个条件叠加后的集合；只按第一个条件筛会多得多，复核时数到十余个候选，两者不矛盾但不可混用），其中 `StringListSetting`（`key`/`explicitEmptyKey`）正是决策 67 那个修复本身用的结构体；`matcher.cpp` 的 `QueryTitleVariant`（两个相邻 bool，实参取值不同）实测**已被 15 个 `tst_matcher` 用例钉住**（在 `48b7b6e` 之后测得；该数字随钉住 `titleViaArtistStrip` 的用例增删而变，本轮就先测出 13、后因任务四新增两例变成 15——引用时请连同测得的提交一起看）——编译器看不见它，但测试看得见，这两件事不矛盾，别把「编译器抓不住」写成「没有保护」 |

---

## 4. 缓存 schema

存 `~/.local/share/plasma-lyrics/lyrics.db`——**不是 `~/.cache`**，因为库内含 `offset` 这类
不可再生数据（waylyrics 把 per-track offset 放在 `~/.cache` 里，有被系统清理的隐患）。

```sql
-- 歌词正文（可再生）
CREATE TABLE lyric (
  provider    TEXT    NOT NULL,          -- 'local' | 'netease' | 'amll' | 'qq' | 'waylyrics'
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
| 播放控制 | 范围外（Q6）。本机已有 4 个音乐 plasmoid 在做 |
| KDE Store 上架 | 分发模型与架构不兼容（见第 7 节） |
| 运行时 provider 插件（.so） | 过度设计。稳定 ABI、版本协商、加载失败处理的代价换不到收益；接口定好了将来要改也不必推翻 |
| 读 waylyrics 的运行时缓存 | 耦合他人私有格式且要求 waylyrics 常驻；只做一次性导入 |
| 设置里的"最近匹配记录"面板 | 实现要两天、半年用两次。`--explain` 子命令覆盖同一需求且离线可重复 |
| 日志文件的轮转 | 有意不做。日志文件以 append 打开，**没有大小上限、不会自动轮转**——它是为"排查一次问题"准备的开关（默认关闭），不是常开设施。日常诊断走 journal，那边由 systemd 负责限额与轮转。若哪天需要长期开着，再补一个按大小截断的处理，而不是现在预先造 |
| `LyricLine.qml` 里 `Kirigami.Units.longDuration > 0` 从句无测试覆盖 | 既有缺口，非本轮引入。`Kirigami.Units` 在 QML 测试套件里是不可变更的全局（恒为 200），这条从句永远不是任何断言通过或失败的原因；要补它需要让整个测试套件在 animation-factor 为 0 的 `XDG_CONFIG_HOME` 下再跑一遍，等于新增一条常驻的第二遍全量运行。代码注释（`LyricLine.qml:132-140`）已写明未覆盖、原因与补法，并顺带下过一句判断（`judged not worth an ongoing double test run for one clause`）；缺的不是判断本身，是它只存在于那一个文件的注释里，没有进入这份集中记录「不做」的清单——**裁定为不修**在此存档，解除条件是接受这个双跑代价 |
| 逐字字形可读性无自动化证据 | 测试只能证明字符串到达与布局几何（宽度、位置、颜色属性值），证明不了"看起来对"。已目视检查过渲染图，**截图不入库**——本仓库没有图像比对 CI，入库的截图会随渲染栈（字体、Qt 版本、GPU 驱动）变化静默过时，钉住一张会过时的截图不比没有更可靠 |
| `splitTrailingGloss` 收紧 | 可辩护的子集只有 `Reprise` 与 `翻自`（真实索引上被剥的 602 条标题/专辑名里，11 种残余风险形状占 135 条，其中 117 条是 `feat.`/`ft.`——剥 `feat.` 通常是对的），整批 11 个词一起收紧大概率净退步。**主要阻碍**：该谓词同时驱动 `searchKeywords` 构造发给 provider 的查询串，收紧会改变**召回本身**，而这对差分回放在构造上不可见——回放只能对着已录制的固定候选池重放，看不到新查询串会召回什么。**解除条件**：差分回放能覆盖召回变化（详见决策 70 的记录） |
| 纯音乐占位符 | 用户明确表示不做 |
