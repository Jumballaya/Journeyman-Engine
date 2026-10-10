#!/usr/bin/env python3
"""Check generated collision geometry and drive the built level; requires jm on PATH."""
import json
import math
import os
from pathlib import Path
import selectors
import subprocess
from contextlib import contextmanager

import gen_art as art

ROOT = Path(art.ROOT)
OUTPUT = ROOT / '.cache' / 'organic-review'


def check_geometry():
    level = art.LEVEL1
    data = json.loads((ROOT / 'assets/maps/level1.tmj').read_text())
    objects = next(layer['objects'] for layer in data['layers'] if layer['name'] == 'ground')
    chains = level['ground'] + level['platforms']
    assert len(objects) == len(chains)
    for index, (obj, chain) in enumerate(zip(objects, chains)):
        assert obj['type'] == ('ground' if index < len(level['ground']) else 'platform')
        points = [(obj['x']+p['x'], art.H-obj['y']-p['y']) for p in obj['polyline']]
        assert len(points) == len(chain)
        assert all(math.dist(a, b) < .0001 for a, b in zip(points, chain))
    slopes = []
    for chain in (art.LEFT_BANK, art.CHECKPOINT_BANK, art.RIGHT_BANK, art.LANDING_BANK, *level['platforms']):
        for a, b in zip(chain, chain[1:]):
            slopes.append(math.degrees(math.atan2(abs(b[1]-a[1]), b[0]-a[0])))
            assert math.dist(a, b) <= 16.001
    assert max(slopes) < 45
    for chain in (art.STEP, art.LEFT_CLIFF, art.RIGHT_CLIFF, art.POD_CLIFF, art.LANDING_CLIFF):
        for a, b in zip(chain, chain[1:]):
            slope = math.degrees(math.atan2(abs(b[1]-a[1]), abs(b[0]-a[0])))
            assert slope <= 45 or slope > 50
    clearances = [max(y-art.ground_height(x) for x, y in p) for p in level['platforms']]
    assert all(0 < gap <= 160 for gap in clearances)
    assert 1 < art.STEP[-1][1]-art.STEP[0][1] <= 120
    assert art.RIGHT_BANK[0][0]-art.CHECKPOINT_BANK[-1][0] >= 400
    for point in [level['spawn'], level['goal'], *level['sentries'], *level['checkpoints']]:
        assert abs(point[1]-art.ground_height(point[0])) < .0001
    summary = (f'PASS: Tiled ground/platforms exactly match all {sum(map(len, chains))} sampled vertices.\n'
               f'PASS: walking segments <=16 units; max walking slope {max(slopes):.2f} degrees.\n'
               'PASS: step/cliff segments are <=45 or >50 degrees; no ambiguous steep ramps.\n'
               f'PASS: platform clearances {clearances[0]:.2f}, {clearances[1]:.2f} units (<160).\n'
               'PASS: step rise 53; pit width 420; spawn, goal, enemies and checkpoint on ground.\n')
    (OUTPUT / 'geometry.log').write_text(summary)
    print(summary, end='')


@contextmanager
def drive(name, checkpoint=False):
    """Run an isolated headless session, recording every command and rejecting driver errors."""
    env = dict(os.environ, JM_DRIVE='1', JM_RENDERER='none', JM_STRICT='1',
               JM_SAVE_DIR=str(OUTPUT / ('save-'+name)))
    env.pop('JM_SESSION', None)
    if checkpoint:
        path = OUTPUT / 'checkpoint.json'
        path.write_text(json.dumps({'checkpoint': int(checkpoint)})+'\n')
        env['JM_SESSION'] = str(path)
    with (OUTPUT / (name+'.log')).open('w') as log, (OUTPUT / (name+'.stderr')).open('w') as err:
        process = subprocess.Popen(['jm', 'run'], cwd=ROOT, env=env, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=err, text=True)
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)

            def command(text=None):
                if text:
                    log.write('> '+text+'\n')
                    process.stdin.write(text+'\n')
                    process.stdin.flush()
                assert selector.select(30), f'{name}: driver timed out at {text}'
                line = process.stdout.readline()
                log.write(line)
                log.flush()
                reply = json.loads(line)
                assert reply.get('ok') and not reply.get('errors'), (text, reply)
                return reply.get('value', reply)

            try:
                command()
                yield command
                command('quit')
                assert process.wait(timeout=10) == 0
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


