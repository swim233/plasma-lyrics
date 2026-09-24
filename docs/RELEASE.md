# 发版规范

适用：在 `CHANGELOG.md` 里写或改任何条目（包括平时往 `## 未发布` 补条目）、切版本、同步 GitHub Release 正文、推送 AUR、改动 `packaging/aur/`。

---

## 1. 更新日志

### 1.1 读者与写法

读者是用户，不是开发者。每条用一句话写清用户会看到什么不同，简洁、易懂，不堆技术细节。

判断一条该不该写、写多少：不看代码的用户读完，能知道自己会遇到什么变化吗？不能，就删掉或改写。据此：

- 修复写**症状**：用户原来遇到什么问题。不写根因和实现（正则、线程、缓存结构、竞态、指针）。
- 纯内部改动不写：重构、测试、CI、只影响开发者的诊断细节。
- 数字只保留用户能感知的，比如默认值、范围、重试次数；性能测量值不写。
- 一个功能即使涉及很多方面，也只写一条，把最主要的行为写清楚，其余细节留在 `docs/DESIGN.md`。

### 1.2 格式

- 用中文写，没有英文版。
- 按变更类型分组，小节标题用英文，顺序固定：Breaking Changes → Added → Changed → Fixed → Removed → Security。空的小节省略；项目规模小，不再按模块分组。
- 一条只写一项变化，以动词开头：新增 / 更改 / 修复 / 移除。
- Breaking Changes 每条都要带 `**迁移**：`，写清用户需要做什么；不需要操作也写明。
- 代码元素用反引号，包括配置键、命令、路径、包名；界面上的名称用「」。
- 次版本发布（`x.Y.0`）开头写一行 `概要：`；修订版（`x.y.Z`）不写。
- 每个版本以一行结尾：`完整变更列表：https://github.com/swim233/plasma-lyrics/compare/<上一个 tag>...<本 tag>`。

### 1.3 位置与提取

- 更新日志写在仓库根目录的 `CHANGELOG.md`，还没发布的改动写在 `## 未发布` 下。
- 版本标题必须是 `## v<x.y.z> - <YYYY-MM-DD>`。CI 按 tag 名加 ` - ` 找到对应的一节，提取到下一个以 `## 未发布` 或 `## v<数字>` 开头的行为止。所以标题必须和 tag 完全一致，节内不能有这样开头的行。
- 提取命令以 `.github/workflows/release.yml` 里「Extract the release notes」一步为准。需要单独取某一节时，直接复制那段 awk，把 `tag` 换成目标 tag。
- `v0.1.0`、`v0.1.1`、`v0.2.0` 三节是用 `gh release view` 从当时已有的 GitHub Release 原样拷来的，保持原文，不要改写。

### 1.4 同步已发布的 Release 正文

GitHub Release 的正文只从 `CHANGELOG.md` 同步，不在网页上直接改。修改了已发布版本的某一节之后：

1. 按 1.3 的 awk 提取这一节，执行 `gh release edit <tag> --notes-file <文件>`。
2. 再用 `gh release view <tag> --json body -q .body` 取回正文，和提取结果做 diff（`diff -B -w`）。

每个改过的 tag diff 都为空，才算同步完成。

---

## 2. 切版本

每一步做到「完成标准」才进入下一步。

### 2.1 发版提交

版本号以用户给出的为准。

1. 把 `## 未发布` 改名为 `## v<x.y.z> - <当天日期>`，在节末补上完整变更列表链接，在它上方新开一个空的 `## 未发布`。如果是修订版，删掉节首的 `概要：`。
2. 把 `CMakeLists.txt` 的 `project(VERSION)` 和 `frontend/plasmoid/package/metadata.json` 的 `Version` 改成同一个版本号（构建会检查两者是否一致）。
3. 提交，消息为：

   ```
   chore(构建): 发布 <x.y.z>

   版本号与 metadata.json 同步为 <x.y.z>，未发布段落归入 v<x.y.z>。
   ```

完成标准：`git show --stat HEAD` 只有这三个文件；按 1.3 的 awk 提取 `v<x.y.z>`，结果不为空，并以完整变更列表链接结尾。

### 2.2 先推 main，再打 tag

1. 只推 `main`，不带 tag，然后等 CI 跑完这个提交：用 `gh run list --commit "$(git rev-parse HEAD)"` 找到这次运行（`--commit` 只认完整 SHA，传短 SHA 会返回空结果），再用 `gh run watch <id> --exit-status` 等它结束。
2. CI 通过后，在同一个提交上打附注 tag，tag 信息就是 tag 名：`git tag -a v<x.y.z> -m v<x.y.z> <sha>`，然后推送这个 tag。

