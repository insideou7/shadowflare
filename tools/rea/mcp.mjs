#!/usr/bin/env node
// Starts the REA MCP server (https://github.com/morluto/rea) for Claude Code.
// Registered in .mcp.json. Pins the REA version so every session gets the same
// tools, and points REA at a Ghidra installed by install-ghidra.sh when
// GHIDRA_INSTALL_DIR is not already set.
import { spawn } from "node:child_process";
import { homedir } from "node:os";
import { join } from "node:path";

const REA_VERSION = "6.4.0";
const GHIDRA_DIR = join(homedir(), ".cache", "rea", "ghidra");

const env = { ...process.env };
if (!env.GHIDRA_INSTALL_DIR) env.GHIDRA_INSTALL_DIR = GHIDRA_DIR;
// ShadowFlare.exe and its DLLs are small, but give first-query analysis room.
env.REA_GHIDRA_STARTUP_TIMEOUT_MS ??= "600000";

const child = spawn("npx", ["-y", `rea-agents@${REA_VERSION}`, "mcp"], {
  env,
  stdio: "inherit",
  shell: process.platform === "win32",
});
child.on("exit", (code, signal) => {
  if (signal) process.kill(process.pid, signal);
  else process.exit(code ?? 1);
});
for (const sig of ["SIGINT", "SIGTERM"]) process.on(sig, () => child.kill(sig));
