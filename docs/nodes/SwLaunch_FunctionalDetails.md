# `SwLaunch` - details par fonctionnalite

Ce document sert de helper de detail. Il ne remplace pas `docs/nodes/SwLaunch.md`. Il permet de zoomer sur une fonction precise et de comprendre:

- a quoi elle sert,
- comment elle s'active,
- ce que `SwLaunch` fait exactement,
- ce qu'il faut verifier,
- quelles erreurs sont les plus frequentes.

Guide general:

- `docs/nodes/SwLaunch.md`

## 1. Charger un `launch.json`

### Objectif

Faire demarrer `SwLaunch` a partir d'un etat desire lisible et reproductible.

### Procedure

1. Demarrer `SwLaunch` avec `--config_file=<path>` ou `--config_json=<json>`.
2. Verifier que le JSON racine est un objet.
3. Verifier qu'au moins un des tableaux `nodes` ou `containers` est present.
4. Laisser `SwLaunch` resoudre tous les chemins relatifs depuis le dossier du fichier de config.

### Effet attendu

`SwLaunch` connait:

- le `baseDir`,
- le `sys`,
- la liste des unites a demarrer,
- les options de supervision,
- l'eventuelle configuration d'API de controle.

### Verification

- pas d'erreur de parsing au demarrage,
- les executables sont trouves,
- les unites sont lancees dans le bon repertoire.

### Erreurs frequentes

- `config_json` valide mais racine non objet,
- executable relatif evalue depuis un mauvais dossier,
- confusion entre `workingDirectory` du launcher et `workingDirectory` d'un child.

## 2. Demarrer un node

### Objectif

Lancer un executable autonome supervise par `SwLaunch`.

### Procedure

1. Declarer une entree dans `nodes`.
2. Fournir `ns`, `name` et `executable`.
3. Ajouter `workingDirectory` si le binaire depend de fichiers relatifs.
4. Ajouter `params`, `options` ou `config_root` si le node doit recevoir une configuration.

### Effet attendu

`SwLaunch`:

- resolve l'executable,
- prepare un `config_file` temporaire si necessaire,
- lance le process,
- le surveille ensuite selon les options.

### Verification

- le node apparait dans `GET /api/launch/state`,
- `running=true`,
- les logs du node montrent qu'il a bien lu son `config_file`.

### Erreurs frequentes

- `ns` ou `name` manquant,
- executable non resolu,
- `config_root` attendu par le node mais non fourni.

## 3. Demarrer un container

### Objectif

Lancer un process de type container, typiquement `SwComponentContainer`, capable d'heberger plusieurs composants.

### Procedure

1. Declarer une entree dans `containers`.
2. Fournir `ns`, `name` et `executable`.
3. Fournir soit `config_file`, soit une `composition`.
4. Regler `workingDirectory` pour que les plugins se resolvent correctement.

### Effet attendu

`SwLaunch` genere si besoin un `config_file` temporaire pour la composition, puis demarre le container.

### Verification

- le container apparait dans l'etat runtime,
- les composants sont ensuite visibles cote container,
- les plugins se chargent depuis le bon CWD.

### Erreurs frequentes

- plugin introuvable a cause d'un `workingDirectory` incorrect,
- confusion entre spec du container et spec interne des composants.

## 4. Restart sur crash

### Objectif

Faire revenir une unite qui s'est terminee anormalement.

### Procedure

1. Activer `options.reloadOnCrash=true`.
2. Regler `restartDelayMs` si le delai par defaut ne convient pas.

### Effet attendu

Si le child se termine avec un `exitCode != 0`, `SwLaunch` attend `restartDelayMs` puis relance ce child.

### Verification

- message de terminaison dans les logs,
- nouveau demarrage du meme `ownerKey`,
- retour a `running=true`.

### Erreurs frequentes

- attendre un restart alors que le child est sorti avec `exitCode == 0`,
- croire que `reloadOnCrash` couvre un blocage logique sans sortie process.

## 5. Restart sur disparition SHM

### Objectif

Relancer un process encore vivant mais qui ne publie plus correctement sa presence dans la registry.

### Procedure

1. Activer `options.reloadOnDisconnect=true`.
2. Ajuster `disconnectTimeoutMs`.
3. Ajuster `disconnectCheckMs` si besoin.

### Effet attendu

Si le marqueur `__config__|...` n'est plus vu assez longtemps pour l'objet attendu, `SwLaunch` restart l'unite.

### Verification

- warning de type offline dans les logs,
- arret puis relance de l'unite,
- retour du marqueur de presence apres redemarrage.

### Erreurs frequentes

- activer le mecanisme sur un child qui ne publie pas de presence exploitable,
- mettre un timeout trop court et provoquer des restarts parasites.

## 6. Activer l'API de controle

### Objectif

