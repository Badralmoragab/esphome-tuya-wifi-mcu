"""End-to-end tests for the extended Tuya MCU features (v2.0.0).

A real ESPHome host build of host-extended.yaml is driven over a PTY that
plays the role of the Tuya WiFi module, and observed through the native API.
The harness (process control, UART framing, API state tracking) is reused
from test_host.py without inheriting its test cases.
"""

import asyncio
import datetime
import os
import re
from pathlib import Path
import pty
import struct
import subprocess
import sys
import time
import tty
import unittest

from aioesphomeapi import APIClient, APIConnectionError

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_host  # noqa: E402
from test_host import (  # noqa: E402
    DP_DOWNLOAD,
    DP_UPLOAD,
    HEARTBEAT,
    PRODUCT_QUERY,
    STATE_QUERY,
    TIMEOUT,
    WIFI_STATE,
    WORK_MODE_QUERY,
    FirmwareTestResult,
    HostFirmwareTest,
    frame,
    value_dp,
)

GET_GMT_TIME = 0x0C
WIFI_TEST_SCAN = 0x0E
WEATHER_OPEN = 0x20
WEATHER_DATA = 0x21
DP_UPLOAD_SYNC = 0x22
DP_UPLOAD_SYNC_RESULT = 0x23
GET_NETWORK_STATUS = 0x2B
GET_MAC = 0x2D
EXTENDED_SERVICE = 0x34


def dp(dp_id, dp_type, value):
    return bytes([dp_id, dp_type]) + len(value).to_bytes(2, "big") + value


def weather_item(key, value):
    if isinstance(value, int):
        return bytes([len(key)]) + key.encode() + b"\x00\x04" + struct.pack(">i", value)
    return bytes([len(key)]) + key.encode() + b"\x01" + bytes([len(value)]) + value.encode()


