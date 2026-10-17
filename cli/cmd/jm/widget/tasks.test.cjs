const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }] } };
const board = (tasks) => ({ structuredContent: { columns: ['To do', 'Doing', 'Done'], tasks } });
const t1 = { id: 't1', text: 'Fix the pit · [[Levels]] · `scenes/main.scene.json` · jm://play/20261015-101500/m2', column: 'To do',
  links: ['[[Levels]]', 'scenes/main.scene.json', 'jm://play/20261015-101500/m2'] };
const t2 = { id: 't2', text: 'Variable jump', column: 'Doing', links: [], notes: ['  - hold to go higher'] };
const answers = { tasks: board([t1, t2]), task_move: board([{ ...t1, column: 'Doing' }, t2]), task_add: board([t1, t2, { id: 't3', text: 'Coins', column: 'To do', links: [] }]) };
const open = async (with_ = answers) => {
  const s = await studio(listing, { answers: with_ });
  s.tab('Tasks').fire('click');
  await settle(8);
  return s;
};
const key = (s, k) => s.find((e) => e.attributes['data-key'] === k)[0];

test('the board shows columns, tasks with their links as buttons, and notes', async () => {
  const s = await open();
  assert.match(s.app.text, /To do · 1/);
  assert.match(s.app.text, /Doing · 1/);
  assert.match(key(s, 'task:t1').text, /Fix the pit\s+Levels\s+scenes\/main\.scene\.json\s+marker 2 of play 20261015-101500/);
  assert.match(key(s, 'task:t2').text, /hold to go higher/);
});

test('a task moves by button or shift+arrow; links go where they point', async () => {
  const s = await open();
  s.button('→').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'task_move').arguments, { id: 't1', column: 'Doing', game: '/g/pip' });
  assert.match(s.app.text, /Doing · 2/);
  key(s, 'task:t2').fire('keydown', { key: 'ArrowLeft', shiftKey: true });
  await settle(6);
  assert.deepEqual(s.calls().filter((c) => c.name === 'task_move')[1].arguments, { id: 't2', column: 'To do', game: '/g/pip' });
  s.button('Levels').fire('click');
  await settle(6);
  assert.equal(s.current(), 'Memory');
});

test('a task is added with its links; removing asks first', async () => {
  const s = await open();
  key(s, 'task-new').fire('input', { target: { value: ' Coins ' } });
  key(s, 'task-links').fire('input', { target: { value: 'Systems, assets/data/coins.json' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'task_add').arguments, { text: 'Coins', links: ['Systems', 'assets/data/coins.json'], game: '/g/pip' });
  key(s, 'task:t2').fire('click');
  s.button('Remove').fire('click');
  assert.equal(s.calls().filter((c) => c.name === 'task_remove').length, 0);
  s.button('Remove?').fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'task_remove').arguments, { id: 't2', game: '/g/pip' });
});

test('the board is the shown game\'s; a card is picked by Enter; a marker goes to the composer', async () => {
  const s = await open();
  assert.deepEqual(s.calls().find((c) => c.name === 'tasks').arguments, { game: '/g/pip' });
  const card = key(s, 'task:t1');
  card.fire('keydown', { key: 'Enter', currentTarget: card });
  assert.ok(s.button('Remove'));
  s.button('marker 2 of play 20261015-101500').fire('click');
  await settle(6);
  assert.match(s.sent.filter((x) => x.method === 'ui/message').at(-1).params.content[0].text, /marker 2 of play 20261015-101500 \(jm:\/\/play\/20261015-101500\/m2\)/);
  assert.equal(s.current(), 'Tasks');
});

test('a listing that fails says why with Try again, keeping the board it had', async () => {
  const failing = { isError: true, content: [{ type: 'text', text: 'Tasks.md: permission denied' }] };
  const asked = { ...answers, tasks: failing };
  const s = await open(asked);
  assert.match(s.app.text, /Couldn't read the board: Tasks\.md: permission denied\s+Try again/);
  assert.doesNotMatch(s.app.text, /Nothing here/);
  asked.tasks = board([]);
  s.button('Try again').fire('click');
  await settle(6);
  assert.match(s.app.text, /To do · 0/);
  assert.doesNotMatch(s.app.text, /Couldn't read/);
  asked.tasks = board([t1]);
  s.tab('Games').fire('click');
  s.tab('Tasks').fire('click');
  await settle(6);
  asked.tasks = failing;
  s.tab('Games').fire('click');
  s.tab('Tasks').fire('click');
  await settle(6);
  assert.match(s.app.text, /permission denied \(this board may be out of date\)/);
  assert.match(key(s, 'task:t1').text, /Fix the pit/);
});
