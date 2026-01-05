# `SwComponentContainer` (container de composants `SwRemoteObject`)

`SwComponentContainer` est un **process** (un node base sur `SwRemoteObject`) qui sait:

- charger des plugins (DLL/SO) qui enregistrent des factories de composants,
- instancier des composants (`SwRemoteObject`) **dans le meme process**,
- appliquer des params a chaud via `setConfigValue(...)`,
- exposer des RPC de management (load/unload/restart, introspection).

Sources de reference:
- `SwNode/SwComponentContainer/SwComponentContainer.cpp` (container: config, threads, RPC, reconciliation).
- `src/core/remote/SwRemoteObjectComponent.h` (API plugin + macros `SW_REGISTER_COMPONENT_NODE*` + symbole exporte).
- `src/core/remote/SwRemoteObjectComponentRegistry.h` (registry type -> create/destroy).
- `src/core/runtime/SwLibrary.h` (chargement dynamique + resolution du suffixe `.dll/.so/.dylib`).
- `src/core/remote/SwRemoteObjectNode.h` (CLI: `SW_REMOTE_OBJECT_NODE`).

## Build

Depuis la racine:
- `build.bat`

Plugins:
- `exemples/24-ComponentPlugin/CMakeLists.txt` construit `PingPongPlugin` (shared lib) avec `SW_COMPONENT_PLUGIN=1`.
- Source: `exemples/24-ComponentPlugin/PingPongPlugin.cpp`.

## Run (node CLI)

Le binaire finit par `SW_REMOTE_OBJECT_NODE(SwComponentContainer)` (voir `SwNode/SwComponentContainer/SwComponentContainer.cpp`), donc il supporte les args standard de `src/core/remote/SwRemoteObjectNode.h`:

- identite: `--sys=... --ns=... --name=...`
- config runtime: `--config_file=...` ou `--config_json=...`
- divers: `--duration_ms=...`, `--config_root=...`

Exemple (config inline):

```bash
SwComponentContainer.exe --sys=demo --ns=demo --name=container --config_json="{\"composition\":{\"plugins\":[\"PingPongPlugin\"],\"components\":[]}}"
```

## Config: `composition/*`

Le container observe 3 cles de config (enregistrees avec `ipcRegisterConfig(...)` dans le constructeur):

- `composition/plugins` (`SwStringList`): liste de plugins a charger (paths ou noms).
- `composition/components` (`SwAny` converti en `SwJsonArray`): liste de composants a instancier.
- `composition/threading` (`SwString`): mode d'execution des composants.
  - defaut: `"same_thread"`
  - autres valeurs acceptees: `"thread_per_plugin"`, `"thread-per-plugin"`, `"per_plugin"` (cf `isThreadPerPlugin_()`).

Un exemple de config typique (fichier passe via `--config_file`, ou genere par `SwLaunch`):

```json
{
  "composition": {
    "threading": "same_thread",
    "plugins": ["PingPongPlugin"],
    "components": [
      { "type": "demo/PingComponent", "ns": "demo", "name": "ping", "params": { "peer": "demo/pong" } },
      { "type": "demo/PongComponent", "ns": "demo", "name": "pong", "params": { "peer": "demo/ping" } }
    ]
  },
  "options": { "watchdog": true }
}
```

### Schema d'un composant (`composition/components[]`)

Parsing dans `reconcile_()` (`SwNode/SwComponentContainer/SwComponentContainer.cpp`):

- `type` (string, requis): nom de type **tel qu'enregistre dans le registry**.
  - avec `SW_REGISTER_COMPONENT_NODE(demo::PingComponent)` le type devient `demo/PingComponent` (normalisation `::` -> `/`), cf `src/core/remote/SwRemoteObjectComponent.h`.
  - astuce: `listPluginsInfo()` te donne la liste exacte des `components` exposes par plugin.
- `ns` / `namespace` (string, optionnel): namespace du composant (defaut: namespace du container).
- `name` / `object` (string, requis): nom du composant.
- `params` (object, optionnel): applique via `setConfigValue(path, value, saveToDisk=false, publishToShm=true)` pour chaque cle.
- `options` (object, optionnel): interprete partiellement aujourd'hui:
  - `watchdog` / `activeWatchDog` (bool): appelle `SwCoreApplication::activeWatchDog()` (voir `applyNodeOptions_()`).
  - `reloadOnCrash` (bool): lu mais **pas exploite** dans l'implementation actuelle (champ conserve dans `Wanted`).
- champs racine acceptes aussi: `activeWatchDog`, `reloadOnCrash` (meme effet que dans `options`).

### Reconciliation (load/unload/update)

Algorithme (cf `applyComposition_()` + `reconcile_()`):

1. charge tous les plugins listes dans `composition/plugins`.
2. construit un set "wanted" (cle = `ns/name`) depuis `composition/components`.
3. detruit les composants qui ne sont plus dans "wanted".
4. pour chaque composant wanted:
   - si deja instancie avec le meme `type`: reapplique `params`,
   - si instancie avec un autre `type`: detruit puis recree,
   - applique l'option `activeWatchDog` sur le thread du composant.

## Threads: `composition/threading`

Deux modes:

