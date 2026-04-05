# `SwLaunch`

`SwLaunch` est devenu un outil d'orchestration complet. Il ne sert plus seulement a lancer des process a partir d'un JSON. Il sait maintenant:

- charger un etat desire depuis un fichier ou une string JSON,
- demarrer des `nodes` et des `containers`,
- surveiller les process et les redemarrer selon des regles simples,
- exposer une API HTTP de controle protegee par token,
- appliquer des changements de configuration a chaud,
- deployer des binaires et des fichiers metier avec verification SHA-256,
- persister l'etat final dans le `launch.json`,
- revenir a l'etat precedent si un deploiement echoue.

Ce document est le guide d'ensemble. Pour le detail fonctionnalite par fonctionnalite, voir `docs/nodes/SwLaunch_FunctionalDetails.md`.

Sources principales:

- `tools/SwNode/SwLaunch/SwLaunch.cpp`
- `tools/SwNode/SwLaunch/SwLaunchController.h`
- `tools/SwNode/SwLaunch/SwLaunchDeploySupport.h`
- `docs/nodes/SwComponentContainer.md`

## Role de `SwLaunch`

`SwLaunch` est le point d'entree qui transforme un JSON de lancement en systeme reel en cours d'execution.

Concretement, il fait quatre choses:

1. Il interprete un etat desire.
2. Il demarre les unites demandees.
3. Il surveille leur sante et leur presence.
4. Il accepte, si on l'active, des ordres de controle et de deploiement a chaud.

Il faut le voir comme un superviseur local, oriente fichier et process, pas comme un orchestrateur distribue.

## Ce que `SwLaunch` sait piloter

Deux types d'unites sont geres:

- `nodes`: executables autonomes, generalement bases sur `SwRemoteObjectNode`.
- `containers`: process comme `SwComponentContainer`, capables d'heberger plusieurs composants.

Chaque unite est identifiee par une cle stable:

- node: `node:<ns>/<name>`
- container: `container:<ns>/<name>`

Cette cle est importante, car elle sert:

- dans l'etat runtime,
- dans les jobs de deploiement,
- dans les artefacts uploades,
- dans la logique de restart cible.

## Modes d'utilisation

### 1. Mode lancement simple

Vous fournissez un `launch.json`. `SwLaunch` demarre les unites et s'arrete quand on lui demande.

### 2. Mode supervision

En plus du lancement, `SwLaunch` peut:

- relancer un process si son `exitCode` est anormal,
- relancer un process si son `SwRemoteObject` disparait de la SHM registry.

### 3. Mode controle HTTP

Si vous demarrez `SwLaunch` avec les bons arguments, il ouvre une API HTTP locale protegee par bearer token.

### 4. Mode deploiement a chaud

Dans ce mode, `SwLaunch` recoit un manifeste multipart avec des fichiers, verifie les checksums, remplace les fichiers non identiques, persiste la nouvelle configuration, redemarre seulement les unites impactees, puis publie le resultat dans un job de deploiement.

## Cycle de vie general

Le fonctionnement nominal est le suivant:

1. `SwLaunch` charge le JSON depuis `--config_file` ou `--config_json`.
2. Il determine le `baseDir`.
3. Il resout les chemins d'executables et de repertoires de travail.
4. Il construit le plan d'unites a gerer.
5. Il demarre les process.
6. Si l'API de controle est activee, il ouvre l'ecoute HTTP.
7. Pendant l'execution, il maintient:
   - l'etat desire courant,
   - la liste des unites actives,
   - la liste des jobs de deploiement,
   - les regles de supervision et de restart.

## CLI

`SwLaunch` exige un des deux arguments suivants:

- `--config_file=<path>`
- `--config_json=<json>`

Overrides globaux:

- `--sys=<domain>`
- `--duration_ms=<ms>`

Activation de l'API de controle:

- `--control_port=<port>`
- `--control_bind=<ipv4>`
- `--control_token=<secret>`

Regles importantes:

- `control_bind` vaut `127.0.0.1` par defaut.
- l'API de controle n'est activee que si `control_port > 0`.
- les mutations runtime sont refusees si `SwLaunch` n'a pas ete demarre avec `--config_file`.
- le token de controle ne doit jamais etre stocke dans le `launch.json`.

## Structure du `launch.json`

Le root JSON contient principalement:

- `sys`
- `duration_ms`
- `control_api` optionnel pour les valeurs non secretes (`bind`, `port`)
- `nodes`
- `containers`

Le modele de verite de `SwLaunch` est un etat desire complet. Cela signifie qu'un `PUT /api/launch/state` remplace l'etat logique du launcher, il ne fait pas un patch partiel opportuniste.

Le fichier peut etre modifie et repersiste par `SwLaunch` lui-meme si une mutation ou un deploiement reussit.

## Resolution des chemins

`baseDir` est le dossier du `--config_file` si vous utilisez un fichier. Sinon, c'est le repertoire courant.

Les champs suivants sont resolus a partir de ce `baseDir`:

- `executable`
- `workingDirectory`
- `config_file`
- certains chemins de travail internes

