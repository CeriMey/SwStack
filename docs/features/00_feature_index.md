# Index des fonctionnalités

Ce dossier regroupe la documentation “par classe de fonctionnalités” (taxonomie stable, ~10 catégories).

Chaque fiche suit une structure fixe:
1) But (Pourquoi) • 2) Périmètre • 3) API & concepts • 4) Flux • 5) Erreurs • 6) Perf • 7) Fichiers • 8) Exemples • 9) TODO/À CONFIRMER

## Liste des catégories

- `docs/features/10_runtime_event_loop_threading.md`  
  Runtime (fibres, event loop, timers, waitables OS, threads)
- `docs/features/20_object_model_signals_properties.md`  
  Modèle objet (SwObject), signaux/slots, propriétés, affinité thread
- `docs/features/25_utilities_and_serialization.md`  
  Types utilitaires (SwString/ByteArray/containers), JSON, regex, helpers
- `docs/features/30_ipc_shared_memory_pubsub.md`  
  IPC pub/sub en mémoire partagée (Signal<T...>, registries, LoopPoller, wakeups)
- `docs/features/40_ipc_rpc_remote_components.md`  
  RPC + discovery/remotes + container/plugins
- `docs/features/50_config_and_nodes.md`  
  Config multi-couches + “nodes” JSON/args (SwRemoteObject, SwRemoteObjectNode, systemConfig)
- `docs/features/60_networking_http_tls.md`  
  Sockets TCP/UDP, TLS, HTTP-like manager
- `docs/features/70_gui_platform_widgets.md`  
  GUI: SwGuiApplication, intégration Win32/X11, widgets/rendu/layout
- `docs/features/80_media_video_pipeline.md`  
  Media: sources/décodeurs vidéo, RTSP/MJPEG, widget vidéo, ring buffer IPC vidéo
- `docs/features/90_platform_filesystem_settings.md`  
  Accès OS: fichiers/dossiers, paths, settings, plateforme Win/POSIX
