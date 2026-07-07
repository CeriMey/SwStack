#!/usr/bin/env python3
import argparse
import base64
import select
import socket
import struct
import subprocess
import sys
import threading
import time
from collections import deque


NAL_VPS = 32
NAL_SPS = 33
NAL_PPS = 34
NAL_FU = 49
H264_NAL_SPS = 7
H264_NAL_PPS = 8
H264_NAL_FU_A = 28


def nal_type_h265(nal):
    if len(nal) < 2:
        return -1
    return (nal[0] >> 1) & 0x3F


def nal_type_h264(nal):
    if not nal:
        return -1
    return nal[0] & 0x1F


def nal_type(codec, nal):
    return nal_type_h264(nal) if codec == "h264" else nal_type_h265(nal)


def is_vcl(codec, nal):
    t = nal_type(codec, nal)
    if codec == "h264":
        return 1 <= t <= 5
    return 0 <= t < 32


def find_start_code(buf, start=0):
    i3 = buf.find(b"\x00\x00\x01", start)
    i4 = buf.find(b"\x00\x00\x00\x01", start)
    if i3 < 0:
        return i4, 4
    if i4 < 0 or i3 < i4:
        return i3, 3
    return i4, 4


def split_annexb(buf):
    out = []
    pos, sc_len = find_start_code(buf, 0)
    if pos < 0:
        return out, buf[-4:]
    pos += sc_len
    while True:
        next_pos, next_len = find_start_code(buf, pos)
        if next_pos < 0:
            return out, buf[pos - sc_len:]
        nal = buf[pos:next_pos].strip(b"\x00")
        if nal:
            out.append(nal)
        pos = next_pos + next_len
        sc_len = next_len


class AnnexBReader(threading.Thread):
    def __init__(self, ffmpeg_args, codec, queue_limit=300):
        super().__init__(daemon=True)
        self.ffmpeg_args = ffmpeg_args
        self.codec = codec
        self.frames = deque(maxlen=queue_limit)
        self.cond = threading.Condition()
        self.stop_event = threading.Event()
        self.process = None
        self.vps = None
        self.sps = None
        self.pps = None
        self.frame_counter = 0
        self.first_params = threading.Event()

    def run(self):
        self.process = subprocess.Popen(
            self.ffmpeg_args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )
        threading.Thread(target=self._drain_stderr, daemon=True).start()
        buf = b""
        pending = []
        while not self.stop_event.is_set():
            chunk = self.process.stdout.read(4096)
            if not chunk:
                if self.process.poll() is not None:
                    break
                continue
            buf += chunk
            nals, buf = split_annexb(buf)
            for nal in nals:
                t = nal_type(self.codec, nal)
                if self.codec == "h265" and t == NAL_VPS:
                    self.vps = nal
                    self._update_params_event()
                elif (self.codec == "h265" and t == NAL_SPS) or (self.codec == "h264" and t == H264_NAL_SPS):
                    self.sps = nal
                    self._update_params_event()
                elif (self.codec == "h265" and t == NAL_PPS) or (self.codec == "h264" and t == H264_NAL_PPS):
                    self.pps = nal
                    self._update_params_event()

                if is_vcl(self.codec, nal):
                    frame_nals = pending + [nal]
                    pending = []
                    with self.cond:
                        self.frame_counter += 1
                        self.frames.append((self.frame_counter, frame_nals))
                        self.cond.notify_all()
                else:
                    pending.append(nal)

    def _drain_stderr(self):
        for raw in iter(self.process.stderr.readline, b""):
            line = raw.decode("utf-8", "replace").rstrip()
            if line:
                print("[ffmpeg]", line, file=sys.stderr, flush=True)

    def _update_params_event(self):
        if self.codec == "h264":
            ready = self.sps and self.pps
        else:
            ready = self.vps and self.sps and self.pps
        if ready:
            self.first_params.set()

    def get_frame(self, timeout=1.0):
        deadline = time.time() + timeout
        with self.cond:
            while not self.frames and not self.stop_event.is_set():
                remaining = deadline - time.time()
                if remaining <= 0:
                    return None
                self.cond.wait(remaining)
            if not self.frames:
                return None
            return self.frames[-1][1]

    def next_frame_after(self, last_seen, timeout=1.0):
        deadline = time.time() + timeout
        with self.cond:
            while not self.stop_event.is_set():
                for sequence, frame in self.frames:
                    if sequence > last_seen:
                        return sequence, frame
                remaining = deadline - time.time()
                if remaining <= 0:
                    return last_seen, None
                self.cond.wait(remaining)
            return last_seen, None

    def stop(self):
        self.stop_event.set()
        if self.process and self.process.poll() is None:
            self.process.terminate()


