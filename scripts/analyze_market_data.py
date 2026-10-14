#!/usr/bin/env python3
"""Strict, streaming offline analysis of normalized book-update CSV files."""

import argparse
import csv
from dataclasses import dataclass
from enum import Enum
from fractions import Fraction
import json
from pathlib import Path
import re
import sys


FIELDS = ["exchange_timestamp_ns", "instrument_id", "sequence", "price", "quantity", "side", "update_type"]
UINT64_MAX = (1 << 64) - 1
INT64_MAX = (1 << 63) - 1
UINT32_MAX = (1 << 32) - 1


class DataError(ValueError):
    """Input cannot be safely interpreted under the declared data contract."""


def unsigned(text, field, maximum, minimum=0):
    if not re.fullmatch(r"[0-9]+", text) or len(text) > 20:
        raise DataError(f"{field}: expected an unsigned integer")
    value = int(text)
    if not minimum <= value <= maximum:
        raise DataError(f"{field}: outside [{minimum}, {maximum}]")
    return value


def price_to_ticks(text, tick_size):
    # Fraction parses decimal strings exactly, independently of Decimal context.
    if len(text) > 64 or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", text):
        raise DataError("price: expected a positive plain decimal")
    ticks = Fraction(text) / tick_size
    if ticks.denominator != 1 or not 1 <= ticks <= INT64_MAX:
        raise DataError("price: must be positive, tick-aligned, and fit int64")
    return int(ticks)


class Side(str, Enum):
    BID = "BID"
    ASK = "ASK"


class UpdateType(str, Enum):
    ADD = "ADD"
    MODIFY = "MODIFY"
    DELETE = "DELETE"


def parse_enum(text, enum_type, field):
    try:
        return enum_type(text)
    except ValueError as exc:
        choices = ", ".join(item.value for item in enum_type)
        raise DataError(f"{field}: expected one of {choices}") from exc


@dataclass(frozen=True)
class BookUpdate:
    exchange_timestamp_ns: int
    instrument_id: int
    sequence: int
    price_ticks: int
    quantity: int
    side: Side
    update_type: UpdateType


def load_updates(stream, tick_size):
    """Yield normalized book updates in file order; never repair or skip bad records."""
    reader = csv.DictReader(stream, strict=True)
    try:
        if reader.fieldnames != FIELDS:
            raise DataError(f"header must be exactly: {','.join(FIELDS)}")
        for row in reader:
            try:
                if None in row or any(value is None for value in row.values()):
                    raise DataError("incorrect number of columns")
                side = parse_enum(row["side"], Side, "side")
                update_type = parse_enum(row["update_type"], UpdateType, "update_type")
                quantity = unsigned(row["quantity"], "quantity", UINT32_MAX)
                if update_type is UpdateType.DELETE:
                    if quantity != 0:
                        raise DataError("quantity: DELETE requires zero")
                elif quantity == 0:
                    raise DataError("quantity: ADD and MODIFY require a positive quantity")
                yield BookUpdate(
                    unsigned(row[FIELDS[0]], FIELDS[0], INT64_MAX),
                    unsigned(row[FIELDS[1]], FIELDS[1], UINT32_MAX),
                    unsigned(row[FIELDS[2]], FIELDS[2], UINT64_MAX),
                    price_to_ticks(row["price"], tick_size),
                    quantity,
                    side,
                    update_type,
                )
            except DataError as exc:
                raise DataError(f"line {reader.line_num}: {exc}") from exc
    except csv.Error as exc:
        raise DataError(f"line {reader.line_num}: malformed CSV") from exc


def ratio(value):
    return {"numerator": value.numerator, "denominator": value.denominator}


