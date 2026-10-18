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
  assert.deepEqual(s.calls().find((c) => c.name === 'memory_note').arguments, { note: 'Design', text: 'too floaty', category: 'feel', game: '/g/pip' });
  s.button('Decide').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-title')[0].fire('input', { target: { value: 'Coins respawn' } });
  s.find((e) => e.attributes['data-key'] === 'mem-why')[0].fire('input', { target: { value: 'serves [[Pillars]]' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.deepEqual(s.calls().find((c) => c.name === 'memory_decide').arguments, { title: 'Coins respawn', why: 'serves [[Pillars]]', game: '/g/pip' });
  s.button('Obsidian').fire('click');
  await settle();
  assert.match(s.sent.find((m) => m.method === 'ui/open-link').params.url, /^obsidian:\/\/open\?path=/);
});

test('no notes yet: plan the game with the agent', async () => {
  const s = await open({ memory: { content: [{ type: 'text', text: 'No design notes yet' }] } });
  s.button('Plan it with the agent').fire('click');
  await settle();
  assert.match(s.sent.find((m) => m.method === 'ui/message').params.content[0].text, /onboard/);
});

test('a deep link opens a note', async () => {
  const s = await studio(listing, { answers: { memory: read }, context: { 'openai/deepLink': { url: '/memory?note=Levels' } } });
  await settle(10);
  assert.equal(s.current(), 'Memory');
  assert.match(s.app.text, /the pit is unfair/);
});

test('a decision saved opens as the tool named it; a failed one keeps its form', async () => {
  let refuse = true;
  const s = await open({
    memory_decide: (a) => ({ content: [{ type: 'text', text: JSON.stringify({ ok: true, note: 'Can enemies jump', path: 'design/decisions/Can enemies jump.md' }) }] }),
    memory_note: () => (refuse ? { isError: true, content: [{ type: 'text', text: 'disk full' }] } : { content: [{ type: 'text', text: '{"ok":true,"note":"Design"}' }] }),
  });
  s.button('Decide').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-title')[0].fire('input', { target: { value: 'Can enemies jump?' } });
  s.find((e) => e.attributes['data-key'] === 'mem-why')[0].fire('input', { target: { value: 'fair' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(12);
  assert.equal(s.calls().filter((c) => c.name === 'memory' && c.arguments.note).at(-1).arguments.note, 'Can enemies jump');
  s.button('Note').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-text')[0].fire('input', { target: { value: 'keep me' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.match(s.app.text, /disk full/);
  assert.equal(s.find((e) => e.attributes['data-key'] === 'mem-text')[0].value, 'keep me', 'the draft stays');
  refuse = false;
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(10);
  assert.equal(s.calls().filter((c) => c.name === 'memory_note').at(-1).arguments.text, 'keep me');
  assert.equal(s.find((e) => e.attributes['data-key'] === 'mem-text').length, 0, 'saved: the form closes');
});

test('only the latest search shows, however its answers arrive', async () => {
  const held = [];
  const s = await open({ memory_search: (a) => new Promise((resolve) => {
    const n = held.length + 1;
    held.push(() => resolve({ structuredContent: { hits: [{ title: `hit-${a.text}-${n}`, path: 'design/x.md' }] } }));
  }) });
  const ask = (value) => s.find((e) => e.attributes['data-key'] === 'mem-search')[0].fire('keydown', { key: 'Enter', target: { value } });
  ask('q');
  ask('other');
  ask('q');
  await settle(6);
  const [first, second, third] = held;
  third(); await settle(6);
  second(); first(); await settle(6);
  assert.match(s.app.text, /hit-q-3/);
  assert.doesNotMatch(s.app.text, /hit-other|hit-q-1/);
});

test('a failed note or decision on a linked note keeps the note, its form and every field', async () => {
  for (const fail of [() => ({ isError: true, content: [{ type: 'text', text: 'disk full' }] }), () => ({ rpcError: 'gone' })]) {
    const s = await studio(listing, { answers: { memory: read, memory_note: fail, memory_decide: fail }, context: { 'openai/deepLink': { url: '/memory?note=Levels' } } });
    await settle(10);
    s.button('Note').fire('click');
    s.find((e) => e.attributes['data-key'] === 'mem-category')[0].fire('input', { target: { value: 'feel' } });
    s.find((e) => e.attributes['data-key'] === 'mem-text')[0].fire('input', { target: { value: 'keep me' } });
    s.find((e) => e.tag === 'form')[0].fire('submit');
    await settle(10);
    assert.equal(s.find((e) => e.attributes['data-key'] === 'mem-text')[0]?.value, 'keep me', 'the note form stays');
    assert.equal(s.find((e) => e.attributes['data-key'] === 'mem-category')[0].value, 'feel');
    assert.match(s.app.text, /the pit is unfair/, 'Levels is still read');
    s.button('Decide').fire('click');
    s.find((e) => e.attributes['data-key'] === 'mem-title')[0].fire('input', { target: { value: 'Pits are fair' } });
    s.find((e) => e.attributes['data-key'] === 'mem-why')[0].fire('input', { target: { value: 'pillars' } });
    s.find((e) => e.tag === 'form')[0].fire('submit');
    await settle(10);
    assert.equal(s.find((e) => e.attributes['data-key'] === 'mem-why')[0]?.value, 'pillars', 'the decide form stays');
  }
});

test("a note saved after another game opened leaves that game's tab alone", async () => {
  let open = '/g/pip', saved;
  const games = () => ({ structuredContent: { open, games: [{ name: 'Pip', path: '/g/pip', opened: 1, scenes: [] }, { name: 'Bo', path: '/g/bo', opened: 1, scenes: [] }] } });
  const s = await studio(games(), { answers: { games, memory: (a) => (a.game === '/g/bo' && !a.note ? { structuredContent: { notes: [note('Bo notes', 'design/Bo notes.md', 'note')] } } : read(a)),
    memory_note: () => new Promise((resolve) => { saved = () => resolve({ content: [{ type: 'text', text: '{"ok":true,"note":"Design"}' }] }); }) } });
  s.tab('Memory').fire('click');
  await settle(8);
  s.button('Note').fire('click');
  s.find((e) => e.attributes['data-key'] === 'mem-text')[0].fire('input', { target: { value: 'for pip' } });
  s.find((e) => e.tag === 'form')[0].fire('submit');
  await settle(4);
  open = '/g/bo';
  await s.notify('ui/notifications/tool-result', games());  // the agent opened Bo
  await settle(8);
  saved();
  await settle(12);
  const asked = s.calls().filter((c) => c.name === 'memory');
  assert.equal(asked.at(-1).arguments.game, '/g/bo', 'nothing of Pip asked after Bo opened');
  assert.match(s.app.text, /Bo notes/);
});

test('a dossier: its properties, its world, playbooks on it; the facts kept, one forgotten', async () => {
  const listed = { structuredContent: { notes: [note('Design', 'design/Design.md', 'index'), note('Irongate', 'design/Locations/Irongate.md', 'location'), note('Odd', 'design/constructor/Odd.md', 'thing')] } };
  const s = await open({
    memory: ({ note: t }) => t === 'Irongate' ? { structuredContent: { note: { title: 'Irongate', path: 'design/Locations/Irongate.md', type: 'location', props: { region: '[[Ashvale]]', tags: [] } },
      text: '# Irongate\n\nVarn\'s capital.\n', backlinks: [] } } : t ? read({ note: t }) : listed,
    world_context: ({ note: t }) => t === 'Irongate' ? { structuredContent: { edges: [{ from: 'Irongate', type: 'road_to', to: 'Duskhold', attrs: { distance: '120 km' } }], unwritten: ['Ost'] } } : { structuredContent: { edges: [] } },
    recall: () => ({ structuredContent: { facts: [{ id: 'f1', category: 'canon', text: 'Varn and Ost are at war.', scope: 'game' }, { id: 'f2', category: 'preference', text: 'Small changes.', scope: 'you' }] } }),
    forget: () => ({ content: [{ type: 'text', text: 'Forgot f2' }] }),
  });
  assert.ok(s.find((e) => e.attributes['data-key'] === 'tree:group:Locations')[0] && s.find((e) => e.attributes['data-key'] === 'tree:group:constructor')[0], 'dossiers by folder');
  assert.match(s.app.text, /Keep in mind[\s\S]*canon\s+Varn and Ost are at war\.[\s\S]*Small changes\.\s*\(yours\)/);
  s.button('Irongate').fire('click');
  await settle(6);
  assert.match(s.app.text, /region\s*Ashvale/);
  assert.match(s.app.text, /Irongate\s+road to\s+Duskhold\s+\(distance: 120 km\)/);
  assert.match(s.app.text, /Linked, no note yet:\s*Ost/);
  s.button('Make it a level').fire('click');
  await settle();
  assert.match(s.sent.filter((m) => m.method === 'ui/message').at(-1).params.content[0].text, /level_from_place.*"place":"Irongate"/);
  s.find((e) => e.attributes['aria-label'] === 'Forget: Small changes.')[0].fire('click');
  await settle(6);
  assert.deepEqual(s.calls().find((c) => c.name === 'forget').arguments, { id: 'f2', scope: 'you', game: '/g/pip' });
});
