# SwProjetNode (pipeline + video stream)

Projet d'exemple pour valider `SwBuild` + `SwLaunch` avec un pipeline nodes:

- `pn/capture` (`PnCaptureManagerNode`): publie une frame pipeline compacte (`device_index = -1` => fallback synthetique, pour eviter le conflit webcam avec le serveur).
- `pn/algoManager` (`PnAlgoManagerNode`): applique un algo (`threshold` ou `invert`).
- `pn/renderManager` (`PnRenderManagerNode`): compose capture + algo.
- `pn/serverManager` (`PnServerManagerNode`): source video configurable via `video_url` (callback dynamique au changement) et flux HTTP direct sans resize.

Flux:

1. `CaptureManager` publie `frame(seq,width,height,rgb)` (par defaut `32x32` pour rester sous la limite de payload SHM IPC).
2. `AlgoManager` consomme `pn/capture#frame` et republie `frame(...)`.
3. `RenderManager` consomme `pn/capture#frame` et `pn/algoManager#frame`, puis republie `frame(...)`.
4. `ServerManager` sert:
   - `GET /health`
   - `GET /stats`
   - `GET /frame.jpg` (snapshot JPEG)
   - `GET /stream.mjpg` (multipart live)
   - `GET /frame.bmp` (alias du stream live)

Config `serverManager`:

- `video_url`: source video dynamique.
  - `webcam://0` ou `camera://0` pour webcam locale.
  - `http://host:port/path` pour une source MJPEG distante.
- `device_index`: fallback webcam si `video_url` est vide ou incomplet.
- `stream_fps`: cadence d'envoi du multipart.
- `jpeg_quality`: qualite JPEG du flux sortant.

## Build (avec `SwBuild` du repo parent)

Depuis la racine du repo (`SwStack`):

```bat
build-win\tools\SwNode\SwBuild\Release\SwBuild.exe --root SwProjetNode --scan src --clean --verbose
```

## Run (avec `SwLaunch` + JSON)

Fichier de launch:

- `SwProjetNode/SwProjetNode.launch.json`

Depuis la racine du repo (`SwStack`):

```bat
build-win\tools\SwNode\SwLaunch\Release\SwLaunch.exe --config_file SwProjetNode\SwProjetNode.launch.json
```

HTTP:

- `http://127.0.0.1:8090/health`
- `http://127.0.0.1:8090/stats`
- `http://127.0.0.1:8090/frame.bmp` (flux live sans resize)
- `http://127.0.0.1:8090/stream.mjpg`
