# `SwRemoteObject` (`src/core/ipc/SwRemoteObject.h`)

> Note: ce document est legacy. Les chemins `src/core/ipc/*` correspondent désormais à `src/core/remote/*`.

Documentation volontairement **très technique** basée sur l’implémentation du header.

`SwRemoteObject` est une classe de base (hérite de `SwObject`) qui combine :

- une **configuration JSON en couches** (global/local/user) avec fusion profonde des objets (`mergeObjectDeep_`),
- une logique de **persistence “sticky”** (clé conservée dans le fichier user dès qu’elle a été modifiée au moins une fois),
- un canal de **synchronisation multi-process** (snapshot JSON complet via SHM) : `__config__|<configId>`,
- des canaux de **mise à jour explicite par clé** (payload texte) : `__cfg__|<configName>`,
- une API “haut niveau” pour binder de la config à des variables (`ipcRegisterConfig*`), suivre une config distante (`ipcBindConfig*`), se connecter à des signaux IPC (`ipcConnect*`) et exposer des RPC (`ipcExposeRpc*`),
- un `sw::ipc::Registry ipcRegistry_` servant d’identité IPC (domaine + objet) pour les signaux SHM, queues RPC et canaux internes.

> Vocabulaire :
> - `nameSpace` = **domaine** IPC (ex: `"demo"` ou `"demo/app1"`)
> - `objectName` = **objet** IPC (ex: `"device1"`)
> - `configId` = identifiant de “bus de config” (plusieurs objets peuvent partager le même `configId`)

---

## 0) Identité IPC : `Registry`, FQN, normalisation

Le constructeur instancie :

- `ipcRegistry_(nameSpace_, objectName)` : identité utilisée par `sw::ipc::Signal` et `sw::ipc::RingQueue`.
- `publisherId_ = makePublisherId_(this)` : identifiant de publication pour filtrer ses propres messages.

### 0.1 FQN d’objet et “full name”

Helpers fournis :