- `"same_thread"`: tous les composants vivent sur le thread du container.
- `"thread_per_plugin"`: les composants charges depuis un plugin vivent sur **un thread dedie par plugin**:
  - le container cree un `SwThread` par `pluginPath` (`ensurePluginThread_()`),
  - la factory `create(sys, ns, name, parent)` est executee sur ce thread (`executeBlockingOnThread`),
  - `params` et `destroy` sont aussi executees sur le thread du composant (cf `applyParamsOnInstanceThread_()` / `destroyComponent_()`).

Changer le mode force un "restart" in-process: `onThreadingModeChanged_()` fait `shutdown_()` puis `applyComposition_()` (recreation des composants).

## Plugins: contrat & macros

### Symbole exporte

Le container charge une DLL/SO via `SwLibrary` puis cherche le symbole:
- `sw::component::plugin::registerSymbolV1()` -> `"swRegisterRemoteObjectComponentsV1"`

Il appelle ensuite la fonction `RegisterFnV1(registry*)` (cf `SwNode/SwComponentContainer/SwComponentContainer.cpp::loadPlugin_()`).

### Enregistrer un composant dans un plugin

Le plus simple: utiliser les macros dans `src/core/remote/SwRemoteObjectComponent.h`:

```cpp
#include "SwRemoteObjectComponent.h"

namespace demo {
class MyComp : public SwRemoteObject { /* ... */ };
} // namespace demo

SW_REGISTER_COMPONENT_NODE(demo::MyComp);
```

Notes:
- le type publie est derive du nom C++ et normalise (ex: `demo::MyComp` -> `demo/MyComp`).
- si tu veux un nom stable/explicite, utilise `SW_REGISTER_COMPONENT_NODE_AS(demo::MyComp, "demo/my_comp")`.
- le plugin doit etre compile avec `SW_COMPONENT_PLUGIN=1` (fait automatiquement par le CMake du dossier `plugins/`) pour exporter `swRegisterRemoteObjectComponentsV1`.

## RPC exposees par le container

Les RPC sont exposees via `ipcExposeRpc(...)` dans le constructeur de `SwComponentContainer` (toutes avec `fireInitial=true`).

Pour appeler une RPC depuis un autre process, utiliser `sw::ipc::RpcMethodClient` (`src/core/remote/SwIpcRpc.h`) avec:
- `domain` = `sys`
- `object` = `ns/name` (sans le sys)
- `methodName` = nom ci-dessous

### Liste des methodes

Tous les retours sont des `SwString` contenant du JSON compact:

- `status()` -> object JSON: `{ ok, sys, container, threading, plugins, pluginsInfo, components }`
- `loadPlugin(path)` -> `{ ok, err? }`
- `listPlugins()` -> array JSON des plugins charges (cles internes = paths resolus)
- `listPluginsInfo()` -> array JSON d'objets `{ path, library, components }`
  - `library` contient l'introspection `SwLibrary::introspectionJson()`
  - `components` est la liste des types ajoutes par ce plugin
- `loadComponent(typeName, nameSpace, objectName, paramsJson)` -> `{ ok, target? , err? }`
  - `paramsJson` doit etre un object JSON sous forme de string (string vide = `{}`)
- `unloadComponent(targetObject)` -> `{ ok, err? }`
- `restartComponent(targetObject)` -> `{ ok, target? , err? }`
- `stopPlugin(pluginQuery)` -> `{ ok, plugin, stopped, failed? }`
- `restartPlugin(pluginQuery)` -> `{ ok, plugin, restarted, failed? }`
- `listComponents()` -> array JSON des composants (voir `listComponentsJson()`):
  - `{ type, plugin, sys, ns, name, objectFqn, target }`

Format de `targetObject`:
- accepte `sys/ns/name` ou `ns/name` (cf `splitTargetObjectFqn_()`).

Format de `pluginQuery`:
- accepte la cle exacte retournee par `listPlugins()` (souvent un path absolu),
- ou un nom "leaf" avec ou sans extension (ex: `PingPongPlugin` / `PingPongPlugin.dll`), si non ambigu (cf `resolveLoadedPluginKey_()`).

### Exemple de calls RPC (client)

```cpp
#include "SwIpcRpc.h"

// target: sys=demo, objectFqn="demo/container"
sw::ipc::RpcMethodClient<SwString> status("demo", "demo/container", "status");
SwString s = status.call();

sw::ipc::RpcMethodClient<SwString, SwString> loadPlugin("demo", "demo/container", "loadPlugin");
SwString r1 = loadPlugin.call(SwString("PingPongPlugin"));

sw::ipc::RpcMethodClient<SwString, SwString, SwString, SwString, SwString> loadComponent(
    "demo", "demo/container", "loadComponent");
SwString r2 = loadComponent.call(SwString("demo/PingComponent"),
                                 SwString("demo"),
                                 SwString("ping"),
                                 SwString("{\"peer\":\"demo/pong\"}"));
```

### CLI `swapi` (equivalent)

Voir `docs/nodes/SwApi.md`.

```bash
swapi container status demo/demo/container --pretty
swapi container plugins info demo/demo/container --pretty
swapi container component load demo/demo/container demo/PingComponent demo ping --params "{\"peer\":\"demo/pong\"}"
```

## Notes: RPC `system/*` (factory)

Comme tout `SwRemoteObject`, le container (et chacun de ses composants) expose aussi:
- `system/saveAsFactory`
- `system/resetFactory`

Voir `docs/features/50_config_and_nodes.md` et `src/core/remote/SwRemoteObject.h`.