class RtspClient(threading.Thread):
    def __init__(self, conn, addr, reader, path, fps, codec):
        super().__init__(daemon=True)
        self.conn = conn
        self.addr = addr
        self.reader = reader
        self.path = path
        self.fps = fps
        self.codec = codec
        self.session = "00000001"
        self.seq = 1
        self.timestamp = 0
        self.ssrc = 0x53485753
        self.streaming = False
        self.lock = threading.Lock()
        self.base_url = f"rtsp://127.0.0.1/{self.path}"
        self.last_frame_sequence = 0

    def run(self):
        try:
            self.conn.settimeout(0.2)
            buf = b""
            while True:
                sent_frame = False
                if self.streaming:
                    frame_started = time.time()
                    sent_frame = self._send_next_frame()
                    timeout = max(0.001, min(0.02, (1.0 / self.fps) - (time.time() - frame_started)))
                else:
                    timeout = 0.2
                self.conn.settimeout(timeout)
                try:
                    data = self.conn.recv(4096)
                except socket.timeout:
                    if self.streaming and sent_frame:
                        elapsed = time.time() - frame_started
                        remaining = (1.0 / self.fps) - elapsed
                        if remaining > 0:
                            time.sleep(remaining)
                    continue
                if not data:
                    break
                buf += data
                while b"\r\n\r\n" in buf:
                    req, buf = buf.split(b"\r\n\r\n", 1)
                    self._handle_request(req.decode("utf-8", "replace"))
        except (ConnectionError, OSError):
            pass
        finally:
            try:
                self.conn.close()
            except OSError:
                pass

    def _headers(self, request):
        lines = request.splitlines()
        first = lines[0] if lines else ""
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        return first, headers

    def _reply(self, cseq, code=200, reason="OK", headers=None, body=b""):
        headers = headers or {}
        lines = [f"RTSP/1.0 {code} {reason}", f"CSeq: {cseq}"]
        if body:
            headers.setdefault("Content-Type", "application/sdp")
            headers.setdefault("Content-Length", str(len(body)))
        for k, v in headers.items():
            lines.append(f"{k}: {v}")
        payload = ("\r\n".join(lines) + "\r\n\r\n").encode("ascii") + body
        self.conn.sendall(payload)

    def _sdp(self):
        self.reader.first_params.wait(5.0)
        sps = base64.b64encode(self.reader.sps or b"").decode("ascii")
        pps = base64.b64encode(self.reader.pps or b"").decode("ascii")
        if self.codec == "h264":
            profile_level_id = "42e01f"
            if self.reader.sps and len(self.reader.sps) >= 4:
                profile_level_id = f"{self.reader.sps[1]:02x}{self.reader.sps[2]:02x}{self.reader.sps[3]:02x}"
            fmtp = (
                "packetization-mode=1;"
                f"profile-level-id={profile_level_id};"
                f"sprop-parameter-sets={sps},{pps}"
            )
            name = "H264"
        else:
            vps = base64.b64encode(self.reader.vps or b"").decode("ascii")
            fmtp = f"sprop-vps={vps};sprop-sps={sps};sprop-pps={pps}"
            name = "H265"
        sdp = (
            "v=0\r\n"
            "o=- 0 0 IN IP4 127.0.0.1\r\n"
            f"s=Basic {name} Webcam\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "t=0 0\r\n"
            "a=control:*\r\n"
            "m=video 0 RTP/AVP 96\r\n"
            f"a=rtpmap:96 {name}/90000\r\n"
            f"a=fmtp:96 {fmtp}\r\n"
            "a=control:trackID=0\r\n"
        )
        return sdp.encode("ascii")

    def _handle_request(self, request):
        first, headers = self._headers(request)
        parts = first.split()
        if len(parts) < 1:
            return
        method = parts[0].upper()
        if len(parts) >= 2 and parts[1].lower().startswith("rtsp://"):
            self.base_url = parts[1].split("?", 1)[0].rstrip("/")
        cseq = headers.get("cseq", "1")
        if method == "OPTIONS":
            self._reply(cseq, headers={"Public": "OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN"})
        elif method == "DESCRIBE":
            self._reply(cseq, headers={"Content-Base": self.base_url + "/"}, body=self._sdp())
        elif method == "SETUP":
            self._reply(
                cseq,
                headers={
                    "Transport": "RTP/AVP/TCP;unicast;interleaved=0-1",
                    "Session": self.session,
                },
            )
        elif method == "PLAY":
            with self.reader.cond:
                self.last_frame_sequence = self.reader.frame_counter
            self.streaming = True
            self._reply(cseq, headers={"Session": self.session, "RTP-Info": f"url={self.base_url}/trackID=0;seq={self.seq};rtptime={self.timestamp}"})
        elif method == "TEARDOWN":
            self._reply(cseq, headers={"Session": self.session})
            raise ConnectionError()
        else:
            self._reply(cseq, 405, "Method Not Allowed")

    def _send_next_frame(self):
        sequence, frame = self.reader.next_frame_after(self.last_frame_sequence, timeout=0.2)
        if not frame:
            return False
        self.last_frame_sequence = sequence
        frame_ts = self.timestamp
        for nal_index, nal in enumerate(frame):
            last_nal = nal_index == len(frame) - 1
            self._send_nal(nal, frame_ts, last_nal)
        self.timestamp = (self.timestamp + int(90000 / self.fps)) & 0xFFFFFFFF
        return True

    def _send_nal(self, nal, timestamp, marker):
        if self.codec == "h264":
            self._send_nal_h264(nal, timestamp, marker)
            return
        self._send_nal_h265(nal, timestamp, marker)

    def _send_nal_h265(self, nal, timestamp, marker):
        mtu = 1200
        if len(nal) <= mtu:
            self._send_rtp(nal, timestamp, marker)
            return
        if len(nal) < 3:
            return
        original_type = nal_type(nal)
        fu_indicator = bytes([(nal[0] & 0x81) | (NAL_FU << 1), nal[1]])
        payload = nal[2:]
        offset = 0
        first = True
        max_fragment = mtu - 3
        while offset < len(payload):
            chunk = payload[offset:offset + max_fragment]
            offset += len(chunk)
            last = offset >= len(payload)
            fu_header = original_type
            if first:
                fu_header |= 0x80
            if last:
                fu_header |= 0x40
            self._send_rtp(fu_indicator + bytes([fu_header]) + chunk, timestamp, marker and last)
            first = False

    def _send_nal_h264(self, nal, timestamp, marker):
        mtu = 1200
        if len(nal) <= mtu:
            self._send_rtp(nal, timestamp, marker)
            return
        if len(nal) < 2:
            return
        original_type = nal_type_h264(nal)
        fu_indicator = bytes([(nal[0] & 0xE0) | H264_NAL_FU_A])
        payload = nal[1:]
        offset = 0
        first = True
        max_fragment = mtu - 2
        while offset < len(payload):
            chunk = payload[offset:offset + max_fragment]
            offset += len(chunk)
            last = offset >= len(payload)
            fu_header = original_type
            if first:
                fu_header |= 0x80
            if last:
                fu_header |= 0x40
            self._send_rtp(fu_indicator + bytes([fu_header]) + chunk, timestamp, marker and last)
            first = False

    def _send_rtp(self, payload, timestamp, marker):
        header = struct.pack(
            "!BBHII",
            0x80,
            (0x80 if marker else 0x00) | 96,
            self.seq & 0xFFFF,
            timestamp & 0xFFFFFFFF,
            self.ssrc,
        )
        packet = header + payload
        frame = b"$" + bytes([0]) + struct.pack("!H", len(packet)) + packet
        with self.lock:
            self.conn.sendall(frame)
        self.seq = (self.seq + 1) & 0xFFFF


