import unittest

from app.dashboard_server import build_dashboard_payload, parse_simulator_csv


class DashboardContractTest(unittest.TestCase):
    def test_parse_csv_keeps_latest_measurement_and_summary(self):
        csv_text = """mode,sequence,timestamp_ms,state,heart_rate_bpm,quality,motion_mg,battery_mv,flags
nominal,0,7990,3,75.0,100,0,3800,1
nominal,1,9990,3,75.0,100,12,3799,1
"""
        payload = parse_simulator_csv(csv_text)
        self.assertEqual(payload["mode"], "nominal")
        self.assertEqual(payload["latest"]["heart_rate_bpm"], 75.0)
        self.assertEqual(payload["latest"]["state_label"], "有效测量")
        self.assertEqual(len(payload["rows"]), 2)

    def test_dashboard_payload_has_stable_card_fields(self):
        payload = build_dashboard_payload("nocontact", 12)
        self.assertEqual(payload["latest"]["state_label"], "等待接触")
        self.assertIn("quality", payload["latest"])
        self.assertIn("rows", payload)


if __name__ == "__main__":
    unittest.main()
