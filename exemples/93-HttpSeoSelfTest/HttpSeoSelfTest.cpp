#include "SwHttpApp.h"

#include <iostream>

static int g_failures = 0;

static void expect(bool condition, const char* label) {
    if (condition) {
        std::cout << "[PASS] " << label << "\n";
    } else {
        std::cout << "[FAIL] " << label << "\n";
        ++g_failures;
    }
}

static SwString responseBody(const SwHttpResponse& response) {
    return SwString(response.body.toStdString());
}

static bool route(SwHttpApp& app, const SwString& method, const SwString& path,
                  SwHttpResponse& response) {
    SwHttpRequest request;
    request.method = method;
    request.path = path;
    request.target = path;
    request.protocol = "HTTP/1.1";
    request.headers["host"] = "docs.swstack.test";
    return app.server().router().route(request, response);
}

class SeoSite : public SwHttpApp {
public:
    SeoSite()
        : SwHttpApp(makeConfig_()) {
        SwHttpSeoPage home;
        home.path = "/";
        home.title = "SwStack";
        home.description = "Framework C++ pour HTTP, interfaces et media.";
        home.schemaType = "SoftwareApplication";
        home.applicationCategory = "DeveloperApplication";
        home.operatingSystem = "Windows, Linux";
        home.lastModified = "2026-07-10";
        addSeoPage(home);

        SwHttpSeoPage guide;
        guide.path = "/guides/http-seo&ia";
        guide.title = "Guide <SEO> & IA";
        guide.description = "Construire des pages utiles pour les moteurs et les humains.";
        guide.schemaType = "TechArticle";
        guide.authorName = "Eymeric O'Neill";
        guide.datePublished = "2026-07-01";
        guide.lastModified = "2026-07-10T09:30:00+02:00";
        addSeoPage(guide);

        SwHttpSeoPage privatePage;
        privatePage.path = "/account";
        privatePage.title = "Compte";
        privatePage.indexable = false;
        addSeoPage(privatePage);

        get("/", [this](SwHttpContext& context) {
            context.html(renderSeoPage(
                "/",
                "<main><h1>SwStack</h1><p>Framework C++ documente.</p>"
                "<a href=\"/guides/http-seo&amp;ia\">Guide SEO</a></main>"));
        });
        get("/api/health", [](SwHttpContext& context) {
            context.text("ok");
        });
    }

private:
    static SwHttpSeoConfig makeConfig_() {
        SwHttpSeoConfig config;
        config.siteUrl = "https://docs.swstack.test/";
        config.siteName = "SwStack";
        config.defaultTitle = "Documentation SwStack";
        config.defaultDescription = "Documentation officielle de SwStack.";
        config.organizationName = "Ariya Consulting";
        config.logoUrl = "/assets/logo.png";
        config.defaultSocialImageUrl = "/assets/social-card.png";
        config.crawlers.allowOpenAiTraining = false;
        config.crawlers.allowAnthropicTraining = false;
        config.crawlers.allowGoogleExtended = false;
        return config;
    }
};

static void testMountAndDiscoveryRoutes() {
    SeoSite app;
    expect(app.seoMounted(), "SEO-aware constructor mounts discovery");
    expect(app.hasRouteName("seo.robots"), "robots route is named");
    expect(app.hasRouteName("seo.sitemap"), "sitemap route is named");
    expect(app.hasRouteName("seo.llms"), "llms route is named");

    SwHttpResponse robots;
    expect(route(app, "GET", "/robots.txt", robots), "robots route resolves");
    const SwString robotsBody = responseBody(robots);
    expect(robots.status == 200, "robots returns 200");
    expect(robots.headers.value("content-type").startsWith("text/plain"),
           "robots returns text/plain");
    expect(robotsBody.contains("Sitemap: https://docs.swstack.test/sitemap.xml"),
           "robots declares canonical sitemap");
    expect(robotsBody.contains("User-agent: GPTBot\nDisallow: /"),
           "robots can disable OpenAI training independently");
    expect(robotsBody.contains("User-agent: ClaudeBot\nDisallow: /"),
           "robots can disable Anthropic training independently");
    expect(!robotsBody.contains("User-agent: OAI-SearchBot\nDisallow: /"),
           "OpenAI search remains discoverable");
    expect(robots.headers.value("x-robots-tag") == "noindex",
           "discovery documents do not become search results");

    SwHttpResponse sitemap;
    expect(route(app, "HEAD", "/sitemap.xml", sitemap), "GET route supports HEAD discovery");
    expect(sitemap.status == 200, "sitemap HEAD returns 200");
    expect(sitemap.headers.value("content-type").startsWith("application/xml"),
           "sitemap returns XML content type");

    expect(route(app, "GET", "/sitemap.xml", sitemap), "sitemap route resolves");
    const SwString sitemapBody = responseBody(sitemap);
    expect(sitemapBody.contains("https://docs.swstack.test/</loc>"),
           "sitemap contains homepage canonical URL");
    expect(sitemapBody.contains("/guides/http-seo&amp;ia</loc>"),
           "sitemap XML-escapes canonical paths");
    expect(!sitemapBody.contains("/account"), "sitemap excludes noindex pages");
    expect(sitemapBody.contains("2026-07-10T09:30:00+02:00"),
           "sitemap exposes truthful ISO lastmod");

    SwHttpResponse llms;
    expect(route(app, "GET", "/llms.txt", llms), "llms route resolves");
    const SwString llmsBody = responseBody(llms);
    expect(llms.headers.value("content-type").startsWith("text/markdown"),
           "llms returns Markdown content type");
    expect(llmsBody.contains("[Guide <SEO> & IA](https://docs.swstack.test/guides/http-seo&ia)"),
           "llms lists registered public pages");
    expect(!llmsBody.contains("Compte"), "llms excludes noindex pages");
}