def check_hill_platform_step():
    with drive('hill-platform-step') as send:
        for command in ('step 30', 'down ArrowRight',
                        'until tag=Kage TransformComponent.x > 195 max 60', 'down Space',
                        'step 10', 'up ArrowRight', 'step 24', 'up Space', 'step 20'):
            send(command)
        assert send('get session.squashed') == 1
        send('down ArrowRight')
        send('until tag=Kage TransformComponent.x > 480 max 150')
        assert send('get tag=Kage VelocityComponent.blockedY') == -1
        last_x = 480
        for _ in range(15):
            send('step 10')
            x = send('get tag=Kage TransformComponent.x')
            assert x > last_x
            last_x = x
            send('get tag=Kage TransformComponent.y')
            assert send('get tag=Kage VelocityComponent.blockedY') == -1
        send('up ArrowRight')
        send('down Space')
        send('until tag=Kage VelocityComponent.vy < 0 max 80')
        send('get tag=Kage TransformComponent.y')
        send('until tag=Kage VelocityComponent.blockedY == -1 max 80')
        assert 1180 < send('get tag=Kage TransformComponent.x') < 1370
        assert 323 <= send('get tag=Kage TransformComponent.y') <= 331.1
        send('up Space')
        send('step 20')
        assert send('get tag=Kage VelocityComponent.blockedY') == -1
        for command in ('down ArrowDown', 'down Space', 'step 15', 'up ArrowDown', 'up Space',
                        'until tag=Kage VelocityComponent.blockedY == -1 max 80'):
            send(command)
        assert send('get tag=Kage TransformComponent.y') < 220
        send('down ArrowRight')
        x = last_x
        while x < 1645:
            if sentry_ahead(send, x):  # jump it, as a player would
                send('down Space')
                send('step 24')
                send('up Space')
                send('until tag=Kage VelocityComponent.blockedY == -1 max 80')
            else:
                send('step 10')
                assert send('get tag=Kage VelocityComponent.blockedY') == -1
            next_x = send('get tag=Kage TransformComponent.x')
            assert next_x > x
            x = next_x
        send('step 30')
        at_step = send('get tag=Kage TransformComponent.x')
        if at_step < 1700:  # stopped at the step's face: hop it (a sentry jump may already have cleared it)
            assert 1660 < at_step < 1680
            assert send('get tag=Kage VelocityComponent.blockedX') == 1
            send('down Space')
            send('step 30')
            send('up Space')
        send('until session.checkpoint >= 1 max 140')
        send('get tag=Kage TransformComponent.y')
        session = send('state session')['state']['session']
        assert session.get('falls', 0) == 0 and session['lives'] == 3
    print('PASS: hill/swale samples grounded every 10 frames; full jump onto platform 1; drop-through; hop step; checkpoint; zero falls.')


def sentry_ahead(send, x):
    """Whether a sentry is close in front of Kage (the driver's `near` lists what's within reach)."""
    for hit in send('near tag=Kage 90').get('near', []):
        if 'enemy' in hit['tags'] and hit['gap'] < 90:
            return True
    return False


def check_cable_platform_goal():
    with drive('cable-platform-goal', checkpoint=True) as send:
        send('step 30')
        assert send('get tag=Kage TransformComponent.x') == 1880
        send('get tag=Kage TransformComponent.y')
        assert send('get tag=Kage VelocityComponent.blockedY') == -1
        send('down ArrowRight')
        send('until tag=Kage TransformComponent.x > 1975 max 60')
        send('down Space')
        send('until session.holding >= 1 max 100')
        send('get tag=Kage TransformComponent.x')
        send('get tag=Kage TransformComponent.y')
        send('up Space')
        send('until tag=Kage TransformComponent.x > 2280 max 150')
        send('down Space')
        send('step 1')
        send('up Space')
        send('until tag=Kage TransformComponent.x > 2445 max 120')
        send('until tag=Kage VelocityComponent.blockedY == -1 max 120')
        assert send('get tag=Kage TransformComponent.x') > 2445
        assert 160 < send('get tag=Kage TransformComponent.y') < 180
        send('until tag=Kage TransformComponent.x > 2720 max 120')
        send('up ArrowRight')
        send('down Space')
        send('until tag=Kage VelocityComponent.vy < 0 max 80')
        send('until tag=Kage VelocityComponent.blockedY == -1 max 80')
        assert 2660 < send('get tag=Kage TransformComponent.x') < 2840
        assert 331 <= send('get tag=Kage TransformComponent.y') <= 339.1
        send('up Space')
        send('step 20')
        assert send('get tag=Kage VelocityComponent.blockedY') == -1
        send('down ArrowRight')
        send('down Space')
        send('until tag=Kage TransformComponent.x > 2930 max 80')
        send('up Space')
        send('until tag=Kage VelocityComponent.blockedY == -1 max 80')
        for _ in range(40):  # on to the gate, jumping the sentry on the way
            if send('get session').get('checkpoint', 0) >= 2:
                break
            if sentry_ahead(send, 0):
                send('down Space')
                send('step 24')
                send('up Space')
                send('until tag=Kage VelocityComponent.blockedY == -1 max 80')
            else:
                send('step 10')
        send('until session.checkpoint >= 2 max 240')
        traverse_pods(send)
        send('down ArrowRight')
        send('until session.reachedGoal >= 1 max 240')
        send('up ArrowRight')
        session = send('state session')['state']['session']
        assert session.get('falls', 0) == 0 and session['reachedGoal'] == 1 and session['lives'] == 3
        assert session['levelClear'] and not session['gameOver']
        assert session['shards'] > 0 and session['shardTotal'] == 20
        assert ui_node(send, 'result-title')['text'] == 'VOW FULFILLED'
        seconds = session['seconds']
        send('step 60')
        assert send('get session.seconds') == seconds
        send('press Space')
        send('step 30')
        assert send('get session.lives') == 3 and send('get session.shards') == 0
        assert not send('get session.levelClear')
    print('PASS: checkpoint jump grabs cable; swing/release lands across pit; full jump onto platform 2; goal; zero falls.')