Quand `workingDirectory` n'est pas fourni, `SwLaunch` prend le dossier de l'executable.

Cette regle est fondamentale, car elle determine:

- ou le child est lance,
- depuis ou les plugins sont resolus,
- ou les artefacts peuvent etre deployes.

## Supervision runtime

Deux mecanismes de supervision existent.

### Restart sur crash

Si `reloadOnCrash` est actif, un process termine avec un `exitCode != 0` est relance apres `restartDelayMs`.

### Restart sur disparition SHM

Si `reloadOnDisconnect` est actif, `SwLaunch` surveille la presence du `SwRemoteObject` dans la registry SHM. Si l'objet attendu disparait plus longtemps que `disconnectTimeoutMs`, le process est considere hors ligne et relance.

Ce mecanisme est utile quand le process reste vivant mais ne publie plus correctement son etat.

## API de controle HTTP

L'API de controle est volontairement etroite. Elle n'est pas une API generique d'administration.

Routes disponibles:

- `GET /api/launch/help`
- `GET /api/launch/help/:topic`
- `GET /api/launch/state`
- `PUT /api/launch/state`
- `POST /api/launch/deploy`
- `GET /api/launch/deploy/:jobId`

Toutes les routes `/api/*` exigent:

- `Authorization: Bearer <token>`

Exception:

- les routes de help HTTP sont lisibles sans token:
  - `GET /api/launch/help`
  - `GET /api/launch/help/:topic`

## Help distant HTTP

Le help distant permet de demander a `SwLaunch` de decrire lui-meme son API, sa version et ses capacites, sans se connecter au terminal du serveur.

Il faut le voir comme une porte d'entree de decouverte. Avant d'utiliser le launcher a distance, vous pouvez d'abord appeler le help HTTP pour savoir:

- quelle version de `SwLaunch` tourne,
- quelles routes sont exposees,
- quelles fonctions sont supportees,
- quels topics de documentation sont disponibles,
- si le flux de logs et le deploiement a chaud sont presents sur cette instance.

### `GET /api/launch/help`

Cette route retourne une vue d'ensemble en JSON avec:

- `product`
- `version`
- `apiVersion`
- `buildStamp`
- `helpTopics`
- `capabilities`
- `routes`
- `auth`
- `features`
- `text`

Exemple d'usage:

```bash
curl http://<ip>:7777/api/launch/help
```

Cette route est utile pour:

- inventorier une instance en cours d'execution,
- verifier qu'un binaire a bien ete mis a jour,
- confirmer que les capacites attendues existent avant d'appeler une route sensible.

### `GET /api/launch/help/:topic`

Cette route retourne le detail d'un sujet precis.

Exemples de topics:

- `control_api`
- `logs`
- `deploy`
- `checksum`
- `rollback`

Exemple d'usage:

```bash
curl http://<ip>:7777/api/launch/help/logs
```

### Exemples operateur

Verifier la version et les capacites:

```bash
curl http://<ip>:7777/api/launch/help
```

Verifier comment fonctionne le flux de logs:

```bash
curl http://<ip>:7777/api/launch/help/logs
```

Verifier la procedure de deploiement:

```bash
curl http://<ip>:7777/api/launch/help/deploy
```

### Ce qu'il faut retenir

Le help distant n'execute aucune action sur le runtime. Il sert uniquement a decrire l'instance `SwLaunch` en cours.

En pratique:

- utilisez `help` pour decouvrir,
- utilisez `state` pour observer,
- utilisez `deploy` ou `PUT /state` pour agir.

### `GET /api/launch/state`

Retourne:

- l'etat desire courant,
- un resume runtime des unites,
- l'etat de l'API de controle.

Le token n'est jamais retourne.

### `PUT /api/launch/state`

Accepte un etat desire complet et applique une reconciliation ciblee:

- unite supprimee du JSON: arret runtime,
- unite ajoutee: demarrage,
- unite modifiee: restart cible,
- unite inchangee: pas d'action.

Cette route est utile pour des changements de configuration sans upload de fichiers.

### `POST /api/launch/deploy`

Accepte un `multipart/form-data` avec:

- une part `manifest`,
- une ou plusieurs parts fichier,
- un etat final desire dans le manifeste.

Le deploiement se fait comme un job asynchrone et retourne `202`.

### `GET /api/launch/deploy/:jobId`

Permet de suivre l'etat d'un job:

- `queued`
- `running`
- `succeeded`
- `failed`

Le job publie aussi:

- `phase`
- `errors`
- `impactedUnits`
- `replacedFiles`
- `skippedFiles`

## Deploiement a chaud

Le deploiement a chaud suit toujours la meme logique.

1. Le client envoie un manifeste + des fichiers.
2. `SwLaunch` verifie que chaque part attendue est bien presente.
3. Il calcule le SHA-256 de chaque upload.
4. Il compare chaque checksum avec le checksum du fichier deja deploye.
5. Il ne remplace pas un fichier deja identique.
6. Il determine les unites impactees.
7. Il arrete seulement ces unites.
8. Il remplace les fichiers non identiques.
9. Il persiste le `launch.json`.
10. Il redemarre l'etat final.
11. Si un echec survient, il rollbacke les fichiers et le runtime.

