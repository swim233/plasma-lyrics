# 多源歌词的设置、并发与进度修复计划

日期：2026-09-10。状态：设计与代码实现已完成；自动化验收通过，人工界面验收待执行。

本文记录三个用户报告问题的根因定位、与用户完成设计访谈后确认的 23 项决策、
分四个提交的实施方案，以及验收流程。交给实现者可独立执行，不需要重新做设计判断。

架构约束沿用 [DESIGN.md](DESIGN.md)：`core/` 不依赖 QtNetwork / QtDBus，
provider 集成在 `providers/`，MPRIS 在 `daemon/`，歌词通过原子整首快照传递，
每个部件实例是快照的只读消费者（例外见 Q12）。
前置工作见 [MULTI_PROVIDER_PLAN.md](MULTI_PROVIDER_PLAN.md)。

## 1. 背景

用户报告了三个问题，并归因于新增多源歌词的那批提交（`13bf60c..3de5727`）：

1. 「歌词服务」设置页调整歌词源优先级后无法保存；且手动填写多行源的形式需要重新设计。
2. 在小部件中频繁手动切换歌词源，有很大概率卡死小部件。
3. 播放中手动切换歌词源，进度大概率不准确。

定位结论：**三个问题的根因都先于多源功能存在**，多源功能只是分别提供了触发条件。
因此回退或关闭多源功能不能修复其中任何一个。

截至本文，这批提交尚未发布（最后的 tag 是 `v0.2.3`，改动全部位于 `CHANGELOG.md`
的 `## 未发布` 段），但问题 2 与问题 3 的病根随 `v0.2.2` / `v0.2.3` 已经发布，
见 Q16。

### 1.1 问题 1：设置页的保存路径

两个独立缺陷。

**D1 — 该页忽略 Apply/OK，全部 15 个字段都不保存。**
`ConfigBackend.qml` 不是 kcfg 支撑的页面：`main.xml` 没有任何该页的键，
页内 `cfg_` 引用数为 0（四个同级页分别是 64 / 63 / 15 / 1）。
Plasma 为非 kcfg 页提供的钩子只有两个——`saveConfig()` 与 `unsavedChanges`，
该页两个都没实现，唯一的写盘入口是页面自己的「保存服务设置」按钮
（`ConfigBackend.qml:129-151`）。

同一仓库里的另一个非 kcfg 页 `ConfigGlobal.qml` 已经解决过这件事，
其头部注释（`:7-13`）明确写了这是 DESIGN.md 决策 41 的一部分，
并实现了 `property bool unsavedChanges`（`:17`）与 `function saveConfig()`（`:35`），
连 OK 路径无法回传保存失败这一残留限制都记录在案（`:18-32`）。
`ConfigBackend.qml` 是同一形状的页面，一条都没套用。

用户机器上的 `~/.config/plasma-lyrics/plasma-lyricsd.ini` **没有 `order` 键**，
证实该键从未被写入。

**D2 — 一次保存可以把设置永久清成零个源，且报告成功。**
`BackendConfig::save()` 把输入按 `netease` / `amll` 白名单过滤并去重
（`backendconfig.cpp:196-202`），过滤结果为空时写入一个空 `QStringList`。
QSettings 的 INI 后端把空列表序列化为 `order=@Invalid()`；
此后 `value("providers/order", {netease, amll})` 返回一个**有效但为空**的
`QStringList`——键存在，内置默认值不再顶上。
`Config::providerOrder()`（`config.cpp:41-55`）随之返回空列表，
`main.cpp:168-180` 构造出空的 `onlineProviders`，守护进程零在线源。
`save()` 返回 `true`，界面报告保存成功。

实测会塌成空值的输入（`save()` 均返回 `true`）：

| 输入 | 结果 |
| --- | --- |
| `netease` 换行 `amll` | 正确 |
| `netease, amll`（逗号同行） | **空** |
| `netease -> amll` / `netease → amll` | **空** |
| `- netease` / `1. netease` 等列表前缀 | **空** |
| 行尾分号或逗号 | **空** |
| 单独 CR（经典 Mac 换行） | **空** |
| 清空字段 | **空** |
| 仅空白 | **空** |
| 显示名（`NetEase` / `AMLL TTML DB`） | 部分丢失 |
| CRLF 粘贴 | 正确（`trimmed()` 会去掉 `\r`） |

最可能的三个真实触发：INI 自身把列表渲染成逗号分隔
（同一文件里 `[filter]` 段的 `platforms=netease, apple` 就是这个样子）；
`MULTI_PROVIDER_PLAN.md:19` 与 `README.md` 用 `netease → amll` 描述默认顺序；
以及「清空字段以恢复默认」——这一个后果最重，因为它正是用户为了拿回默认值而做的动作。

**D4 — 同一个 `@Invalid()` 机制已经作用在另外两个列表字段上，且在实机上已经发生。**
`save()` 对 `players/blacklist` 与 `filter/musicUrlPrefixes` 用的是同一个 `list()`
（`backendconfig.cpp:181-182`）。开发机的 `~/.config/plasma-lyrics/plasma-lyricsd.ini`
当前是：

```ini
[filter]
musicUrlPrefixes=@Invalid()
platforms=netease, apple
...
[players]
blacklist=@Invalid()
```