- `buildObjectFqn(ns, obj)` → `"ns/.../obj"` (normalise `\` en `/`, supprime les `/` en tête/queue).
- `buildFullName(ns, obj, leaf)` → `"ns/.../obj#leaf"` (séparateur **recommandé** `#`).
- `ipcFullName(leaf)` → `buildFullName(nameSpace_, objectName, leaf)`.

Parsing (utilisé par `ipcConnect*` / `ipcBindConfig*`) :

- format recommandé : `"ns/.../obj#leaf"` (leaf peut contenir `/` si `#` est utilisé),
- format legacy : `"ns/.../obj/leaf"` (leaf **ne doit pas** contenir `/`).

### 0.2 `publisherId_`

`publisherId_` est calculé comme :

```
(pid << 32) ^ (selfPtr * 0x9e3779b97f4a7c15)
```

Propriétés :

- unique “probable” par process + instance d’objet,
- **non stable** entre exécutions (dépend de l’adresse de `this`),
- utilisé pour ignorer ses propres publications (`if (pubId == publisherId_) return;`).

---

## 1) Configuration sur disque : layout, racine, sanitization

### 1.1 Racine

- `configRoot_` défaut : `"systemConfig"`.
- `configRootAbsolute_()` :
  - si `root` est absolu (`/`, `\` ou `C:`…), utilisé tel quel,
  - sinon : `SwDir::currentPath() + root`.

API :

- `SwString configRootDirectory() const`
- `void setConfigRootDirectory(const SwString& rootDir)`
  - met à jour `configRoot_`,
  - crée les répertoires,
  - recharge (`loadConfig_locked()`),
  - rafraîchit les configs enregistrées,
  - émet `configLoaded(merged)`.

### 1.2 Chemins résolus

Layout (merge “dernier gagne”) :

- Global : `<root>/global/<objectName>.json`
- Local  : `<root>/local/<nameSpace>_<objectName>.json`
- User   : `<root>/user/<nameSpace>_<objectName>.json`

Sanitization :

- `sanitizeSegment(x)` : remplace tout caractère hors `[a-zA-Z0-9_.-]` par `_`, fallback `"root"`.
- `sanitizeNsForFile_(ns)` : remplace `/` et `\` par `_`, trim `_` et `/` aux extrémités, puis `sanitizeSegment`.

API :

- `ConfigPaths configPaths() const` → `{globalPath, localPath, userPath}`

---

## 2) Modèle en mémoire : documents et invariants

Docs internes (protégés par `mutex_`) :

- `globalDoc_`, `localDoc_`, `userDoc_` : couches “raw” (toujours objets JSON, sinon forcés via `ensureObjectRoot()`),
- `mergedDoc_` : fusion profonde de `global+local+user`,
- `userTouchedPaths_` : map `path -> true` des chemins “touchés” (persistence sticky).

Chargement (`loadConfig_locked`) :

1. reset docs à `{}` et `userTouchedPaths_.clear()`,
2. charge chaque fichier si présent (sinon couche vide),
3. pour `userDoc_` :
   - `stripTouchedMetaFromDoc_(userDoc_, &userTouchedPaths_)` (retire `__swconfig__` si présent),
   - `collectLeafConfigPaths_(userDoc_, "", userTouchedPaths_)` (tout leaf présent est “touched”),
4. `recomputeMerged_locked()`.

`loadConfig()` :

- exécute `loadConfig_locked()` + `refreshRegisteredConfigs_locked_()`,
- **déporte** les callbacks `onChange` hors lock (liste `pending`),
- émet `configLoaded(merged)`.

---

## 3) Fusion profonde (deep merge) : spécification exacte

La fusion est réalisée par `mergeObjectDeep_(SwJsonObject& target, const SwJsonObject& src)` :

- pour chaque clé `k` de `src` :
  - si `target[k]` et `src[k]` sont tous deux des **objets JSON** → fusion récursive (`mergeValueDeep_`),
  - sinon → **écrasement** (`target[k] = src[k]` / insertion).

Conséquences :

- les tableaux (`array`) ne sont **pas** fusionnés élément par élément : ils écrasent,
- les scalaires écrasent,
- seule la structure “objets JSON” est fusionnée.

---

## 4) Addressage des clés : `path` et normalisation

Les méthodes “config” utilisent des chemins `/` :

- lecture : `configValue("image/brightness")`
  - découpe sur `/` (après remplacement `\` → `/`),
  - ignore les segments vides,
  - traverse **uniquement des objets** (pas d’indexation de tableau),
  - si absent → `SwJsonValue()` (null).

- écriture : `setConfigValue(path, SwJsonValue, ...)`
  - passe par `SwJsonDocument::find(path, createMissing=true)` (création de nœuds manquants côté doc).

Normalisation interne des chemins de config (pour la sticky map) : `normalizeConfigPath_` :

- trim,
- `\` → `/`,
- collapse `//`,
- supprime `/` en tête et en queue.

---

## 5) Persistence “sticky” : `userTouchedPaths_` + pruning vs baseline

Objectif : le fichier user (`userDoc_`) doit contenir un **minimum** d’overrides, tout en conservant la “mémoire” des clés modifiées au moins une fois.

### 5.1 Baseline (`baselineConfigValue_locked_`)

La baseline est :

1. merge deep de `globalDoc_ + localDoc_`,
2. + injection des **defaults** de toutes les configs enregistrées via `ipcRegisterConfig*` (si la clé n’existe pas).

Cette baseline sert à décider si une valeur user est “redondante” (égale à la baseline) donc prunable.

### 5.2 Égalité profonde JSON (`jsonDeepEqual_`)

Utilisée pendant le pruning :

- compare récursivement objets et tableaux,
- compare string/bool/null,
- traite les nombres “compatibles” (int vs double) comme égaux (selon `SwJsonValue`).

### 5.3 Pruning d’un doc user : `buildUserOverrideValue_`

Entrées :

- `candidate` = doc user “candidat” (après application d’une modification),
- `base`      = baseline,
- `keepPaths` = `userTouchedPaths_` (chemins sticky),
- `pathPrefix` = chemin courant (récursion).

Règle :

- si `candidate[path] == base[path]` **et** `path` n’est pas sticky → supprimer,
- si `path` est sticky → conserver même si redondant,
- pour les objets : pruning récursif par clé, suppression si l’objet devient vide.

Résultat :

- `outOverride` = JSON minimal à persister comme couche user,
- retour `false` si tout est redondant (doc user vide).

### 5.4 Meta “touched” dans le snapshot IPC : `__swconfig__`

Pour transporter l’info sticky entre processus, un meta-bloc est injecté **uniquement** dans les JSON publiés sur SHM :

```json
{
  "...": "...",
  "__swconfig__": {
    "v": 1,
    "touched": ["a/b/c", "mode/profile"]
  }
}
```

Implémentation :

- injection : `injectTouchedMeta_locked_(doc)` dans `effectiveConfigJson_locked_`,
- extraction : `stripTouchedMetaFromDoc_(doc, &userTouchedPaths_)` (fusionne dans la map),
- **jamais écrit sur disque** : `writeUserDocToFile_locked_` strippe systématiquement le meta avant écriture.

---

## 6) API publique : config locale (semantique et effets de bord)

### 6.1 Lecture

- `SwJsonObject mergedConfig() const` : copie de `mergedDoc_` (mutex).
- `SwJsonValue configValue(path) const` : lookup read-only dans `mergedDoc_` (mutex).

### 6.2 Chargement / sauvegarde

- `bool loadConfig()` :
  - recharge les fichiers,
  - recompute merged,
  - rafraîchit les configs enregistrées,
  - émet `configLoaded(merged)`,
  - retourne `true` (best-effort, pas d’erreur détaillée).

- `bool saveLocalConfig(pretty)` :
  - écrit `localDoc_` tel quel (après `ensureObjectRoot`).

- `bool saveUserConfig(pretty)` :
  - annule le debounce,
  - écrit `userDoc_` **sans** `__swconfig__`.

### 6.3 Écriture : `setConfigValue(path, value, saveToDisk, publishToShm)`

Pipeline exact :

1. clone `candidateDoc = userDoc_`,
2. `candidateDoc.find(path, true) = value`,
3. `markConfigPathTouched_locked_(path)`,
4. calcule `baseline = baselineConfigValue_locked_()`,
5. prune `candidateDoc` vs baseline en respectant `userTouchedPaths_` (`buildUserOverrideValue_`),
6. si doc user change :
   - `userDoc_ = nextUserDoc`,
   - `recomputeMerged_locked()`,
   - si `saveToDisk` : `scheduleUserDocSave_locked_()` (debounced, pretty),
   - si `publishToShm && shmConfigEnabled_` : publish snapshot `effectiveConfigJson_locked_(Compact)` sur `__config__|<configId>`,
   - `refreshRegisteredConfigs_locked_()` (remplit `pending`),
7. exécute `pending` hors lock,
8. si doc a changé : émet `configChanged(merged)`.

Point clé : le payload publié sur SHM n’est **pas** `userDoc_`, mais le **snapshot effectif** :

- `mergedDoc_`,
- + defaults injectés des `ipcRegisterConfig*` absents,
- + `__swconfig__` (touched).

---

## 7) Synchronisation SHM “config complète” : `__config__|<configId>`

### 7.1 Signal interne

`SwRemoteObject` instancie :

- `sw::ipc::Signal<uint64_t, SwString> shmConfig_(ipcRegistry_, "__config__|"+configId_)`

La signature est volontairement :

- `pubId` : `uint64_t` (filtrage),
- `json`  : `SwString` (payload JSON compact).

### 7.2 Activation / désactivation

- `enableSharedMemoryConfig(true)` :
  - démarre la subscription (`connect(..., fireInitial=true)`),
  - le callback ignore `pubId == publisherId_`.

- `disableSharedMemoryConfig()` :
  - stoppe la subscription.

### 7.3 Publication initiale

Dans le constructeur, après `enableSharedMemoryConfig(true)` :

- publie un snapshot initial (si `shmConfigEnabled_`) pour permettre l’introspection (ex: SwBridge).

### 7.4 Réception : `applyRemoteConfig(pubId, json)`

Pipeline :

1. parse JSON → doit être un objet, sinon ignore,
2. extrait/merge `__swconfig__` (touched) dans `userTouchedPaths_`,
3. calcule baseline (global+local+defaults enregistrés),
4. prune le doc reçu vs baseline en respectant `userTouchedPaths_`,
5. `collectLeafConfigPaths_(nextUserDoc, "", userTouchedPaths_)`,
6. si `userDoc_` change :
   - `userDoc_ = nextUserDoc`,
   - recompute merged,
   - `scheduleUserDocSave_locked_()`,
   - refresh configs enregistrées,
   - émet (hors lock) :
     - `configChanged(merged)`
     - `remoteConfigReceived(remotePublisherId, merged)`
7. si seul le set touched a changé : schedule save (le fichier user ne contiendra pas le meta, mais le pruning peut dépendre du sticky set).

Threading :

- si `SwCoreApplication::instance(false)` existe, la réception est “repostée” via `app->postEvent(...)` pour exécuter `applyRemoteConfig` sur le thread de l’event loop.

---

## 8) IPC config “par clé” : `__cfg__|<configName>`

### 8.1 `ipcRegisterConfigT` (binding variable ↔ config)

Objectif : attacher une variable C++ à une clé de config locale.

Effets :

- initialisation : `storage = configValueFromDocs_(mergedDoc_, configName, defaultValue)`
  - si absent → `defaultValue`,
  - si présent mais conversion impossible → `defaultValue`.

- enregistrement :
  - stocke `defaultJson = valueToJson_(defaultValue)` (sert à la baseline),
  - stocke `onMergedChanged` (met à jour `storage` si `!(storage == next)`),
  - `registeredConfigs_` est indexé par `fullName = "ns/obj#configName"`.

- canal “update explicite” :
  - crée `Signal<uint64_t, SwString>(ipcRegistry_, "__cfg__|"+configName)` via `ensureConfigSignal_`,
  - se subscribe (`fireInitial=false`) : payload `SwString` → `T` via `stringToValue_`,
  - si update distante reçue :
    - applique localement via `ipcUpdateConfig<T>(configName, next, SaveToDisk)` (donc `publishToShm=false`),
    - émet `remoteConfigValueReceived(pubId, configName)`.

Nota : à l’enregistrement, un snapshot `__config__|<configId>` est republié (si SHM activée) pour inclure les defaults “injectés”.

### 8.2 `ipcUpdateConfig(configName, value, savePolicy)` (local-only)

- écrit `configName` dans la couche user (`setConfigValue(..., publishToShm=false)`),
- optionnellement persiste sur disque,
- **ne publie pas** `__config__|<configId>` (pas de propagation).

### 8.3 `ipcUpdateConfig(targetObject, configName, value)` (remote push)

Publie sur la cible :

- `Signal<uint64_t, SwString>(Registry(ns,obj), "__cfg__|"+configName).publish(publisherId_, valueToString_(value))`

La cible n’appliquera l’update que si elle a enregistré la clé via `ipcRegisterConfig*` (c’est `ipcRegisterConfigT` qui installe le listener).

### 8.4 `ipcBindConfigT(storage, "ns/obj#configName")`

Abonne localement à la config d’un autre objet via son canal `__cfg__|<leaf>` :

- `fireInitial=true` (récupère le “dernier message” si disponible),
- conversion texte → type via `stringToValue_`,
- update de `storage` + callback utilisateur.

Retourne un `token` utilisable avec `ipcDisconnect(token)`.

---

## 9) Conversions de types (templates) : JSON ↔ valeur, texte ↔ valeur

Le système combine deux axes :

1) **JSON** (pour disque + snapshot `__config__`) via `valueToJson_` / `jsonToValue_`  
2) **Texte** (pour updates `__cfg__`) via `valueToString_` / `stringToValue_`

