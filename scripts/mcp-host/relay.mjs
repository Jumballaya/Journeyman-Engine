#!/usr/bin/env node
// A local MCP Apps host for jm's pages: see a widget change (Studio, plays,
// scene view) in a browser without ChatGPT or Claude. This relays `jm mcp`
// (stdio) to host.html, which frames each app page and answers it as a host
// does: tool calls, resources, ui/message, model context, display modes, theme
// and elicitation forms. The page's left column logs what the model would see.
//
//   HOME=<scratch> node scripts/mcp-host/relay.mjs [--jm <jm>] [--port 8765]
//   then open http://127.0.0.1:8765/?tool=studio (&args={"...": ...})
//
// jm runs with this environment: point HOME (and JM_GAMES) at a scratch folder
// to keep it off your own games and settings.
import { spawn } from "node:child_process";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import { createInterface } from "node:readline";
import { parseArgs } from "node:util";

const { values } = parseArgs({ options: { jm: { type: "string", default: "jm" }, port: { type: "string", default: "8765" } } });
const jm = spawn(values.jm, ["mcp"], { stdio: ["pipe", "pipe", "inherit"] });
jm.on("error", (e) => { console.error(`can't run ${values.jm}: ${e.message}`); process.exit(1); });
jm.on("exit", (code) => { console.error(`jm mcp exited (${code})`); process.exit(1); });

// jm's messages go to every open page; what it says before one listens waits for it.
const pages = new Set();
const backlog = [];
createInterface({ input: jm.stdout }).on("line", (line) => {
  if (!pages.size) backlog.push(line);
  for (const page of pages) page.write(`data: ${line}\n\n`);
});

const host = readFileSync(new URL("./host.html", import.meta.url));
createServer((req, res) => {
  const url = new URL(req.url, "http://localhost");
  if (url.pathname === "/events") {  // jm's messages, as server-sent events
    res.writeHead(200, { "content-type": "text/event-stream", "cache-control": "no-cache" });
    pages.add(res);
    for (const line of backlog.splice(0)) res.write(`data: ${line}\n\n`);
    req.on("close", () => pages.delete(res));
  } else if (url.pathname === "/send" && req.method === "POST") {  // one message to jm
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
