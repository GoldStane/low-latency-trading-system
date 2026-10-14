import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from fractions import Fraction

from scripts.analyze_market_data import DataError, FIELDS, Side, UpdateType, analyze, load_updates, price_to_ticks


ROOT = Path(__file__).resolve().parents[2]
HEADER = ','.join(FIELDS) + '\n'


def report(rows):
    return analyze(load_updates(io.StringIO(HEADER + "".join(row + ",BID,ADD\n" for row in rows.splitlines())), Fraction(1, 100)))


class MarketDataTests(unittest.TestCase):
    def test_enum_values_and_update_statistics(self):
        rows = ('10,1,1,100,10,BID,ADD\n'
                '11,1,2,101,20,ASK,ADD\n'
                '12,1,3,100,5,BID,MODIFY\n'
                '13,1,4,101,0,ASK,DELETE\n')
        updates = list(load_updates(io.StringIO(HEADER + rows), Fraction(1, 100)))
        self.assertIs(updates[0].side, Side.BID)
        self.assertIs(updates[1].side, Side.ASK)
        self.assertEqual([u.update_type for u in updates],
                         [UpdateType.ADD, UpdateType.ADD, UpdateType.MODIFY, UpdateType.DELETE])
        state = analyze(updates)['instruments']['1']
        self.assertEqual(state['side_counts'], {'BID': 2, 'ASK': 2})
        self.assertEqual(state['update_type_counts'], {'ADD': 2, 'MODIFY': 1, 'DELETE': 1})
        self.assertEqual(state['reported_quantity_sum'], 35)
        self.assertEqual(state['quantity_weighted_update_price_ticks'],
                         {'numerator': 70400, 'denominator': 7})

    def test_invalid_enums_and_quantity_semantics(self):
        for suffix in ['1,bid,ADD', '1,BUY,ADD', '1,,ADD', '1,BID,add',
                       '1,ASK,REPLACE', '1,ASK,', '0,BID,ADD', '0,ASK,MODIFY',
                       '1,ASK,DELETE', '1, BID,ADD']:
            with self.subTest(suffix=suffix), self.assertRaisesRegex(DataError, 'line 2:'):
                list(load_updates(io.StringIO(HEADER + '10,1,1,100,' + suffix + '\n'),
                                  Fraction(1, 100)))

    def test_delete_only_has_no_weighted_price(self):
        result = analyze(load_updates(io.StringIO(HEADER + '10,1,1,100,0,ASK,DELETE\n'),
                                      Fraction(1, 100)))
        self.assertIsNone(result['instruments']['1']['quantity_weighted_update_price_ticks'])

    def test_old_schema_is_rejected(self):
        with self.assertRaisesRegex(DataError, 'header must be exactly'):
            list(load_updates(io.StringIO('exchange_timestamp_ns,instrument_id,sequence,price,quantity\n'),
                              Fraction(1, 100)))

    def test_exact_price_conversion_and_bounds(self):
        self.assertEqual(price_to_ticks('100.01', Fraction(1, 100)), 10001)
        self.assertEqual(price_to_ticks('92233720368547758.07', Fraction(1, 100)), 2**63 - 1)
        for value in ['1.001', 'NaN', 'Infinity', '-1', '0', '1e2', '92233720368547758.08']:
            with self.subTest(value=value), self.assertRaises(DataError):
                price_to_ticks(value, Fraction(1, 100))

    def test_known_statistics_interleaving_and_equal_times(self):
        result = report('10,1,8,100.00,10\n9,2,0,50,1\n10,1,9,100.02,20\n11,1,10,99.99,10\n')
        self.assertEqual(result['event_count'], 4)
        state = result['instruments']['1']
        self.assertEqual(state['reported_quantity_sum'], 40)
        self.assertEqual(state['quantity_price_ticks_sum'], 400030)
        self.assertEqual(state['quantity_weighted_update_price_ticks'], {'numerator': 40003, 'denominator': 4})
        self.assertEqual([state[k] for k in ['open_ticks', 'high_ticks', 'low_ticks', 'close_ticks']],
                         [10000, 10002, 9999, 9999])

    def test_discontinuities_fail(self):
        for row in ['11,1,8,100,1', '11,1,7,100,1', '11,1,10,100,1', '9,1,9,100,1']:
            with self.subTest(row=row), self.assertRaises(DataError):
                report('10,1,8,100,1\n' + row + '\n')

    def test_malformed_rows_fail(self):
        rows = ['10,1,1,100,0', '10,1,1,100,-1', '10,1,1,100,1.5',
                '10,1,1,100,4294967296', '-1,1,1,100,1',
                '9223372036854775808,1,1,100,1', '10,4294967296,1,100,1',
                '10,1,18446744073709551616,100,1', '10,1,1,100',
                '10,1,1,100,1,extra', '10,1,1,"100,1']
        for row in rows:
            with self.subTest(row=row), self.assertRaises(DataError):
                report(row + '\n')

    def test_empty_and_bad_headers(self):
        for text in ['', HEADER, 'price,quantity\n100,1\n', HEADER.replace('price', 'quantity')]:
            with self.subTest(text=text), self.assertRaises(DataError):
                analyze(load_updates(io.StringIO(text), Fraction(1, 100)))

    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(ROOT / 'scripts/analyze_market_data.py'), *args],
                              capture_output=True, text=True, check=False)

    def test_cli_sample_is_deterministic(self):
        args = (str(ROOT / 'data/sample/trades.csv'), '--tick-size', '0.01')
        first = self.run_cli(*args)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(first.stdout, self.run_cli(*args).stdout)
        self.assertEqual(json.loads(first.stdout)['event_count'], 5)
        self.assertEqual(json.loads(first.stdout)['schema_version'], 2)

    def test_cli_failures_emit_no_report(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.csv'
            for content in [b'bad header\n', HEADER.encode() + b'1,1,1,100,1,BID,ADD\n2,1,3,100,1,ASK,ADD\n', b'\xff']:
                path.write_bytes(content)
                result = self.run_cli(str(path), '--tick-size', '0.01')
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, '')
                self.assertIn('failed', result.stderr)
            self.assertEqual(self.run_cli(str(path), '--tick-size', '0').returncode, 2)
            self.assertEqual(self.run_cli(str(path)).returncode, 2)
            self.assertEqual(self.run_cli(str(path / 'missing'), '--tick-size', '0.01').returncode, 1)


if __name__ == '__main__':
    unittest.main()
