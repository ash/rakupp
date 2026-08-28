// VS Code client for the rakupp language server.
//
// It does almost nothing itself: it launches `rakupp --lsp` as a child process
// and hands the wiring to vscode-languageclient, which speaks LSP over the
// child's stdin/stdout. All the intelligence — parse errors, lint diagnostics —
// lives in the rakupp binary, so this client never needs to change when the
// server gains features.

import { workspace, ExtensionContext, window } from "vscode";
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export function activate(context: ExtensionContext) {
  const config = workspace.getConfiguration("rakupp");
  const command = config.get<string>("path", "rakupp");

  // Same invocation for normal and debug runs: `rakupp --lsp` on stdio.
  const serverOptions: ServerOptions = {
    run: { command, args: ["--lsp"], transport: TransportKind.stdio },
    debug: { command, args: ["--lsp"], transport: TransportKind.stdio },
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

  client.start().catch((err) => {
    window.showErrorMessage(
      `Raku++: failed to start language server ('${command} --lsp'). ` +
        `Set 'rakupp.path' to your rakupp binary. Details: ${err}`
    );
  });
}

export function deactivate(): Thenable<void> | undefined {
  return client?.stop();
}
