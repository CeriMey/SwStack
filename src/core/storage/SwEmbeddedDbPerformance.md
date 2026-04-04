# SwEmbeddedDb - Notes de performance

## Objectif

Ce document resume les mesures recentes faites entre `SwEmbeddedDb` et `QSql` avec `SQLite`.

Le but est simple :

- dire ce que `SwEmbeddedDb` couvre deja correctement,
- dire ce qu'elle ne couvre pas encore au niveau de `SQLite`,
- fixer un plan de travail clair pour le chemin des scans.

## Validation fonctionnelle

Le self-test utilise :

- `exemples/51-EmbeddedDbSelfTest/EmbeddedDbSelfTest.cpp`

Au moment de cette note, il passe completement :

- `27 / 27 PASS`

Le self-test couvre notamment :

- write/read simple,
- blobs externes,
- index secondaires,
- reader read-only,
- notification SHM,
- blob GC,
- lazy write visible en memoire avant `sync()`.

## Protocole de mesure

Le comparatif utilise :

- `exemples/53-EmbeddedDbQtSqlBench/EmbeddedDbQtSqlBench.cpp`

Configuration du run :

- disque `D:` local en SSD NVMe,
- `20000` enregistrements,
- `batch-size = 1`,
- valeur primaire de `128 B`,
- blob de `4096 B` tous les `20` enregistrements,
- `SQLite` en `journal_mode = WAL`,
- `SQLite` en `synchronous = FULL`.

Les logs de reference courants sont :

- `build-embeddeddb-validate/embeddeddb_qtsql_compare_readmodel_v2.txt`
- `build-embeddeddb-validate/embeddeddb_scan_bench_readmodel_v8.txt`

## Ce que le bench couvre

Le bench mesure :

- `writer-open`,
- `write`,
- `writer-close`,
- `reader-open`,
- `get(primaryKey)`,
- `get(blob)`,
- `scanPrimary` page `100`,
- `scanPrimary` page `1000`,
- `scanPrimary` sur `5000` lignes,
- `scanIndex`.

Le bench ne mesure pas :

- requetes SQL complexes,
- jointures,
- agregations,
- charge multi-processus longue duree,
- recovery/crash dans ce comparatif precis.

## Resultats durables

Comparatif utile et equitable : `SwEmbeddedDb` en mode durable contre `SQLite FULL`.

| Operation | SwEmbeddedDb | SQLite | Lecture |
| --- | ---: | ---: | --- |
| writer-open | `40.10 ms` | `553.17 ms` | avantage `SwEmbeddedDb` |
| write p50 | `2358.20 us` | `3012.70 us` | avantage `SwEmbeddedDb` |
| write p95 | `10313.79 us` | `10883.28 us` | leger avantage `SwEmbeddedDb` |
| writer-close | `5258.07 ms` | `1335.21 ms` | retard `SwEmbeddedDb` |
| reader-open | `2248.04 ms` | `382.99 ms` | retard `SwEmbeddedDb` |
| get p50 | `29.20 us` | `101.65 us` | net avantage `SwEmbeddedDb` |
| get p95 | `142.63 us` | `763.76 us` | net avantage `SwEmbeddedDb` |
| get-blob p50 | `90.00 us` | `128.30 us` | avantage `SwEmbeddedDb` |
| get-blob p95 | `503.73 us` | `1556.59 us` | net avantage `SwEmbeddedDb` |
| scan-primary-page-100 | `1.57 ms` | `0.77 ms` | leger retard `SwEmbeddedDb` |
| scan-primary-page-1000 | `7.94 ms` | `4.83 ms` | retard modere `SwEmbeddedDb` |
| scan-primary 5000 | `35.98 ms` | `36.22 ms` | quasi parite, leger avantage `SwEmbeddedDb` |
| scan-index | `0.46 ms` | `1.68 ms` | net avantage `SwEmbeddedDb` |

## Resultats lazy

Le mode lazy sert a decoreler la latence visible par le process de la durabilite disque.

Dans ce mode :

- `write()` publie d'abord en memoire,
- le reader du meme process voit la nouvelle valeur tout de suite,
- la durabilite arrive plus tard,
- `sync()` force la vidange durable.

Ce mode n'est donc pas directement comparable a `SQLite FULL` sur la notion de durabilite immediate.

Malgre cela, il est utile pour lire le plafond de latence visible par le process appelant :

