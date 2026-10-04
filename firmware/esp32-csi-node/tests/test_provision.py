import csv
import importlib.util
import io
import types
import unittest
from pathlib import Path


PROVISION_PATH = Path(__file__).resolve().parents[1] / "provision.py"
SPEC = importlib.util.spec_from_file_location("provision", PROVISION_PATH)
provision = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(provision)


def make_args(**overrides):
    values = {name: None for name, _ in provision.CONFIG_VALUE_CHECKS}
    values["hop_dwell"] = 200
    values.update(overrides)
    return types.SimpleNamespace(**values)


def csv_rows(content):
    return list(csv.DictReader(io.StringIO(content)))


class ProvisionConfigValueTests(unittest.TestCase):
    def test_swarm_and_hopping_flags_count_as_config_values(self):
        cases = [
            {"hop_channels": "1,6,11"},
            {"seed_token": "token-123"},
            {"swarm_hb": 15},
            {"swarm_ingest": 3},
        ]

        for values in cases:
            with self.subTest(values=values):
                self.assertTrue(provision.has_config_value(make_args(**values)))

    def test_operational_flags_alone_do_not_count_as_config_values(self):
        self.assertFalse(provision.has_config_value(make_args()))

    def test_swarm_and_hopping_values_are_written_to_csv(self):
        args = make_args(
            hop_channels="1,6,11",
            hop_dwell=250,
            seed_token="token-123",
            swarm_hb=15,
            swarm_ingest=3,
        )

        rows = csv_rows(provision.build_nvs_csv(args))
        values_by_key = {row["key"]: row["value"] for row in rows}

        self.assertEqual(values_by_key["hop_count"], "3")
        self.assertEqual(values_by_key["chan_list"], "01060b")
        self.assertEqual(values_by_key["dwell_ms"], "250")
        self.assertEqual(values_by_key["seed_token"], "token-123")
        self.assertEqual(values_by_key["swarm_hb"], "15")
        self.assertEqual(values_by_key["swarm_ingest"], "3")


class OtaPskTests(unittest.TestCase):
    PSK = "ab" * 32

    def psk_file(self, content):
        import tempfile
        f = tempfile.NamedTemporaryFile("w", suffix=".psk", delete=False)
        self.addCleanup(Path(f.name).unlink)
        f.write(content)
        f.close()
        return f.name

    def test_ota_psk_file_counts_as_config_value(self):
        path = self.psk_file(self.PSK + "\n")
        self.assertTrue(provision.has_config_value(make_args(ota_psk_file=path)))

    def test_psk_is_written_to_the_security_namespace(self):
        path = self.psk_file(self.PSK.upper() + "\n")
        rows = csv_rows(provision.build_nvs_csv(make_args(ota_psk_file=path, zone="z")))
        keys = [(row["key"], row["type"]) for row in rows]
        security = keys.index(("security", "namespace"))
        self.assertLess(keys.index(("zone_name", "data")), security,
                        "csi_cfg keys must stay in the csi_cfg namespace")
        self.assertEqual(rows[security + 1]["key"], "ota_psk")
        self.assertEqual(rows[security + 1]["value"], self.PSK)

    def test_no_psk_file_means_no_security_namespace(self):
        rows = csv_rows(provision.build_nvs_csv(make_args(zone="z")))
        self.assertNotIn("security", [row["key"] for row in rows])

    def test_malformed_psk_is_refused(self):
        for bad in ("", "ab" * 31, "ab" * 33, "zz" * 32):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    provision.read_ota_psk(self.psk_file(bad))

    def test_state_keeps_the_path_not_the_key(self):
        path = self.psk_file(self.PSK)
        merged = provision.merge_state_into_args(make_args(ota_psk_file=path), {})
        self.assertEqual(merged["ota_psk_file"], path)
        self.assertNotIn(self.PSK, str(merged))
        later = make_args(zone="z")
        provision.merge_state_into_args(later, merged)
        self.assertEqual(later.ota_psk_file, path, "re-provisioning must keep the PSK")


if __name__ == "__main__":
    unittest.main()
