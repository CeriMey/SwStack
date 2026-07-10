# Découverte SEO et moteurs de réponse avec `SwHttpApp`

## Objectif

`SwHttpApp` peut installer une base de découvrabilité complète avant l'enregistrement des routes
d'un site : HTML sémantique côté serveur, URL canoniques, métadonnées sociales, JSON-LD,
`robots.txt`, `sitemap.xml`, `llms.txt` et protection `noindex` des espaces privés.

La couche ne promet ni position dans un moteur ni citation par une IA. Elle rend le site
techniquement accessible et compréhensible. La qualité du contenu, les liens entrants et la
réputation du domaine restent déterminants.

## Démarrage recommandé pour un site dérivé

Le constructeur SEO installe le middleware avant les routes déclarées par la classe dérivée :

```cpp
#include "SwHttpApp.h"

class DocumentationSite : public SwHttpApp {
public:
    DocumentationSite()
        : SwHttpApp(makeSeoConfig_()) {
        SwHttpSeoPage home;
        home.path = "/";
        home.title = "SwStack";
        home.description = "Framework C++ pour applications, réseau et média.";
        home.schemaType = "SoftwareApplication";
        home.applicationCategory = "DeveloperApplication";
        home.operatingSystem = "Windows, Linux";
        home.lastModified = "2026-07-10";
        addSeoPage(home);

        get("/", [this](SwHttpContext& context) {
            const SwString body =
                "<main><h1>SwStack</h1>"
                "<p>Framework C++ documenté avec des exemples reproductibles.</p>"
                "<a href=\"/guides/http\">Guide HTTP</a></main>";
            context.html(renderSeoPage("/", body));
        });

        // /api fait partie des préfixes noindex par défaut.
        get("/api/health", [](SwHttpContext& context) {
            context.text("ok");
        });
    }

private:
    static SwHttpSeoConfig makeSeoConfig_() {
        SwHttpSeoConfig config;
        config.siteUrl = "https://docs.example.com";
        config.siteName = "SwStack";
        config.defaultTitle = "Documentation SwStack";
        config.defaultDescription = "Documentation officielle de SwStack.";
        config.defaultLanguage = "fr";
        config.organizationName = "Ariya Consulting";
        config.logoUrl = "/assets/logo.png";
        config.defaultSocialImageUrl = "/assets/social-card.png";
        return config;
    }
};
```

Pour une instance non dérivée, appeler `mountSeo(config, &error)` avant `get`, `post`, `group`,
`mountAuthApi` et les autres déclarations de routes.

## Registre explicite des pages

Seules les pages ajoutées par `addSeoPage` apparaissent dans le sitemap et dans `llms.txt`. Le
routeur n'est jamais parcouru automatiquement : une route `/:id`, une API ou une console
d'administration n'est pas une page canonique.

Champs utiles de `SwHttpSeoPage` :

- `path`, `title` et `description` décrivent l'URL publique ;
- `lastModified` et `datePublished` utilisent ISO 8601 et doivent refléter une vraie modification ;
- `schemaType` accepte notamment `WebPage`, `TechArticle` ou `SoftwareApplication` ;
- `authorName`, `applicationCategory` et `operatingSystem` enrichissent le JSON-LD ;
- `indexable = false` retire la page du sitemap et produit `noindex, nofollow` dans son HTML.

Un ajout avec le même `path` remplace les métadonnées existantes. Les wildcards, fragments,
query strings et dates non ISO sont rejetés.

## Endpoints générés

Avec les valeurs par défaut :

- `GET`/`HEAD /robots.txt` autorise le crawl général et déclare le sitemap ;
- `GET`/`HEAD /sitemap.xml` expose au plus 50 000 URL publiques et canoniques ;
- `GET`/`HEAD /llms.txt` fournit un index Markdown complémentaire destiné aux outils qui le lisent.

Les documents de découverte ont un cache public d'une heure, `nosniff` et `X-Robots-Tag:
noindex`. `llms.txt` reste optionnel et ne remplace jamais les pages HTML ni le sitemap.

Les préfixes `/api`, `/admin` et `/auth` reçoivent par défaut `X-Robots-Tag: noindex,
nofollow`. Ils ne sont volontairement pas ajoutés à `Disallow` : un robot doit pouvoir lire le
header `noindex` si une URL a été découverte. Une route sensible doit toujours être protégée par
une vraie authentification, indépendamment du SEO.

## Recherche et entraînement des IA

La recherche et l'entraînement sont deux politiques séparées. Tous les robots sont autorisés par
défaut. Pour conserver la visibilité dans les moteurs de réponse tout en refusant l'entraînement :

```cpp
SwHttpSeoConfig config;
// ... identité du site ...
config.crawlers.allowOpenAiSearch = true;       // OAI-SearchBot
config.crawlers.allowAnthropicSearch = true;    // Claude-SearchBot
config.crawlers.allowPerplexitySearch = true;   // PerplexityBot
config.crawlers.allowOpenAiTraining = false;    // GPTBot
config.crawlers.allowAnthropicTraining = false; // ClaudeBot
config.crawlers.allowGoogleExtended = false;    // entraînement/grounding Gemini
```

Des groupes spécifiques ne sont écrits dans `robots.txt` que lorsqu'un robot est bloqué. Les
robots autorisés héritent ainsi des règles générales sans perdre les éventuels `Disallow` communs.

## Rendu HTML

`renderSeoPage` fabrique un document complet contenant :

- `charset`, viewport, titre et description ;
- directive robots sans limitation d'extrait ;
- URL canonical ;
- Open Graph et Twitter Cards ;
- un graphe JSON-LD `WebSite`, `Organization` et type propre à la page ;
- le HTML du body et le complément de `<head>` fournis par l'application.

Le moteur échappe les métadonnées HTML/XML et neutralise les fermetures de `<script>` dans le
JSON-LD. Le body et `extraHeadHtml` restent du HTML de confiance appartenant à l'application : ne
pas y injecter directement une entrée utilisateur.

## Déploiement

1. Utiliser une `siteUrl` de production stable, publique et de préférence HTTPS.
2. Servir `robots.txt` à la racine de l'hôte public ; ajuster le reverse proxy si l'application est
   montée sous un sous-chemin.
3. Générer des liens internes avec de vrais éléments `<a href>` vers les URL canoniques.
4. Soumettre `/sitemap.xml` dans Google Search Console et Bing Webmaster Tools.
5. Contrôler les réponses, logs de crawl, redirections et erreurs 404 après chaque déploiement.
6. Si la documentation Doxygen est publiée séparément, renseigner aussi `SITEMAP_URL` dans
   `docs/doxygen/Doxyfile` avec sa propre URL canonique.

## Fichiers et validation

- moteur : `src/core/io/http/SwHttpSeo.h` ;
- intégration : `src/core/io/SwHttpApp.h` ;
- test exécutable : `exemples/93-HttpSeoSelfTest/HttpSeoSelfTest.cpp`.
