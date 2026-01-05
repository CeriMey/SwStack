# Parité Qt → SwCore GUI (inventaire)

Objectif : viser une API “Qt Widgets”-like (noms de classes + signatures proches) avec des widgets `Sw...` dans `src/core/gui`, et valider systématiquement le rendu via snapshots (galeries PNG).

Ce doc est un état des lieux : ce qui est déjà fait, ce qui existe mais reste “MVP/fragile”, et ce qui manque pour se rapprocher de Qt.

## Légende

- **Mature** : utilisé dans des exemples/apps + rendu validé visuellement (snapshots).
- **Fait mais faible** : MVP / couverture partielle (souvent “snapshot-first”), manque de features Qt ou de validations.
- **Manquant** : pas encore implémenté.

## Avancement (lots + validations)

- **P0 (inputs/containers/scrolling)** : `SwCheckBox`, `SwRadioButton`, `SwProgressBar`, `SwComboBox`, `SwFrame`, `SwGroupBox`, `SwScrollBar`, `SwScrollArea`.
  - Validation : `exemples/31-WidgetGallerySnapshot` → `widget_gallery_inputs.png`, `widget_gallery_combo_open.png`, `widget_gallery_containers.png`, `widget_gallery_scrolling.png`, `widget_gallery_joystick.png`.
- **P1 (editors/numeric/tools)** : `SwPlainTextEdit`, `SwTextEdit`, `SwSpinBox`, `SwDoubleSpinBox`, `SwToolButton`, `SwSplitter`, `SwStackedWidget`.
  - Validation : `exemples/31bis-WidgetGallerySnapshot` → `widget_gallery_31bis_*.png` (dont `widget_gallery_31bis_combobox.png`, `widget_gallery_31bis_combobox_open.png`).
- **P2 (MVC)** : `SwModelIndex`, `SwAbstractItemModel`, `SwItemSelectionModel`, `SwStandardItemModel`, `SwTableView`, `SwTreeView`, `SwListView`, `SwStyledItemDelegate` + widgets `SwTableWidget`, `SwTreeWidget`, `SwListWidget`.
  - Validation : `exemples/32-MvcTreeTableSnapshot` → `mvc_table.png`, `mvc_tree.png`, `mvc_list.png`, `mvc_widgets.png`.
- **P3 (windowing & dialogs)** : `SwAction`, `SwMenu`, `SwMenuBar`, `SwToolBar`, `SwStatusBar`, `SwDialog`, `SwMessageBox`, `SwFileDialog`, `SwColorDialog`, `SwFontDialog`.
  - Validation : `exemples/31bis-WidgetGallerySnapshot` → `widget_gallery_31bis_windowing.png`, `widget_gallery_31bis_menu_open.png`, `widget_gallery_31bis_dialogs.png`.
- **P4 (GraphicsView)** : `SwGraphicsScene`, `SwGraphicsView`, `SwGraphicsItem`, `SwGraphics*Item`, `SwGraphicsProxyWidget` + types `SwImage`/`SwPixmap`/`SwIcon`, `SwPen`/`SwBrush`/`SwGradient`/`SwPainterPath`/`SwTransform`/`SwFontMetrics`.
  - Validation : `exemples/31bis-WidgetGallerySnapshot` → `widget_gallery_31bis_graphics.png`.
  - Validation “app” : `exemples/33-Nodeeditor/NodeEditorExample.cpp`.
  - NodeEditor : Ctrl multi-select + Shift rubber-band selection + drag moves selection.
- **P5 (Charts / curves)** : `SwChartView`, `SwChart`, `SwValueAxis`, `SwAbstractSeries` + séries `SwLineSeries`, `SwStepLineSeries`, `SwSplineSeries`, `SwScatterSeries`, `SwAreaSeries`, `SwBarSeries`, `SwCandlestickSeries`, `SwPieSeries`.
  - Validation : `exemples/31bis-WidgetGallerySnapshot` -> `widget_gallery_31bis_charts*.png`.

## Ce qui est déjà fait (inventaire)

### Mature

- Base : `SwGuiApplication`, `SwMainWindow`, `SwWidget`, `SwWidgetInterface`, `SwWidgetPlatformAdapter`.
- Rendu/style : `SwPainter`, `SwStyle`, `StyleSheet`, `SwFont` (+ `SwFontMetrics` côté graphics).
- Layouts : `SwLayout` (+ vertical/horizontal/grid).
- Widgets : `SwLabel`, `SwPushButton`, `SwLineEdit`, `SwSlider`, `SwTabWidget`, `SwJoystick`.
- Inputs/containers : `SwCheckBox`, `SwRadioButton`, `SwProgressBar`, `SwFrame`, `SwGroupBox`, `SwScrollBar`, `SwScrollArea`, `SwToolButton`, `SwSplitter`, `SwStackedWidget`.
- Menu/windowing : `SwAction`, `SwMenu`, `SwMenuBar`, `SwToolBar`, `SwStatusBar`.
- MVC “utilisable” : `SwModelIndex`, `SwAbstractItemModel`, `SwItemSelectionModel`, `SwTableView`, `SwListView`, `SwStyledItemDelegate` + widgets `SwTableWidget`, `SwTreeWidget`, `SwListWidget`.
- GraphicsView : `SwGraphicsScene`, `SwGraphicsView`, `SwGraphicsItem`, `SwGraphics*Item`, `SwGraphicsProxyWidget` + `SwPainterPath`/`SwTransform`/`SwPen`/`SwBrush`/`SwGradient`/`SwImage`/`SwPixmap`/`SwIcon`.
- Outils : `SwWidgetSnapshot`.
- Extras : `SwDragDrop` (feedback visuel in-app uniquement, pas de drag&drop OS) â†’ validation : `exemples/31bis-WidgetGallerySnapshot` â†’ `widget_gallery_31bis_dragdrop_ok.png`, `widget_gallery_31bis_dragdrop_no.png`.
- Widgets “produit” : `SwEmojiPiker` (alias `SwEmojiPicker`), `chatbubble/*` (utilisé par `exemples/32-WhatsApp`).
- Media : `SwVideoWidget`, `SwMediaControlWidget` (voir `exemples/16-VideoWidget`, `exemples/20-RtspVideoWidget`).