`daemon/src/config.cpp:18,20` 用同样的 `value(key, default)` 读这两个键，
于是两个内置默认值都已被永久遮蔽：
`players/blacklist` 的 `org.mpris.MediaPlayer2.kdeconnect.*` 不再生效，
与 DESIGN.md 决策 2「kdeconnect 手机源支持但默认忽略」及决策 10 记载的默认值不符；
`filter/musicUrlPrefixes` 的两个网易云 URL 前缀同样不再生效。

但这两个字段与 `providers/order` **性质不同**，Q7 的理由不能照搬：
对黑名单和自定义 URL 前缀来说，「空」是用户可以合法表达的意图
（不屏蔽任何播放器 / 不加自定义前缀），`@Invalid()` 读回空列表恰好实现了这个意图；
而对 `providers/order` 来说「空」等于功能完全消失，没有界面能表达它。
真正的缺口是**默认值在清空后无法通过界面找回**——想让 kdeconnect 重新被忽略，
只能手工编辑 INI。这一条属于既有缺陷，范围待定，见 Q24。

**D3 — 同一份白名单与去重逻辑存在两份副本**，
写入侧 `backendconfig.cpp:196-202`，读取侧 `config.cpp:41-55`，两份都静默丢弃。
写入侧的副本使 D2 具有破坏性，读取侧的副本使它无法被发现。

### 1.2 问题 2：卡死

切换本身是干净的异步：`asyncCall` + `QDBusPendingCallWatcher`，显式 3000 ms 超时
（`lyricsource.cpp:519-526`），错误通知走 `QDBus::NoBlock`（`:558`）。
整条路径上没有 `QDBusInterface::call()`、`QEventLoop`、`waitForFinished()`
或 `processEvents()`。

阻塞在 SQLite，且与切换无关：

- `LyricSource::updateServiceHealth()` 每 **2000 ms** 执行一次
  （`lyricsource.cpp:89,95`），在 plasmashell 的 GUI 线程上**开库两次**——
  一次在 `refreshGlobalOffsetCache()`，一次直接开（`lyricsource.cpp:249-276`）。
- `LyricStore::open()` 调用 `executeSchema()`，后者**无条件执行 `BEGIN IMMEDIATE`**
  （`core/store/lyricstore.cpp:75`），随后跑 8 条 `CREATE TABLE IF NOT EXISTS`
  与列迁移探测。因此每一次 open 都取**写**锁并做完整 schema 工作。
- 文件内没有任何 `setConnectOptions`，即未设 `QSQLITE_BUSY_TIMEOUT`，
  Qt QSQLITE 驱动的默认值 **5000 ms** 生效。
- 实机数据库为 `journal_mode=delete`（非 WAL），读写完全互斥。

于是部件每两秒在 GUI 线程上对守护进程正在写的库取两次写锁，等待上限 5 秒。
多源功能提供的是触发所需的**写入风暴**：手动切换是 `force = true` 的解析，
绕过全部缓存与负缓存短路（`resolver.cpp:196,220,265,367`），
每次切换都产生网络请求与对 `lyric`、`provider_fingerprint`、`provider_miss` 的写入。
频繁切换使风暴持续，2 秒轮询反复落在被锁的库上。
实测连续两次轮询分别阻塞 3037 ms 与 1233 ms。

因为那是 plasmashell 唯一的 GUI 线程，卡住的是整个面板与桌面，不只这一个部件。

部件对 SQLite 的全部使用仅四处，都在 GUI 线程：

| 位置 | 用途 |
| --- | --- |
| `lyricsource.cpp:217-229` `refreshGlobalOffsetCache()` | 读 `globalOffsetEnabled()` / `globalOffsetMs()`，构造时与每 2 秒各一次 |
| `lyricsource.cpp:265-267` `updateServiceHealth()` | 读 `store.offset()`，用于感知其他实例改过的单曲偏移 |
| `lyricsource.cpp:459-470` `adjustOffset()` | 写偏移 |
| `lyricsource.cpp:487-497` `resetOffset()` | 写偏移 |

### 1.3 问题 3：进度不准

**违反的不变量**：`positionUs` 必须与 `anchorMonotonicNs` 在**同一时刻**采样。
部件是唯一的消费者，按 `position + (now − anchor) × rate` 外推
（`frontend/qmlmodule/lyricsource.cpp:416-427`），并据此选当前歌词行。

`pollPosition()` 破坏了这个不变量（`daemon/src/mpris/mprisplayer.cpp:219-224`）：

```cpp
m_state.positionUs = position;        // 总是写
if (jump) {
    m_state.anchorMonotonicNs = now;  // 仅在检测到跳变时写
}
m_lastSamplePositionUs = position;    // 总是写
m_lastSampleMonotonicNs = now;        // 总是写
```

MPRIS 的 `Position` 声明 `EmitsChangedSignal=false`，所以稳定播放中
`PropertiesChanged` 几乎不带 Position，锚点停在上一次换曲 / 恢复播放 / 检测到 seek 的时刻，
而 `positionUs` 每秒被轮询推进。缓存的配对因此几乎始终不自洽。

