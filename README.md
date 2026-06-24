# VideoPlay

基于Qt、FFmpeg、SDL2 和 QOpenGLWidget 的视频播放器。项目支持本地视频文件播放、网络视频流播放，并可以在 SDL2 和 OpenGL 两种绘制方式之间切换。

## 功能特性

- 播放本地视频文件，支持常见格式如 `mp4`、`avi`、`mkv`、`flv`、`mov` 等。
- 播放网络视频流，支持 `http`、`https`、`rtsp`、`rtmp` 等地址。
- 使用 FFmpeg 负责解封装、查找视频流和解码视频帧。
- 支持 SDL2 和 QOpenGLWidget 两种视频绘制方式。
- 可在菜单中切换绘制方式，切换前如果正在播放，切换后会继续播放。
- 支持播放、暂停、停止。
- 本地视频支持左右方向键快退、快进。
- 支持右键菜单进入或退出全屏，`Esc` 可退出全屏。
- 状态栏显示文件名、分辨率、帧率和解码器信息。
- 网络流总时长未知时，界面使用 `-` 表示未知时长。

## 开发环境

项目使用 qmake 管理工程，当前主要依赖如下：

- Qt 5.12.1 MinGW 64-bit
- Qt Widgets
- Qt OpenGL
- FFmpeg 5.x
- SDL2
- C++11

当前验证过的依赖版本：

```text
SDL2 VersionInfo: 2.30.10
FFmpeg VersionInfo: n5.1.6-18-g1bcb1be4a2-20250212
```

当前 `VideoPlay.pro` 中配置的依赖路径为 Windows 本机路径：

```pro
FFMPEG_HOME = Y:\work-1\win\video\ffmpeg-n5.1.6-18-g1bcb1be4a2-win64-gpl-shared-5.1
SDL2HOME = Y:\work-1\win\video\SDL2-2.30.10\x86_64-w64-mingw32
```

在其他机器上构建前，需要根据本机安装位置修改 `FFMPEG_HOME` 和 `SDL2HOME`。请确保 `include`、`lib` 路径有效，并且运行时能找到 FFmpeg 和 SDL2 的 DLL。

## 使用说明

1. 启动程序。
2. 通过菜单打开本地视频文件，或打开网络视频地址。
3. 使用 `播放/暂停` 按钮控制播放状态。
4. 使用 `停止` 按钮停止播放并清空画面。
5. 在菜单中选择 `使用 SDL2 绘制` 或 `使用 OpenGL 绘制` 切换渲染方式。
6. 播放本地视频时，可使用左方向键快退、右方向键快进。
7. 在视频区域右键可进入或退出全屏。
8. 全屏状态下按 `Esc` 退出全屏。

## 渲染说明

项目当前提供两种渲染方式：

- SDL2：使用 SDL Window、Renderer 和 IYUV Texture 绘制视频帧。
- OpenGL：使用 QOpenGLWidget 接入 Qt 界面，当前将视频帧转换为 RGB 图像后绘制。

切换渲染方式时，主窗口只会让当前选中的渲染器接收 `VideoDecode::videoPacket` 信号。这样可以避免 SDL2 和 OpenGL 同时消费同一个 `AVFrame`，导致重复释放或崩溃。

SDL2 全屏模式使用独立 SDL 窗口，而不是直接把嵌入 Qt 的 SDL 窗口拉成全屏。这样可以减少部分环境下全屏后黑屏或不刷新的问题。SDL2 渲染器会缓存最后一帧，在切换全屏、重建 Renderer 或刷新窗口后尽量恢复画面。

## License

本项目源码使用 MIT License，详见 [LICENSE](LICENSE)。

项目依赖 Qt、FFmpeg 和 SDL2，这些第三方库不属于本项目 MIT License 的授权范围，需要分别遵守它们各自的许可证要求。第三方依赖说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

当前 `VideoPlay.pro` 使用的是 GPL shared 版本 FFmpeg：

```pro
FFMPEG_HOME = Y:\work-1\win\video\ffmpeg-n5.1.6-18-g1bcb1be4a2-win64-gpl-shared-5.1
```

如果发布包含该 FFmpeg DLL 的二进制程序，需要遵守 FFmpeg/GPL 的相关要求。若希望降低二进制分发时的 GPL 传染风险，请改用符合要求的 LGPL shared FFmpeg 构建，并确认该 FFmpeg 没有启用 `--enable-gpl` 或 `--enable-nonfree`。
