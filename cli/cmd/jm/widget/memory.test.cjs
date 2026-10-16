const assert = require('node:assert/strict');
const { test } = require('node:test');
const { studio, settle } = require('./studio-host.cjs');

const listing = { structuredContent: { open: '/g/pip', games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: ['scenes/main.scene.json'] }] } };
const note = (title, path, type, observations = []) => ({ title, path, type, observations, links: [] });
const notes = { structuredContent: { notes: [note('Design', 'design/Design.md', 'index'), note('Levels', 'design/Levels.md', 'note', [{ category: 'feel', text: 'x' }]),
  note('Jumps are variable', 'design/decisions/Jumps are variable.md', 'decision')] } };
const texts = {
  Design: '---\ntitle: "Design"\ntype: index\n---\n\n# Design\n\nWhat it is.\n\n## Relations\n- includes [[Levels]]\n',
  Levels: '---\ntitle: "Levels"\n---\n\n# Levels\n\nSee `scenes/main.scene.json` and `assets/data/x.json`.\n\n## Observations\n- [feel] the pit is unfair\n',
};
const read = ({ note: t }) => t ? { structuredContent: { note: { title: t, path: `design/${t}.md`, type: 'note' }, text: texts[t] || '', backlinks: t === 'Levels' ? ['Design'] : [], obsidian: 'obsidian://open?path=%2Fg%2Fpip%2Fdesign%2F' + t + '.md' } } : notes;
const open = async (answers = {}, context) => {
  const s = await studio(listing, { answers: { memory: read, ...answers }, context });
  s.tab('Memory').fire('click');
  await settle(8);
  return s;
};

test('the notes are listed, Design read first; [[links]] open notes, backlinks show', async () => {
  const s = await open();
  assert.match(s.app.text, /What it is\./);
  assert.ok(s.find((e) => e.attributes['data-key'] === 'tree:Jumps are variable')[0], 'decisions listed');
  s.button('Levels').fire('click');
  await settle(6);
  assert.match(s.app.text, /feel\s+the pit is unfair/);
  assert.match(s.app.text, /Linked from\s+Design/);
  assert.equal(s.contexts().at(-1).structuredContent.note, 'Levels');
});

test('a game file named in a note opens in Files', async () => {
  const s = await open();
  s.button('Levels').fire('click');
  await settle(6);
  s.button('assets/data/x.json').fire('click');
  await settle(6);
  assert.equal(s.current(), 'Files');
});

test('note and decide go through the tools; Obsidian opens outside the page', async () => {
  const s = await open();
  s.button('Note').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-text')[0].fire('input', { target: { value: 'too floaty' } });
  s.find((e) => e.attributes['data-key'] === 'mem-category')[0].fire('input', { target: { value: 'feel' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'memory_note').arguments, { note: 'Design', text: 'too floaty', category: 'feel' });
  s.button('Decide').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-title')[0].fire('input', { target: { value: 'Coins respawn' } });
  s.find((e) => e.attributes['data-key'] === 'mem-why')[0].fire('input', { target: { value: 'serves [[Pillars]]' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'memory_decide').arguments, { title: 'Coins respawn', why: 'serves [[Pillars]]' });
  s.button('Obsidian').fire('click');
  await settle();
  assert.match(s.sent.find((m) => m.method === 'ui/open-link').params.url, /^obsidian:\/\/open\?path=/);
});

test('no notes yet: plan the game with the agent', async () => {
  const s = await open({ memory: { content: [{ type: 'text', text: 'No design notes yet' }] } });
  s.button('Plan it with the agent').fire('click');
  await settle();
  assert.match(s.sent.find((m) => m.method === 'ui/message').params.content[0].text, /new_game_interview/);
});

test('a deep link opens a note', async () => {
  const s = await studio(listing, { answers: { memory: read }, context: { 'openai/deepLink': { url: '/memory?note=Levels' } } });
  await settle(10);
  assert.equal(s.current(), 'Memory');
  assert.match(s.app.text, /the pit is unfair/);
});