`jump` 这个标志同时承担了两件无关的事：决定**是否发布**，以及决定**是否维持配对自洽**。
它只有资格承担前者。

误差量 = 上次轮询时刻 − 上次锚点事件时刻，即**该曲自上次换曲或 seek 以来播放了多久**。
锚点事件在 `(P₀, T₀)`，三十秒后缓存配对是 `(P₀+30s, T₀)`，
发布后部件算出 `(P₀+30s) + (now − T₀) = P₀ + 60s`，正好重复计入三十秒。
这不是概率性的，而是与已播放时长成正比；只有紧接换曲之后切换才看起来正确。

`publish` lambda（`daemon/src/main.cpp:253-259`）有五个调用点：

| # | 调用点 | 来源 | 是否重锚 |
| --- | --- | --- | --- |
| 1 | `main.cpp:267` | `update()`，无播放器，写 `positionUs:0, anchor:0` | 不适用 |
| 2 | `main.cpp:280` | `update()` 换曲 | 是 |
| 3 | `main.cpp:287` | `update()` 同曲 | 是 |
| 4 | `main.cpp:300` | `Resolver::resolved`，异步歌词到达 | **否** |
| 5 | `main.cpp:338` | `forceResolve`，三个 D-Bus 控制方法 | **否** |

站点 2 / 3 由构造保证安全：`activeStateChanged` 的四个发出点
（`mprismanager.cpp:107`、`mprisplayer.cpp:221` 的 jump 分支、`:248` 的 `onSeeked`、
`mprismanager.cpp:124` 的播放器切换）都重锚，且非活动播放器不被轮询
（`mprismanager.cpp:38-42` 只轮询 `m_activeService`），其配对必然自洽。

到达站点 4 / 5 的全部触发：异步歌词到达（`resolver.cpp:158`）、
`SetPreferredProvider`（`controlservice.cpp:50`）、`ClearPreferredProvider`（`:62`）、
`Research`（`:71`）、`playbackRoundStarted` 重试（`main.cpp:312`）、
AMLL 索引在解析中刷新（`resolver.cpp:303-318`）。
偏移调整当前不经过守护进程，不是发布路径。

手动切换还叠加**第二个与延迟成正比的分量**（落在站点 4）：
`main.cpp:291` 的 `manager.activeState()` 在两段链式异步往返
（`resolver.cpp:401` 搜索、`:437` 获取）**之后**重读缓存状态，
此间又有若干次 1 秒轮询推进了 `positionUs` 而锚点未动。
手动切换必走网络路径（`force` 跳过全部缓存短路），因此会发布两次，
第二次误差 = 第一次误差 + ⌊延迟 / 1 秒⌋ 秒。

指纹与 Position 跳变检测均未被牵涉，两者都不参与本次修改：
`MprisPolicy::fingerprint()`（`mprispolicy.cpp:68-85`）以 `mediaSrc` 路径段或
`(title, artists, album, lengthUs)` 哈希为依据，`mpris:trackid` 不是它的输入；
`MprisPolicy::isPositionJump()`（`mprispolicy.cpp:199-213`）读的是
`m_lastSamplePositionUs` / `m_lastSampleMonotonicNs`，这两个字段每次轮询都刷新，
因此跳变检测今天是自洽且正确的。CLAUDE.md 中「不得改为纯 track-id 逻辑」的约束不受影响。

### 1.4 附带发现：LocalProvider 不是歌词源

`LocalProvider` 的 `supportsSearch()` 返回 `false`（`localprovider.cpp:28-31`），
`search()` 与 `fetch()` 是返回空值的存根（`:33-41`）。
它唯一工作的方法是 `overrideFor(providerId, trackId)`（`:43-52`），
读取 `~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`，
即手工覆盖机制，见 README.md:112,141、README.en.md:52、CHANGELOG.md:121。

三个后果：

1. `local` 从不出现在源列表里——`Resolver::availableProviders()` 按
   `supportsSearch()` 过滤（`resolver.cpp:51`）。
2. 它的位置已经没有意义——`Resolver::searchChain()` 用同一过滤（`resolver.cpp:63`），
   所以尽管 `main.cpp:245` 把 `&local` 放在 `m_providers` 首位，它从不进搜索链。
3. 它不与在线源竞争，而是对在线源的结果做后处理：覆盖以**对方 provider 的 id 与
   track id** 为键（`resolver.cpp:76-88`），在胜出的那个源之后运行。

用户据此确认要新增一个真正可搜索的本地源（Q13），并把覆盖机制从 `Provider`
接口中移出（Q21）。

## 2. 决策清单

以下 23 项均由用户确认。实现时不得改变这些行为；接口名与具体 schema 可调整。

### 范围与架构