### 9.1 `valueToJson_`

- types explicites : `SwString`, `bool`, `int`, `float`, `double`, `SwAny`, `SwList<T>`, `SwMap<SwString,V>`.
- fallback générique `T` : sérialise en **string** via `valueToString_<T>()`.

⚠️ Conséquence : un type custom non géré explicitement sera stocké en JSON **string**, pas en objet/array.

### 9.2 `valueToString_`

- fallback générique : `SwAny::from<T>(v).convert<SwString>()` (peut dépendre des conversions disponibles).
- `SwAny` : encode en JSON compact si objet/array, sinon `"true"`, `"42"`, etc.
- listes/maps : JSON compact.

### 9.3 `stringToValue_`

- fallback générique : `SwAny(s).convert<T>()` (try/catch → false si échec).
- `SwAny` : tente parse JSON ; sinon garde la string.
- `SwList<T>` :
  1. tente parse JSON array,
  2. sinon fallback CSV `"a,b,c"` (trim + retire quotes `'`/`"`).
- `SwMap<SwString,V>` : parse JSON object (sinon false).

---

## 10) IPC signaux génériques : `SW_REGISTER_SHM_SIGNAL` + `ipcConnect*`

`SwRemoteObject` ne “magicalise” pas les signaux : il fournit `ipcRegistry_` et des helpers.

