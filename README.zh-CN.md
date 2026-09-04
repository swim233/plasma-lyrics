# Plasma 6 桌面歌词

这是一个原生 Plasma 6 部件：监听会话 MPRIS 播放器，搜索并缓存网易云歌词，然后用单调时钟在
每个部件实例内推进时间轴。它同时适配桌面与面板；桌面形态只呈现歌词，面板形态则可配合
「窗口置于下方」获得 KDE 原生的全屏可见效果。

## 构建与安装

需要 CMake 3.24+、Qt 6、KDE Frameworks 6（ECM、KI18n）、Plasma 6、Qt SQLite 驱动与
C++20 编译器。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build
systemctl --user enable --now plasma-lyricsd.service
```

然后从 Plasma 部件选择器添加「桌面歌词」。外观设置按桌面/面板分成两套；服务页的设置会影响
所有歌词部件，保存后需重启 `plasma-lyricsd`。

## 诊断与数据

```sh
plasma-lyricsd --explain "歌名" "歌手"
journalctl --user -u plasma-lyricsd.service -f
```

右键菜单可把当前歌曲提前/延后 500 ms。SQLite 数据在
`~/.local/share/plasma-lyrics/lyrics.db`，手工覆盖歌词放到
`~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`。

导入 waylyrics JSON 缓存：

```sh
/usr/libexec/plasma-lyrics/import-waylyrics --source ~/.cache/waylyrics
```

许可证：GPL-2.0-or-later。