Piloter le launcher a chaud sans redemarrage manuel.

### Procedure

1. Demarrer `SwLaunch` avec `--control_port`.
2. Fournir `--control_token`.
3. Laisser `--control_bind=127.0.0.1` sauf besoin particulier.

### Effet attendu

Le launcher ecoute localement et protege ses routes par bearer token.

### Verification

- `GET /api/launch/state` retourne `200` avec un bearer valide,
- la meme route retourne `401` sans token ou avec un mauvais token.

### Erreurs frequentes

- oublier `--control_token`,
- exposer l'API trop largement sans comprendre le perimetre de securite.

## 7. Consulter le help distant HTTP

### Objectif

Demander a une instance `SwLaunch` en cours d'execution de decrire son API, sa version et ses capacites, sans acces au terminal local.

### Procedure

1. Appeler `GET /api/launch/help` pour obtenir la vue d'ensemble.
2. Lire `version`, `apiVersion`, `capabilities`, `routes` et `helpTopics`.
3. Appeler ensuite `GET /api/launch/help/:topic` pour demander le detail d'une fonction precise.

### Effet attendu

Le launcher retourne un JSON lisible contenant:

- l'identite du produit,
- la version du binaire,
- la liste des routes exposees,
- les capacites presentes sur cette instance,
- un texte d'aide correspondant au topic demande.

### Verification

- `GET /api/launch/help` retourne `200`,
- le champ `product` vaut `SwLaunch`,
- le champ `version` est renseigne,
- le champ `capabilities` mentionne par exemple `logs.sse_stream` si le flux logs est disponible.

### Erreurs frequentes

- confondre help distant et route d'action: le help decrit, il ne modifie rien,
- supposer qu'un serveur a les memes capacites qu'un autre sans verifier `version` et `capabilities`,
- oublier que le help detaille se lit par topic, par exemple `logs` ou `deploy`.

## 8. Lire l'etat courant

### Objectif

Recuperer l'image logique et runtime du launcher.

### Procedure

1. Appeler `GET /api/launch/state`.
2. Lire `desiredState`.
3. Lire `runtime.units`.

### Effet attendu

Vous obtenez:

- ce que `SwLaunch` veut faire,
- ce qui tourne reellement,
- les informations de controle API.

### Verification

- l'unite attendue existe,
- `running` vaut `true` pour les process en vie,
- le token n'apparait nulle part.

### Erreurs frequentes

- confondre etat desire et etat runtime,
- croire qu'un process hors ligne a deja ete retire de `desiredState`.

## 9. Modifier l'etat desire par `PUT /api/launch/state`

### Objectif

Changer la configuration cible sans upload de binaire.

### Procedure

1. Lire l'etat courant.
2. Construire un nouvel etat complet.
3. Envoyer ce root JSON a `PUT /api/launch/state`.

### Effet attendu

`SwLaunch` compare l'ancien et le nouvel etat, puis:

- ajoute les nouvelles unites,
- supprime celles retirees,
- redemarre celles dont la spec a change,
- laisse intactes les autres.

### Verification

- reponse `200`,
- `impactedUnits` coherent,
- nouvel etat visible dans `GET /api/launch/state`,
- `launch.json` persiste si demarrage via `--config_file`.

### Erreurs frequentes

- envoyer un patch partiel au lieu d'un etat complet,
- attendre une mutation alors que `SwLaunch` a ete lance via `--config_json`.

## 10. Deployer un bundle multipart

### Objectif

Mettre a jour un binaire ou un fichier metier en une seule operation transactionnelle.

### Procedure

1. Construire un manifeste avec `desiredState` et `artifacts`.
2. Ajouter une part `manifest`.
3. Ajouter une part par fichier reference dans `artifacts`.
4. Envoyer le tout a `POST /api/launch/deploy`.

### Effet attendu

`SwLaunch` cree un job, verifie les checksums, stage les fichiers, applique le remplacement puis redemarre les unites impactees.

### Verification

- reponse `202`,
- presence d'un `jobId`,
- suivi du job via `GET /api/launch/deploy/:jobId`.

### Erreurs frequentes

- nom de part absent ou incoherent,
- part manquante par rapport au manifeste,
- checksum faux.

## 11. Controle du checksum

### Objectif

Verifier a la fois l'integrite et l'utilite d'un remplacement.

### Procedure

1. Fournir le `sha256` attendu dans chaque artefact.
2. Laisser `SwLaunch` recalculer le SHA-256 de l'upload.
3. Laisser `SwLaunch` comparer avec le fichier deja deploye.

### Effet attendu

Deux cas:

- checksum upload different du manifeste: rejet,
- checksum upload egal au fichier deja present: skip du remplacement.

### Verification

- `replacedFiles` contient seulement les fichiers vraiment remplaces,
- `skippedFiles` contient les fichiers deja identiques.