### 10.1 Publier/écouter un signal SHM depuis une classe dérivée

Déclarez vos signaux avec `SW_REGISTER_SHM_SIGNAL` (macro de `SwSharedMemorySignal.h`) :

```cpp
class MyObj : public SwRemoteObject {
  SW_REGISTER_SHM_SIGNAL(ping, int, SwString);
  SW_REGISTER_SHM_SIGNAL(pong, int, SwString);
};
```

Le membre est un `sw::ipc::SignalProxy<...>` :

- `ping.publish(args...)` ou `ping(args...)`
- `ping.connect(cb, fireInitial=true, timeoutMs=0)`

### 10.2 Se connecter à un signal distant

- `ipcConnectT(fullName, cb, fireInitial)`
  - `fullName` : `"ns/obj#signal"` (ou legacy `"ns/obj/signal"`),
  - déduit `A...` depuis la signature de `cb`,
  - crée `sw::ipc::Signal<A...>(Registry(ns,obj), leaf)` et `connect`,
  - stocke la subscription, retourne un token.

- `ipcConnectScopedT(targetObject, leaf, context, cb, fireInitial)`
  - crée une `IpcConnection` enfant de `context`,
  - stop automatique à la destruction de `context`.

Déconnexion :

- `ipcDisconnect(token)`

