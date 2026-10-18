#!/usr/bin/env python3
"""Plans how tools/build_web.sh packs content/ for the website.

    python3 tools/web_parts.py <repository> <stage folder> <part size limit in MB>

Writes one <name>.list per part into the stage folder (the paths of the
files in it, one a line) and prints the part names, one a line.

The parts are chosen so that a change to content/ touches as few of them as
possible, because git keeps every rebuilt part whole and the browser only
fetches the parts whose version changed:

  text        the scenarios' scripts and the pack's text files at its root
              (what changes most, and small)
  maps-<act>  the maps' data and area pictures, for the act whose scenarios
              use the map first (act1 to act4, tower for the Tower of Ordeal)
  enemies-<act>  each monster kind's pictures, for the act it first appears in
  common      everything else: the shared sheets, the interface, the player,
              the people and the monsters no scenario places

A part that would pass the limit is split in path order (-2, -3, ...), so
the files inside a part stay in the same order from build to build.
"""
import os
import re
import sys


def act_of(scenario):
    """The act a scenario of the pack belongs to: 10xxxxxx and 11xxxxxx are
    the first, 12 to 14 the second to fourth, 19 the Tower of Ordeal."""
    group = int(scenario) // 1000000 - 10
    return 'tower' if group == 9 else f'act{max(group, 1)}'


def main():
    repo, stage, limit_mb = sys.argv[1], sys.argv[2], int(sys.argv[3])
    limit = limit_mb * 1024 * 1024
    content = os.path.join(repo, 'content')

    first_map, first_enemy = {}, {}
    scenarios = sorted(d for d in os.listdir(os.path.join(content, 'Scenario')) if d.isdigit())
    for scenario in scenarios:  # ascending, so the first act that uses one wins
        act = act_of(scenario)
        text = open(os.path.join(content, 'Scenario', scenario, 'scenario.txt'), encoding='utf-8').read()
        for name in re.findall(r'^map=(\S+)', text, re.M):
            first_map.setdefault(name, act)
        for kind in re.findall(r'^enemy=(\d+),', text, re.M):
            first_enemy.setdefault(f'{int(kind):08d}', act)

    def group(rel):
        top, _, rest = rel.partition('/')
        if '/' not in rel or top == 'Scenario':
            return 'text' if rel.endswith(('.txt', '.md')) or top == 'Scenario' else 'common'
        if top == 'Map':
            stem = os.path.splitext(rest.removeprefix('Overview/'))[0]
            if '/' not in rest.removeprefix('Overview/') and stem in first_map and (
                    rest.startswith('Overview/') or rest.endswith('.txt')):
                return f'maps-{first_map[stem]}'
        if top == 'Character' and rest.startswith('ENEMY/'):
            kind = rest.split('/')[1]
            if kind in first_enemy:
                return f'enemies-{first_enemy[kind]}'
        return 'common'

    groups = {}
    for root, dirs, names in os.walk(content):
        dirs.sort()
        for name in sorted(names):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, repo).replace(os.sep, '/')
            groups.setdefault(group(os.path.relpath(path, content).replace(os.sep, '/')), []).append((rel, os.path.getsize(path)))

    os.makedirs(stage, exist_ok=True)
    for name in sorted(groups):
        parts, size = [[]], 0
        for rel, n in groups[name]:
            if n > limit:
                sys.exit(f'{rel} is bigger than {limit_mb} MB on its own')
            if size + n > limit and parts[-1]:
                parts.append([])
                size = 0
            parts[-1].append(rel)
            size += n
        for i, part in enumerate(parts, 1):
            part_name = name if i == 1 else f'{name}-{i}'
            with open(os.path.join(stage, f'{part_name}.list'), 'w') as f:
                f.write('\n'.join(part) + '\n')
            print(part_name)


main()
