import unittest
from http.server import ThreadingHTTPServer
from threading import Thread
from urllib.error import HTTPError
from urllib.request import urlopen
import json

from app.dashboard_server import DashboardHandler, build_dashboard_payload, parse_simulator_csv


class DashboardContractTest(unittest.TestCase):
    def test_parse_csv_keeps_latest_measurement_and_summary(self):
        csv_text = """mode,sequence,timestamp_ms,state,heart_rate_bpm,temperature_c,quality,motion_mg,battery_mv,flags
nominal,0,7990,3,75.0,33.36,100,0,3800,1
nominal,1,9990,3,75.0,33.38,100,12,3799,1
"""
        payload = parse_simulator_csv(csv_text)
        self.assertEqual(payload["mode"], "nominal")
        self.assertEqual(payload["latest"]["heart_rate_bpm"], 75.0)
        self.assertEqual(payload["latest"]["state_label"], "有效测量")
        self.assertEqual(len(payload["rows"]), 2)

    def test_temperature_survives_csv_to_json_without_becoming_a_constant(self):
        csv_text = """mode,sequence,timestamp_ms,state,heart_rate_bpm,temperature_c,quality,motion_mg,battery_mv,flags
nominal,0,7990,3,75.0,32.18,100,0,3800,1
nominal,1,9990,3,75.0,34.62,100,0,3800,1
"""
        payload = parse_simulator_csv(csv_text)
        self.assertEqual(payload["rows"][0].get("temperature_c"), 32.18)
        self.assertEqual(payload["latest"].get("temperature_c"), 34.62)

    def test_invalid_heart_rate_is_null_and_raw_wire_value_is_preserved(self):
        csv_text = """mode,sequence,timestamp_ms,state,heart_rate_bpm,temperature_c,quality,motion_mg,battery_mv,flags
nocontact,0,7990,1,0.0,33.36,0,0,3800,2
motion,1,9990,4,0.0,33.38,20,180,3800,0
nominal,2,11990,3,75.0,33.39,100,0,3800,0
nominal,3,13990,3,75.0,33.40,100,0,3800,1
"""
        rows = parse_simulator_csv(csv_text)["rows"]
        for row in rows[:3]:
            self.assertFalse(row.get("heart_rate_valid", True))
            self.assertIsNone(row["heart_rate_bpm"])
        self.assertEqual(rows[0]["heart_rate_bpm_raw"], 0.0)
        self.assertEqual(rows[2]["heart_rate_bpm_raw"], 75.0)
        self.assertTrue(rows[3]["heart_rate_valid"])
        self.assertEqual(rows[3]["heart_rate_bpm"], 75.0)

    def test_simulated_temperature_changes_slowly_and_is_deterministic(self):
        payload = build_dashboard_payload("nominal", 12)
        temperatures = [row.get("temperature_c") for row in payload["rows"]]
        self.assertTrue(all(value is not None for value in temperatures))
        self.assertGreater(temperatures[-1], temperatures[0])
        self.assertGreater(temperatures[0], 33.3)
        self.assertLess(temperatures[-1], 33.5)
        # Hand-checked 1 Hz sample at t=7 s, carried into the 7.99 s packet.
        self.assertEqual(temperatures[0], 33.36)
        self.assertEqual(payload, build_dashboard_payload("nominal", 12))

    def test_dashboard_payload_has_stable_card_fields(self):
        payload = build_dashboard_payload("nocontact", 12)
        self.assertEqual(payload["latest"]["state_label"], "等待接触")
        self.assertIn("quality", payload["latest"])
        self.assertIn("rows", payload)
        self.assertIsNone(payload["latest"]["heart_rate_bpm"])
        self.assertFalse(payload["latest"]["heart_rate_valid"])

    def test_invalid_parameters_are_rejected_before_running_simulator(self):
        for mode, seconds in [("unknown", 12), ("nominal", 7), ("nominal", 61), ("nominal", 8.5)]:
            with self.subTest(mode=mode, seconds=seconds), self.assertRaises(ValueError):
                build_dashboard_payload(mode, seconds)


class DashboardHttpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(("127.0.0.1", 0), DashboardHandler)
        cls.thread = Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def test_api_returns_each_scenario_with_temperature_and_validity(self):
        for mode in ("nominal", "motion", "nocontact"):
            with self.subTest(mode=mode), urlopen(f"{self.base_url}/api/run?mode={mode}&seconds=12") as response:
                payload = json.load(response)
                self.assertEqual(payload["mode"], mode)
                self.assertIn("temperature_c", payload["latest"])
                self.assertIn("heart_rate_valid", payload["latest"])

    def test_bad_query_returns_json_error(self):
        for query in ("mode=unknown", "seconds=7", "seconds=61", "seconds=8.5", "seconds=abc"):
            with self.subTest(query=query):
                with self.assertRaises(HTTPError) as caught:
                    urlopen(f"{self.base_url}/api/run?{query}")
                self.assertEqual(caught.exception.code, 400)
                self.assertIn("error", json.load(caught.exception))


if __name__ == "__main__":
    unittest.main()
