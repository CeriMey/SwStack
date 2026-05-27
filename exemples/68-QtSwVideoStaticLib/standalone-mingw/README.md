# Standalone Qt MinGW Video Player SDK

This folder is the source layout for a package that can be copied outside `SwStack`
after the static library and runtime files have been generated.

Generated package contents:

- `include/SwQtVideoPlayerWidget.h`: public Qt widget API
- `lib/mingw/libSwQtVideoPlayer.a`: static SwStack video player library
- `bin/`: runnable executable and Qt/MinGW runtime DLLs
- `example/`: qmake project using only this folder

Build the example after `lib/mingw/libSwQtVideoPlayer.a` exists:

```powershell
.\build.ps1
```

Deploy runtime DLLs after rebuilding:

```powershell
.\deploy-runtime.ps1
```

Run:

```powershell
.\bin\QtStaticVideoPlayer.exe "rtsp://172.16.40.80:5004/video?transport=udp"
```

The toolbar contains an editable URL field. Press `Enter`, `Open`, or
`Reconnect` to reopen the stream with the current URL.

The package is built for Qt 6.9.3 MinGW 13.1.0. To use another compiler or Qt ABI,
rebuild `libSwQtVideoPlayer.a` with the same toolchain used by your Qt project.