| # | 决策 |
| --- | --- |
| Q1 | **四个提交，顺序锁定**。见第 4 节。两个约束强制了顺序：问题 3 的修复必须先于任何新增发布路径（否则提交 2 新增的偏移发布会带上同样的漂移）；D1 的修复必须先于任何需要反复保存配置来验证的工作 |
| Q8 | **部件完全不打开 SQLite**，回到纯快照消费者。理由不是性能：三个 bug 有同一形状——部件绕过快照自取数据，于是快照的原子性保证不覆盖它。仅把阻塞挪出 GUI 线程（worker thread）会让卡死消失但偏移读取仍莫名延迟数秒，且给纯消费者引入线程；仅改成只读打开则留着「两进程各自开同一个库」的结构，并要求 `core/` 长出第二种打开语义 |
| Q12 | **配置对话框的 `GlobalConfig` 保持直连 SQLite**，是 Q8 的有意例外。`ConfigGlobal.qml:35` 的 `saveConfig()` 依赖同步 `save()` 返回值报告失败，而那段注释（`:18-32`）明确说这是决策 41 里「用户真正会注意到的那个信号」；改成异步 D-Bus 会让这个错误提示失去回传通道。`LyricStore` 从此只有守护进程与配置对话框两类调用方，部件不在其中 |

### 快照与控制接口

| # | 决策 |
| --- | --- |
| Q9 | **快照新增 `globalOffsetEnabled`，并把现有 `offsetMs` 改为守护进程算好的生效值**。部件仍需要那个开关——`main.qml:355-373` 的菜单文案与 `canAdjustOffset`（`lyricsource.cpp:156`）都依赖它。生效偏移是守护进程本来就知道的东西（`resolver.cpp:95` 已在给 document 盖 offset），两边各算一次是 bug 的温床。注意这会改变 DESIGN.md 决策 41 记载的 `lyric.offsetMs` 语义（原为「per-track 原始值」），需同步更新 |
| Q10 | **2 秒健康定时器保留，只做 pid 检测**（`lyricsource.cpp:250` 的 `processExists`，读 `/proc`，无锁无阻塞）。守护进程被 kill 后快照文件仍在原地、`seq` 不再变，靠「多久没变算死」判断需要一个凭感觉的阈值，而 pid 检测是确定性的。间隔保持 2 秒：改完每轮只剩一次 `/proc` stat |
| Q11 | **`ControlService` 新增 `AdjustOffset(expectedFingerprint, deltaMs)` 与 `ResetOffset(expectedFingerprint)`**，由守护进程内部判断全局 / 单曲模式，写入后重新发布快照。用 delta 语义而非 `SetOffset(ms)`：后者要求部件基于快照旧值算目标值，两个实例同时点「+500ms」会丢掉一次调整。全局模式下 `expectedFingerprint` 是多余的，但保留以匹配另外三个方法的形状，并挡住「指纹已变、用户其实在给上一首调偏移」 |
| Q15 | **快照新增表达「正在切换到某源」的字段**，与 `temporaryFallback` 同构，由 `forceResolve` 置上、`Resolver::finish` 清掉。不能复用 `lyric.state == "searching"`：`forceResolve` 在有可留存旧歌词时走另一条分支（`main.cpp:317-319`）保留 `state == "ok"` 继续显示旧歌词，而那正是最常见的情况（用户看着歌词想换源），此时 `searching` 根本不出现。也不用部件本地标志加超时（现状）：其 3 秒过期与 D-Bus 的 3 秒超时（`lyricsource.cpp:521`）同量级，两个计时器互相赛跑 |

### 设置页与优先级控件

| # | 决策 |
| --- | --- |
| Q2 | **用可拖拽排序 + 勾选的列表取代自由文本框**。这不是外观改良：D2 之所以存在，是因为自由文本能表达系统无法表示的值；换成选择器后不存在「无法解析」的状态，`backendconfig.cpp:196-202` 的静默过滤随之成为死代码并删除。用校验报错来修 D2 是治症状。不用两栏穿梭框（对 2–3 个源过重），不用逐位 ComboBox（源数量变化时会出现「同一源被选两次」的非法状态） |
| Q3 | **勾选即启用，新增 `providers/enabled` 保存启用集合，`providers/order` 保存全部源的顺序**。多一个键换来「取消勾选不丢位置」，并与已有的 `filter/platforms` 集合型键风格一致。若让「未启用」等于「不在 order 里」，用户临时停用某源再打开时它会跑到末尾，正好破坏他本来要调的顺序 |
| Q4 | **源列表由守护进程经 D-Bus 提供，服务未运行时降级为静态列表并提示**。`BackendConfig` 目前纯读 QSettings、完全不依赖 D-Bus，这个优点要保留，所以是「能连上就用真实列表，连不上退回静态列表并显示服务未运行」。不在 C++ 侧建 provider 注册表：放 `core/` 不能带 QtDBus，放 `providers/` 又要被 `frontend/` 依赖 |
| Q14 | **保留配置里不认识的 provider id，只是不装配它**。现状是写入侧与读取侧两份副本都静默丢弃，后果是：装一个未编译 AMLL 的构建（`build-no-amll` 说明这是支持的），打开设置页保存一次，`amll` 就从顺序里永久消失。Q4 让列表来自守护进程，界面天然只显示可用源，真正的风险是保存动作删掉没显示的东西，所以要求新控件写回时把不认识的行原样带上 |
| Q7 | **`Config::providerOrder()` 结果为空时回退内置默认顺序，并在 journal 写一行说明**。零 provider 永远不是用户想要的状态——它等于功能完全消失，而且没有界面能表达这个意图。这样不需要迁移逻辑，也不需要区分「用户故意清空」与「被 bug 写坏」。不做「启动时删掉解析为空的键」：对一个从未发布的 bug 过重 |
| Q5 | **保留单曲切换交互，并加「进行中」状态**：切换后菜单项变灰、显示正在从某源获取，期间不接受新的切换请求。这既是正确反馈，也给后端一个串行化点，但**不替代** Q8 的后端修复——UI 禁用只降低触发概率。不改成「试下一个源」：指定源是这个功能的主要价值 |

