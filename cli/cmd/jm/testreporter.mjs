// jm test --json: node's test events as JSON lines, in jm build --json's
// shape. A passing test is {"test", "result": "pass"}; a failing one an error
// diagnostic with the test's name and, from an assertion, its file, line and
// column; the last line is {"result": "ok"|"failed", "passed", "failed"}.
export default async function* jsonLines(source) {
  let passed = 0, failed = 0;
  for await (const event of source) {
    if (event.type !== 'test:pass' && event.type !== 'test:fail') continue;
    if (event.data.details?.type === 'suite' || event.data.nesting > 0) continue;
    const test = event.data.name;
    if (event.type === 'test:pass') {
      passed++;
      yield JSON.stringify({ test, result: 'pass' }) + '\n';
      continue;
    }
    failed++;
    const error = event.data.details?.error;
    const message = String(error?.cause?.message ?? error?.message ?? 'failed');
    const line = { level: 'error', category: 'test', test, message };
    const where = message.match(/ at (\S+):(\d+):(\d+)$/);
    if (where) {
      line.message = message.slice(0, where.index).replace(/^[^:]*: /, '');
      Object.assign(line, { file: where[1], line: Number(where[2]), column: Number(where[3]) });
    }
    yield JSON.stringify(line) + '\n';
  }
  yield JSON.stringify({ result: failed ? 'failed' : 'ok', passed, failed }) + '\n';
}