| Operation | SwEmbeddedDb lazy | SQLite FULL | Lecture |
| --- | ---: | ---: | --- |
| write p50 | `13.00 us` | `2656.05 us` | avantage massif `SwEmbeddedDb` |
| write p95 | `56.40 us` | `6783.61 us` | avantage massif `SwEmbeddedDb` |
| get p50 | `24.50 us` | `24.70 us` | equivalent |
| get p95 | `152.27 us` | `37.40 us` | retard `SwEmbeddedDb` |
| get-blob p50 | `90.60 us` | `25.90 us` | retard `SwEmbeddedDb` |
| scan-primary-page-1000 | `21.84 ms` | `0.95 ms` | gros retard `SwEmbeddedDb` |
| scan-primary 5000 | `94.78 ms` | `4.75 ms` | gros retard `SwEmbeddedDb` |
| scan-index | `4.54 ms` | `0.22 ms` | gros retard `SwEmbeddedDb` |

## Resume court

Aujourd'hui, `SwEmbeddedDb` est deja bonne sur les acces cibles suivants :

- write unitaire durable,
- `get(primaryKey)`,
- `get(blob)`,
- `scanIndex`,
- `scanPrimary` long sur etat chaud,
- mode `lazyWrite` quand on veut une visibilite immediatement dans le process.

En revanche, `SwEmbeddedDb` n'est pas encore au niveau de `SQLite` sur :

- le temps d'ouverture d'un reader,
- le temps de fermeture d'un writer.
- le scan primaire pagine court `100/1000` lignes reste encore un peu derriere.

La lecture la plus importante est la suivante :

- `SwEmbeddedDb` est maintenant competitive, et meme devant, sur plusieurs hot paths `KV + blob + index secondaire simple`,
- `SwEmbeddedDb` reste encore derriere sur les temps d'ouverture/fermeture et sur certaines petites pages de scan.

## Ce qu'il faut faire pour les scans

### Objectif

Le chemin `scanPrimary` et `scanIndex` doit devenir un chemin de parcours sequentiel specialise, pas une succession de lectures couteuses par ligne.

### Priorite 1 - Vrai range scan natif

Les scans doivent travailler par intervalle ordonne et non comme une repetition de recherches ponctuelles.

Il faut :

1. ajouter un vrai mode `seek(startKey)` sur les iterateurs de tables,
2. garder un merge `k-way` entre memtable, immutables et SSTables,
3. dedupliquer par cle visible sans recreer des structures lourdes a chaque ligne,
4. avancer bloc par bloc jusqu'a `limit`, sans relancer une recherche complete pour chaque element.

Effet attendu :

- le cout d'un scan long devient surtout un cout sequentiel,
- la latence progresse de facon presque lineaire avec le nombre de lignes.

### Priorite 2 - Caches de blocs et de metadonnees

Aujourd'hui, les chiffres montrent que le chemin scan paie encore trop de travail par table et par ligne.

Il faut introduire :

1. un cache des index de blocs,
2. un cache des bloom filters et footers,
3. un cache de blocs de donnees pour les scans chauds,
4. un prefetch sequentiel pour les scans longs.

Effet attendu :

- moins de decode repetitif,
- moins de relectures de metadonnees,
- meilleur comportement sur les pages `1000` et `5000`.

### Priorite 3 - Ne pas charger la valeur complete si le scan ne la demande pas

Le scan ne doit pas toujours reconstruire `row.value`.

Il faut proposer des variantes :

- scan cle seule,
- scan cle + metadonnees,
- scan complet avec valeur.

Pour `scanIndex`, il faut aussi pouvoir choisir :

- retour index seul,
- retour index + `primaryKey`,
- retour index + record complet.

Effet attendu :

- les scans d'index ne paient plus systematiquement un cout de resolution primaire,
- les pages de listing deviennent beaucoup moins cheres.

### Priorite 4 - Index secondaire couvrant pour les cas frequents

Quand un ecran a seulement besoin de quelques champs stables, l'index secondaire doit pouvoir porter une petite projection utile au lieu d'obliger un retour complet sur le record primaire.

Exemples de charges couvertes :

- liste par groupe,
- liste par statut,
- ordre chronologique simple,
- listing d'administration avec `id`, `state`, `updatedAt`.

Effet attendu :

- les scans d'index deviennent presque des scans directs,
- le nombre d'aller-retour vers la table primaire chute fortement.