def build_ffmpeg_args(args):
    codec_args = [
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-tune",
        "zerolatency",
        "-profile:v",
        "baseline",
        "-pix_fmt",
        "yuv420p",
        "-b:v",
        f"{args.bitrate_kbps}k",
        "-maxrate",
        f"{args.bitrate_kbps}k",
        "-bufsize",
        f"{args.bitrate_kbps}k",
        "-g",
        str(args.fps),
        "-bf",
        "0",
        "-x264-params",
        f"keyint={args.fps}:min-keyint={args.fps}:scenecut=0:repeat-headers=1",
        "-f",
        "h264",
    ]
    if args.codec == "h265":
        codec_args = [
            "-c:v",
            "libx265",
            "-preset",
            "ultrafast",
            "-tune",
            "zerolatency",
            "-profile:v",
            "main",
            "-pix_fmt",
            "yuv420p",
            "-b:v",
            f"{args.bitrate_kbps}k",
            "-maxrate",
            f"{args.bitrate_kbps}k",
            "-bufsize",
            f"{args.bitrate_kbps}k",
            "-g",
            str(args.fps),
            "-bf",
            "0",
            "-x265-params",
            f"vbv-maxrate={args.bitrate_kbps}:vbv-bufsize={args.bitrate_kbps}:keyint={args.fps}:min-keyint={args.fps}:scenecut=0:bframes=0:ref=1:repeat-headers=1:aud=1:open-gop=0:log-level=error",
            "-f",
            "hevc",
        ]
    if args.source == "testsrc":
        input_args = [
            "-f",
            "lavfi",
            "-i",
            f"{args.test_pattern}=size={args.width}x{args.height}:rate={args.fps}",
        ]
    else:
        input_format_args = [
            "-pixel_format",
            args.pixel_format,
        ]
        if args.input_codec:
            input_format_args = [
                "-vcodec",
                args.input_codec,
            ]
        input_args = [
            "-f",
            "dshow",
            "-rtbufsize",
            "64M",
            "-video_size",
            args.input_size,
            "-framerate",
            str(args.input_fps),
            *input_format_args,
            "-i",
            f"video={args.device}",
        ]
    return [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "warning",
        *input_args,
        "-an",
        "-vf",
        f"fps={args.fps},scale={args.width}:{args.height},format=yuv420p",
        *codec_args,
        "pipe:1",
    ]


