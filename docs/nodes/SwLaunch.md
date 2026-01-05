# `SwLaunch` (launcher "launch" JSON, style ROS2 launch)

Ce binaire est un **launcher multi-process** pilote par JSON:

- il lit un JSON de "launch" (fichier ou string),
- il demarre des **process fils**: containers (typiquement `SwComponentContainer`) et/ou nodes executables,
- il peut **relancer** un process sur crash, ou si le `SwRemoteObject` du process "disparait" de la SHM registry (watchdog de presence),
- il genere au besoin des `--config_file` temporaires (dans le dossier Temp) pour ne pas embarquer de JSON projet dans le repo.

Sources de reference:
- `SwNode/SwLaunch/SwLaunch.cpp` (parsing JSON + start/monitor/restart des process).
- `src/core/remote/SwRemoteObjectNode.h` (CLI et `main()` standard des nodes via `SW_REMOTE_OBJECT_NODE`).
- `SwNode/SwComponentContainer/SwComponentContainer.cpp` (schema `composition/*` + RPC du container).

## Build

Depuis la racine:
- `build.bat`

Targets utiles:
- `SwLaunch` (launcher)
- `SwComponentContainer` (container)
- `PingPongPlugin` (plugin demo, construit depuis `exemples/24-ComponentPlugin/PingPongPlugin.cpp`)
- `SwPingNode`, `SwPongNode` (nodes demo, construits depuis `exemples/29-IpcPingPongNodes/SwPingNode.cpp` et `exemples/29-IpcPingPongNodes/SwPongNode.cpp`)

## Run (CLI)

`SwLaunch` exige **un** des deux arguments:
- `--config_file=<path>` (JSON lu depuis disque)
- `--config_json=<json>` (string JSON)

Overrides optionnels (prioritaires sur le JSON):
- `--sys=<domain>` (sys par defaut des enfants; defaut JSON: `"demo"`)
- `--duration_ms=<ms>` (stoppe tout apres ce delai; `0` = infini)

Codes retour (cf `main()` dans `SwNode/SwLaunch/SwLaunch.cpp`):
- `2`: JSON manquant / invalide / schema incomplet
- `3`: un child n'a pas pu demarrer

## Regles de resolution des chemins

Dans `SwNode/SwLaunch/SwLaunch.cpp`:

- `baseDir` = dossier du `--config_file` si fourni, sinon `SwDir::currentPath()`.
- `executable` et `workingDirectory` sont resolus par `resolvePath_(baseDir, value)`:
  - si `value` est relatif, il est interprete depuis `baseDir`,
  - puis converti en chemin absolu (plateforme).
- si `workingDirectory` est absent: fallback sur le dossier de l'exe.

