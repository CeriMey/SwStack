# `swapi` (CLI d'introspection, style `ros2`)

`swapi` est un outil CLI qui permet d'introspecter un systeme `SwRemoteObject` via la SHM registry et les RPC.

Sources:
- `SwNode/SwAPI/SwApi/*` (CLI + parser + commandes).

## Build

Depuis la racine:
- `build.bat`

Target:
- `swapi` (voir `SwNode/SwAPI/SwApi/CMakeLists.txt`).

## Notion de `target`

La plupart des commandes prennent un `target` sous la forme:
- `sys/ns/name` (recommande)

Ou en 2 segments:
- `ns/name` + `--domain <sys>`

## Commandes (resume)

### Apps/domains (equivalent du "daemon/discovery")

- `swapi app list [--json] [--pretty]`

### Nodes

- `swapi node list [--domain <sys>] [--ns <prefix>] [--include-stale] [--json] [--pretty]`
- `swapi node info <target> [--domain <sys>] [--json] [--pretty]`
- `swapi node save-as-factory <target> [--domain <sys>] [--timeout_ms <ms>]`
- `swapi node reset-factory <target> [--domain <sys>] [--timeout_ms <ms>]`

### Config/params

Alias:
- `swapi param ...` == `swapi config ...`

Commandes:
- `swapi config dump <target> [--domain <sys>] [--pretty]`
- `swapi config get <target> <path> [--domain <sys>] [--pretty]`
- `swapi config set <target> <path> <value> [--domain <sys>]`
- `swapi config watch <target> [path] [--domain <sys>] [--duration_ms <ms>] [--no-initial]`

Notes:
- `dump/watch` lisent le snapshot `__config__|*` (config effective).
- `set` publie une update sur `__cfg__|<path>`.

### Signals (topics SHM)

- `swapi signal list <target> [--domain <sys>] [--json] [--pretty]`
- `swapi signal echo <target> <signal> [--domain <sys>] [--duration_ms <ms>] [--no-initial]`

### RPC (services)

- `swapi rpc list <target> [--domain <sys>] [--json] [--pretty]`

### Containers (gestion de composants/plugins)

Les commandes `container` appellent les RPC exposees par `SwComponentContainer` et affichent directement le JSON retourne.

- `swapi container status <containerTarget> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container plugins list <containerTarget> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container plugins info <containerTarget> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container plugin load <containerTarget> <path> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container plugin stop <containerTarget> <query> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container plugin restart <containerTarget> <query> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container components list <containerTarget> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container component load <containerTarget> <type> <ns> <name> [--params <json>] [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container component unload <containerTarget> <targetObject> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`
- `swapi container component restart <containerTarget> <targetObject> [--domain <sys>] [--timeout_ms <ms>] [--pretty]`

### Graph (subscribers)

- `swapi graph connections [--domain <sys>] [--ns <prefix>] [--kind <all|topic|rpc|config|internal>] [--resolve] [--json] [--pretty]`

Notes:
- `--kind topic` filtre les signaux internes (`__config__`, `__cfg__`, `__rpc__*`).
- `subObject` / `subTarget` apparaissent quand la subscription est faite via `SwRemoteObject` (`ipcConnect*`, `ipcBindConfig*`), y compris en container (multi-nodes par process).
- `--resolve` fait un fallback best-effort `subPid` -> `subTargets` si `subTarget` n'est pas disponible.

## Mapping rapide ROS2 -> Sw

- `ros2 node list` -> `swapi node list --domain <sys>`
- `ros2 node info` -> `swapi node info <sys/ns/name>`
- `ros2 param get/set/dump` -> `swapi config get/set/dump`
- `ros2 topic list/echo` -> `swapi signal list/echo`
- `ros2 service list` -> `swapi rpc list`
- `ros2 component list/load/unload` -> `swapi container components list` / `swapi container component load/unload`
- `ros2 launch ...` -> `SwLaunch` (voir `docs/nodes/SwLaunch.md`)