### 本地歌词源

| # | 决策 |
| --- | --- |
| Q13 | **新增一个真正可搜索的本地源**，于是它名正言顺地可排序、可勾选。这是新功能，独立于三个 bug，排在最后一个提交 |
| Q17 | **歌词目录 + 音频文件同级 sidecar，两者都做**，先查 sidecar 再查歌词目录。两半分量不同：本项目主要场景是浏览器与 Cider 等流媒体，那些场景下 `mediaSrc` 是 `https://` URL、不存在本地音频文件，只有歌词目录能生效，因此它是必需的一半；sidecar 只对播放本地文件的用户有意义，但几乎免费（`mediaSrc` 已在 `MprisState` 里，判断是否 `file://` 再拼 `.lrc` 即可） |
| Q18 | **复用 `core/match/` 的 `Matcher`，`Candidate` 字段由文件名与 LRC ID 标签填充**（`[ti:]` `[ar:]` `[al:]` `[length:]`），`contentId` 存文件路径。只靠文件名最多得到标题与歌手，`scoreCandidate` 的 album 与 duration 两项永远不贡献分数，`chooseMatch` 只能在弱证据上判断；ID 标签是 LRC 格式标准的一部分，正好填上那两个字段。不做 `<artist> - <title>.lrc` 精确匹配：强迫用户遵守命名约定，且任何标点或本地化差异都会失配，而那正是 `normalizeSearchText` 与 `cleanArtists` 存在的理由。**注意 `LrcParser` 当前识别 ID 标签但丢弃**——`metadataExpression`（`lrcparser.cpp:56`）在 `:71` 被用来跳过这些行，`ParsedLrc` 只带 `lines` 与 `embeddedOffsetMs`（`lrcparser.h:9-12`），需要扩展 |
| Q19 | **内置默认顺序改为 `{local, netease, amll}`；升级时把 `local` 前置到既有顺序**。本地命中零网络、零失败模式，排在网络源之后意味着有本地文件的用户还要先等网易云。若把既有两项配置理解成「local 未启用」，所有升级用户都会静默地拿不到这个功能；而 `local` 是**新识别**的 id，「配置里没有」无法区分「用户故意禁用」与「配置比代码旧」，所以必须显式选一个。Q7 的内置兜底默认值同步改为三项 |
| Q20 | **本地源参与负缓存，`cacheVersion()` 返回歌词目录状态**（mtime 或文件数），目录一变所有本地 miss 立即失效。`Provider::cacheVersion()` 就是为这件事设计的（`provider.h:44-46` 的注释），AMLL 已在用（`amllprovider.cpp:251`）。要紧的是失败模式：用户放入新 `.lrc` 后下一轮就能命中。完全不参与等于永久重复扫描；用短 TTL 则需要一个凭感觉的值，且 TTL 选多长都答不对「刚放进去的文件多久生效」 |
| Q23 | **sidecar 未命中时不写负缓存行**。`Provider::cacheVersion()` 的签名是 `virtual QString cacheVersion() const`（`provider.h:46`），provider 级别、与请求无关，而 sidecar 位置每首歌都不同。若负缓存行按歌词目录状态盖版本，用户把 `.lrc` 放到音频文件旁边后歌词目录 mtime 未变、负缓存仍有效、新文件被忽略——正是 Q20 想避免的失败模式从 sidecar 一半漏出。本决策让两半各自保住正确的失败模式，代价是本地文件播放时每首歌多一次 `QFile::exists`，且只在 `mediaSrc` 为 `file://` 时发生 |
| Q21 | **把 `overrideFor` 从 `Provider` 接口移除，改为 Resolver 的独立步骤**。它是一个只有单个子类实现的虚函数，而 `Resolver::overridden()` 要遍历所有 provider 去找它（`resolver.cpp:76-88`），是一个钩子伪装成 provider 能力。（设计访谈中曾建议不要在新增 provider 的同批提交里改 `Provider` 接口，用户选择一并改掉。） |
| Q22 | **覆盖文件的读取器放 `core/store/`，与 `LyricStore` 并列**，`Resolver` 调用它。覆盖目录与 `LyricStore` 是同一类东西——按 `TrackRef` 键控的用户自有数据。内联进 `resolver.cpp` 会把文件 IO 与路径配置塞进编排层，测试它就得先备好 store 与整条 provider 链；而 `providers/tests/tst_localprovider.cpp:14-30` 那个现成测试搬到 `core/tests/` 几乎不用改。不在 `providers/` 下为一个明确「不是 provider」的东西开目录 |

### 验证与文档

