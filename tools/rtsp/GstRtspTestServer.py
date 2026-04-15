#!/usr/bin/env python3

import argparse
import signal
import sys

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstRtspServer", "1.0")

from gi.repository import GLib, Gst, GstRtspServer


def build_h264_pipeline(width: int, height: int, fps: int, bitrate_kbps: int) -> str:
    return (
        "( videotestsrc is-live=true pattern=smpte ! "
        f"video/x-raw,width={width},height={height},framerate={fps}/1 ! "
        f"x264enc tune=zerolatency speed-preset=ultrafast bitrate={bitrate_kbps} "
        f"key-int-max={fps} byte-stream=true ! "
        "rtph264pay name=pay0 pt=96 config-interval=1 )"
    )


def build_h265_pipeline(width: int, height: int, fps: int, bitrate_kbps: int) -> str:
    return (
        "( videotestsrc is-live=true pattern=ball ! "
        f"video/x-raw,width={width},height={height},framerate={fps}/1 ! "
        f"x265enc tune=zerolatency speed-preset=ultrafast bitrate={bitrate_kbps} "
        f"key-int-max={fps} ! "
        "rtph265pay name=pay0 pt=96 config-interval=1 )"
    )


def mount_stream(server: GstRtspServer.RTSPServer, mount_path: str, pipeline: str) -> None:
    factory = GstRtspServer.RTSPMediaFactory()
    factory.set_launch(pipeline)
    factory.set_shared(True)
    mounts = server.get_mount_points()
    mounts.add_factory(mount_path, factory)


def main() -> int:
    parser = argparse.ArgumentParser(description="Small RTSP test server for SwStack X11 runs.")
    parser.add_argument("--port", type=int, default=8554)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--bitrate-kbps", type=int, default=2048)
    args = parser.parse_args()

    Gst.init(None)

    server = GstRtspServer.RTSPServer()
    server.set_service(str(args.port))

    mount_stream(
        server,
        "/test",
        build_h264_pipeline(args.width, args.height, args.fps, args.bitrate_kbps),
    )
    mount_stream(
        server,
        "/hevc",
        build_h265_pipeline(args.width, args.height, args.fps, args.bitrate_kbps),
    )

    server.attach(None)

    loop = GLib.MainLoop()

    def stop_loop(*_args) -> None:
        loop.quit()

    signal.signal(signal.SIGINT, stop_loop)
    signal.signal(signal.SIGTERM, stop_loop)

    print("RTSP server ready:")
    print(f"  rtsp://127.0.0.1:{args.port}/test (H264)")
    print(f"  rtsp://127.0.0.1:{args.port}/hevc (H265)")
    sys.stdout.flush()

    loop.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