### Fait mais faible (MVP / parité partielle)

- `SwComboBox` : OK visuellement (popup) + navigation clavier/roue + scrolling du popup ; parité Qt encore partielle (editable, model-based, completer, etc.).
- Tooltips : `SwToolTip` (delay + popup simple), à renforcer (positionnement multi-écrans, contenu riche, API/style).
- Éditeurs multi-lignes : `SwPlainTextEdit` (édition clavier), `SwTextEdit` (rich text minimal + HTML subset) → encore loin de Qt (sélection/clipboard/undo/redo, etc.).
- MVC : `SwStandardItemModel` (minimal), `SwTreeView` (minimal : indentation/expand + sélection).
- Dialogs : `SwDialog`, `SwMessageBox`, `SwFileDialog`, `SwColorDialog`, `SwFontDialog` (approche “snapshot-first”, UX/fonctions Qt non couvertes à 100%).
- Extras : `SwToolBox` (toolbox/accordion utilisable) + `SwUiLoader` (subset XML .ui-like : containers + props de base) -> validation : `exemples/31bis-WidgetGallerySnapshot` -> `widget_gallery_31bis_extras.png`.
- Charts : `SwChartView` / `SwChart` + séries `SwLineSeries`/`SwStepLineSeries`/`SwSplineSeries`/`SwScatterSeries`/`SwAreaSeries`/`SwBarSeries`/`SwCandlestickSeries`/`SwPieSeries` (axes auto ou fixes, zoom molette, pan middle, play-mode auto-scroll X) -> parité QtCharts encore partielle (multi-axes, rubber-band zoom, tooltips, barsets/catégories, animations, etc.).

## Points faibles transverses (à renforcer)

- Navigation clavier / focus : tab-order, focus rings cohérents, raccourcis (beaucoup de widgets restent “souris-first”).
- Text editing : sélection (mouse+keyboard), copy/cut/paste, undo/redo (idéalement via `SwUndoStack`), scroll/viewport plus riche.
- MVC : multi-selection (range), édition via delegates, header (≈ `QHeaderView`), proxy models (tri/filtre), drag&drop “réel”.
- StyleSheet : couverture encore partielle/inégale selon widgets (QSS-like vs valeurs hardcodées).
- Platform : HiDPI scaling, accessibilité, IME, parité Win32/X11 à confirmer.

## Manquants (backlog Qt “classique”)

### Priorité haute

- `SwHeaderView` (≈ `QHeaderView`) + colonnes/rows resizable (table/tree).
- `SwSortFilterProxyModel` (≈ `QSortFilterProxyModel`).
- Sélection avancée + navigation clavier dans `SwListView` / `SwTableView` / `SwTreeView` (range, multi, shortcuts).
- API shortcuts (≈ `QShortcut` / key sequences).

### Priorité moyenne

- Docking (≈ `QDockWidget`) / split docking.
- Widgets date/time (≈ `QDateEdit`, `QTimeEdit`, `QDateTimeEdit`, `QCalendarWidget`).
- Inputs avancés : completer (≈ `QCompleter`), etc.
- Clipboard & drag&drop OS (≈ `QClipboard`, `QDrag`/`QDropEvent`) au-delà du simple feedback visuel.

### Priorité basse / hors scope possible

- Printing (≈ `QPrinter`), OpenGL (≈ `QOpenGLWidget`), WebEngine/QML, accessibilité complète.

## Mode opératoire (dev → intégration → snapshots)

But : chaque nouveau widget `Sw...` se développe dans `src/core/gui` (API Qt-like + types `Sw*`) puis se valide visuellement via des snapshots.

1. Développer le widget dans `src/core/gui`
   - API : rester proche de Qt (noms/symboles/signatures) pour faciliter la parité.
   - Types : privilégier `SwString`, `SwVector`, etc. et limiter `std::` au strict nécessaire.
   - Rendu : viser une qualité graphique "premium" dès la V1 (spacing, alignements, arrondis, hover/pressed/focus, disabled).
   - StyleSheet : exposer un maximum de contrôle via `setStyleSheet(...)`.

2. Intégrer le widget dans un exemple de snapshot
   - `exemples/31-WidgetGallerySnapshot` : galerie “de base”.
   - `exemples/31bis-WidgetGallerySnapshot` : galerie étendue.
   - `exemples/32-MvcTreeTableSnapshot` : MVC.
   - Ajouter des cas représentatifs (enabled/disabled/checked, etc) + des styles de démo.

3. Générer les snapshots PNG
   - Lancer la cible `WidgetGallerySnapshot` ou `WidgetGallerySnapshotBis`.
   - 1er argument optionnel : dossier de sortie (sinon : `d:/coreSwExample/build-codex-sanity/`).

4. Valider puis itérer
   - Ouvrir les PNG, valider cohérence/typographie/couleurs/alignements/states.
   - Corriger et relancer jusqu’à validation.

## Notes

La “parité Qt” complète est un gros périmètre : l’approche actuelle est d’avancer par lots, avec validation snapshot systématique pour éviter de “casser” le rendu.
