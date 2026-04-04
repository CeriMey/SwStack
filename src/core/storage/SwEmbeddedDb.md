# SwEmbeddedDb

## But

`SwEmbeddedDb` est une base embarquee locale orientee :

- stockage cle -> valeur binaire,
- blobs volumineux sur disque local,
- index secondaires ordonnes,
- un seul writer actif,
- plusieurs readers possibles.

Le module est pense pour un disque local SSD/NVMe. Il n'est pas prevu pour un filesystem reseau ni pour un usage SQL.

## API publique

Header public :

- `src/core/storage/SwEmbeddedDb.h`

Types principaux :

- `SwEmbeddedDbOptions`
- `SwDbStatus`
- `SwDbWriteBatch`
- `SwDbEntry`
- `SwDbIterator`
- `SwDbSnapshot`
- `SwDbMetrics`
- `SwEmbeddedDb`

Operations exposees :

- `open`
- `close`
- `get`
- `write`
- `sync`
- `scanPrimary`
- `scanIndex`
- `refresh`
- `createSnapshot`
- `metricsSnapshot`

## Voir aussi

- `src/core/storage/SwEmbeddedDbPerformance.md`

## Usage minimal

```cpp
#include "SwEmbeddedDb.h"

SwEmbeddedDb db;

SwEmbeddedDbOptions options;
options.dbPath = "C:/data/my-db";
options.lazyWrite = false;
options.commitWindowMs = 20;
options.memTableBytes = 64ull * 1024ull * 1024ull;
options.inlineBlobThresholdBytes = 256ull * 1024ull;
options.readCacheBytes = 512ull * 1024ull * 1024ull;
options.maxBackgroundJobs = 2;
options.enableShmNotifications = true;

SwDbStatus status = db.open(options);
if (!status.ok()) {
    // gerer l'erreur
}

SwDbWriteBatch batch;

SwMap<SwString, SwList<SwByteArray> > secondaryKeys;
SwList<SwByteArray> values;
values.append(SwByteArray("group-a"));
secondaryKeys["by_group"] = values;

batch.put(SwByteArray("user:42"), SwByteArray("payload"), secondaryKeys);

status = db.write(batch);
if (!status.ok()) {
    // gerer l'erreur
}

status = db.sync();
if (!status.ok()) {
    // gerer l'erreur de durabilite
}

SwByteArray value;
status = db.get(SwByteArray("user:42"), &value, nullptr);

db.close();
```

## Ecriture

Les ecritures passent par `SwDbWriteBatch`.

Deux modes existent :

- `lazyWrite = false` : `write()` ne reussit qu'une fois la durabilite WAL atteinte.
- `lazyWrite = true` : `write()` publie d'abord en memoire dans le process, puis la durabilite est faite en fond. Utiliser `sync()` pour attendre que l'etat visible soit durable.

`sync()` force un drain immediat de la file d'ecritures en attente, sans attendre la fin du `commitWindowMs`.

### Inserer ou mettre a jour

```cpp
SwDbWriteBatch batch;

SwMap<SwString, SwList<SwByteArray> > secondaryKeys;
SwList<SwByteArray> tags;
tags.append(SwByteArray("camera-1"));
secondaryKeys["by_tag"] = tags;

batch.put(primaryKey, value, secondaryKeys);
db.write(batch);
```

### Supprimer

```cpp
SwDbWriteBatch batch;
batch.erase(primaryKey);
db.write(batch);
```

Important :

- les `secondaryKeys` sont fournis explicitement par l'appelant,
- la base ne parse pas le blob applicatif pour fabriquer les index,
- un `put` remplace la valeur primaire et republie l'etat des index secondaires du record.
- en `lazyWrite`, un reader du meme process voit la nouvelle valeur immediatement, mais un reader externe ne la voit qu'apres la durabilite WAL ou un `sync()`.

## Lecture

### Lecture directe

`get(primaryKey, &value, &secondaryKeysOut)`

Retour :

- `Ok` si la cle existe,
- `NotFound` sinon.

### Scan primaire

```cpp
SwDbIterator it = db.scanPrimary(startKey, endKey);
while (it.isValid()) {
    const SwDbEntry& row = it.current();
    it.next();
}
```

### Scan d'index secondaire

```cpp
SwDbIterator it = db.scanIndex("by_group", startSecondaryKey, endSecondaryKey);
while (it.isValid()) {
    const SwDbEntry& row = it.current();
    // row.secondaryKey
    // row.primaryKey
    // row.value
    it.next();
}
```

## Snapshots

`createSnapshot()` capture une vue coherente de l'etat visible au moment de l'appel.

En `lazyWrite`, cette vue peut inclure des writes encore non durables sur disque.

Cas d'usage :

- plusieurs lectures coherentes sur le meme etat,
- scan + get sans voir des commits intermediaires,
- iteration stable pendant flush/compaction.

Exemple :

