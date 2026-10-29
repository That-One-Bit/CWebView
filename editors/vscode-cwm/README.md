# CWMenu Language Support for Visual Studio Code

This is a dependency-free VS Code extension for CWebView `.cwm` files.

## Features

- TextMate syntax highlighting for directives, element kinds, keys, strings, numbers, colors, comments, and `$variables`.
- Semantic tokens for element/menu names, actions, variables, directives, and element kinds.
- Diagnostics for unknown element kinds, duplicate element names, unresolved local `pair` targets, undeclared script variables, and likely misspelled keys.
- Hover help for directives and keys, plus starter completions and snippets.
- The unknown-action diagnostic is intentionally disabled by default: applications are expected to define their own host actions.

## Install locally

From the repository root:

```sh
code --install-extension ./editors/vscode-cwm
```

For a distributable VSIX, install `@vscode/vsce` and run:

```sh
cd editors/vscode-cwm
vsce package
```

The extension does not require a Node runtime in the user's CWebView application; Node is only used by VS Code to host the extension.

## Scope and caveats

The diagnostics are static checks, not a full execution of the C parser. Linked scripts can define menus and variables missing from the current file, so unresolved-reference diagnostics are warnings/information rather than errors. Host action names are deliberately open-ended. Semantic highlighting depends on the active VS Code theme honoring semantic token scopes.
