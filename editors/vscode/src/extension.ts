// VS Code client for the rakupp language server.
//
// It does almost nothing itself: it launches `rakupp --lsp` as a child process
// and hands the wiring to vscode-languageclient, which speaks LSP over the
// child's stdin/stdout. All the intelligence — diagnostics, hover, completion —
// lives in the rakupp binary, so this client never needs to change when the
// server gains features.

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
  const config = workspace.getConfiguration("rakupp");
  const command = config.get<string>("path", "rakupp");

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

  client.start().catch(async (err) => {
    const notFound = /ENOENT/.test(String(err));
    const choice = await window.showErrorMessage(
      notFound
        ? `Raku++: '${command}' was not found. Install rakupp, or set ` +
            `'rakupp.path' to the absolute path of the binary.`
        : `Raku++: failed to start language server ('${command} --lsp'). ` +
            `Set 'rakupp.path' to your rakupp binary. Details: ${err}`,
      "Install rakupp",
      "Open Settings"
    );
    if (choice === "Install rakupp") {
      env.openExternal(Uri.parse(INSTALL_GUIDE));
    } else if (choice === "Open Settings") {
      commands.executeCommand("workbench.action.openSettings", "rakupp.path");
    }
  });
}

export function deactivate(): Thenable<void> | undefined {
  return client?.stop();
}
