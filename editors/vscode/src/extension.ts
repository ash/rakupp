// VS Code client for the rakupp language server.
//
// It does almost nothing itself: it launches `rakupp --lsp` as a child process
// and hands the wiring to vscode-languageclient, which speaks LSP over the
// child's stdin/stdout. All the intelligence — diagnostics, hover, completion —
// lives in the rakupp binary, so this client never needs to change when the
// server gains features.

import * as fs from "fs";
import * as path from "path";
import { workspace, ExtensionContext, window, env, Uri, commands } from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
} from "vscode-languageclient/node";

const INSTALL_GUIDE =
  "https://github.com/ash/rakupp/blob/main/docs/guide/INSTALL.md";

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext) {
  startClient();

  // A new rakupp.path takes effect at once, without reloading the window.
  context.subscriptions.push(
    workspace.onDidChangeConfiguration(async (e) => {
      if (e.affectsConfiguration("rakupp.path")) {
        await stopClient();
        startClient();
      }
    })
  );
}

export function deactivate(): Thenable<void> | undefined {
  return stopClient();
}

function startClient() {
  const command = workspace.getConfiguration("rakupp").get<string>("path", "rakupp");

  // Checked here rather than left to the client: when the spawn fails,
  // vscode-languageclient always adds its own "couldn't create connection"
  // notification, so the user would get two messages for one problem.
  if (!findExecutable(command)) {
    notFound(command);
    return;
  }

  // Same invocation for normal and debug runs: `rakupp --lsp` on stdio.
  // No `transport`: TransportKind.stdio would append `--stdio`, which
  // rakupp 5.2.1 and older reject; without it the client still talks over
  // the child's stdin/stdout.
  const serverOptions: ServerOptions = {
    run: { command, args: ["--lsp"] },
    debug: { command, args: ["--lsp"] },
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "raku" }],
    synchronize: {
      fileEvents: workspace.createFileSystemWatcher(
        "**/*.{raku,rakumod,rakutest,p6,pl6,pm6}"
      ),
    },
    outputChannelName: "Raku++",
  };

  client = new LanguageClient(
    "rakupp",
    "Raku++ Language Server",
    serverOptions,
    clientOptions
  );

  // Any other start failure is reported by the client itself, with a button
  // that opens the Raku++ output channel.
  client.start().catch(() => undefined);
}

async function stopClient() {
  const c = client;
  client = undefined;
  if (c?.isRunning()) {
    await c.stop();
  }
}

async function notFound(command: string) {
  const choice = await window.showErrorMessage(
    `Raku++: '${command}' was not found. Install rakupp, or set ` +
      `'rakupp.path' to the absolute path of the binary.`,
    "Install rakupp",
    "Open Settings"
  );
  if (choice === "Install rakupp") {
    env.openExternal(Uri.parse(INSTALL_GUIDE));
  } else if (choice === "Open Settings") {
    commands.executeCommand("workbench.action.openSettings", "rakupp.path");
  }
}

// The file a command runs: a path is checked as it is, a bare name is looked
// up on PATH (with PATHEXT's extensions on Windows).
function findExecutable(command: string): string | undefined {
  const exts =
    process.platform === "win32"
      ? ["", ...(process.env.PATHEXT ?? ".EXE;.CMD;.BAT").split(";")]
      : [""];
  const dirs = command.includes("/") || command.includes("\\")
    ? [""]
    : (process.env.PATH ?? "").split(path.delimiter).filter((d) => d);
  for (const dir of dirs) {
    for (const ext of exts) {
      const file = dir ? path.join(dir, command + ext) : command + ext;
      try {
        if (fs.statSync(file).isFile()) {
          return file;
        }
      } catch {
        // not here; keep looking
      }
    }
  }
  return undefined;
}