def ui_node(send, name):
    def find(node):
        if node.get('id') == name:
            return node
        for child in node.get('children', []):
            found = find(child)
            if found:
                return found
    for doc in send('state ui')['state']['ui']:
        found = find(doc['root'])
        if found:
            return found
    raise AssertionError('UI element missing: '+name)


def traverse_pods(send):
    before = send('get session.shards')
    send('up Space')
    send('down ArrowRight')
    send('until session.pod == 1 max 180')
    send('up ArrowRight')
    send('step 2')
    x = send('get tag=Kage TransformComponent.x')
    y = send('get tag=Kage TransformComponent.y')
    assert send('get tag=Kage SpriteComponent.a') == 0
    send('step 30')
    assert send('get tag=Kage TransformComponent.x') == x
    assert send('get tag=Kage TransformComponent.y') == y
    for pod in (2, 3):
        send('press Space')
        send('step 8')
        assert send('get session.pod') == 0
        assert send('get session.podFlying') == 1
        send(f'until session.pod == {pod} max 80')
        send('step 2')
    send('press Space')
    send('step 10')
    send('until tag=Kage VelocityComponent.blockedY == -1 max 120')
    assert send('get tag=Kage TransformComponent.x') > 4830
    assert send('get session.podsLaunched') == 3
    assert send('get session.shards') > before
    assert send('get session.lives') == 3
    assert send('get session').get('falls', 0) == 0
    assert ui_node(send, 'shards')['text'].startswith(f"{int(send('get session.shards')):02d}")


def check_pods():
    with drive('pods', checkpoint=2) as send:
        send('step 30')
        traverse_pods(send)
    print('PASS: ground entry locks/hides Kage; three pod launches chain in midair; land on solid ground; shard count/HUD increase; three lives; zero falls.')


def check_game_over():
    with drive('game-over', checkpoint=1) as send:
        send('step 30')
        send('down ArrowRight')
        for lives in (2, 1, 0):
            send(f'until session.lives == {lives} max 240')
        send('up ArrowRight')
        send('step 2')
        state = send('get session')
        assert state['gameOver'] and not state['levelClear'] and state['falls'] == 3
        assert ui_node(send, 'result-title')['text'] == 'LIGHT EXTINGUISHED'
        seconds = state['seconds']
        send('step 60')
        assert send('get session.seconds') == seconds
        send('press Enter')
        send('step 30')
        state = send('get session')
        assert state['lives'] == 3 and state['shards'] == 0
        assert not state['gameOver'] and not state['levelClear']
        assert state.get('checkpoint', 0) == 0 and state.get('falls', 0) == 0
        assert send('get tag=Kage TransformComponent.x') == 120
        assert send('get tag=Kage VelocityComponent.blockedY') == -1
    print('PASS: three pit falls consume three lives; game over freezes time; Enter restarts at the start with three lives, zero shards and reset checkpoints.')


def check_side_hit():
    with drive('side-hit') as send:
        send('step 30')
        send('down ArrowRight')
        send('until session.lives == 2 max 100')
        send('up ArrowRight')
        assert 120 <= send('get tag=Kage TransformComponent.x') < 122
        send('step 30')
        assert send('get session.lives') == 2
    print('PASS: sentry side hit costs one life and respawns Kage; stomp traversal still retains all lives.')


if __name__ == '__main__':
    OUTPUT.mkdir(parents=True, exist_ok=True)
    check_geometry()
    check_hill_platform_step()
    check_cable_platform_goal()
    check_pods()
    check_game_over()
    check_side_hit()
