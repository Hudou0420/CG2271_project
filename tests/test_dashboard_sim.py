"""Local HTTP/auth behaviour; never opens the real secrets file or Firebase."""
import importlib.util
from pathlib import Path
import ssl
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "dashboard_sim", Path(__file__).resolve().parents[1] / "esp32/tools/dashboard_sim.py"
)
sim = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sim)


class Response:
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False

    def read(self):
        return b'{"state":"LOCKED"}'


class SimulatorTests(unittest.TestCase):
    def test_transient_ssl_read_is_retried(self):
        with patch.object(sim.urllib.request, "urlopen", side_effect=[ssl.SSLError("EOF"), Response()]) as request, patch.object(sim.time, "sleep"):
            self.assertEqual(sim.http_json("GET", "https://example.invalid/status.json"), {"state": "LOCKED"})
            self.assertEqual(request.call_count, 2)

    def test_command_write_is_never_automatically_retried(self):
        with patch.object(sim.urllib.request, "urlopen", side_effect=ssl.SSLError("EOF")) as request:
            with self.assertRaises(SystemExit):
                sim.http_json("POST", "https://example.invalid/commands.json", {"action": "disarm"})
            self.assertEqual(request.call_count, 1)

    def test_public_reads_do_not_create_anonymous_accounts(self):
        with patch.object(sim, "http_json") as request:
            client = sim.Db("https://example.invalid", "public-key")
            self.assertIsNone(client.token)
            request.assert_not_called()

    def test_owner_commands_use_password_sign_in(self):
        with patch.object(sim, "http_json", return_value={"idToken": "fixture-token"}) as request, patch.object(sim.getpass, "getpass", return_value="fixture-password"):
            client = sim.Db("https://example.invalid", "public-key", "owner@example.invalid")
            self.assertEqual(client.token, "fixture-token")
            args = request.call_args.args
            self.assertIn("signInWithPassword", args[1])
            self.assertEqual(args[2]["email"], "owner@example.invalid")


if __name__ == "__main__":
    unittest.main()