static void testHtmlAndMiddleware() {
    SeoSite app;
    bool ok = false;
    const SwString html = app.renderSeoPage(
        "/guides/http-seo&ia",
        "<main><h1>SEO &amp; IA</h1></main>",
        "<link rel=\"stylesheet\" href=\"/assets/site.css\">",
        &ok);
    expect(ok, "registered page renders");
    expect(html.contains("<title>Guide &lt;SEO&gt; &amp; IA | SwStack</title>"),
           "HTML title is escaped and branded");
    expect(html.contains("rel=\"canonical\" href=\"https://docs.swstack.test/guides/http-seo&amp;ia\""),
           "HTML contains escaped canonical URL");
    expect(html.contains("max-snippet:-1"), "HTML permits full search snippets");
    expect(html.contains("application/ld+json"), "HTML contains JSON-LD");
    expect(html.contains("\"@type\": \"TechArticle\""), "JSON-LD preserves page schema type");
    expect(html.contains("Guide \\u003CSEO\\u003E \\u0026 IA"),
           "embedded JSON-LD neutralizes HTML script delimiters");
    expect(html.contains("og:image\" content=\"https://docs.swstack.test/assets/social-card.png\""),
           "HTML resolves social image URL");
    expect(html.contains("/assets/site.css"), "HTML keeps trusted extra head markup");

    SwHttpResponse privateApi;
    expect(route(app, "GET", "/api/health", privateApi), "private API route resolves");
    expect(privateApi.headers.value("x-robots-tag") == "noindex, nofollow",
           "private prefix receives X-Robots-Tag");

    SwHttpResponse homepage;
    expect(route(app, "GET", "/", homepage), "public page route resolves");
    expect(!homepage.headers.contains("x-robots-tag"), "public page remains indexable");
    expect(responseBody(homepage).contains("<h1>SwStack</h1>"),
           "public page contains semantic server-rendered content");
}

static void testValidation() {
    SwHttpApp app;
    SwHttpSeoConfig invalid;
    invalid.siteName = "Invalid";
    SwString error;
    expect(!app.mountSeo(invalid, &error), "relative or missing site URL is rejected");
    expect(!error.isEmpty(), "invalid config returns a diagnostic");

    invalid.siteUrl = "https:///missing-host";
    expect(!app.mountSeo(invalid, &error), "canonical URL without a host is rejected");

    invalid.siteUrl = "https://example.test";
    invalid.robotsDisallowPaths.append("/private\nUser-agent: injected");
    expect(!app.mountSeo(invalid, &error), "robots path injection is rejected");

    SwHttpSeoConfig valid;
    valid.siteUrl = "https://example.test";
    valid.siteName = "Example";
    expect(app.mountSeo(valid, &error), "valid SEO config mounts");
    expect(!app.mountSeo(valid, &error), "SEO cannot be mounted twice");

    SwHttpSeoPage dynamicPage;
    dynamicPage.path = "/items/*";
    dynamicPage.title = "Dynamic";
    expect(!app.addSeoPage(dynamicPage, &error), "wildcard routes are rejected from sitemap");

    SwHttpSeoPage invalidDate;
    invalidDate.path = "/invalid-date";
    invalidDate.title = "Invalid date";
    invalidDate.lastModified = "10/07/2026";
    expect(!app.addSeoPage(invalidDate, &error), "non-ISO lastmod is rejected");

    SwHttpSeoPage first;
    first.path = "/same";
    first.title = "First";
    expect(app.addSeoPage(first), "first canonical page registers");
    first.title = "Updated";
    expect(app.addSeoPage(first), "duplicate canonical path updates in place");
    SwHttpSeoPage stored;
    expect(app.seo().page("/same", stored) && stored.title == "Updated",
           "updated page metadata is observable");

    expect(app.seo().shouldNoIndex("/api"), "exact private prefix is noindex");
    expect(app.seo().shouldNoIndex("/api/v1/items"), "private prefix descendants are noindex");
    expect(!app.seo().shouldNoIndex("/apiculture"), "prefix matching respects path boundaries");
}

int main() {
    testMountAndDiscoveryRoutes();
    testHtmlAndMiddleware();
    testValidation();

    if (g_failures == 0) {
        std::cout << "[HttpSeoSelfTest] PASS\n";
        return 0;
    }
    std::cout << "[HttpSeoSelfTest] FAIL count=" << g_failures << "\n";
    return 1;
}