Attention: **les plugins** du container sont resolus par `SwLibrary` **dans le process container** (depuis son CWD). Donc, pour des paths de plugin simples (ex: `"PingPongPlugin"`), mets un `workingDirectory` coherent (souvent le dossier de l'exe qui contient aussi les `.dll`).

## Schema JSON (launch)

Le root JSON doit etre un object et contenir au moins un tableau non vide:
- `containers` et/ou `nodes` (sinon erreur: "launch json must contain 'containers' and/or 'nodes' arrays")

### Root

- `sys` (string, optionnel): sys par defaut des process enfants. Defaut: `"demo"`.
- `duration_ms` (int, optionnel): delai avant arret de tout. Defaut: `0` (infini).
- `containers` (array<object>, optionnel): liste des process containers a demarrer.
- `nodes` (array<object>, optionnel): liste des process nodes "standalone" a demarrer.

### Container entry

Champs d'identite:
- `sys` (string, optionnel): override du sys pour CE container.
- `ns` / `namespace` (string, requis): namespace du container.
- `name` / `object` (string, requis): nom du container.

Execution:
- `executable` (string, requis): chemin vers l'exe container (ex: `SwComponentContainer.exe`).
- `workingDirectory` (string, optionnel): CWD du process (defaut: dossier de l'exe).
- `duration_ms` (int, optionnel): passe au container via `--duration_ms` (0 = run jusqu'a arret du launcher).

Config:
- `config_file` (string, optionnel): si present, passe tel quel au container via `--config_file`.
- sinon: `SwLaunch` genere un fichier temporaire `sw_launch_<ns_name>.json` dans `SwStandardLocation::Temp` (voir `ensureConfigFile_()`):
  - il y met `{ "composition": { ... }, "options": { ... } }`,
  - puis le passe au container via `--config_file`.

Composition (si pas de `config_file`):
- `composition` (object, optionnel): object "composition" a forwarder au container.
  - alias accepte: si `composition` est absent, `ensureConfigFile_()` utilise l'object de la spec comme source.
  - alias accepte: `nodes` est copie vers `components` si `components` est absent.

Options launcher + flags process (dans `spec.options`):
- `reloadOnCrash` (bool): restart si exitCode != 0 (voir `onTerminated_()`).
- `restartDelayMs` (int): delai avant restart (defaut: 1000).
- `reloadOnDisconnect` (bool): restart si le `SwRemoteObject` du container n'est plus vu en SHM registry.
- `disconnectTimeoutMs` (int): timeout d'absence en registry avant restart (defaut: 5000).
- `disconnectCheckMs` (int): periode de polling registry (defaut: 1000).
- Windows (flags `SwProcess`): `createNoWindow`, `createNewConsole`, `detached`, `suspended` (voir `processFlagsFromOptions_()`).

### Node entry (process standalone)

Champs d'identite:
- `sys` (string, optionnel): override sys pour CE node.
- `ns` / `namespace` (string, requis): namespace du node.
- `name` / `object` (string, requis): nom du node.

Execution:
- `executable` (string, requis): chemin vers l'exe du node (base sur `SW_REMOTE_OBJECT_NODE` ou compatible).
- `workingDirectory` (string, optionnel): CWD du process (defaut: dossier de l'exe).
- `duration_ms` (int, optionnel): passe au node via `--duration_ms`.
- `config_root` (string, optionnel): passe au node via `--config_root` (voir `SW_REMOTE_OBJECT_NODE`).

Config:
- `config_file` (string, optionnel): si present, passe tel quel au node via `--config_file`.
- sinon: si `params` et/ou `options` et/ou `config_root` est present, `SwLaunch` genere `sw_node_<ns_name>.json` dans `SwStandardLocation::Temp` avec `{ "params": {...}, "options": {...}, "config_root": "..." }`.

Options launcher + flags process (dans `spec.options`):
- meme cles que pour un container (`reloadOnCrash`, `reloadOnDisconnect`, `restartDelayMs`, `disconnectTimeoutMs`, `disconnectCheckMs`, flags Windows).
- en plus, si un `config_file` temporaire est genere, le bloc `options` est aussi forwarde au node: `SW_REMOTE_OBJECT_NODE` n'utilise aujourd'hui que `watchdog` / `activeWatchDog` (voir `src/core/remote/SwRemoteObjectNode.h`).

## Supervision "reloadOnDisconnect" (presence SHM)

Implementation (cf `remoteObjectLastSeenMs_()` + `checkOnline_()` dans `SwNode/SwLaunch/SwLaunch.cpp`):

- le launcher lit un snapshot: `sw::ipc::shmRegistrySnapshot(sys)`,
- il cherche une entree dont:
  - `object` == `<ns>/<name>`
  - `signal` commence par `__config__|` (marqueur de presence publie par `SwRemoteObject` quand la config SHM est active),
- si rien n'est vu pendant `disconnectTimeoutMs`, alors restart.

## Exemple (inline JSON)

Exemple minimal: 1 container + 1 plugin + 2 composants.

```json
{
  "sys": "demo",
  "containers": [
    {
      "ns": "demo",
      "name": "container",
      "executable": "./SwComponentContainer.exe",
      "workingDirectory": ".",
      "options": {
        "reloadOnCrash": true,
        "reloadOnDisconnect": true,
        "disconnectTimeoutMs": 5000
      },
      "composition": {
        "threading": "same_thread",
        "plugins": ["PingPongPlugin"],
        "components": [
          { "type": "demo/PingComponent", "ns": "demo", "name": "ping", "params": { "peer": "demo/pong" } },
          { "type": "demo/PongComponent", "ns": "demo", "name": "pong", "params": { "peer": "demo/ping" } }
        ]
      }
    }
  ]
}
```

Notes:
- les `type` des composants viennent de `SW_REGISTER_COMPONENT_NODE(...)` et sont normalises (ex: `demo::PingComponent` -> `demo/PingComponent`). Voir `src/core/remote/SwRemoteObjectComponent.h`.
- pour le schema precis du container + ses RPC: `docs/nodes/SwComponentContainer.md`.