CI 失败时就停在这一步：修好后提交新的发版提交，重新等 CI。这样公开的 tag 一定指向构建通过的提交。

完成标准：tag 指向的提交在 CI 中通过；推 tag 触发的 Release 工作流全部 job 成功。

### 2.3 核对草稿，交给用户发布

Release 工作流会建出一个草稿 Release。核对下面三项：

- 正文和按 1.3 提取的 `v<x.y.z>` 一节 diff 为空。
- 附件共 7 个：`plasma-lyrics-<v>.tar.gz`、`plasma-lyrics_<v>-1.deb13_amd64.deb`、`plasma-lyrics-<v>-1-x86_64.pkg.tar.zst`、`plasma-lyrics-bin-<v>-1-x86_64.pkg.tar.zst`、`plasma-lyrics-<v>-x86_64-bin.tar.gz`、`plasma-lyrics-<v>-aur.tar.gz`、`SHA256SUMS`。
- 下载全部附件，`sha256sum -c SHA256SUMS` 全部通过（不加 `--ignore-missing`，缺少附件时校验会失败）。

核对完后停在草稿状态，告诉用户可以发布了。发布由用户本人在 GitHub 上操作，代理只负责核对。

完成标准：三项核对都通过，并且用户已经确认发布（`gh release view v<x.y.z> --json isDraft` 为 `false`）。

### 2.4 推送 AUR

草稿期间，附件地址对外返回 404，而两个 AUR 包的 `source=` 都指向这些附件。所以这一步要等用户发布之后才做。

发版时要推送的是 `plasma-lyrics` 和 `plasma-lyrics-bin`。`plasma-lyrics-git` 的 `pkgver()` 从 git 推导版本号，发版时不用动它。本机的 AUR 仓库在 `~/aur/<包名>`。

1. 从已发布的 Release 下载 `plasma-lyrics-<v>-aur.tar.gz` 和 `SHA256SUMS`，校验后解压到临时目录。压缩包里每个包一个目录，各含 `PKGBUILD` 和 `.SRCINFO`。
2. 对两个包分别执行：
   1. `git fetch`，确认工作区干净。如果本地和 `origin/master` 分叉，先确认 `git diff HEAD origin/master` 为空，再 `git reset --hard origin/master`。
   2. 只复制 `PKGBUILD` 和 `.SRCINFO` 进去。
   3. 把仓库复制到临时目录，在副本里执行 `makepkg --verifysource`，确认公开下载地址和 sha256 都对。在副本里做，是为了不让下载的源码包混进 AUR 仓库。
   4. 提交 `upgpkg: <包名> <v>-1`，推送到 `master`。

完成标准：`curl -s 'https://aur.archlinux.org/rpc/v5/info?arg[]=plasma-lyrics&arg[]=plasma-lyrics-bin'` 返回的两个包 `Version` 都是 `<v>-1`。

---

## 3. AUR 打包

- `packaging/aur/PKGBUILD`（即 `-git` 包）是唯一手工维护的 PKGBUILD。两个发版用的 PKGBUILD 由 `packaging/aur/generate.sh` 生成：`plasma-lyrics` 用 sed 从它派生，所以 `build()`/`check()`/`package()` 只有一份；`plasma-lyrics-bin` 用模板生成，`depends` 数组原样拼进去。改依赖只改 `packaging/aur/PKGBUILD`，不要去编辑生成出来的 PKGBUILD。过去 `depends` 有第二份拷贝，结果源码包连续四个版本漏掉了守护进程直接链接的 `zlib`。想看 CI 会生成什么，就在本地运行这个脚本（用法见脚本开头）。
- `packaging/aur/namcap-check.sh` 对构建出来的**包**运行 namcap，不在白名单里的结果都会让检查失败。要对包运行，不要对 PKGBUILD 运行：PKGBUILD 里没有 ELF 数据，看不出缺少的链接，`zlib` 的缺口就是这样在一直运行着的 namcap 下漏过去的。namcap 的输出取决于分析机器上装了什么：在空容器里它什么也解析不到，会报约 25 行 "uninstalled dependency"。所以只有先装好包的 `depends`，结果才有意义。白名单里有两项，脚本里各写了原因；出现第三项就算构建失败。
- CI 只负责生成和校验 AUR 包，没有 AUR 凭据，推送从本机完成（见 2.4）。
- `plasma-lyrics-bin` 的 `source=` 指向 `plasma-lyrics-<v>-x86_64-bin.tar.gz` 附件。只要这版 PKGBUILD 还在 AUR 上，这个附件就要一直保留。
