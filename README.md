# Plasma 6 桌面歌词

<img width="961" height="409" alt="image" src="https://github.com/user-attachments/assets/e66dc90e-1889-4dab-9531-fb7821eb6000" />


[English](README.en.md)

原生 Plasma 6 部件：监听会话中的 MPRIS 播放器并显示同步歌词。首个版本经
`plasma-browser-integration` 支持浏览器中的网易云音乐，歌词源与前端留有
扩展接口。歌词上方有一行常驻的曲目信息（标题 — 歌手），桌面默认开启、
面板默认关闭，两者可独立配置。

## 构建

需要 CMake 3.24+、Qt 6、KDE Frameworks 6（ECM 与 KI18n）、Plasma 6、
Qt SQLite 驱动与 C++20 编译器。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
DESTDIR="$PWD/staging" cmake --install build
```

顶层选项 `BUILD_DAEMON`、`BUILD_PLASMOID`、`BUILD_IMPORT_WAYLYRICS` 与
`ENABLE_PROVIDER_NETEASE` 可分别关闭各部分。

安装后启动用户服务：

```sh
systemctl --user enable --now plasma-lyricsd.service
```

然后把「桌面歌词」添加到桌面或面板。将面板设为「窗口置于下方」是 Plasma
原生保持歌词在最大化窗口周围可见的方式；桌面部件无法位于普通窗口之上。

## 诊断

```sh
plasma-lyricsd --explain "歌名" "歌手"
journalctl --user -u plasma-lyricsd.service -f
```

右键菜单可将当前歌曲的时序前后调整 500 ms。手工覆盖歌词放到
`~/.local/share/plasma-lyrics/overrides/<provider>:<track-id>.lrc`。

导入 waylyrics JSON 缓存：

```sh
plasma-lyrics-import-waylyrics --source ~/.cache/waylyrics
```

## 开发检查

```sh
ctest --test-dir build --output-on-failure
/usr/lib/qt6/bin/qmllint -I build/bin frontend/plasmoid/package/contents/ui/*.qml
xmllint --noout frontend/plasmoid/package/contents/config/main.xml
QML2_IMPORT_PATH="$PWD/build/bin" plasmoidviewer -a io.github.swim233.plasma-lyrics -f planar
```

许可证：GPL-2.0-or-later。
