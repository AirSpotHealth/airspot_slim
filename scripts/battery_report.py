#!/usr/bin/env python3
"""Summarize observed endurance events; never extrapolate runtime from voltage."""
import argparse
import csv
from datetime import datetime
import json
from pathlib import Path


def summarize(path):
    with Path(path).open(newline='') as stream:
        rows = list(csv.DictReader(stream))
    if not rows or rows[0].get('event') != 'start':
        raise ValueError('First event must be start after a full charge')
    times = [datetime.fromisoformat(row['timestamp'].replace('Z', '+00:00')) for row in rows]
    if any(t.tzinfo is None for t in times):
        raise ValueError('Timestamps must include timezone')
    if any(b < a for a, b in zip(times, times[1:])):
        raise ValueError('Events must be chronological')
    allowed = {'start', 'sample', 'connected', 'disconnected', 'low_battery_shutdown',
               'interrupted', 'crash', 'observation'}
    if any(row['event'] not in allowed for row in rows):
        raise ValueError('Unknown event')
    terminal = {'low_battery_shutdown', 'interrupted', 'crash'}
    if any(row['event'] in terminal for row in rows[:-1]):
        raise ValueError('Terminal event must be last')
    mode = rows[0].get('interval_s')
    if mode not in {'5', '60', '180'}:
        raise ValueError('Start interval_s must be 5, 60 or 180')
    if any(row.get('interval_s') not in {'', mode} for row in rows):
        raise ValueError('Sampling mode changed; start a separate run')
    if not rows[0].get('notes', '').strip():
        raise ValueError('Start notes must identify firmware, board, phone/app and charge conditions')
    hours = (times[-1] - times[0]).total_seconds() / 3600
    end = rows[-1]['event']
    samples = [t for t, row in zip(times, rows) if row['event'] == 'sample']
    intervals = [(b-a).total_seconds() for a,b in zip(samples, samples[1:])]
    return {'interval_s': int(mode), 'elapsed_hours': hours, 'elapsed_days': hours / 24,
            'result': 'measured_cutoff' if end == 'low_battery_shutdown' else
                      'failed_crash' if end == 'crash' else 'incomplete',
            'lower_bound_only': end not in {'low_battery_shutdown', 'crash'},
            'disconnect_events': sum(row['event'] == 'disconnected' for row in rows),
            'sample_intervals_s': {'min': min(intervals), 'max': max(intervals)} if intervals else None,
            'conditions': rows[0]['notes'],
            'limitation': 'Single-unit observation. Disconnects are not evidence of battery cutoff.'}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('events', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(summarize(args.events), indent=2))
    except (ValueError, KeyError) as error:
        parser.exit(2, f'Invalid run: {error}\n')
