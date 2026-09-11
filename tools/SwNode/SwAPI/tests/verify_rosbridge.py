#!/usr/bin/env python3
"""Exercise real rosbridge WebSocket envelopes against native IPC (no ROS dependency)."""
import argparse
import base64
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request
import urllib.error
import uuid


class WebSocket:
    def __init__(self, port, path="/"):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.buffer = b""
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {path} HTTP/1.1\r\nHost: localhost:{port}\r\n"
                           "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                           f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n").encode())
        while b"\r\n\r\n" not in self.buffer:
            self.buffer += self.sock.recv(4096)
        header, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        assert header.startswith(b"HTTP/1.1 101"), header
        expected = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest())
        assert expected in header, header

    def close(self):
        self.sock.close()

    def frame(self, payload, opcode=1, fin=True):
        mask = os.urandom(4)
        length = len(payload)
        header = bytes([(128 if fin else 0) | opcode])
        if length < 126:
            header += bytes([128 | length])
        elif length < 65536:
            header += bytes([128 | 126]) + struct.pack("!H", length)
        else:
            header += bytes([128 | 127]) + struct.pack("!Q", length)
        self.sock.sendall(header + mask + bytes(x ^ mask[i % 4] for i, x in enumerate(payload)))

    def send(self, message):
        self.frame(json.dumps(message).encode())

    def read(self, size):
        while len(self.buffer) < size:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError("WebSocket closed")
            self.buffer += data
        result, self.buffer = self.buffer[:size], self.buffer[size:]
        return result

    def receive_frame(self):
        first, second = self.read(2)
        assert not second & 128, "server frames must not be masked"
        length = second & 127
        if length == 126:
            length = struct.unpack("!H", self.read(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self.read(8))[0]
        return first & 15, self.read(length)

    def receive(self):
        opcode, payload = self.receive_frame()
        assert opcode == 1, (opcode, payload)
        return json.loads(payload)

    def call(self, service, args=None, **extra):
        identifier = uuid.uuid4().hex
        self.send(dict(op="call_service", id=identifier, service=service,
                       args={} if args is None else args, **extra))
        result = self.receive()
        assert result["op"] == "service_response" and result["id"] == identifier, result
        assert result["service"] == service, result
        return result


def ports():
    for _ in range(100):
        with socket.socket() as first:
            first.bind(("127.0.0.1", 0))
            port = first.getsockname()[1]
            if port == 65535:
                continue
            try:
                with socket.socket() as second, socket.socket() as ros:
                    second.bind(("127.0.0.1", port + 1))
                    ros.bind(("127.0.0.1", 0))
                    return port, ros.getsockname()[1]
            except OSError:
                pass
    raise RuntimeError("no free port pair")


def wait_port(process, port):
    until = time.monotonic() + 10
    while time.monotonic() < until:
        assert process.poll() is None, f"bridge exited {process.returncode}"
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=.2):
                return
        except OSError:
            time.sleep(.05)
    raise TimeoutError("bridge did not listen")


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def verify(bridge, fixture, work):
    domain = "swros_test_" + uuid.uuid4().hex[:12]
    http, ros = ports()
    target = domain + "/demo/counter"
    mapping = {
        "services": {
            "/old/add": {"target": target, "method": "add", "fields": ["a", "b"],
                         "type": "example_interfaces/srv/AddTwoInts", "response_field": "sum"}},
        "topics": {"/old/input": {"target": target, "signal": "input", "type": "std_msgs/msg/Int32"}},
        "parameters": {"/old/limit": {"target": target, "path": "limit"}},
    }
    map_file = work / "bindings.json"
    map_file.write_text(json.dumps(mapping))
    log = (work / "processes.log").open("w+")
    processes, sockets = [], []
    try:
        base = [str(bridge), str(http), "--rosbridge-port", str(ros),
                "--rosbridge-domain", domain, "--rosbridge-map", str(map_file)]
        process = subprocess.Popen(base, cwd=work, stdout=log, stderr=log)
        processes.append(process)
        wait_port(process, ros)
        ws, other = WebSocket(ros), WebSocket(ros)
        sockets.extend([ws, other])
        early = WebSocket(ros)
        sockets.append(early)
        early.send({"op": "subscribe", "topic": "/demo/counter/telemetry", "type": "std_msgs/msg/Int32"})
        # The bridge must accept this request while no producer/registry entry exists.
        assert ws.call("/rosapi/nodes")["values"]["nodes"] == []
        native = subprocess.Popen([str(fixture), domain], cwd=work, stdout=log, stderr=log)
        processes.append(native)
        assert early.receive()["op"] == "publish"
        early.close()
        sockets.remove(early)
        for _ in range(50):
            if "/demo/counter" in ws.call("/rosapi/nodes")["values"]["nodes"]:
                break
            time.sleep(.05)
        else:
            raise AssertionError("native node not discovered")

        assert ws.call("/old/add", {"b": 22, "a": 20}, type="example_interfaces/AddTwoInts")["values"] == {"sum": 42}
        assert ws.call("/demo/counter/add", [2, 3])["values"] == {"result": 5}
        assert ws.call("/demo/counter/echo", {"nested": {"value": 42}})["values"] == {"nested": {"value": 42}}
        assert ws.call("/demo/counter/reset")["values"] == {}
        failure = ws.call("/demo/counter/fail")
        assert not failure["result"] and "fixture failure" in failure["values"], failure
        assert not ws.call("/missing")["result"]
        assert not ws.call("/old/add", {"a": "bad", "b": 2})["result"]
        assert not ws.call("/old/add", {"a": 1, "b": 2}, type="wrong/Type")["result"]
        print("PASS service envelopes, native RPC, JSON, aliases and errors", flush=True)

        for i in range(12):
            ws.send({"op": "call_service", "service": "/demo/counter/add", "id": str(i), "args": [i, 100]})
            other.send({"op": "call_service", "service": "/demo/counter/add", "id": str(i), "args": [i, 200]})
        for connection, offset in ((ws, 100), (other, 200)):
            responses = [connection.receive() for _ in range(12)]
            assert {r["id"] for r in responses} == {str(i) for i in range(12)}, responses
            assert all(r["result"] and r["values"]["result"] == int(r["id"]) + offset for r in responses), responses
        with concurrent.futures.ThreadPoolExecutor() as executor:
            def http_rpc():
                data = json.dumps({"target": target, "method": "slow", "args": [701]}).encode()
                request = urllib.request.Request(f"http://127.0.0.1:{http}/api/rpc", data=data,
                                                 headers={"Content-Type": "application/json"})
                with urllib.request.urlopen(request, timeout=4) as response:
                    return json.load(response)
            pending = executor.submit(http_rpc)
            result = ws.call("/demo/counter/slow", [702])
            assert result["values"]["result"] == 702, result
            assert pending.result()["result"] == 701
        result = ws.call("/demo/counter/slow", [55], timeout=.02)
        assert not result["result"] and "timeout" in result["values"], result
        assert ws.call("/demo/counter/slow", [56])["values"]["result"] == 56
        print("PASS concurrent clients, shared HTTP routing and late replies", flush=True)
        for method, args, expected in (("missing", [], 404), ("add", ["wrong", 2], 400)):
            data = json.dumps({"target": target, "method": method, "args": args}).encode()
            try:
                urllib.request.urlopen(urllib.request.Request(f"http://127.0.0.1:{http}/api/rpc", data=data,
                    headers={"Content-Type": "application/json"}), timeout=3)
                raise AssertionError("HTTP RPC error did not set an HTTP error status")
            except urllib.error.HTTPError as error:
                assert error.code == expected, (error.code, expected)

        ws.send({"op": "advertise", "topic": "/old/input", "type": "std_msgs/Int32"})
        ws.send({"op": "publish", "topic": "/old/input", "msg": {"data": 73}})
        for _ in range(30):
            if ws.call("/demo/counter/seen")["values"]["result"] == 73:
                break
            time.sleep(.02)
        else:
            raise AssertionError("publish did not reach native subscriber")
        ws.send({"op": "subscribe", "id": "first", "topic": "/demo/counter/telemetry", "throttle_rate": 100})
        ws.send({"op": "subscribe", "id": "second", "topic": "/demo/counter/telemetry", "throttle_rate": 100})
        result = ws.receive()
        assert result["op"] == "publish" and result["topic"] == "/demo/counter/telemetry" and result["msg"]["data"] > 0, result
        ws.send({"op": "unsubscribe", "id": "first", "topic": "/demo/counter/telemetry"})
        assert ws.receive()["op"] == "publish"
        ws.send({"op": "unsubscribe", "topic": "/demo/counter/telemetry"})
        # Drain any publication already in flight before the unsubscribe was processed.
        ws.send({"op": "call_service", "id": "barrier", "service": "/rosapi/nodes"})
        while ws.receive().get("id") != "barrier":
            pass
        ws.sock.settimeout(.2)
        try:
            assert False, ws.receive()
        except socket.timeout:
            pass
        ws.sock.settimeout(3)
        print("PASS publish reaches IPC, subscriptions and unsubscribe IDs", flush=True)

        # Both scalar and sized native rings are introspected without assuming 4096 bytes.
        latest_url = f"http://127.0.0.1:{http}/api/signalLatest?" + urllib.parse.urlencode(
            {"target": target, "name": "largeState"})
        with urllib.request.urlopen(latest_url, timeout=3) as response:
            assert json.load(response)["args"] == ["x" * 5000]
        large = WebSocket(ros)
        sockets.append(large)
        large.send({"op": "subscribe", "topic": "/demo/counter/largeState"})
        initial = large.receive()
        assert initial["op"] == "publish" and initial["msg"]["data"] == "x" * 5000, initial
        large.send({"op": "publish", "topic": "/demo/counter/largeState", "msg": {"data": "y" * 6000}})
        updated = large.receive()
        # A single SwString topic receives the complete JSON message in its native payload.
        assert updated["op"] == "publish" and updated["msg"] == {"data": "y" * 6000}, updated
        with urllib.request.urlopen(latest_url, timeout=3) as response:
            assert json.loads(json.load(response)["args"][0]) == {"data": "y" * 6000}
        large.close()
        sockets.remove(large)
        data = json.dumps({"target": target, "name": "largeState", "args": ["z" * 7000]}).encode()
        request = urllib.request.Request(f"http://127.0.0.1:{http}/api/signal", data=data,
                                         headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(request, timeout=3) as response:
            assert json.load(response)["ok"]
        with urllib.request.urlopen(latest_url, timeout=3) as response:
            assert json.load(response)["args"] == ["z" * 7000]
        ws.send({"op": "subscribe", "topic": "/demo/counter/dynamic"})
        assert ws.receive()["msg"]["data"] == 42
        ws.send({"op": "unsubscribe", "topic": "/demo/counter/dynamic"})
        print("PASS dynamic ring discovery, sized snapshots and publications above 4096 bytes", flush=True)

        assert json.loads(ws.call("/rosapi/get_param", {"name": "/old/limit"})["values"]["value"]) == 10
        result = ws.call("/rosapi/set_param", {"name": "/old/limit", "value": "37"})
        assert result["result"] and result["values"]["successful"], result
        assert ws.call("/demo/counter/limit")["values"]["result"] == 37
        result = ws.call("/rosapi/set_param", {"name": "/demo/counter/label", "value": json.dumps('a "quote"')})
        assert result["result"] and result["values"]["successful"], result
        assert ws.call("/demo/counter/label")["values"]["result"] == 'a "quote"'
        assert not ws.call("/rosapi/set_param", {"name": "/old/limit", "value": '"wrong type"'})["values"]["successful"]
        names = ws.call("/rosapi/get_param_names")["values"]["names"]
        assert "/demo/counter:tracking.gain" in names, names
        assert json.loads(ws.call("/rosapi/get_param", {"name": "/demo/counter:limit"})["values"]["value"]) == 37
        missing = ws.call("/rosapi/get_param", {"name": "/demo/counter:missing", "default_value": "42"})
        assert missing["result"] and not missing["values"]["successful"] and missing["values"]["value"] == "42", missing
        values = ws.call("/demo/counter/get_parameters", {"names": ["limit", "tracking.gain", "absent"]})["values"]["values"]
        assert [v["type"] for v in values] == [2, 3, 0], values
        assert values[0]["integer_value"] == 37 and values[1]["double_value"] == 1.5, values
        result = ws.call("/demo/counter/set_parameters", {"parameters": [
            {"name": "limit", "value": {"type": 2, "integer_value": 39}},
            {"name": "absent", "value": {"type": 1, "bool_value": True}}]})
        assert [r["successful"] for r in result["values"]["results"]] == [True, False], result
        assert ws.call("/demo/counter/limit")["values"]["result"] == 39
        listed = ws.call("/demo/counter/list_parameters", {"prefixes": ["tracking"], "depth": 1})
        assert listed["values"]["result"]["names"] == ["tracking.gain"], listed
        # SwRemoteObject debounces its disk writes independently of applying a value.
        for _ in range(50):
            saved = list((work / "systemConfig/user").glob("*.json"))
            if any(json.loads(p.read_text()).get("limit") == 39 for p in saved):
                break
            time.sleep(.02)
        else:
            raise AssertionError(f"native config was not persisted: {saved}")
        print("PASS rosapi and ROS 2 parameters applied and persisted by native config", flush=True)

        ws.frame(b'{"op":')
        assert ws.receive()["level"] == "error"
        ws.send({"op": "send_action_goal", "id": "action"})
        assert ws.receive()["id"] == "action"
        ws.send({"op": "subscribe", "id": "compression", "topic": "/demo/counter/telemetry", "compression": "cbor"})
        assert ws.receive()["id"] == "compression"
        data = json.dumps({"op": "call_service", "service": "/rosapi/nodes", "id": "fragmented"}).encode()
        ws.frame(data[:20], fin=False)
        ws.frame(data[20:], opcode=0)
        assert ws.receive()["id"] == "fragmented"
        ws.frame(b"ping", opcode=9)
        assert ws.receive_frame() == (10, b"ping")
        transient = WebSocket(ros)
        transient.send({"op": "call_service", "service": "/demo/counter/slow", "args": [1000]})
        transient.close()
        assert ws.call("/demo/counter/add", [3, 4])["values"]["result"] == 7
        assert process.poll() is None
        print("PASS malformed JSON, unsupported operations, framing, ping and disconnect", flush=True)

        for connection in sockets:
            connection.close()
        sockets.clear()
        stop(process)
        secured = subprocess.Popen(base + ["--api-key", "test key"], cwd=work, stdout=log, stderr=log)
        processes.append(secured)
        wait_port(secured, ros)
        denied = WebSocket(ros)
        sockets.append(denied)
        assert denied.receive_frame()[0] == 8
        allowed = WebSocket(ros, "/?api_key=test%20key")
        sockets.append(allowed)
        assert allowed.call("/demo/counter/add", [1, 2])["values"]["result"] == 3
        print("PASS API-key enforcement", flush=True)
    except BaseException:
        log.flush()
        print((work / "processes.log").read_text())
        raise
    finally:
        for connection in sockets:
            connection.close()
        for process in reversed(processes):
            stop(process)
        log.close()
        subprocess.run([str(fixture), "--cleanup", domain], cwd=work, check=True, timeout=5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bridge", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="swros-test-") as directory:
        verify(arguments.bridge.resolve(), arguments.fixture.resolve(), Path(directory))