### Priorite 5 - API de scan plus precise

Il faut exposer des scans plus explicites que "commence au debut et prends `N` lignes".

API cibles recommandees :

- `scanPrimaryRange(startPrimaryKey, limit)`
- `scanPrimaryPrefix(prefix, limit)`
- `scanIndexRange(indexName, startSecondaryKey, startPrimaryKey, limit)`
- `scanIndexPrefix(indexName, prefix, limit)`

Effet attendu :

- meilleur controle du cout,
- pagination stable,
- moins de travail inutile quand l'appelant sait deja ou il veut commencer.

### Priorite 6 - Bench dedie scan

Le bench actuel donne deja la tendance, mais il faut un bench specialise scan pour piloter les optimisations.

Ce bench doit separer :

- cache froid / cache chaud,
- page `100`, `1000`, `5000`, `50000`,
- scan primaire,
- scan index secondaire,
- scan cle seule contre scan record complet,
- presence ou absence de blobs.

Effet attendu :

- on saura enfin quel sous-chemin est le vrai goulet,
- chaque optimisation scan pourra etre validee sans ambiguite.

## Ordre de travail recommande

Pour reduire rapidement l'ecart avec `SQLite`, l'ordre recommande est :

1. vrai `range scan` avec merge `k-way` specialise,
2. cache de blocs / metadonnees,
3. scans sans chargement systematique de la valeur,
4. index secondaires couvrants,
5. bench dedie scan.

## Conclusion

Le point fort actuel de `SwEmbeddedDb` est le chemin `KV`.

Le point faible actuel de `SwEmbeddedDb` est le chemin scan.

Le moteur n'a donc pas besoin d'une "petite retouche" sur les scans. Il faut traiter les scans comme un sous-systeme a part entiere, avec :

- une API de range claire,
- un chemin de lecture sequentiel specialise,
- des caches de blocs,
- des benchmarks dedies.

## Etat apres introduction du cache chaud/froid

Une nouvelle passe a introduit un cache de lecture chaud partage pour les blocs de tables touches par les scans.

Idee retenue :

- les scans promeuvent les blocs touches dans un cache RAM partage,
- les blocs primaires sont gardes sous forme deja decodee,
- les blocs d'index sont eux aussi pre-decodes,
- les snapshots et curseurs reconsomment ensuite ces blocs deja materialises.

Resultat mesure sur `D:` avec `EmbeddedDbBench` :

| Operation | Avant | Apres cache chaud/froid | Gain |
| --- | ---: | ---: | ---: |
| scan-primary-page-100 | `6.91 ms` | `2.28 ms` | `x3.0` |
| scan-primary-page-1000 | `174.63 ms` | `4.48 ms` | `x39.0` |
| scan-primary 5000 | `1643.00 ms` | `22.45 ms` | `x73.2` |
| scan-index | `9.71 ms` | `9.78 ms` | stable |

Le self-test reste valide apres cette passe.

## Comparatif SQL apres cache chaud/froid

Comparatif `SwEmbeddedDb` vs `QSql/SQLite` relance apres cette passe :

| Operation | SwEmbeddedDb | SQLite | Lecture |
| --- | ---: | ---: | --- |
| write p50 | `1128.30 us` | `1372.05 us` | avantage `SwEmbeddedDb` |
| write p95 | `1556.10 us` | `2360.03 us` | avantage `SwEmbeddedDb` |
| get p50 | `13.60 us` | `13.00 us` | equivalent |
| get p95 | `38.32 us` | `35.90 us` | equivalent a leger retard |
| scan-primary-page-1000 | `13.98 ms` | `0.92 ms` | retard `SwEmbeddedDb` |
| scan-primary 5000 | `60.39 ms` | `2.22 ms` | retard `SwEmbeddedDb` |
| scan-index | `17.79 ms` | `0.12 ms` | gros retard `SwEmbeddedDb` |

Lecture honnete apres cette passe :

- le scan primaire a fait un bond net,
- l'ecart avec `SQLite` reste important,
- le scan d'index reste le prochain gros chantier.

## Priorite suivante

Le prochain levier n'est plus le scan primaire general. Le prochain levier est :

1. un vrai chemin `scanIndex` couvrant, qui evite de refaire un `lookupPrimary()` complet pour chaque ligne,
2. une projection de listing directement dans l'index secondaire,
3. un cache chaud pour les resolutions primaires provoquees par `scanIndex`.