### Erreurs frequentes

- confondre checksum manifeste et checksum du fichier deja sur disque,
- croire qu'un skip est une erreur alors que c'est un resultat normal.

## 12. Choisir les unites impactees

### Objectif

Limiter le restart aux seules unites concernees.

### Procedure

1. Associer chaque artefact a un `ownerKey`.
2. Laisser `SwLaunch` comparer les specs courantes et cibles.
3. Laisser `SwLaunch` calculer la liste finale des unites impactees.

### Effet attendu

Seules les unites liees a des changements de spec ou de fichiers sont arretees puis relancees.

### Verification

- `impactedUnits` du job,
- absence de restart sur les autres unites,
- comportement stable lors d'un redeploiement identique.

### Erreurs frequentes

- mauvais `ownerKey`,
- penser qu'un changement interne de composant redemarre seulement ce composant: en V1, c'est le container proprietaire qui redemarre.

## 13. Rollback en cas d'echec

### Objectif

Eviter un etat disque/runtime partiellement applique.

### Procedure

1. Laisser `SwLaunch` sauvegarder les fichiers necessaires.
2. En cas d'echec, laisser `SwLaunch` restaurer:
   - les fichiers remplaces,
   - le `launch.json`,
   - le runtime precedent autant que possible.

### Effet attendu

Le systeme revient vers l'etat precedent plutot que de rester dans un entre-deux.

### Verification

- job `failed`,
- messages de rollback dans les logs si necessaire,
- retour de l'ancien comportement runtime.

### Erreurs frequentes

- supposer qu'un echec laisse automatiquement les nouveaux fichiers en place,
- oublier que le rollback est borne au perimetre gere par le launcher.

## 14. Suivre un job de deploiement

### Objectif

Savoir exactement ou en est un deploiement.

### Procedure

1. Recuperer le `jobId` a la creation.
2. Poller `GET /api/launch/deploy/:jobId`.

### Effet attendu

Le job indique:

- son statut global,
- sa phase courante,
- les unites impactees,
- les fichiers remplaces,
- les fichiers ignores,
- les erreurs.

### Verification

- `status=succeeded` pour un succes,
- `status=failed` et `errors` si le flux a casse.

### Erreurs frequentes

- ne verifier que le `202` initial sans suivre le job,
- ne pas exploiter `skippedFiles` pour comprendre un redeploiement idempotent.

## 15. Persister la configuration

### Objectif

Faire du `launch.json` la verite durable de ce qui a ete applique.

### Procedure

1. Demarrer `SwLaunch` avec `--config_file`.
2. Appliquer une mutation ou un deploiement reussi.
3. Laisser `SwLaunch` reecrire le fichier.

### Effet attendu

Le prochain demarrage repart sur le bon etat desire.

### Verification

- le `launch.json` a ete reecrit,
- le token de controle n'apparait pas,
- le nouvel etat desire est bien celui du runtime.

### Erreurs frequentes

- attendre une persistance alors que le launcher a ete demarre en `--config_json`,
- injecter des secrets dans le JSON alors qu'ils doivent rester en CLI.

## 16. Savoir ou `SwLaunch` ecrit sur disque

### Objectif

Comprendre les traces et les repertoires de staging.

### Procedure

Observer en particulier:

- `systemConfig/`
- `_runlogs/_http_multipart/`
- `_runlogs/_deploy/`
- les fichiers temporaires `sw_launch_*` ou `sw_node_*`

### Effet attendu

Vous savez differencier:

- les fichiers metier deployes,
- les fichiers temporaires,
- les fichiers de supervision et de log.

### Verification

- les artefacts temporaires sont lies a une operation precise,
- les chemins de travail restent dans le perimetre attendu.

### Erreurs frequentes

- prendre un fichier temporaire pour une source metier,
- oublier de verifier le `workingDirectory` d'une unite avant de diagnostiquer un deploiement.

## 17. Lecture rapide des routes utiles

Pour l'exploitation quotidienne:

- etat global: `GET /api/launch/state`
- changement de config sans fichiers: `PUT /api/launch/state`
- deploiement avec fichiers: `POST /api/launch/deploy`
- suivi d'un deploiement: `GET /api/launch/deploy/:jobId`

## 18. Tests deja en place

Le comportement est maintenant couvre par:

- `tools/SwNode/SwLaunch/tests/SwLaunchSelfTest.cpp`
- `tools/SwNode/SwLaunch/tests/SwLaunchDeployIntegrationTest.cpp`

Le test d'integration couvre le scenario critique:

1. lancement d'un vrai child,
2. remplacement reel d'un binaire,
3. remplacement reel d'un fichier metier,
4. restart cible,
5. redeploiement identique avec `skip` checksum et sans restart supplementaire.