⚠️ Typage : le type réel du signal SHM doit correspondre exactement aux types attendus (sinon mismatch à l’exécution selon la politique de `sw::ipc::Signal`).

### 10.3 Gros payload (images/frames) : `sw::ipc::NoCopyRingBuffer<Meta>` (0-copy lecteur)

Limite importante : les signaux SHM et les queues RPC utilisent un payload fixe (actuellement **4096 bytes**). Pour transporter des images (plusieurs Mo), utilisez un ring-buffer SHM dédié et ne passez sur IPC que des "petits" évènements (seq).

Header : `src/core/remote/SwIpcNoCopyRingBuffer.h`

Côté producteur (ex: objet dérivé de `SwRemoteObject`) :

```cpp
struct FrameMeta {
  static constexpr const char* kTypeName = "FrameMetaV1";
  uint32_t w=0, h=0, fmt=0;
  uint64_t bytes=0;
};

class CameraObj : public SwRemoteObject {
  SW_REGISTER_SHM_NOCOPY_RINGBUFFER(video, FrameMeta, 100, 10 * 1024 * 1024); // 100 slots, 10MB max/slot

  void onFrame(const uint8_t* src, uint32_t bytes, const FrameMeta& meta) {
    auto w = video.beginWrite();        // 0-copy producteur si vous remplissez directement w.data()
    if (!w.isValid()) return;           // ring plein -> drop (best-effort)
    std::memcpy(w.data(), src, bytes);  // ou pipeline -> w.data()
    w.meta() = meta;
    (void)w.commit(bytes);
  }
};
```

Côté lecteur :

```cpp
sw::ipc::Registry reg(domain, objectFqn);               // registry du producteur
using RB = sw::ipc::NoCopyRingBuffer<FrameMeta>;
RB rb = RB::open(reg, "video");
RB::Consumer c = rb.consumer();                         // keep-up multi-consumer
auto sub = rb.connect(c, [](uint64_t seq, RB::ReadLease lease) {
  // lease.data()/lease.bytes() pointent dans la SHM (pas de copie)
});
```

Notes :
- (NoCopyRingBuffer) discovery: signal `__rbmap__|<name>` + typeName JSON + shmName,
- (NoCopyRingBuffer) notify: signal `__rb__|<name>` (payload = `uint64_t seq`),
- (NoCopyRingBuffer) keep-up multi-consumer: publisher drops when full (`droppedCount()`),
- (NoCopyRingBuffer) demo: `exemples/27-IpcVideoFrameRingBuffer/IpcVideoFrameRingBuffer.cpp`,
- découverte via la registry (signal `__rbmap__|<name>` + typeName JSON + shmName),
- notification via un signal `__rb__|<name>` (payload = `uint64_t seq`),
- `publishCopy()` copie ; `beginWrite()` permet d'ecrire directement dans la SHM (si votre pipeline le supporte).
- mémoire réservée ~ `capacity * maxBytes` (100 * 10MB ~ 1GB).