Le deploiement est donc:

- cible,
- controle par checksum,
- transactionnel a l'echelle du launcher.

## Manifeste de deploiement

Le manifeste V1 contient:

- `formatVersion`
- `deploymentId` optionnel
- `desiredState`
- `artifacts`

Chaque artefact contient:

- `partName`
- `ownerKey`
- `relativePath`
- `sha256`

Contraintes importantes:

- `relativePath` doit rester relatif,
- les chemins absolus sont rejetes,
- les chemins avec `..` sont rejetes,
- l'artefact doit appartenir a une unite connue du nouvel etat desire.

## Comment `SwLaunch` choisit le repertoire d'ecriture

L'ecriture d'un artefact n'est pas libre. Elle est limitee a la racine de l'unite cible:

- `workingDirectory` si defini,
- sinon repertoire de l'executable.

Autrement dit, `SwLaunch` sait remplacer des fichiers du perimetre d'une unite, mais ne devient pas un serveur d'ecriture arbitraire sur toute la machine.

## Effet du checksum

Le checksum sert a deux choses:

1. verifier l'integrite de l'upload,
2. eviter de remplacer un fichier deja identique.

Si un binaire ou un fichier metier est deja identique:

- le remplacement est ignore,
- l'artefact apparait dans `skippedFiles`,
- il n'y a pas de restart induit par ce fichier seul.

Cette regle permet de rejouer un bundle sans provoquer de restart inutile.

## Persistance du `launch.json`

Quand une mutation reussit, `SwLaunch` reecrit le `launch.json`.

Cette reecriture:

- conserve le modele d'etat desire courant,
- n'ecrit pas le token de controle,
- se fait de maniere atomique avec fichier temporaire puis remplacement.

Si l'application runtime echoue, la persistance est rollbackee avec le reste.

## Repertoires et fichiers generes

Selon les cas, `SwLaunch` peut creer:

- des `config_file` temporaires pour les enfants,
- `systemConfig/` pour la configuration trace,
- `_runlogs/_http_multipart/` pour le staging HTTP multipart,
- `_runlogs/_deploy/` pour les jobs de deploiement,
- `log/SwLaunch.log` selon la configuration de trace.

Ces dossiers ne sont pas des entrees metier de votre etat desire. Ce sont des repertoires techniques de fonctionnement.

## Ce qui redemarre et ce qui ne redemarre pas

`SwLaunch` cherche a minimiser l'impact.

Redemarrage cible:

- changement de spec d'un node: restart de ce node,
- changement de spec d'un container: restart de ce container,
- artefact d'un node: restart de ce node,
- artefact d'un container: restart de ce container.

En V1, un changement interne a la composition d'un container est normalise en restart du container proprietaire. Il n'y a pas de hot reload fin par plugin ou composant via RPC dans le flux de deploiement.

## Sequencement et concurrence

`SwLaunch` ne traite qu'une mutation a la fois.

Consequences:

- si un `PUT /api/launch/state` arrive pendant une autre mutation, il recoit `409`,
- si un `POST /api/launch/deploy` arrive pendant un job en cours, il recoit `409`.

Cette serialisation est volontaire. Elle simplifie les garanties de rollback.

## Ce qu'il faut verifier en exploitation

Verification minimale:

1. l'API de controle repond si elle est activee,
2. `GET /api/launch/state` retourne bien l'unite attendue avec `running=true`,
3. un job de deploiement passe a `succeeded`,
4. les `replacedFiles` et `skippedFiles` correspondent a ce qui etait attendu,
5. le `launch.json` persiste bien le nouvel etat,
6. le token de controle n'apparait jamais dans le fichier de config.

## Cas d'usage typiques

### Cas 1. Lancement local simple

Vous avez un `launch.json` fixe et vous voulez juste lancer un systeme de demo ou d'integration.

### Cas 2. Supervision locale

Vous voulez que les process reviennent automatiquement apres crash ou disparition SHM.

### Cas 3. Teleoperation locale

Vous exposez l'API de controle sur `127.0.0.1` et vous pilotez le launcher depuis un outil d'admin ou un service compagnon.

### Cas 4. Mise a jour ciblee

Vous envoyez un nouveau binaire pour un seul node avec son manifeste. `SwLaunch` remplace uniquement ce binaire et ne redemarre que ce node.

## Limites actuelles

- l'API de controle est locale par defaut et attend un bearer token simple,
- la mutation runtime exige `--config_file`,
- le hot reload granulaire d'un composant interne n'est pas le mode de deploiement V1,
- les suppressions de fichiers metier obsoletes ne sont pas automatisees,
- le modele reste volontairement local et fichier-centrique.

## Document suivant

Pour comprendre une fonctionnalite precise sans relire tout le guide, ouvrir `docs/nodes/SwLaunch_FunctionalDetails.md`.
