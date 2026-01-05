# coreSw Control Center (VS Code glue)

This is a small VS Code extension that helps you debug and inspect a `coreSw` domain.

It provides:

- A sidebar view: `coreSw` -> `Project` (create/edit launch.json, create nodes/components, build/run)
- A sidebar view: `coreSw` -> `Run` (discover `*launch*.json` and run them via `SwLaunch`)
- A sidebar view: `coreSw` -> `Debug` (domains -> targets with PID + attach)
- A Dashboard (webview): graph + config + signals + RPC + kill actions (via `SwBridge`)
- A command usable from `launch.json`: `${command:sw.pickTargetPid}`

It calls:

- `swapi` for discovery + attach PID refresh (`swapi app list/node list/node info --json`)
- `SwLaunch` to run launcher JSON files (`SwLaunch --config_file=<file>`)
- `SwBuild` to build project folders (`SwBuild --root <folder> --scan src ...`)
- `SwBridge` HTTP API for the dashboard (`/api/apps`, `/api/devices`, `/api/connections`, `/api/config`, `/api/signals`, `/api/rpcs`, ...)

If your workspace contains a top-level `bin/` folder (like this repo), the extension will prefer executables from there.

## Install (local)

Option A (folder):

1. In VS Code: `Extensions` -> `...` -> `Install from Location...`
2. Pick this folder: `tools/vscode-sw-target-attach`
3. Reload VS Code.

Option B (VSIX):

1. Build the VSIX: `tools/vscode-sw-target-attach/build-vsix.ps1`
2. In VS Code: `Extensions` -> `...` -> `Install from VSIX...`
3. Pick: `tools/vscode-sw-target-attach/dist/sw-target-attach.vsix`
4. Reload VS Code.

## Configure

VS Code settings:

- `coreSw.swapiPath`: path to `swapi` (supports `${workspaceFolder}`).
  - Default is `swapi` and the extension also tries `${workspaceFolder}/bin/swapi(.exe)` then common build outputs like `build-win/SwNode/SwAPI/SwApi/Release/swapi.exe`.
- `coreSw.defaultDomain`: optional, to skip the domain prompt.
- `coreSw.includeStaleNodes`: optional.
- `coreSw.debuggerType`: attach debugger type (default: `cppvsdbg`). Use `cppdbg` on Linux.
- `coreSw.autoRefreshMs`: auto-refresh interval for the tree views (set `0` to disable).
- `coreSw.bridgeUrl`: SwBridge base URL (default: `http://127.0.0.1:8088`) — important on Windows because `localhost` can resolve to IPv6 (`::1`) while SwBridge listens on IPv4.
- `coreSw.bridgeAutoStart`: auto-start SwBridge when needed (default: `true`).
- `coreSw.swBridgePath`: path to `SwBridge(.exe)` (optional; auto-detected in `${workspaceFolder}/bin` then common build outputs in this repo).
- `coreSw.bridgePort`: port when auto-starting SwBridge (default: `8088`).
- `coreSw.bridgeTimeoutMs`: HTTP timeout for SwBridge requests (default: `1500`).
- `coreSw.dashboardAutoRefreshMs`: auto-refresh interval inside the Dashboard (set `0` to disable).
- `coreSw.swLaunchPath`: path to `SwLaunch(.exe)` (optional; auto-detected in `${workspaceFolder}/bin` then common build outputs in this repo).
- `coreSw.swBuildPath`: path to `SwBuild(.exe)` (optional; auto-detected in `${workspaceFolder}/bin` then common build outputs in this repo).
- `coreSw.launcherSearchPatterns`: glob patterns to find launcher JSON files.
- Tip: you can set `coreSw.launcherSearchPatterns` to `["**/*.json"]` if you want to scan everything.
- `coreSw.launcherSearchExclude`: glob exclude for launcher search.
- `coreSw.launcherMaxResults`: max launcher files scanned (per pattern).

## Use the sidebar

1. Click the `coreSw` icon in the Activity Bar (left sidebar).
2. Open `Debug`.
3. Expand a domain to see targets and their PIDs.
4. Click a target to open the Dashboard, or right-click for actions (copy PID / attach / kill).

## Use Launchers

1. Open `coreSw` -> `Launchers`.
2. Expand a domain (sys) to see available launcher JSON files.
3. Click a launcher to run `SwLaunch --config_file=<file>` (you will be asked to confirm).
4. Right-click a running launcher to stop it, or open the JSON file.

## Use Project

1. Open `coreSw` -> `Project`.
2. Create a project folder with `New project...` (or a single file with `New launch.json`).
3. Select a launch file on the left.
   - The "project root" is the folder that contains this launch file; generated code goes under `<root>/src` and build artifacts under `<root>/install`.
4. Use actions: `New node...`, `Build`, `Build + Run`.

Tip: run `coreSw: Diagnostics` if `swapi`/`SwBridge`/`SwBuild`/`SwLaunch` are not detected.

## Use the Dashboard

1. Open a target (click it in the tree).
2. Use the tabs:
   - `Graph`: shows connections for the selected domain.
   - `Config`: view/edit config and “Send all”.
   - `Signals`: list signals and publish (basic arg support).
   - `RPC`: list RPCs and call (basic arg support).
3. Use `Kill target` / `Kill domain` carefully (force kill).

## Use with C++ attach

Example `launch.json` snippet (Windows/MSVC):

```json
{
  "name": "Attach to Sw target",
  "type": "cppvsdbg",
  "request": "attach",
  "processId": "${command:sw.pickTargetPid}"
}
```