---

## 11) RPC IPC : `ipcExposeRpc*` (ringbuffer, multi-client)

### 11.1 Exposition côté serveur (`SwRemoteObject`)

- `ipcExposeRpcT(methodName, handler, fireInitial)`
  - `handler` supporte :
    - `Ret(Args...)`
    - `Ret(sw::ipc::RpcContext, Args...)` (ctx injecté par le framework)

Implémentation :

- requêtes : `sw::ipc::RingQueue<10, uint64_t, uint32_t, SwString, Args...>`
  - champs : `callId`, `clientPid`, `clientInfo`, `args...`
  - queue name : `sw::ipc::rpcRequestQueueName(methodName)` (voir `SwIpcRpc.h`)
- réponses :
  - si `Ret` non-void : `RingQueue<10, uint64_t, bool, SwString, Ret>`
  - si `Ret` void     : `RingQueue<10, uint64_t, bool, SwString>`
  - queue name : `sw::ipc::rpcResponseQueueName(methodName, clientPid)`

Threading :

- si `SwCoreApplication` existe : `req.connect(...)` (handler exécuté sur le thread de l’event loop),
- `req.connect(...)` (threadless via `LoopPoller`).

Limitations actuelles :

- pas de `try/catch` autour du handler,
- la réponse est envoyée avec `ok=true` et `err=""` (pas de propagation d’erreur implémentée),
- push réponse best-effort (retour ignoré) → un client peut timeout si la response queue sature.

### 11.2 Côté client (hors `SwRemoteObject`)

Le client est dans `SwIpcRpc.h` : `sw::ipc::RpcMethodClient<Ret, Args...>`.

---

## 12) Thread-safety, callbacks, cycle de vie

### 12.1 Verrous

- `mutex_` protège : docs JSON, `userTouchedPaths_`, subscription maps, flags de save.
- `rpcRespMutex_` protège les caches de queues RPC de réponse.

### 12.2 Déport de callbacks

`ipcRegisterConfigT` et `loadConfig` construisent une liste `pending` de lambdas à exécuter **hors du lock** (pattern anti-deadlock).

### 12.3 Garde “alive”

Les callbacks IPC capturent `std::shared_ptr<std::atomic_bool> alive_` :

- destruction : `alive_->store(false)`,
- callbacks : `if (!alive || !alive->load()) return;`

But : éviter du use-after-free lorsque des callbacks arrivent après destruction.

### 12.4 Destruction

Le destructeur :

- flush le save debounced (best-effort),
- `disableSharedMemoryConfig()` + stop subscriptions,
- stoppe toutes les subscriptions IPC stockées,
- clear les caches RPC.

---

## 13) Signaux `SwObject` exposés par `SwRemoteObject` (in-process)

Déclarés via `DECLARE_SIGNAL(...)` (signaux **locaux**, pas SHM) :

- `configChanged(const SwJsonObject&)`
- `configLoaded(const SwJsonObject&)`
- `remoteConfigReceived(uint64_t publisherId, const SwJsonObject&)`
- `remoteConfigValueReceived(uint64_t publisherId, const SwString& configName)`

Ordre typique :

- `loadConfig()` / `setConfigRootDirectory()` → `configLoaded(merged)`
- `setConfigValue(...)` (si changement effectif) → `configChanged(merged)`
- réception SHM `__config__` (si changement effectif) → `configChanged(merged)` puis `remoteConfigReceived(pubId, merged)`
- réception `__cfg__` (si changement effectif) → `configChanged(merged)` puis `remoteConfigValueReceived(pubId, configName)`

---

## 14) Références / exemples

- `exemples/23-ConfigurableObjectDemo/DemoSubscriber.cpp` : `ipcRegisterConfig`, `ipcConnect`, `ipcExposeRpc`.
- `exemples/23-ConfigurableObjectDemo/RpcClientDemo.cpp` : client RPC (`sw::ipc::RpcMethodClient`).
- `SwNode/SwAPI/SwBridge/SwBridgeHttpServer.cpp` : tooling (introspection registry, ping, `__cfg__`).