| # | 决策 |
| --- | --- |
| Q6 | **单元测试 + 可在 CI 跑的压力测试 + asan/lsan**。三个问题的要求不同：问题 3 已可确定性单测（假时钟推进 → 轮询 → 无锚点事件下发布），不需要真实 Plasma 会话；问题 2 依赖写锁竞争窗口，手动验证一个「大概率」发生的问题恰恰会给出假阴性，必须有压力测试；问题 1 是纯往返测试 |
| Q24 | **待决**：D4 中 `players/blacklist` 与 `filter/musicUrlPrefixes` 被 `@Invalid()` 遮蔽默认值的问题是否纳入本次范围。这两个字段「空」是合法意图，因此不能套用 Q7 的读取侧回退；可选做法是让新控件为列表字段提供显式的「恢复默认」动作，或在清空与未设置之间引入可区分的存储表示。未决前，实现者不要改动这两个键的读写行为 |
| Q16 | **写三条针对已发布版本的 Fixed，并改写 `未发布` 段的 Added 文案**。问题 2 的病根随 `v0.2.2` 的「全局歌词进度调整」进来（「改动立即对所有桌面歌词部件生效」就是靠那个 2 秒轮询实现的）；问题 3 的病根更早，且在 `v0.2.3` 的「网易云全链路异步」之后已可观测——站点 4（异步歌词到达）成为不重锚的发布路径，已发布版本里的症状是「歌词加载完成的瞬间进度偏移约等于取词耗时」，量级 1–3 秒；问题 1 中旧字段（网易云地址、超时、黑名单等）的 Apply/OK 失效同样是已发布缺陷。按 CLAUDE.md「for fixes, state the symptom, not the internals」，症状必须是上一个发布版本的用户真实经历过的，因此不提「切换歌词源」。全部不写会让装着 v0.2.3 的用户在发布说明里找不到自己遇到的面板卡顿被修了 |

## 3. 当前代码与需要调整的接缝

| 位置 | 现状 | 计划调整 |
| --- | --- | --- |
| `daemon/src/mpris/mprisplayer.cpp` | `pollPosition()` 仅在 jump 时更新锚点 | 无条件更新锚点；`jump` 只决定是否发布 |
| `daemon/src/mpris/mpristypes.h` | `anchorMonotonicNs` 与 `m_lastSampleMonotonicNs` 语义不同 | 修复后三个写入点使两者恒等，决定是否合并 |
| `daemon/src/snapshot.{h,cpp}` | 发 `offsetMs`（per-track 原始值）、`availableProviders` 等 | 新增 `globalOffsetEnabled`、切换中字段；`offsetMs` 改为生效值 |
| `daemon/src/controlservice.{h,cpp}` | 三个控制方法，均带指纹校验 | 新增 `AdjustOffset` / `ResetOffset`，写入后重新发布 |
| `daemon/src/resolver.cpp` | `overridden()` 遍历 provider 找 `overrideFor` | 改为调用 `core/store/` 的覆盖读取器；两个调用点在 `:201`、`:223` |
| `daemon/src/main.cpp` | `LocalProvider local; providers{&local}` 前置且不进搜索链 | 本地源按配置进有序链；覆盖目录传给 Resolver |
| `daemon/src/config.cpp` | `providerOrder()` 空结果直接返回 | 空则回退内置默认并记日志；读 `providers/enabled` |
| `providers/provider.h` | `overrideFor` 是基类虚函数 | 移除该虚函数 |
| `providers/local/` | 只有覆盖能力，`supportsSearch()` 为 false | 改为可搜索源：目录扫描 + sidecar + `cacheVersion` |
| `core/lyric/lrcparser.{h,cpp}` | 识别但丢弃 ID 标签 | 扩展 `ParsedLrc` 暴露 `[ti:]` `[ar:]` `[al:]` `[length:]` |
| `core/store/` | `LyricStore` 一家 | 新增覆盖读取器；`open()` 的无条件写锁待评估 |
| `frontend/qmlmodule/lyricsource.{h,cpp}` | 四处直接开 SQLite | 全部删除，改读快照字段与调 D-Bus |
| `frontend/qmlmodule/backendconfig.{h,cpp}` | `providerOrder` 是 `QString`，`save()` 静默过滤 | 改为结构化的顺序 + 启用集合；删除静默过滤；补 D-Bus 源发现与降级 |
| `frontend/plasmoid/.../config/ConfigBackend.qml` | 无 `saveConfig()` / `unsavedChanges`；优先级是 `TextArea` | 补两个钩子；换成拖拽排序 + 勾选列表 |
| `frontend/plasmoid/.../ui/main.qml` | 源菜单硬编码 netease / amll；偏移直写 | 由 `availableProviders` 驱动；偏移改走 D-Bus；加进行中状态 |

## 4. 实施顺序

四个提交，顺序锁定。每个提交保持可构建并通过相关测试。

### 提交 1 — `fix(守护进程)`：位置锚点

把 `m_state.anchorMonotonicNs = now;` 移出 `pollPosition()` 的 `jump` 判断
（`mprisplayer.cpp:220-222`），使 `positionUs` 与锚点始终同刻采样。
`jump` 继续只决定是否 `Q_EMIT changed()`。
跳变检测读 `m_lastSample*`，不受影响；指纹逻辑不动。

修复后 `anchorMonotonicNs` 与 `m_lastSampleMonotonicNs` 在三个写入点
（`apply()` 的 `:167-172`、`pollPosition()`、`onSeeked()` 的 `:248`）恒等，
需决定是否合并为一个字段。合并可减少一类未来同源 bug，但会改动
`MprisState` 的公开形状与相关测试；不合并则应在 `mpristypes.h` 注明两者必须同刻写入。

