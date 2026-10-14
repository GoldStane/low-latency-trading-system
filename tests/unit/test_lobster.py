import io
import unittest
from scripts.lobster import analyze_pair

BOOK = '1010000,20,1000000,10\n'


class LobsterTests(unittest.TestCase):
    def analyze(self, messages, books=BOOK):
        return analyze_pair(io.StringIO(messages), io.StringIO(books))

    def test_exact_time_price_and_execution_direction(self):
        report = self.analyze('34200.000000001,4,12,3,1000000,1\n')
        self.assertEqual(report['first_timestamp_ns_since_midnight'], 34200000000001)
        self.assertEqual(report['order_side_counts'], {'BID': 1, 'ASK': 0})
        self.assertEqual(report['executed_quantity']['VISIBLE_EXECUTION'], 3)
        self.assertEqual(report['mean_spread_price_units'], {'numerator': 10000, 'denominator': 1})

    def test_subnanosecond_timestamp_is_preserved(self):
        report = self.analyze('1.0000000001,1,1,1,100,1\n')
        self.assertEqual(report['first_timestamp_ns_since_midnight'],
                         {'numerator': 10000000001, 'denominator': 10})

    def test_all_types_and_halts(self):
        messages = ''.join(f'34200,{kind},12,3,1000000,1\n' for kind in range(1, 7))
        messages += ''.join(f'34200,7,0,0,{price},-1\n' for price in [-1, 0, 1])
        report = self.analyze(messages, BOOK * 9)
        self.assertEqual(report['event_count'], 9)
        self.assertEqual(report['halt_status_counts'], {'HALTED': 1, 'QUOTING': 1, 'TRADING': 1})

    def test_padding_and_crossed_books(self):
        report = self.analyze('1,1,1,1,100,1\n', '9999999999,0,-9999999999,0\n')
        self.assertIsNone(report['mean_spread_price_units'])
        report = self.analyze('1,1,1,1,100,1\n', '100,1,101,1\n')
        self.assertEqual(report['locked_or_crossed_snapshot_count'], 1)

    def test_invalid_inputs(self):
        valid = '1,1,1,1,100,1\n'
        for messages, books in [('', ''), (valid, ''), ('', BOOK),
                                ('1,8,1,1,100,1\n', BOOK),
                                ('1,1,1,1,100,0\n', BOOK),
                                ('86400,1,1,1,100,1\n', BOOK),
                                ('2,1,1,1,100,1\n' + valid, BOOK*2),
                                (valid, '9999999999,1,100,1\n'),
                                (valid, '100,1,99,1,98,1,97,1\n'),
                                (valid, '100,1,99\n')]:
            with self.subTest(messages=messages, books=books), self.assertRaises(ValueError):
                self.analyze(messages, books)
