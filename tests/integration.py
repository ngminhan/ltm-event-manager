import json
import selectors
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def frame(message):
    body = json.dumps(message, ensure_ascii=False).encode()
    return struct.pack("!I", len(body)) + body


class Peer:
    def __init__(self, port):
        self.socket = socket.create_connection(("127.0.0.1", port), timeout=4)
        self.reader = self.socket.makefile("rb")
        self.serial = 0

    def close(self):
        self.reader.close()
        self.socket.close()

    def receive(self):
        size, = struct.unpack("!I", self.reader.read(4))
        return json.loads(self.reader.read(size))

    def packet(self, op, **fields):
        self.serial += 1
        return frame(dict(type="request", op=op, id=self.serial, **fields))

    def call(self, op, **fields):
        self.socket.sendall(self.packet(op, **fields))
        message = self.receive()
        assert message["type"] == "response" and message["id"] == self.serial
        return message["data"] if message["ok"] else message["error"]["code"]


class Integration(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.db = Path(directory.name) / "events.db"
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            self.port = sock.getsockname()[1]
        self.start()

    def start(self):
        self.server = subprocess.Popen([ROOT / "server", str(self.port), self.db])
        self.addCleanup(self.stop, self.server)
        for _ in range(100):
            try:
                self.peer()
                return
            except ConnectionRefusedError:
                if self.server.poll() is not None:
                    self.fail("Server exited during startup")
                time.sleep(.02)
        self.fail("Server did not accept connections")

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)

    def peer(self):
        peer = Peer(self.port)
        self.addCleanup(peer.close)
        return peer

    def login(self, name, register=True):
        peer = self.peer()
        credentials = dict(username=name, password="demo-password")
        if register:
            self.assertEqual(peer.call("REGISTER", **credentials), {"username": name})
        self.assertEqual(peer.call("LOGIN", **credentials), {"username": name})
        return peer

    def test_event_lifecycle(self):
        an, binh, chi = map(self.login, ("an", "binh", "chi"))
        event = an.call("CREATE_EVENT", starts_at="2026-11-28T19:00:00+07:00",
                        location="Thư viện", description="Học nhóm")["event_id"]
        fields = dict(event_id=event)
        self.assertEqual(chi.call("INVITE", username="binh", **fields), "FORBIDDEN")
        self.assertEqual(an.call("INVITE", username="missing", **fields), "NOT_FOUND")
        self.assertEqual(an.call("INVITE", username="binh", **fields), {})
        self.assertEqual(binh.receive(), dict(type="notification", op="INVITE_RECEIVED",
                                              event_id=event, **{"from": "an"}))
        self.assertEqual(an.call("INVITE", username="binh", **fields), "CONFLICT")
        self.assertEqual(chi.call("ACCEPT_INVITE", **fields), "NOT_FOUND")
        self.assertEqual(chi.call("JOIN_REQUEST", **fields), {})
        self.assertEqual(an.receive()["op"], "JOIN_REQUEST_RECEIVED")
        self.assertEqual(binh.call("APPROVE_JOIN", username="chi", **fields), "FORBIDDEN")
        self.stop(self.server)
        self.start()
        an, binh = (self.login(name, False) for name in ("an", "binh"))
        self.assertEqual(binh.call("LIST_PENDING")["invites"], [dict(fields, **{"from": "an"})])
        self.assertEqual(an.call("LIST_PENDING")["join_requests"], [dict(fields, username="chi")])
        self.assertEqual(an.call("APPROVE_JOIN", username="chi", **fields), {})
        self.assertEqual(binh.call("ACCEPT_INVITE", **fields), {})
        self.assertEqual(binh.call("JOIN_REQUEST", **fields), "CONFLICT")
        self.assertEqual(an.call("LIST_EVENTS")["events"][0]["location"], "Thư viện")
        with sqlite3.connect(self.db) as db:
            self.assertEqual(db.execute("SELECT member,state FROM participation ORDER BY member").fetchall(),
                             [("binh", "joined"), ("chi", "joined")])
        self.assertEqual(an.call("LIST_PARTICIPANTS", **fields)["participants"],
                         [{"username": "binh"}, {"username": "chi"}])
        self.assertEqual(binh.call("LIST_PARTICIPANTS", **fields), "FORBIDDEN")
        binh.close()
        self.assertEqual(an.call("CREATE_EVENT", starts_at="invalid"), "BAD_REQUEST")
        self.assertEqual(an.call("CREATE_EVENT", starts_at="2026-12-01T12:00:00Z",
                                 location="A", description="B"), {"event_id": 2})
        self.assertEqual(an.call("INVITE", event_id=2, username="binh"), {})
        self.assertEqual(self.login("binh", False).call("LIST_PENDING")["invites"][0]["event_id"], 2)

    def test_pagination_and_retry(self):
        owner = self.login("owner")
        fields = dict(starts_at="2028-02-29T12:00:00Z", location="A", description='"' * 4096)
        expected = []
        for i in range(20):
            result = owner.call("CREATE_EVENT", creation_key=str(i), **fields)
            expected.append(result["event_id"])
        self.stop(self.server)
        self.start()
        owner = self.login("owner", False)
        self.assertEqual(owner.call("CREATE_EVENT", creation_key="0", **fields), {"event_id": expected[0]})
        self.assertEqual(owner.call("CREATE_EVENT", creation_key="0", **dict(fields, location="B")), "CONFLICT")
        page = owner.call("LIST_EVENTS")
        self.assertIn("next_cursor", page)
        found = []
        while True:
            found.extend(item["event_id"] for item in page["events"])
            if "next_cursor" not in page:
                break
            self.assertEqual(page["next_cursor"], found[-1])
            page = owner.call("LIST_EVENTS", after=found[-1])
        self.assertEqual(found, expected)
        for date in ("2026-02-29", "2100-02-29", "2026-04-31"):
            self.assertEqual(owner.call("CREATE_EVENT", **dict(fields, starts_at=date+"T12:00:00Z")), "BAD_REQUEST")

    def test_literal_password_and_session(self):
        password = " secret | phrase "
        p = self.peer()
        p.call("REGISTER", username="guest", password=password)
        args = json.dumps(["guest", password])
        result = subprocess.run([ROOT / "client", "localhost", str(self.port)],
                                input=f"LOGIN --json {args}\nLOGIN other | password\nLIST_EVENTS\nQUIT\n",
                                capture_output=True, text=True, check=True, timeout=5)
        first, second, third = map(json.loads, result.stdout.splitlines())
        self.assertEqual(first["data"]["username"], "guest")
        self.assertEqual(second["error"]["code"], "CONFLICT")
        self.assertTrue(third["ok"])

    def test_pipelined_auth(self):
        p = self.peer()
        credentials = dict(username="guest", password="demo")
        p.socket.sendall(p.packet("REGISTER", **credentials) + p.packet("LOGIN", **credentials)
                         + p.packet("LIST_PENDING"))
        for i in range(1, 4):
            reply = p.receive()
            self.assertEqual(reply["id"], i)
            self.assertTrue(reply["ok"])

    def test_framing_and_auth(self):
        p = self.peer()
        first = p.packet("LIST_EVENTS")
        p.socket.sendall(first[:2])
        p.socket.sendall(first[2:11])
        p.socket.sendall(first[11:] + p.packet("LIST_PENDING"))
        for i in (1, 2):
            reply = p.receive()
            self.assertEqual((reply["id"], reply["error"]["code"]), (i, "AUTH_REQUIRED"))
        self.assertEqual(p.call("LOGIN", username="a", password="x"), "INVALID_CREDENTIALS")
        self.assertEqual(p.call("REGISTER", username="a", password="x"), {"username": "a"})
        self.assertEqual(p.call("REGISTER", username="a", password="x"), "CONFLICT")
        for bad in (struct.pack("!I", 65537), frame({"op": "LOGIN"})):
            broken = self.peer()
            broken.socket.sendall(bad)
            self.assertEqual(broken.reader.read(1), b"")

    def test_slow_reader(self):
        reader = self.login("reader")
        writer = self.login("writer")
        for _ in range(8):
            writer.call("CREATE_EVENT", starts_at="2026-12-01T12:00:00Z",
                        location="A", description="x" * 4000)
        reader.socket.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1024)
        reader.socket.sendall(b"".join(reader.packet("LIST_EVENTS") for _ in range(800)))
        self.assertEqual(len(writer.call("LIST_EVENTS")["events"]), 8)

    def test_notification_at_idle_terminal(self):
        owner = self.login("owner")
        event = owner.call("CREATE_EVENT", starts_at="2026-11-28T19:00:00Z",
                           location="A", description="Test")["event_id"]
        with subprocess.Popen([ROOT / "client", "localhost", str(self.port)],
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE) as client:
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(client.stdout, selectors.EVENT_READ)

                    def message():
                        self.assertTrue(selector.select(4), "Terminal did not display a message")
                        return json.loads(client.stdout.readline())

                    for op in ("REGISTER", "LOGIN"):
                        client.stdin.write(f"{op} guest | demo-password\n".encode())
                        client.stdin.flush()
                        self.assertTrue(message()["ok"])
                    owner.call("INVITE", event_id=event, username="guest")
                    self.assertEqual(message()["op"], "INVITE_RECEIVED")
                    client.stdin.write(b"QUIT\n")
                    client.stdin.flush()
                    self.assertEqual(client.wait(timeout=4), 0)
            finally:
                self.stop(client)

    def test_large_event_id(self):
        owner = self.login("owner")
        guest = self.login("guest")
        limit = 999_999_999_999_999
        with sqlite3.connect(self.db) as db:
            db.execute("INSERT INTO events VALUES (?, ?, ?, ?, ?)",
                       (limit - 1, "owner", "2026-12-01T12:00:00Z", "A", "Seed"))
        fields = dict(starts_at="2026-12-01T12:00:00Z", location="A", description="Test",
                      creation_key="last-event")
        self.assertEqual(owner.call("CREATE_EVENT", **fields), {"event_id": limit})
        self.assertEqual(owner.call("CREATE_EVENT", **fields), {"event_id": limit})
        events = owner.call("LIST_EVENTS", after=limit - 1)["events"]
        self.assertEqual([event["event_id"] for event in events], [limit])
        self.assertEqual(owner.call("INVITE", event_id=limit, username="guest"), {})
        self.assertEqual(guest.receive()["event_id"], limit)
        self.assertEqual(guest.call("ACCEPT_INVITE", event_id=limit), {})
        self.assertEqual(owner.call("LIST_PARTICIPANTS", event_id=limit),
                         {"participants": [{"username": "guest"}]})
        self.assertEqual(owner.call("CREATE_EVENT", **dict(fields, creation_key="overflow")),
                         "INTERNAL_ERROR")
        self.assertEqual(owner.call("LIST_EVENTS", after=limit), {"events": []})

    def test_terminal_rejects_json_nul(self):
        lines = 'REGISTER --json ["mai","pass\\u0000tail"]\nLOGIN mai | pass\nQUIT\n'
        result = subprocess.run([ROOT / "client", "localhost", str(self.port)], input=lines,
                                capture_output=True, text=True, timeout=5, check=True)
        self.assertIn("Usage: REGISTER", result.stderr)
        reply = json.loads(result.stdout)
        self.assertEqual(reply["error"]["code"], "INVALID_CREDENTIALS")

    def test_terminal(self):
        lines = "REGISTER mai | demo\nLOGIN mai | demo\nLIST_PENDING\nQUIT\n"
        result = subprocess.run([ROOT / "client", "localhost", str(self.port)], input=lines,
                                capture_output=True, text=True, timeout=5, check=True)
        replies = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([r["id"] for r in replies], [1, 2, 3])
        self.assertEqual(replies[-1]["data"], {"invites": [], "join_requests": []})


if __name__ == "__main__":
    unittest.main(verbosity=2)
