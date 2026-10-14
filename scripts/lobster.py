"""Offline LOBSTER pair adapter; order events are not absolute level updates."""
import csv
from fractions import Fraction
from itertools import zip_longest
import re

TYPES = {1: 'SUBMISSION', 2: 'PARTIAL_CANCELLATION', 3: 'DELETION',
         4: 'VISIBLE_EXECUTION', 5: 'HIDDEN_EXECUTION', 6: 'CROSS_TRADE', 7: 'HALT'}


def integer(text, minimum=0, maximum=2**64-1):
    if len(text) > 21 or not re.fullmatch(r'-?[0-9]+', text):
        raise ValueError('expected integer')
    value = int(text)
    if not minimum <= value <= maximum:
        raise ValueError('integer out of range')
    return value


def analyze_pair(messages, books):
    """Stream aligned rows. Prices are USD/10000; time is local since midnight."""
    result = {'format': 'lobster', 'schema_version': 1,
              'price_unit': 'USD/10000', 'timestamp_basis': 'nanoseconds_since_midnight',
              'event_count': 0, 'event_type_counts': {name: 0 for name in TYPES.values()},
              'order_side_counts': {'BID': 0, 'ASK': 0},
              'executed_quantity': {'VISIBLE_EXECUTION': 0, 'HIDDEN_EXECUTION': 0, 'CROSS_TRADE': 0},
              'halt_status_counts': {'HALTED': 0, 'QUOTING': 0, 'TRADING': 0},
              'two_sided_snapshot_count': 0, 'locked_or_crossed_snapshot_count': 0}
    previous_time = None
    width = None
    spread_sum = 0
    readers = (csv.reader(messages, strict=True), csv.reader(books, strict=True))
    row_number = 0
    try:
        for row_number, (message, book) in enumerate(zip_longest(*readers), 1):
            if message is None or book is None:
                raise ValueError('message/orderbook row counts differ')
            if len(message) != 6:
                raise ValueError('message must contain six columns')
            if not re.fullmatch(r'[0-9]{1,5}(?:\.[0-9]{1,18})?', message[0]):
                raise ValueError('invalid seconds since midnight')
            time_ns = Fraction(message[0]) * 1_000_000_000
            if not 0 <= time_ns < 86400_000_000_000:
                raise ValueError('time outside day')
            reported_time = (int(time_ns) if time_ns.denominator == 1 else
                             {'numerator': time_ns.numerator, 'denominator': time_ns.denominator})
            if previous_time is not None and time_ns < previous_time:
                raise ValueError('timestamp moved backwards')
            previous_time = time_ns
            kind = integer(message[1], 1, 7)
            integer(message[2])  # Provider order ID; never a feed sequence number.
            quantity = integer(message[3], 0, 2**32-1)
            price = integer(message[4], -1, 2**63-1)
            direction = integer(message[5], -1, 1)
            if kind == 7:
                if price not in (-1, 0, 1) or quantity != 0 or message[2] != '0' or direction != -1:
                    raise ValueError('invalid halt indicator')
                result['halt_status_counts'][{-1: 'HALTED', 0: 'QUOTING', 1: 'TRADING'}[price]] += 1
            else:
                if price <= 0 or quantity == 0 or direction not in (-1, 1):
                    raise ValueError('invalid order/execution message')
                if kind != 6:
                    result['order_side_counts']['BID' if direction == 1 else 'ASK'] += 1
            if not book or len(book) % 4 or (width is not None and len(book) != width):
                raise ValueError('inconsistent orderbook depth/column count')
            width = len(book)
            sides = {'ASK': [], 'BID': []}
            for offset, side, dummy in [(0, 'ASK', 9999999999), (2, 'BID', -9999999999)]:
                missing = False
                last_price = None
                for index in range(offset, width, 4):
                    level_price = integer(book[index], -9999999999, 2**63-1)
                    size = integer(book[index+1], 0, 2**32-1)
                    if level_price == dummy:
                        if size != 0:
                            raise ValueError('dummy level has nonzero size')
                        missing = True
                        continue
                    if missing or level_price <= 0 or size == 0:
                        raise ValueError('invalid occupied level')
                    if last_price is not None and ((side == 'ASK' and level_price <= last_price) or
                                                   (side == 'BID' and level_price >= last_price)):
                        raise ValueError('levels are not strictly price ordered')
                    sides[side].append((level_price, size))
                    last_price = level_price
            if row_number == 1:
                result['first_timestamp_ns_since_midnight'] = reported_time
            result['last_timestamp_ns_since_midnight'] = reported_time
            result['event_count'] += 1
            name = TYPES[kind]
            result['event_type_counts'][name] += 1
            if name in result['executed_quantity']:
                result['executed_quantity'][name] += quantity
            if sides['ASK'] and sides['BID']:
                spread = sides['ASK'][0][0] - sides['BID'][0][0]
                spread_sum += spread
                result['two_sided_snapshot_count'] += 1
                result['locked_or_crossed_snapshot_count'] += int(spread <= 0)
            result['last_snapshot'] = sides
    except (ValueError, csv.Error) as exc:
        raise ValueError(f'LOBSTER row {row_number}: {exc}') from exc
    if not result['event_count']:
        raise ValueError('LOBSTER input contains no events')
    result['depth'] = width // 4
    count = result['two_sided_snapshot_count']
    mean = Fraction(spread_sum, count) if count else None
    result['mean_spread_price_units'] = ({'numerator': mean.numerator, 'denominator': mean.denominator}
                                         if mean is not None else None)
    return result
