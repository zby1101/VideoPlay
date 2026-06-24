# Third Party Notices

This project uses third-party libraries. The source code of this project is licensed under the MIT License, but third-party libraries remain under their own licenses.

This document is provided for license notice and compliance tracking. It is not legal advice.

## Qt

- Website: https://www.qt.io/
- License: Commercial license or open-source licenses such as LGPL/GPL, depending on the Qt version, module and distribution method.
- Used modules in this project:
  - Qt Core
  - Qt Gui
  - Qt Widgets
  - Qt Network
  - Qt OpenGL

When using Qt under LGPL, make sure the application distribution satisfies the LGPL obligations, including license notice, dynamic linking or relinking requirements, and access to the corresponding Qt source code where required.

## FFmpeg

- Website: https://ffmpeg.org/
- License: LGPL 2.1 or later by default; GPL applies when FFmpeg is built with GPL components or `--enable-gpl`.
- Used libraries in this project:
  - libavcodec
  - libavdevice
  - libavfilter
  - libavformat
  - libavutil
  - libswresample
  - libswscale
  - libpostproc

The current `VideoPlay.pro` points to a GPL shared FFmpeg build:

```pro
FFMPEG_HOME = Y:\work-1\win\video\ffmpeg-n5.1.6-18-g1bcb1be4a2-win64-gpl-shared-5.1
```

If you distribute binaries linked with this GPL FFmpeg build, you must comply with the applicable FFmpeg/GPL license requirements. If you want to reduce copyleft obligations for binary distribution, use a compatible LGPL shared FFmpeg build and verify that it was built without `--enable-gpl` and without `--enable-nonfree`.

## SDL2

- Website: https://www.libsdl.org/
- License: zlib license for SDL 2.0 and newer.
- Version used during verification:

```text
SDL2 VersionInfo: 2.30.10
```

Keep SDL2 license notices when redistributing SDL2 source or binaries.

## Runtime Distribution Checklist

Before publishing binary releases, verify the following:

- Include this project's MIT `LICENSE`.
- Include third-party license notices for Qt, FFmpeg and SDL2.
- If FFmpeg DLLs are distributed, provide the matching FFmpeg license information and source availability required by the selected FFmpeg build.
- Do not claim third-party libraries are licensed under this project's MIT License.
- Keep DLL names and attribution clear, especially for FFmpeg libraries.