def main():
    parser = argparse.ArgumentParser(description="Minimal RTSP/TCP H265 server backed by FFmpeg.")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=5004)
    parser.add_argument("--path", default="webcam")
    parser.add_argument("--codec", choices=["h264", "h265"], default="h265")
    parser.add_argument("--source", choices=["dshow", "testsrc"], default="dshow")
    parser.add_argument("--test-pattern", default="testsrc2")
    parser.add_argument("--device", default="OBSBOT Virtual Camera")
    parser.add_argument("--input-codec", choices=["mjpeg", "h264"], default="")
    parser.add_argument("--input-size", default="720x1280")
    parser.add_argument("--input-fps", type=int, default=60)
    parser.add_argument("--pixel-format", default="nv12")
    parser.add_argument("--width", type=int, default=720)
    parser.add_argument("--height", type=int, default=1280)
    parser.add_argument("--fps", type=int, default=30)
    parser.add_argument("--bitrate-kbps", type=int, default=500)
    args = parser.parse_args()

    reader = AnnexBReader(build_ffmpeg_args(args), args.codec)
    reader.start()
    if not reader.first_params.wait(10.0):
        print("Timed out waiting for H265 VPS/SPS/PPS from FFmpeg", file=sys.stderr)
        return 1

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.port))
    server.listen(4)
    print(f"RTSP ready: rtsp://127.0.0.1:{args.port}/{args.path}")
    print(f"{args.codec.upper()} {args.width}x{args.height}@{args.fps} {args.bitrate_kbps}kbps")
    sys.stdout.flush()

    try:
        while True:
            readable, _, _ = select.select([server], [], [], 0.5)
            if not readable:
                continue
            conn, addr = server.accept()
            print(f"client {addr[0]}:{addr[1]}", flush=True)
            RtspClient(conn, addr, reader, args.path, args.fps, args.codec).start()
    except KeyboardInterrupt:
        pass
    finally:
        reader.stop()
        server.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