此提交必须最先落地：它使**任何**发布路径永久安全，包括提交 2 新增的偏移发布路径。

### 提交 2 — `fix(部件)`：部件不再打开 SQLite

- 快照新增 `globalOffsetEnabled`，`offsetMs` 改为守护进程算好的生效值（Q9）。
  守护进程在 `main.cpp:283-285`、`:296-298` 与 `resolver.cpp:95` 盖 offset 的位置
  需一并考虑全局模式。
- 删除 `lyricsource.cpp` 的四处 SQLite 调用（见 1.2 表格）。
  `refreshGlobalOffsetCache()` 整体移除；`updateServiceHealth()` 只留 pid 检测（Q10）。
- `ControlService` 新增 `AdjustOffset` / `ResetOffset`（Q11），写入后重新发布快照。
  `main.qml:354-376` 的偏移 action 改为调用它们。
- `GlobalConfig` 与配置对话框不变（Q12），并在 DESIGN.md 记录这个有意例外。

### 提交 3 — `fix(设置)`：保存路径与优先级控件

- `ConfigBackend.qml` 补 `property bool unsavedChanges` 与 `function saveConfig()`，
  照 `ConfigGlobal.qml:17,35` 的形状（D1）。
- 优先级换成拖拽排序 + 勾选列表（Q2），新增 `providers/enabled`（Q3），
  源列表经 D-Bus 发现并在服务未运行时降级（Q4），写回时保留不认识的 id（Q14）。
  删除 `backendconfig.cpp:196-202` 的静默过滤——控件已无法产出非法值。
- `Config::providerOrder()` 空结果回退内置默认并记日志（Q7）。
- 快照新增切换中字段（Q15），`main.qml` 据此在切换期间禁用源菜单并显示进行中（Q5）。

### 提交 4 — `feat(歌词源)`：可搜索的本地源

- 扩展 `ParsedLrc` 暴露 ID 标签（Q18 的前置）。
- `core/store/` 新增覆盖读取器（Q22），从 `Provider` 移除 `overrideFor`（Q21），
  `Resolver::overridden()` 改为调用它；`providers/tests/tst_localprovider.cpp`
  的覆盖测试迁到 `core/tests/`。
- `LocalProvider` 改为可搜索源：`supportsSearch()` 返回 true，
  实现歌词目录扫描 + 元数据匹配（Q18）与 sidecar 查找（Q17），
  `cacheVersion()` 返回歌词目录状态（Q20），sidecar 未命中不写负缓存（Q23）。
- 内置默认顺序改为 `{local, netease, amll}`，升级时把 `local` 前置到既有顺序（Q19）。
- 设置页为本地源增加歌词目录路径字段。

## 5. 验收标准

### 5.1 位置锚点（提交 1）

- [x] 假时钟推进 → 轮询若干次 → 在无锚点事件的情况下发布，
      断言发布出的 `(positionUs, anchorMonotonicNs)` 自洽。此测试在修复前必须失败。
- [x] 已播放 N 秒后经由 `SetPreferredProvider` 触发发布，断言部件侧外推结果
      与真实位置的偏差不随 N 增长。
- [x] 异步歌词到达（站点 4）路径同样断言自洽，覆盖取词延迟带来的第二个分量。
- [x] `isPositionJump` 既有用例结果不变；指纹既有用例结果不变。
- [x] MPRIS 脏数据重放、播放器发现与生命周期、Position 跳变、原子快照与 stale 回归全部通过。

### 5.2 部件与偏移（提交 2）

- [x] `frontend/qmlmodule/LyricSource` 中不再有 `LyricStore` 的任何引用；配置页 `GlobalConfig` 保留 Q12 例外。
- [x] 快照往返测试覆盖 `globalOffsetEnabled` 与生效 `offsetMs`：
      全局模式开启 / 关闭、全局值非零、单曲值非零各一例。
- [x] `AdjustOffset` 的 delta 语义测试：连续两次 `+500` 得到 `+1000`；
      指纹不符时拒绝并反馈失败。
- [x] 多部件实例在一次偏移调整后看到一致的生效偏移，不需要重启服务。
- [x] `ControlService` 全部方法在私有会话总线上的压力测试：
      快速连续调用 N 次，断言无死锁、无在途请求泄漏、最终状态确定。
- [x] 在 `build-mpris-asan` 与 `build-lsan` 中运行受影响测试，无报告。
- [ ] 人工：播放中频繁切换歌词源，面板与桌面无卡顿。

### 5.3 设置页（提交 3）

- [x] 修改任一字段后按 Apply 或 OK，改动落盘；不再依赖页面自己的保存按钮。
- [x] 往返测试覆盖顺序与启用集合，含取消勾选再勾回不丢位置。
- [x] 曾使 `order` 塌成空值的输入（见 1.1 表格）在新控件下无法产生。
- [x] `providers/order` 为 `@Invalid()` 或空时，`Config::providerOrder()`
      回退内置默认并写 journal。
