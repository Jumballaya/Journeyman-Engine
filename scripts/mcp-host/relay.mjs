#!/usr/bin/env node
// A local MCP Apps host for jm's pages: see a widget change (Studio, plays,
// scene view) in a browser without ChatGPT or Claude. Each open host page gets
// its own `jm mcp` (stdio), as each chat gets its own session; host.html frames
// each app page and answers it as a host does: tool calls, resources,
// ui/message, model context, display modes, theme and elicitation forms. The
// page's left column logs what the model would see.
//
//   HOME=<scratch> node scripts/mcp-host/relay.mjs [--jm <jm>] [--port 8765]
//   then open http://127.0.0.1:8765/?tool=studio (&args={"...": ...})
//
// jm runs with this environment: point HOME (and JM_GAMES) at a scratch folder
// to keep it off your own games and settings.
import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { createInterface } from "node:readline";
import { parseArgs } from "node:util";

const { values } = parseArgs({ options: { jm: { type: "string", default: "jm" }, port: { type: "string", default: "8765" } } });
const host = readFileSync(new URL("./host.html", import.meta.url));
const sessions = new Map(); // page id → its jm mcp

// A page's session: jm's messages to it as server-sent events, for as long as it's open.
function open(res) {
  const id = randomUUID();
  const jm = spawn(values.jm, ["mcp"], { stdio: ["pipe", "pipe", "inherit"] });
  const say = (event, data) => res.write(`event: ${event}\ndata: ${data}\n\n`);
  jm.on("error", (e) => say("gone", `can't run ${values.jm}: ${e.message}`));
  jm.on("exit", (code) => { say("gone", `jm mcp exited (${code})`); sessions.delete(id); });
  createInterface({ input: jm.stdout }).on("line", (line) => say("message", line));
  sessions.set(id, jm);
  res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache" });
  say("session", id);
  return () => { sessions.delete(id); jm.kill(); };
}

createServer((req, res) => {
  const url = new URL(req.url, "http://localhost");
  if (url.pathname === "/events") {
    req.on("close", open(res));
  } else if (url.pathname === "/send" && req.method === "POST") {  // one message to the page's jm
    const jm = sessions.get(url.searchParams.get("session"));
    if (!jm) return res.writeHead(410).end("no such session: reload the page");
    let body = "";
    req.on("data", (d) => { body += d; });
    req.on("end", () => { jm.stdin.write(JSON.stringify(JSON.parse(body)) + "\n"); res.end(); });
  } else if (url.pathname === "/") {
    res.writeHead(200, { "content-type": "text/html" }).end(host);
  } else {
    res.writeHead(404).end();
  }
}).on("error", (e) => { console.error(`can't serve on port ${values.port}: ${e.message} (--port another)`); process.exit(1); })
  .listen(Number(values.port), "127.0.0.1", () => console.error(`MCP Apps host: http://127.0.0.1:${values.port}/`));