```cpp
SwDbSnapshot snapshot = db.createSnapshot();
SwByteArray value;
snapshot.get(primaryKey, &value, nullptr);

SwDbIterator it = snapshot.scanPrimary();
while (it.isValid()) {
    it.next();
}
```

## Readers read-only

Pour un reader :

- ouvrir avec `options.readOnly = true`,
- appeler `refresh()` si l'application veut recharger explicitement l'etat disque,
- ou activer `enableShmNotifications` pour profiter d'un auto-refresh opportuniste.

Important :

- un reader ne peut pas ecrire,
- un seul writer est autorise a la fois,
- le verrou writer repose sur `LOCK`.

## Layout disque

Le dossier de base contient :

- `CURRENT`
- `LOCK`
- `OPTIONS.json`
- `MANIFEST-*.dbm`
- `wal/WAL-*.log`
- `tables/L*-*.sst`
- `blobs/BLOB-*.dat`
- `tmp/`

Role des fichiers :

- `CURRENT` : pointe vers le manifest courant,
- `MANIFEST-*` : etat persistant des tables visibles,
- `WAL-*` : journal d'ecriture avant application en memoire,
- `L*-*.sst` : tables immuables,
- `BLOB-*` : stockage externe des grosses valeurs,
- `tmp/` : ecriture atomique et fichiers temporaires.

## Architecture interne

Le module public inclut des helpers internes situes dans `src/core/storage/embeddeddb/`.

### Couches de format et lecture

- `SwEmbeddedDbCodec.h`
  - encodage binaire,
  - payload primaire,
  - records de table,
  - noms de fichiers,
  - comparateurs de tables.
- `SwEmbeddedDbBloomFilter.h`
  - bloom filter des SSTables.
- `SwEmbeddedDbTable.h`
  - ouverture, parsing et lecture de blocs d'une SSTable.

### Couches de read model et snapshot

- `SwEmbeddedDbSnapshotState.h`
  - etat capture d'un snapshot,
  - vue read-only du moteur visible.
- `SwEmbeddedDbReadModel.h`
  - segments primaires immuables pre-ordonnes,
  - segments d'index secondaires couvrants,
  - structure specialisee pour les scans.
- `SwEmbeddedDbReadModelIterators.h`
  - iterateurs `scanPrimary` et `scanIndex` sur le read model,
  - resolution blob a la demande pendant l'iteration.
- `SwEmbeddedDbSnapshotFacade.h`
  - facade `SwDbIterator` / `SwDbSnapshot`.

### Couches d'orchestration

- `SwEmbeddedDbLifecycle.h`
  - ouverture, fermeture, snapshot, lecture facade.
- `SwEmbeddedDbManifest.h`
  - chargement et persistance du manifest.
- `SwEmbeddedDbWal.h`
  - ouverture WAL, replay WAL, codec WAL.
- `SwEmbeddedDbWriteCoordinator.h`
  - group commit, application des batches, index secondaires.

### Couches de maintenance disque

- `SwEmbeddedDbLookup.h`
  - lookup primaire visible et resolution blob.
- `SwEmbeddedDbMaterializer.h`
  - rematerialisation de l'etat vivant primaire,
  - construction du read model specialise scan.
- `SwEmbeddedDbTableWriter.h`
  - ecriture SSTable et housekeeping WAL couvert.
- `SwEmbeddedDbFlushScheduler.h`
  - rotation memtable -> immutable et flush async/sync.
- `SwEmbeddedDbBlobStore.h`
  - externalisation et lecture d'etat blob.
- `SwEmbeddedDbBlobGcWorker.h`
  - garbage collection des blobs.
- `SwEmbeddedDbCompactionWorker.h`
  - compaction `L0 -> L1`.
- `SwEmbeddedDbNotifications.h`
  - notifications SHM pour readers.
- `SwEmbeddedDbMaintenanceHelpers.h`
  - helpers techniques de maintenance.

## Pipeline de donnees

### Ecriture

1. `write(batch)`
2. ajout a la file pending
3. group commit
4. ecriture WAL + `sync`
5. application dans la memtable mutable
6. publication SHM optionnelle
7. rotation en immutable si seuil memtable depasse
8. flush en SSTable
9. compaction et blob GC en tache de fond

### Lecture

Ordre de resolution logique :

1. memtable mutable
2. memtables immutables
3. SSTables du plus recent au plus ancien
4. si valeur externe : lecture du blob

### Scan

Les scans ne parcourent plus directement les memtables et SSTables visibles.

Ils consomment un read model specialise :

- segments primaires immuables tries par `primaryKey`,
- segments d'index secondaires tries par `secondaryKey + primaryKey`,
- references directes de l'index secondaire vers les lignes primaires materialisees,
- resolution blob uniquement au moment ou la ligne scannee en a reellement besoin.

Effet recherche :