- [x] 配置中存在未编译或不认识的 provider id 时，保存后该 id 仍在文件里。
- [x] 守护进程未运行时设置页可打开，源列表降级为静态并显示提示。
- [x] 切换期间源菜单禁用并显示进行中；切换完成或失败后恢复可用，不清空已有歌词。
- [x] 用 Qt 6 `qmllint` 验证：`/usr/lib/qt6/bin/qmllint --bare -I build/bin -I /usr/lib/qt6/qml`。
      不要用 `PATH` 上的 `qmllint`（Qt 5 的，对未知类型与不存在的属性静默退出 0）。
- [x] 新增文案进入现有翻译流程。

### 5.4 本地歌词源（提交 4）

- [x] `ParsedLrc` 暴露 `[ti:]` `[ar:]` `[al:]` `[length:]`，含缺失、重复、非法值的 fixture。
- [x] 歌词目录扫描 + 匹配：标题歌手齐备命中、时长参与评分、
      标点与全半角差异仍命中、明显不同的曲目不命中。
- [x] sidecar 命中（`mediaSrc` 为 `file://`）；`mediaSrc` 为 `https://` 时不尝试 sidecar。
- [x] 负缓存：目录状态变化后本地 miss 立即失效；sidecar 未命中不写负缓存行。
- [x] 覆盖机制行为不变：`overrides/<provider>:<track-id>.lrc` 仍覆盖对应结果，
      既有测试迁移后通过。
- [x] 默认顺序为 `{local, netease, amll}`；既有两项配置在升级后 `local` 被前置。
- [x] 本地命中时不发起网络请求。

### 5.5 现有质量门槛

- [x] 完整运行受影响的 core / provider / daemon / frontend 测试。
- [x] 分别关闭 AMLL 与网易云编译选项仍可构建并通过测试。
- [x] QML 模块从 staging 安装树加载的检查照旧。
- [ ] 人工验证桌面与面板两种形态。

## 6. 需要同步更新的文档

- `docs/DESIGN.md`：决策清单当前到 54，新决策从 **55** 起。需记录
  Q12 的有意例外（`LyricStore` 只有守护进程与配置对话框两类调用方）、
  Q21 的接口收窄、Q13 的新 provider、Q3 的新配置键。
  另需修订 2.2 节中「`lyric.offsetMs` 自决策 41 起语义固定为 per-track 原始值」
  一句——Q9 把它改为生效值。
- `CHANGELOG.md`：按 Q16，在 `## 未发布` 写三条 Fixed（面板卡顿、
  歌词加载后进度偏移、「歌词服务」页改动点确定后未生效），
  一条 Added（可搜索的本地歌词源），
  并改写现有那条「可在「歌词服务」设置页调整全局顺序」的 Added 文案以描述新控件。
- `README.md` / `README.en.md`：新增本地歌词源与歌词目录；
  区分「歌词目录」与既有的「覆盖目录」。
- `docs/MULTI_PROVIDER_PLAN.md:19` 用 `netease → amll` 描述默认顺序，
  按 Q19 改为三项；该写法在新控件下不再能写坏配置，但描述本身需要正确。

## 7. 实施记录

2026-09-10 已按四阶段顺序完成实现并拆分提交：

1. 位置轮询无条件同步 `positionUs` 与 `anchorMonotonicNs`，并补普通轮询回归测试。
2. 部件移除全部 SQLite 访问；快照提供全局开关和生效偏移，偏移修改改走带指纹校验的
   `AdjustOffset` / `ResetOffset`，写入后重发快照。
3. 「歌词服务」页接入 `unsavedChanges` / `saveConfig()`，来源配置改为拖拽排序与勾选集合；
   来源通过 D-Bus 发现，停机时降级，未知 id 保留；强制解析进度由快照表达。
4. `local` 成为可搜索来源，支持音频 sidecar 与歌词目录；LRC ID 标签参与统一 Matcher；
   覆盖读取器移入 `core/store/`，`Provider` 接口移除覆盖钩子。

自动化验证覆盖 core/provider/daemon/frontend 单元测试、私有 D-Bus 控制压力、Qt 6 QML lint，
并构建关闭 AMLL、关闭网易云的配置。Q24 保持待决，未改变黑名单与自定义 URL 前缀的空值语义。

QA 回归修正进一步将 D-Bus 的「构建支持来源」与 Resolver 的「启用解析链」分离，并把本地音频
请求的不可负缓存属性贯穿搜索、获取、空歌词和过滤后为空的全部失败路径；禁用来源重启恢复与
sidecar 后增/原地修复均有自动化覆盖。普通 MPRIS 播放器没有 `kde:mediaSrc` 时，本地查询回退
到标准 `xesam:url`，不触碰既有指纹和身份判定。

审查回归进一步为本地歌词目录建立按 `cacheVersion()` 失效的递归内存索引，只收录可读且含
有效时间行的 `.lrc`，每次 provider 尝试只扫描一次目录状态，并把交给 Resolver 的候选限制为
前 50 条；损坏 sidecar 会继续查询目录。全局偏移恢复无当前歌曲时的部件菜单调整，设置页刷新
只重发当前指纹对应的快照。D-Bus 压力测试拆为可直接发现的
`allMethodsCompleteUnderConcurrentDbusLoad()`。
