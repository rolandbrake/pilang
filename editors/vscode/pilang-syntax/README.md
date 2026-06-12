# Pilang Syntax for VS Code

This folder contains a small VS Code extension that adds syntax highlighting for `.pi` files.

## Use it while developing

Open this folder in VS Code and press `F5` to launch an Extension Development Host:

```powershell
code editors\vscode\pilang-syntax
```

Then open any `.pi` file in the new VS Code window.

## Install locally

Copy this folder to your VS Code extensions directory and restart VS Code:

```powershell
Copy-Item -Recurse editors\vscode\pilang-syntax "$env:USERPROFILE\.vscode\extensions\pilang.pilang-syntax-0.1.0"
```

After restart, `.pi` files should be detected as Pilang automatically.