- `scanPrimary` devient un parcours sequentiel memory-first,
- `scanIndex` n'a plus besoin de refaire un `lookupPrimary()` complet par ligne,
- les readers read-only peuvent recharger le read model quand l'etat visible change.

## Regles de fonctionnement

- un seul writer,
- plusieurs readers read-only,
- les SSTables sont immuables,
- les blobs peuvent etre inline ou externes,
- les index secondaires stockent des cles composites `secondaryKey + primaryKey`,
- les deletes sont geres avec tombstones,
- les manifests rendent l'etat visible atomiquement.

## Ce que le module ne fait pas

- pas de SQL,
- pas de multi-writer arbitraire,
- pas de transactions longues multi-sessions,
- pas de full-text,
- pas de support filesystem reseau.

## Validation actuelle

Self-test principal :

- `exemples/51-EmbeddedDbSelfTest/EmbeddedDbSelfTest.cpp`

Benchmark dedie :

- `exemples/52-EmbeddedDbBench/EmbeddedDbBench.cpp`
- `exemples/53-EmbeddedDbQtSqlBench/EmbeddedDbQtSqlBench.cpp`

Le self-test couvre aujourd'hui :

- ouverture writer / reader,
- write / get simple,
- scan d'index secondaire,
- flush blob et externalisation,
- lecture blob,
- second writer refuse,
- reader auto-refresh via SHM,
- scan reader apres refresh,
- blob GC sur blobs vivants / morts.

## Benchmark

Le benchmark `EmbeddedDbBench` mesure :

- debit d'ingestion `write`,
- latences `get`,
- latences `get` sur gros blobs,
- temps d'ouverture writer / reader,
- latence de scans pagines `100` et `1000` lignes,
- debit de `scanPrimary`,
- debit d'un `scanIndex` sur une tranche d'index secondaire.

Le benchmark `EmbeddedDbQtSqlBench` compare la meme charge entre :

- `SwEmbeddedDb`
- `QSql + SQLite`

Il permet de positionner le moteur face a une base locale mature avec `WAL`.

Exemple de lancement :

```bash
EmbeddedDbBench --records 100000 --batch-size 200 --value-bytes 512 --blob-bytes 262144 --blob-every 50 --read-samples 10000 --commit-window-ms 0

EmbeddedDbBench --records 100000 --batch-size 1 --lazy-write --commit-window-ms 20
```

```bash
EmbeddedDbQtSqlBench --db-root D:/EmbeddedDbQtSqlCompare-run01 --records 2000 --batch-size 100 --value-bytes 128 --blob-bytes 4096 --blob-every 20 --read-samples 200 --scan-limit 500 --index-cardinality 64 --commit-window-ms 0 --memtable-bytes 262144 --inline-blob-threshold-bytes 1024 --sqlite-journal-mode WAL --sqlite-synchronous FULL
```

Options utiles :

- `--db-path` pour garder la base apres le run,
- `--keep-db` pour eviter le nettoyage automatique du dossier temporaire,
- `--commit-window-ms 20` pour mesurer le comportement proche du defaut runtime,
- `--inline-blob-threshold-bytes` pour forcer ou non l'externalisation blob,
- `--scan-limit 0` pour scanner tout l'etat visible,
- `--assert-targets` pour faire echouer le benchmark si les objectifs internes par defaut ne sont pas tenus.

Le mode `--assert-targets` valide actuellement :

- `writer-open <= 1000 ms`
- `reader-open <= 1000 ms`
- `write p50 <= 500 us`, `p95 <= 2000 us`, `p99 <= 5000 us`
- `get p50 <= 100 us`, `p95 <= 500 us`, `p99 <= 2000 us`
- `get-blob p50 <= 1000 us`, `p95 <= 3000 us`, `p99 <= 5000 us`
- `scan-primary-page-100 <= 2 ms`
- `scan-primary-page-1000 <= 10 ms`
- `writer-close <= 500 ms`

## Points d'attention pour l'integration

- fournir des `secondaryKeys` coherents a chaque `put`,
- utiliser des cles binaires stables et ordonnables,
- ne pas supposer qu'un reader voit un commit sans `refresh()` si les notifications SHM sont desactivees,
- garder le `dbPath` sur stockage local,
- prevoir des tests applicatifs si le format du blob a un sens metier fort.

## Point d'entree recommande

Pour comprendre rapidement le module, lire dans cet ordre :

1. `SwEmbeddedDb.h`
2. `SwEmbeddedDb.md`
3. `SwEmbeddedDbLifecycle.h`
4. `SwEmbeddedDbWriteCoordinator.h`
5. `SwEmbeddedDbWal.h`
6. `SwEmbeddedDbTable.h`
7. `SwEmbeddedDbReadModel.h`
8. `SwEmbeddedDbReadModelIterators.h`
9. `SwEmbeddedDbSnapshotFacade.h`
10. `SwEmbeddedDbBlobGcWorker.h`
11. `SwEmbeddedDbCompactionWorker.h`