class ExtendedE2ETest(unittest.IsolatedAsyncioTestCase):
    fixture = "host-extended.yaml"
    node_name = "tuya-host-extended"

    # Borrow the harness helpers (not the tests) from the original suite.
    setUpClass = HostFirmwareTest.__dict__["setUpClass"]
    read_uart = HostFirmwareTest.read_uart
    send = HostFirmwareTest.send
    send_raw = HostFirmwareTest.send_raw
    next_frame = HostFirmwareTest.next_frame
    collect_frames = HostFirmwareTest.collect_frames
    expect_frames = HostFirmwareTest.expect_frames
    on_state = HostFirmwareTest.on_state
    wait_state = HostFirmwareTest.wait_state
    disconnect_client = HostFirmwareTest.disconnect_client
    stop_host = HostFirmwareTest.stop_host
    assert_running = HostFirmwareTest.assert_running
    asyncTearDown = HostFirmwareTest.asyncTearDown

    async def asyncSetUp(self):
        self.master, self.slave = pty.openpty()
        self.addCleanup(os.close, self.master)
        self.addCleanup(os.close, self.slave)
        tty.setraw(self.slave)
        os.set_blocking(self.master, False)
        self.uart_path.symlink_to(os.ttyname(self.slave))
        self.addCleanup(self.uart_path.unlink, missing_ok=True)
        self.rx = bytearray()
        self.frames = asyncio.Queue()
        self.loop = asyncio.get_running_loop()
        self.loop.add_reader(self.master, self.read_uart)
        self.addCleanup(self.loop.remove_reader, self.master)
        log_dir = test_host.HERE / ".esphome/logs"
        log_dir.mkdir(parents=True, exist_ok=True)
        self.log_path = log_dir / f"{type(self).__name__}.{self._testMethodName}.log"
        self.log = self.log_path.open("wb")
        self.addCleanup(self.log.close)
        self.process = subprocess.Popen(
            [str(self.binary)],
            cwd=self.workspace.name,
            env={**os.environ, "ESPHOME_PREFDIR": str(Path(self.workspace.name) / self._testMethodName)},
            stdout=self.log,
            stderr=subprocess.STDOUT,
        )
        self.addAsyncCleanup(self.stop_host)
        self.client = APIClient("127.0.0.1", self.api_port, None)
        self.addAsyncCleanup(self.disconnect_client)
        async with asyncio.timeout(TIMEOUT):
            while True:
                self.assert_running()
                try:
                    await self.client.connect(login=True, log_errors=False)
                    break
                except APIConnectionError:
                    await asyncio.sleep(0.05)
        entities, services = await asyncio.wait_for(self.client.list_entities_services(), TIMEOUT)
        self.entities = {entity.name: entity.key for entity in entities}
        self.services = {service.name: service for service in services}
        self.states = {}
        self.client.subscribe_states(self.on_state)
        await self.collect_frames(0.15)

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------

    async def next_raw(self, timeout=TIMEOUT):
        """Next outgoing frame with any version byte: (version, command, payload)."""
        self.assert_running()
        raw = await asyncio.wait_for(self.frames.get(), timeout)
        self.assertEqual(raw[:2], b"\x55\xaa", raw.hex())
        self.assertEqual(raw[-1], sum(raw[:-1]) & 0xFF, raw.hex())
        return raw[2], raw[3], raw[6:-1]

    async def wait_for_log(self, pattern):
        deadline = time.monotonic() + TIMEOUT
        while time.monotonic() < deadline:
            self.log.flush()
            match = re.search(pattern, self.log_path.read_text(errors="replace"))
            if match:
                self.log_match = match
                return
            await asyncio.sleep(0.05)
        self.fail(f"Log line not found: {pattern}")

    async def call(self, service, data=None):
        await asyncio.wait_for(self.client.execute_service(self.services[service], data or {}), TIMEOUT)

    async def expect_state(self, name, value):
        await self.wait_state(name, lambda state: not getattr(state, "missing_state", False) and state.state == value)

    async def init_module(self, wifi_state=0x03, extra=None):
        """Module init handshake, answering every request the firmware queues on readiness.

        Returns the commands of the MCU-initiated requests in the order they were sent.
        """
        extra = extra or {}
        self.send(HEARTBEAT)
        await self.expect_frames([(HEARTBEAT, b"\x00")])
        self.send(PRODUCT_QUERY)
        command, payload = await self.next_frame()
        self.assertEqual(command, PRODUCT_QUERY)
        self.assertEqual(payload, b'{"p":"hostextended","v":"1.0.0","m":0,"n":1}')
        self.send(WORK_MODE_QUERY)
        requests = []
        got_work_mode = got_wifi_ack = False
        while True:
            try:
                _, command, payload = await self.next_raw(0.5)
            except TimeoutError:
                break
            if command == WORK_MODE_QUERY:
                # LED 16, reset 28, BLE LED 20: the 3-byte module-mode reply.
                self.assertEqual(payload, bytes([16, 28, 20]))
                got_work_mode = True
                self.send(WIFI_STATE, bytes([wifi_state]))
                continue
            if command == WIFI_STATE:
                self.assertEqual(payload, b"")
                got_wifi_ack = True
                continue
            requests.append(command)
            if command in extra:
                self.send(command, extra[command])
            elif command == EXTENDED_SERVICE and payload == b"\x04":
                self.send(EXTENDED_SERVICE, b"\x04\x00")
            elif command == GET_MAC:
                self.send(GET_MAC, bytes([0x00, 0x50, 0x8A, 0x06, 0xE3, 0xA2, 0xD9]))
            elif command == GET_GMT_TIME:
                t = datetime.datetime.now(datetime.timezone.utc).replace(tzinfo=None, microsecond=0)
                t += datetime.timedelta(seconds=3)
                self.sent_time = t
                self.send(
                    GET_GMT_TIME,
                    bytes([0x01, t.year - 2000, t.month, t.day, t.hour, t.minute, t.second]),
                )
            else:
                self.fail(f"Unexpected init request 0x{command:02X} {payload.hex()}")
        self.assertTrue(got_work_mode and got_wifi_ack)
        self.assertTrue({EXTENDED_SERVICE, GET_MAC, GET_GMT_TIME}.issubset(requests), requests)
        return requests

    # ------------------------------------------------------------------
    # Tests
    # ------------------------------------------------------------------

    async def test_init_queries_mac_time_and_reset_notification(self):
        await self.init_module()
        await self.expect_state("Tuya MAC", "50:8A:06:E3:A2:D9")
        # The GMT fields must become exactly the matching UTC epoch (the host
        # itself may refuse settimeofday, so the converted epoch is checked).
        await self.wait_for_log(r"Tuya GMT time .* -> epoch (\d+)")
        expected = int(self.sent_time.replace(tzinfo=datetime.timezone.utc).timestamp())
        self.assertEqual(int(self.log_match.group(1)), expected)
        await self.expect_state("Tuya Network", "Connected to router")

    async def test_requests_wait_for_module_initialisation(self):
        await self.call("query_network")
        self.assertEqual(await self.collect_frames(0.4), [])
        requests = await self.init_module(
            extra={GET_NETWORK_STATUS: b"\x04", WEATHER_OPEN: b"\x00\x02"}
        )
        # The early query is held back, then sent first once the module is ready.
        self.assertEqual(requests[0], GET_NETWORK_STATUS)
        await self.expect_state("Tuya Network", "Connected to cloud")

    async def test_cloud_connection_enables_weather_and_parses_data(self):
        await self.init_module()
        self.send(WIFI_STATE, b"\x04")
        self.assertEqual(await self.next_frame(), (WIFI_STATE, b""))
        _, command, payload = await self.next_raw()
        self.assertEqual(command, WEATHER_OPEN)
        keys, offset = [], 0
        while offset < len(payload):
            length = payload[offset]
            keys.append(payload[offset + 1 : offset + 1 + length].decode())
            offset += 1 + length
        self.assertEqual(keys, ["w.sunrise", "w.temp", "t.local", "w.date.1"])
        self.send(WEATHER_OPEN, b"\x01\x00")
        self.send(
            WEATHER_DATA,
            b"\x01" + weather_item("w.temp", -3) + weather_item("w.sunrise", "2026-10-29 06:02"),
        )
        version, command, payload = await self.next_raw()
        self.assertEqual((version, command, payload), (0x00, WEATHER_DATA, b""))
        await self.expect_state("Weather Temp", -3.0)
        await self.expect_state("Weather Sunrise", "2026-10-29 06:02")

    async def test_generic_dp_download_trigger_and_ack(self):
        await self.init_module()
        self.send(DP_DOWNLOAD, dp(70, 0x04, b"\x03"))
        await self.expect_frames([(DP_UPLOAD, dp(70, 0x04, b"\x03"))])
        await self.expect_state("Last Generic", "70:3")

    async def test_number_select_text_round_trip(self):
        await self.init_module()
        self.send(DP_DOWNLOAD, value_dp(10, 235) + dp(11, 0x04, b"\x02") + dp(12, 0x03, b"abc"))
        await self.expect_frames([
            (DP_UPLOAD, value_dp(10, 235)),
            (DP_UPLOAD, dp(11, 0x04, b"\x02")),
            (DP_UPLOAD, dp(12, 0x03, b"abc")),
        ])
        await self.wait_state("Tuya Setpoint", lambda s: abs(s.state - 23.5) < 1e-4)
        await self.expect_state("Tuya Mode", "memory")
        await self.expect_state("Tuya Label", "abc")

        self.client.number_command(self.entities["Tuya Setpoint"], -12.3)
        await self.expect_frames([(DP_UPLOAD, dp(10, 0x02, struct.pack(">i", -123)))])
        self.client.select_command(self.entities["Tuya Mode"], "on")
        await self.expect_frames([(DP_UPLOAD, dp(11, 0x04, b"\x01"))])
        self.client.text_command(self.entities["Tuya Label"], "xyz")
        await self.expect_frames([(DP_UPLOAD, dp(12, 0x03, b"xyz"))])

    async def test_invalid_enum_index_is_rejected(self):
        await self.init_module()
        self.send(DP_DOWNLOAD, dp(11, 0x04, b"\x05"))
        self.assertEqual(await self.collect_frames(0.3), [])

    async def test_bound_sensor_respects_report_delta(self):
        await self.init_module()
        await self.call("set_local_temp", {"value": 21.0})
        await self.expect_frames([(DP_UPLOAD, value_dp(13, 210))])
        await self.call("set_local_temp", {"value": 21.2})
        self.assertEqual(await self.collect_frames(0.3), [])
        await self.call("set_local_temp", {"value": 21.4})
        await self.expect_frames([(DP_UPLOAD, value_dp(13, 214))])
        await self.wait_state("Tuya Temp", lambda s: abs(s.state - 21.4) < 1e-4)

    async def test_bitmap_and_raw_downloads(self):
        await self.init_module()
        self.send(DP_DOWNLOAD, dp(14, 0x05, b"\x01\x02") + dp(15, 0x00, b"\x0a\x0b"))
        await self.expect_frames([
            (DP_UPLOAD, dp(14, 0x05, b"\x01\x02")),
            (DP_UPLOAD, dp(15, 0x00, b"\x0a\x0b")),
        ])
        await self.expect_state("Tuya Bitmap", 258.0)
        await self.expect_state("Tuya Raw", "0a0b")

    async def test_upload_actions_async_sync_and_record(self):
        await self.init_module()
        await self.call("upload_raw")
        await self.expect_frames([(DP_UPLOAD, dp(62, 0x00, b"\x0a\x0b\xff"))])
        await self.call("upload_string", {"label": "hello"})
        await self.expect_frames([(DP_UPLOAD, dp(63, 0x03, b"hello"))])

        await self.call("upload_sync")
        _, command, payload = await self.next_raw()
        self.assertEqual((command, payload), (DP_UPLOAD_SYNC, dp(60, 0x01, b"\x01")))
        self.send(DP_UPLOAD_SYNC_RESULT, b"\x01")
        await self.expect_state("Last Upload", "ok/sync")

        await self.call("upload_record")
        _, command, payload = await self.next_raw()
        # 0x0B sub-command, 0x01, time source 0x00 (module clock), then DP 61 = 125.
        self.assertEqual((command, payload), (EXTENDED_SERVICE, b"\x0b\x01\x00" + value_dp(61, 125)))
        self.send(EXTENDED_SERVICE, b"\x0b\x02")
        await self.expect_state("Last Upload", "fail/record")

    async def test_wifi_test_and_request_timeout(self):
        await self.init_module()
        # An unanswered request times out and does not block the next one.
        await self.call("query_network")
        await self.call("wifi_test")
        _, command, _ = await self.next_raw()
        self.assertEqual(command, GET_NETWORK_STATUS)
        start = time.monotonic()
        _, command, _ = await self.next_raw(4)
        self.assertEqual(command, WIFI_TEST_SCAN)
        self.assertGreaterEqual(time.monotonic() - start, 1.3)
        self.send(WIFI_TEST_SCAN, b"\x01\x50")
        await self.expect_state("Tuya WiFi Test", 80.0)

    async def test_module_reset_notification_is_acknowledged(self):
        await self.init_module()
        self.send(EXTENDED_SERVICE, b"\x05\x01")
        await self.expect_frames([(EXTENDED_SERVICE, b"\x05")])
        await self.expect_state("Last Reset", "1")

    async def test_state_query_is_batched_into_one_frame(self):
        await self.init_module()
        self.send(DP_DOWNLOAD, value_dp(10, 50))
        await self.expect_frames([(DP_UPLOAD, value_dp(10, 50))])
        await self.call("set_local_temp", {"value": 19.5})
        await self.expect_frames([(DP_UPLOAD, value_dp(13, 195))])
        self.send(STATE_QUERY)
        # Only DPs with a known value are reported, aggregated in one frame.
        await self.expect_frames([(DP_UPLOAD, value_dp(10, 50) + value_dp(13, 195))])


if __name__ == "__main__":
    unittest.main(testRunner=unittest.TextTestRunner(resultclass=FirmwareTestResult, verbosity=2))
