"""Replay JSONL without materializing a trace. File names are fixed, never user paths."""
import json
from pathlib import Path


class JsonLines:
    def __init__(self, path, count):
        self.path, self.count = Path(path), int(count)

    def __len__(self):
        return self.count

    def __iter__(self):
        with self.path.open(encoding='utf-8') as stream:
            for line in stream:
                if line.strip():
                    yield json.loads(line)


def attach_streams(result, folder):
    meta = result.get('streamed', {})
    if meta.get('schema') != 2:
        return result
    folder = Path(folder)
    for key in ('events', 'packets', 'elements'):
        result[key] = JsonLines(folder / (key + '.jsonl'), meta[key])
    result['sources'] = json.loads((folder / 'sources.json').read_text(encoding='utf-8'))
    return result
