#!/usr/bin/env python3
"""Run native tests with replayable stress seeds and machine-readable results."""
import argparse
import os
from pathlib import Path
import sys
import time
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))

class Report(unittest.TextTestResult):
    def startTest(self, test):
        self.started = time.monotonic()
        super().startTest(test)
    def stopTest(self, test):
        case = ET.SubElement(self.xml, 'testcase', name=test.id(), time=str(time.monotonic()-self.started))
        for kind, entries in [('failure', self.failures), ('error', self.errors)]:
            for failed, detail in entries:
                if failed is test:
                    ET.SubElement(case, kind).text = detail
        for skipped, reason in self.skipped:
            if skipped is test:
                ET.SubElement(case, 'skipped', message=reason)
        super().stopTest(test)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stress', action='store_true')
    args = parser.parse_args()
    output = ROOT / os.environ.get('VAULT_TEST_OUTPUT', 'test-reports/native')
    output.mkdir(parents=True, exist_ok=True)
    os.environ['VAULT_TEST_OUTPUT'] = str(output)
    suite = unittest.TestSuite()
    if args.stress:
        from test_firmware import FirmwareTests
        for seed in [8817, 1, 42, 65535, 20261028, 314159, 271828, 123456789, 4294967295, 987654321]:
            # Each case sets its own seed at execution, preserving an exact replay.
            class Seeded(FirmwareTests):
                def test_seed(self, seed=seed):
                    os.environ['VAULT_TEST_OUTPUT'] = str(output / str(seed))
                    os.environ['VAULT_TEST_SEED'] = str(seed)
                    os.environ['VAULT_TEST_ITERATIONS'] = '10000'
                    self.test_seeded_protocol_and_game_properties()
            Seeded.__qualname__ = 'Seed_' + str(seed)
            suite.addTest(Seeded('test_seed'))
    else:
        suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'), pattern='test_*.py')
    Report.xml = ET.Element('testsuite', name='native-stress' if args.stress else 'native')
    result = unittest.TextTestRunner(verbosity=2, resultclass=Report).run(suite)
    Report.xml.set('tests', str(result.testsRun))
    Report.xml.set('failures', str(len(result.failures)))
    Report.xml.set('errors', str(len(result.errors)))
    ET.ElementTree(Report.xml).write(output / 'junit.xml', encoding='unicode')
    return not result.wasSuccessful()

if __name__ == '__main__':
    sys.exit(main())