def analyze(updates):
    """Aggregate with O(number of instruments) memory and exact arithmetic."""
    instruments = {}
    count = 0
    for update in updates:
        key = update.instrument_id
        if key not in instruments:
            instruments[key] = {
                "event_count": 0, "reported_quantity_sum": 0, "quantity_price_ticks_sum": 0,
                "side_counts": {side.value: 0 for side in Side},
                "update_type_counts": {kind.value: 0 for kind in UpdateType},
                "first_exchange_timestamp_ns": update.exchange_timestamp_ns,
                "last_exchange_timestamp_ns": update.exchange_timestamp_ns,
                "first_sequence": update.sequence, "last_sequence": update.sequence,
                "open_ticks": update.price_ticks, "close_ticks": update.price_ticks,
                "low_ticks": update.price_ticks, "high_ticks": update.price_ticks,
            }
        state = instruments[key]
        if state["event_count"]:
            if update.sequence != state["last_sequence"] + 1:
                raise DataError(f"instrument {key}: sequence discontinuity at {update.sequence}")
            if update.exchange_timestamp_ns < state["last_exchange_timestamp_ns"]:
                raise DataError(f"instrument {key}: exchange timestamp moved backwards")
        state["event_count"] += 1
        state["side_counts"][update.side.value] += 1
        state["update_type_counts"][update.update_type.value] += 1
        state["reported_quantity_sum"] += update.quantity
        state["quantity_price_ticks_sum"] += update.price_ticks * update.quantity
        state["last_exchange_timestamp_ns"] = update.exchange_timestamp_ns
        state["last_sequence"] = update.sequence
        state["close_ticks"] = update.price_ticks
        state["low_ticks"] = min(state["low_ticks"], update.price_ticks)
        state["high_ticks"] = max(state["high_ticks"], update.price_ticks)
        count += 1
    if not count:
        raise DataError("input contains no updates")
    for state in instruments.values():
        state["quantity_weighted_update_price_ticks"] = (
            ratio(Fraction(state["quantity_price_ticks_sum"], state["reported_quantity_sum"]))
            if state["reported_quantity_sum"] else None
        )
    return {"event_count": count, "instruments": {str(k): instruments[k] for k in sorted(instruments)}}


def parse_tick_size(text):
    if len(text) > 64 or not re.fullmatch(r"[0-9]+(?:\.[0-9]+)?", text):
        raise argparse.ArgumentTypeError("tick size must be a positive plain decimal")
    value = Fraction(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("tick size must be positive")
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="normalized book-update CSV (UTF-8)")
    parser.add_argument("--tick-size", type=parse_tick_size,
                        help="explicit price increment, e.g. 0.01; applies to every instrument")
    parser.add_argument('--format', choices=['normalized', 'lobster'], default='normalized')
    parser.add_argument('--orderbook', type=Path, help='paired LOBSTER orderbook CSV; inferred from message filename if omitted')
    args = parser.parse_args(argv)
    if args.format == 'normalized' and (args.tick_size is None or args.orderbook is not None):
        parser.error('normalized format requires --tick-size and does not accept --orderbook')
    if args.format == 'lobster' and args.tick_size is not None:
        parser.error('LOBSTER prices use fixed USD/10000 units; omit --tick-size')
    try:
        if args.format == 'lobster':
            if __package__:
                from .lobster import analyze_pair
            else:
                from lobster import analyze_pair
            book_path = args.orderbook
            if book_path is None:
                if '_message_' not in args.input.name:
                    raise DataError('provide --orderbook or use a LOBSTER _message_ filename')
                book_path = args.input.with_name(args.input.name.replace('_message_', '_orderbook_'))
            with args.input.open(encoding='utf-8', newline='') as messages, book_path.open(encoding='utf-8', newline='') as books:
                report = analyze_pair(messages, books)
        else:
            with args.input.open(encoding="utf-8", newline="") as stream:
                report = analyze(load_updates(stream, args.tick_size))
            report.update(schema_version=2, tick_size=ratio(args.tick_size))
        print(json.dumps(report, indent=2, sort_keys=True))
    except (ValueError, OSError, UnicodeError) as exc:
        print(f"market-data analysis failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
