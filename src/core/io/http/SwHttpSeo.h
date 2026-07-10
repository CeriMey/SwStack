#pragma once

/**
 * @file src/core/io/http/SwHttpSeo.h
 * @ingroup core_http
 * @brief SEO and answer-engine discovery primitives for applications served by SwHttpApp.
 *
 * SwHttpSeo keeps an explicit registry of public pages and generates the machine-readable
 * documents that search engines and answer engines use to discover a site.  The registry is
 * deliberately separate from the HTTP router: API, authentication, administration and dynamic
 * route patterns must never become indexable merely because they exist.
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 ***************************************************************************************************/

#include "../../fs/SwMutex.h"
#include "../../types/SwJsonArray.h"
#include "../../types/SwJsonDocument.h"
#include "../../types/SwJsonObject.h"
#include "../../types/SwList.h"
#include "../../types/SwString.h"

#include <cctype>
#include <cstddef>

struct SwHttpSeoCrawlerPolicy {
    // Search and answer-engine discovery are enabled by default.
    bool allowGoogleSearch = true;
    bool allowBingSearch = true;
    bool allowOpenAiSearch = true;
    bool allowAnthropicSearch = true;
    bool allowPerplexitySearch = true;

    // User-triggered fetchers are independent from search indexes.
    bool allowChatGptUser = true;
    bool allowClaudeUser = true;
    bool allowPerplexityUser = true;

    // Training/grounding controls remain independently configurable.
    bool allowOpenAiTraining = true;
    bool allowAnthropicTraining = true;
    bool allowGoogleExtended = true;
};

struct SwHttpSeoConfig {
    SwString siteUrl;
    SwString siteName;
    SwString defaultTitle;
    SwString defaultDescription;
    SwString defaultLanguage = "fr";
    SwString titleSeparator = " | ";

    SwString organizationName;
    SwString organizationUrl;
    SwString logoUrl;
    SwString defaultSocialImageUrl;

    SwString robotsPath = "/robots.txt";
    SwString sitemapPath = "/sitemap.xml";
    SwString llmsPath = "/llms.txt";

    bool enableRobotsTxt = true;
    bool enableSitemap = true;
    bool enableLlmsTxt = true;
    bool appendSiteNameToTitle = true;

    SwString discoveryCacheControl = "public, max-age=3600";
    std::size_t maxSitemapUrls = 50000;

    SwHttpSeoCrawlerPolicy crawlers;
    SwList<SwString> robotsAllowPaths;
    SwList<SwString> robotsDisallowPaths;
    SwList<SwString> noIndexPrefixes;

    SwHttpSeoConfig() {
        // These paths are not added to robotsDisallowPaths: crawlers must be able to read the
        // X-Robots-Tag noindex response when a public endpoint is accidentally linked.
        noIndexPrefixes.append("/api");
        noIndexPrefixes.append("/admin");
        noIndexPrefixes.append("/auth");
    }
};

struct SwHttpSeoPage {
    SwString path;
    SwString title;
    SwString description;
    SwString language;
    SwString lastModified;
    SwString datePublished;
    SwString authorName;
    SwString schemaType = "WebPage";
    SwString openGraphType = "website";
    SwString socialImageUrl;
    SwString applicationCategory;
    SwString operatingSystem;
    bool indexable = true;
};

class SwHttpSeo {
public:
    SwHttpSeo() = default;

    bool configure(const SwHttpSeoConfig& config, SwString* errorOut = nullptr) {
        SwHttpSeoConfig normalized;
        SwString error;
        if (!normalizeConfig_(config, normalized, error)) {
            setError_(errorOut, error);
            return false;
        }

        SwMutexLocker locker(&m_mutex);
        std::size_t indexableCount = 0;
        for (std::size_t i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].indexable) {
                ++indexableCount;
            }
        }
        if (indexableCount > normalized.maxSitemapUrls) {
            setError_(errorOut, "configured sitemap limit is lower than the registered page count");
            return false;
        }
        m_config = normalized;
        m_configured = true;
        setError_(errorOut, SwString());
        return true;
    }

    bool isConfigured() const {
        SwMutexLocker locker(&m_mutex);
        return m_configured;
    }

    SwHttpSeoConfig config() const {
        SwMutexLocker locker(&m_mutex);
        return m_config;
    }

    bool addPage(const SwHttpSeoPage& page, SwString* errorOut = nullptr) {
        SwHttpSeoPage normalized = page;
        SwString error;
        if (!normalizePage_(normalized, error)) {
            setError_(errorOut, error);
            return false;
        }

        SwMutexLocker locker(&m_mutex);
        if (!m_configured) {
            setError_(errorOut, "SEO must be configured before pages are registered");
            return false;
        }

        int existingIndex = -1;
        std::size_t indexableCount = 0;
        for (std::size_t i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].path == normalized.path) {
                existingIndex = static_cast<int>(i);
            } else if (m_pages[i].indexable) {
                ++indexableCount;
            }
        }
        if (normalized.indexable && indexableCount >= m_config.maxSitemapUrls) {
            setError_(errorOut, "sitemap URL limit reached; split the site into sitemap indexes");
            return false;
        }

        if (existingIndex >= 0) {
            m_pages[static_cast<std::size_t>(existingIndex)] = normalized;
        } else {
            m_pages.append(normalized);
        }
        setError_(errorOut, SwString());
        return true;
    }

    bool removePage(const SwString& path) {
        const SwString normalizedPath = normalizePagePath_(path);
        SwMutexLocker locker(&m_mutex);
        for (std::size_t i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].path == normalizedPath) {
                m_pages.removeAt(static_cast<int>(i));
                return true;
            }
        }
        return false;
    }

    void clearPages() {
        SwMutexLocker locker(&m_mutex);
        m_pages.clear();
    }

    SwList<SwHttpSeoPage> pages() const {
        SwMutexLocker locker(&m_mutex);
        return m_pages;
    }

    bool page(const SwString& path, SwHttpSeoPage& pageOut) const {
        const SwString normalizedPath = normalizePagePath_(path);
        SwMutexLocker locker(&m_mutex);
        for (std::size_t i = 0; i < m_pages.size(); ++i) {
            if (m_pages[i].path == normalizedPath) {
                pageOut = m_pages[i];
                return true;
            }
        }
        return false;
    }

    SwString canonicalUrl(const SwString& pagePath) const {
        SwHttpSeoConfig snapshot;
        {
            SwMutexLocker locker(&m_mutex);
            snapshot = m_config;
        }
        return joinUrl_(snapshot.siteUrl, normalizePagePath_(pagePath));
    }

    bool shouldNoIndex(const SwString& requestPath) const {
        SwList<SwString> prefixes;
        {
            SwMutexLocker locker(&m_mutex);
            prefixes = m_config.noIndexPrefixes;
        }

        const SwString normalizedPath = normalizePagePath_(requestPath);
        for (std::size_t i = 0; i < prefixes.size(); ++i) {
            if (pathPrefixMatches_(normalizedPath, prefixes[i])) {
                return true;
            }
        }
        return false;
    }

    SwString robotsText() const {
        SwHttpSeoConfig snapshot;
        {
            SwMutexLocker locker(&m_mutex);
            snapshot = m_config;
        }

        SwString output;
        output += "# Generated by SwHttpApp / SwHttpSeo\n";
        appendCrawlerGroup_(output, "*", true, snapshot.robotsAllowPaths, snapshot.robotsDisallowPaths);

        appendBlockedCrawler_(output, "Googlebot", snapshot.crawlers.allowGoogleSearch);
        appendBlockedCrawler_(output, "bingbot", snapshot.crawlers.allowBingSearch);
        appendBlockedCrawler_(output, "OAI-SearchBot", snapshot.crawlers.allowOpenAiSearch);
        appendBlockedCrawler_(output, "ChatGPT-User", snapshot.crawlers.allowChatGptUser);
        appendBlockedCrawler_(output, "GPTBot", snapshot.crawlers.allowOpenAiTraining);
        appendBlockedCrawler_(output, "Claude-SearchBot", snapshot.crawlers.allowAnthropicSearch);
        appendBlockedCrawler_(output, "Claude-User", snapshot.crawlers.allowClaudeUser);
        appendBlockedCrawler_(output, "ClaudeBot", snapshot.crawlers.allowAnthropicTraining);
        appendBlockedCrawler_(output, "PerplexityBot", snapshot.crawlers.allowPerplexitySearch);
        appendBlockedCrawler_(output, "Perplexity-User", snapshot.crawlers.allowPerplexityUser);
        appendBlockedCrawler_(output, "Google-Extended", snapshot.crawlers.allowGoogleExtended);

        if (snapshot.enableSitemap) {
            output += "\nSitemap: ";
            output += joinUrl_(snapshot.siteUrl, snapshot.sitemapPath);
            output += "\n";
        }
        return output;
    }

    SwString sitemapXml() const {
        SwHttpSeoConfig configSnapshot;
        SwList<SwHttpSeoPage> pageSnapshot;
        snapshot_(configSnapshot, pageSnapshot);

        SwString output;
        output += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        output += "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
        for (std::size_t i = 0; i < pageSnapshot.size(); ++i) {
            const SwHttpSeoPage& item = pageSnapshot[i];
            if (!item.indexable) {
                continue;
            }
            output += "  <url>\n";
            output += "    <loc>";
            output += escapeXml_(joinUrl_(configSnapshot.siteUrl, item.path));
            output += "</loc>\n";
            if (!item.lastModified.isEmpty()) {
                output += "    <lastmod>";
                output += escapeXml_(item.lastModified);
                output += "</lastmod>\n";
            }
            output += "  </url>\n";
        }
        output += "</urlset>\n";
        return output;
    }

    SwString llmsText() const {
        SwHttpSeoConfig configSnapshot;
        SwList<SwHttpSeoPage> pageSnapshot;
        snapshot_(configSnapshot, pageSnapshot);

        const SwString description = configSnapshot.defaultDescription.trimmed();
        SwString output;
        output += "# ";
        output += markdownInline_(configSnapshot.siteName);
        output += "\n\n";
        if (!description.isEmpty()) {
            output += "> ";
            output += markdownInline_(description);
            output += "\n\n";
        }
        output += "Canonical site: ";
        output += configSnapshot.siteUrl;
        output += "\n";
        output += "Language: ";
        output += configSnapshot.defaultLanguage;
        output += "\n\n## Public pages\n\n";

        for (std::size_t i = 0; i < pageSnapshot.size(); ++i) {
            const SwHttpSeoPage& item = pageSnapshot[i];
            if (!item.indexable) {
                continue;
            }
            output += "- [";
            output += markdownLinkLabel_(item.title);
            output += "](";
            output += markdownLinkUrl_(joinUrl_(configSnapshot.siteUrl, item.path));
            output += ")";
            if (!item.description.trimmed().isEmpty()) {
                output += ": ";
                output += markdownInline_(item.description);
            }
            output += "\n";
        }
        return output;
    }

    SwString renderDocument(const SwHttpSeoPage& page,
                            const SwString& bodyHtml,
                            const SwString& extraHeadHtml = SwString()) const {
        SwHttpSeoConfig configSnapshot;
        {
            SwMutexLocker locker(&m_mutex);
            configSnapshot = m_config;
        }
        return renderDocument_(configSnapshot, page, bodyHtml, extraHeadHtml);
    }

    SwString renderPage(const SwString& pagePath,
                        const SwString& bodyHtml,
                        const SwString& extraHeadHtml = SwString(),
                        bool* okOut = nullptr) const {
        SwHttpSeoPage registeredPage;
        if (!page(pagePath, registeredPage)) {
            if (okOut) {
                *okOut = false;
            }
            return SwString();
        }
        if (okOut) {
            *okOut = true;
        }
        return renderDocument(registeredPage, bodyHtml, extraHeadHtml);
    }

private:
    static void setError_(SwString* errorOut, const SwString& error) {
        if (errorOut) {
            *errorOut = error;
        }
    }

    static bool normalizeConfig_(const SwHttpSeoConfig& input,
                                 SwHttpSeoConfig& output,
                                 SwString& error) {
        output = input;
        output.siteUrl = output.siteUrl.trimmed();
        output.siteName = output.siteName.trimmed();
        output.defaultLanguage = output.defaultLanguage.trimmed();
        output.organizationName = output.organizationName.trimmed();
        output.organizationUrl = output.organizationUrl.trimmed();

        if (!isAbsoluteHttpUrl_(output.siteUrl)) {
            error = "siteUrl must be an absolute http:// or https:// URL without query or fragment";
            return false;
        }
        while (output.siteUrl.size() > 8 && output.siteUrl.endsWith("/")) {
            output.siteUrl.chop(1);
        }
        if (output.siteName.isEmpty()) {
            error = "siteName is required";
            return false;
        }
        if (output.defaultTitle.trimmed().isEmpty()) {
            output.defaultTitle = output.siteName;
        }
        if (output.defaultLanguage.isEmpty()) {
            output.defaultLanguage = "fr";
        }
        if (output.titleSeparator.isEmpty()) {
            output.titleSeparator = " | ";
        }
        if (output.organizationUrl.isEmpty()) {
            output.organizationUrl = output.siteUrl;
        } else if (!isAbsoluteHttpUrl_(output.organizationUrl)) {
            error = "organizationUrl must be an absolute http:// or https:// URL";
            return false;
        }
        if (output.maxSitemapUrls == 0 || output.maxSitemapUrls > 50000) {
            error = "maxSitemapUrls must be between 1 and 50000";
            return false;
        }
        if (output.discoveryCacheControl.contains("\r") ||
            output.discoveryCacheControl.contains("\n")) {
            error = "discoveryCacheControl must not contain line breaks";
            return false;
        }

        if (!normalizeEndpointPath_(output.robotsPath, error) ||
            !normalizeEndpointPath_(output.sitemapPath, error) ||
            !normalizeEndpointPath_(output.llmsPath, error)) {
            return false;
        }
        if ((output.enableRobotsTxt && output.enableSitemap && output.robotsPath == output.sitemapPath) ||
            (output.enableRobotsTxt && output.enableLlmsTxt && output.robotsPath == output.llmsPath) ||
            (output.enableSitemap && output.enableLlmsTxt && output.sitemapPath == output.llmsPath)) {
            error = "SEO endpoint paths must be distinct";
            return false;
        }

        if (!normalizePathList_(output.robotsAllowPaths, "robotsAllowPaths", error) ||
            !normalizePathList_(output.robotsDisallowPaths, "robotsDisallowPaths", error) ||
            !normalizePathList_(output.noIndexPrefixes, "noIndexPrefixes", error)) {
            return false;
        }
        error.clear();
        return true;
    }

    static bool normalizeEndpointPath_(SwString& path, SwString& error) {
        path = normalizePagePath_(path);
        if (!isConcretePath_(path)) {
            error = "SEO endpoint paths must be concrete paths without query, fragment or wildcard";
            return false;
        }
        return true;
    }

    static bool normalizePathList_(SwList<SwString>& paths,
                                   const SwString& fieldName,
                                   SwString& error) {
        SwList<SwString> normalized;
        for (std::size_t i = 0; i < paths.size(); ++i) {
            SwString path = normalizePagePath_(paths[i]);
            if (path.size() > 1 && path.endsWith("/")) {
                path.chop(1);
            }
            if (!isConcretePath_(path)) {
                error = fieldName + " must contain concrete paths without query, fragment or wildcard";
                return false;
            }
            if (!normalized.contains(path)) {
                normalized.append(path);
            }
        }
        paths = normalized;
        return true;
    }

    static bool normalizePage_(SwHttpSeoPage& page, SwString& error) {
        page.path = normalizePagePath_(page.path);
        page.title = page.title.trimmed();
        page.description = page.description.trimmed();
        page.language = page.language.trimmed();
        page.schemaType = page.schemaType.trimmed();
        page.openGraphType = page.openGraphType.trimmed();

        if (!isConcretePath_(page.path)) {
            error = "SEO page paths must be concrete paths without query, fragment or wildcard";
            return false;
        }
        if (page.indexable && page.title.isEmpty()) {
            error = "indexable SEO pages require a title";
            return false;
        }
        if (!page.lastModified.isEmpty() && !isIsoDate_(page.lastModified)) {
            error = "lastModified must use ISO 8601 (at least YYYY-MM-DD)";
            return false;
        }
        if (!page.datePublished.isEmpty() && !isIsoDate_(page.datePublished)) {
            error = "datePublished must use ISO 8601 (at least YYYY-MM-DD)";
            return false;
        }
        if (page.schemaType.isEmpty()) {
            page.schemaType = "WebPage";
        }
        if (page.openGraphType.isEmpty()) {
            page.openGraphType = "website";
        }
        error.clear();
        return true;
    }

    static SwString normalizePagePath_(const SwString& rawPath) {
        SwString path = rawPath.trimmed();
        if (path.isEmpty()) {
            return "/";
        }
        if (!path.startsWith("/")) {
            path.prepend("/");
        }
        while (path.contains("//")) {
            path.replace("//", "/");
        }
        return path;
    }

    static bool isConcretePath_(const SwString& path) {
        if (path.isEmpty() || !path.startsWith("/") || path.contains("?") ||
            path.contains("#") || path.contains("*") || path.contains("\\")) {
            return false;
        }
        for (std::size_t i = 0; i < path.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(path[i])) != 0) {
                return false;
            }
        }
        return true;
    }

    static bool isAbsoluteHttpUrl_(const SwString& value) {
        const SwString trimmed = value.trimmed();
        if ((!trimmed.startsWith("https://") && !trimmed.startsWith("http://")) ||
            trimmed.contains("?") || trimmed.contains("#")) {
            return false;
        }
        const int schemeEnd = trimmed.indexOf("://");
        if (schemeEnd < 0 || static_cast<std::size_t>(schemeEnd + 3) >= trimmed.size()) {
            return false;
        }
        const char firstAuthorityCharacter = trimmed[static_cast<std::size_t>(schemeEnd + 3)];
        if (firstAuthorityCharacter == '/' || firstAuthorityCharacter == ':' ||
            firstAuthorityCharacter == '.') {
            return false;
        }
        for (std::size_t i = 0; i < trimmed.size(); ++i) {
            if (std::isspace(static_cast<unsigned char>(trimmed[i])) != 0) {
                return false;
            }
        }
        return true;
    }

    static bool isIsoDate_(const SwString& value) {
        const SwString date = value.trimmed();
        if (date.size() < 10 || date[4] != '-' || date[7] != '-') {
            return false;
        }
        for (std::size_t i = 0; i < 10; ++i) {
            if (i == 4 || i == 7) {
                continue;
            }
            if (!std::isdigit(static_cast<unsigned char>(date[i]))) {
                return false;
            }
        }
        const int month = (date[5] - '0') * 10 + (date[6] - '0');
        const int day = (date[8] - '0') * 10 + (date[9] - '0');
        if (month < 1 || month > 12 || day < 1 || day > 31) {
            return false;
        }
        if (date.size() == 10) {
            return true;
        }
        if (date.size() < 20 || date[10] != 'T' || date[13] != ':' || date[16] != ':' ||
            !isTwoDigits_(date, 11) || !isTwoDigits_(date, 14) || !isTwoDigits_(date, 17)) {
            return false;
        }
        const int hour = (date[11] - '0') * 10 + (date[12] - '0');
        const int minute = (date[14] - '0') * 10 + (date[15] - '0');
        const int second = (date[17] - '0') * 10 + (date[18] - '0');
        if (hour > 23 || minute > 59 || second > 59) {
            return false;
        }

        std::size_t position = 19;
        if (position < date.size() && date[position] == '.') {
            ++position;
            const std::size_t fractionStart = position;
            while (position < date.size() &&
                   std::isdigit(static_cast<unsigned char>(date[position])) != 0) {
                ++position;
            }
            if (position == fractionStart) {
                return false;
            }
        }
        if (position < date.size() && date[position] == 'Z') {
            return position + 1 == date.size();
        }
        if (position >= date.size() || (date[position] != '+' && date[position] != '-')) {
            return false;
        }
        if (position + 6 != date.size() || date[position + 3] != ':' ||
            !isTwoDigits_(date, position + 1) || !isTwoDigits_(date, position + 4)) {
            return false;
        }
        const int offsetHour = (date[position + 1] - '0') * 10 + (date[position + 2] - '0');
        const int offsetMinute = (date[position + 4] - '0') * 10 + (date[position + 5] - '0');
        return offsetHour <= 23 && offsetMinute <= 59;
    }

    static bool isTwoDigits_(const SwString& value, std::size_t position) {
        return position + 1 < value.size() &&
               std::isdigit(static_cast<unsigned char>(value[position])) != 0 &&
               std::isdigit(static_cast<unsigned char>(value[position + 1])) != 0;
    }

    static bool pathPrefixMatches_(const SwString& path, const SwString& prefixValue) {
        SwString prefix = normalizePagePath_(prefixValue);
        if (prefix.size() > 1 && prefix.endsWith("/")) {
            prefix.chop(1);
        }
        if (prefix == "/") {
            return true;
        }
        if (!path.startsWith(prefix)) {
            return false;
        }
        return path.size() == prefix.size() || path[prefix.size()] == '/';
    }

    static SwString joinUrl_(const SwString& baseValue, const SwString& pathValue) {
        SwString base = baseValue.trimmed();
        while (base.size() > 8 && base.endsWith("/")) {
            base.chop(1);
        }
        const SwString path = normalizePagePath_(pathValue);
        return base + path;
    }

    static SwString escapeHtml_(const SwString& value) {
        SwString output;
        output.reserve(value.size() + 16);
        for (std::size_t i = 0; i < value.size(); ++i) {
            switch (value[i]) {
                case '&': output += "&amp;"; break;
                case '<': output += "&lt;"; break;
                case '>': output += "&gt;"; break;
                case '\"': output += "&quot;"; break;
                case '\'': output += "&#39;"; break;
                default: output.append(value[i]); break;
            }
        }
        return output;
    }

    static SwString escapeXml_(const SwString& value) {
        return escapeHtml_(value);
    }

    static SwString markdownInline_(const SwString& value) {
        SwString output;
        output.reserve(value.size());
        bool previousWasSpace = false;
        for (std::size_t i = 0; i < value.size(); ++i) {
            char c = value[i];
            if (c == '\r' || c == '\n' || c == '\t') {
                c = ' ';
            }
            if (c == ' ') {
                if (previousWasSpace) {
                    continue;
                }
                previousWasSpace = true;
            } else {
                previousWasSpace = false;
            }
            output.append(c);
        }
        return output.trimmed();
    }

    static SwString markdownLinkLabel_(const SwString& value) {
        SwString output = markdownInline_(value);
        output.replace("\\", "\\\\");
        output.replace("]", "\\]");
        output.replace("[", "\\[");
        return output;
    }

    static SwString markdownLinkUrl_(const SwString& value) {
        SwString output = value;
        output.replace("(", "%28");
        output.replace(")", "%29");
        return output;
    }

    static void appendCrawlerGroup_(SwString& output,
                                    const SwString& userAgent,
                                    bool allowed,
                                    const SwList<SwString>& allowPaths,
                                    const SwList<SwString>& disallowPaths) {
        output += "User-agent: ";
        output += userAgent;
        output += "\n";
        if (!allowed) {
            output += "Disallow: /\n\n";
            return;
        }
        if (allowPaths.isEmpty() && disallowPaths.isEmpty()) {
            output += "Allow: /\n\n";
            return;
        }
        for (std::size_t i = 0; i < allowPaths.size(); ++i) {
            output += "Allow: ";
            output += allowPaths[i];
            output += "\n";
        }
        for (std::size_t i = 0; i < disallowPaths.size(); ++i) {
            output += "Disallow: ";
            output += disallowPaths[i];
            output += "\n";
        }
        output += "\n";
    }

    static void appendBlockedCrawler_(SwString& output,
                                      const SwString& userAgent,
                                      bool allowed) {
        if (allowed) {
            return;
        }
        SwList<SwString> empty;
        appendCrawlerGroup_(output, userAgent, false, empty, empty);
    }

    void snapshot_(SwHttpSeoConfig& configOut, SwList<SwHttpSeoPage>& pagesOut) const {
        SwMutexLocker locker(&m_mutex);
        configOut = m_config;
        pagesOut = m_pages;
    }

    static SwJsonObject jsonIdReference_(const SwString& id) {
        SwJsonObject reference;
        reference["@id"] = id;
        return reference;
    }

    static SwString structuredDataJson_(const SwHttpSeoConfig& config,
                                        const SwHttpSeoPage& page,
                                        const SwString& canonical,
                                        const SwString& title,
                                        const SwString& description,
                                        const SwString& language) {
        const SwString websiteId = joinUrl_(config.siteUrl, "/#website");
        const SwString organizationId = joinUrl_(config.organizationUrl, "/#organization");
        const SwString pageId = canonical + "#webpage";

        SwJsonArray graph;

        SwJsonObject website;
        website["@type"] = "WebSite";
        website["@id"] = websiteId;
        website["url"] = config.siteUrl + "/";
        website["name"] = config.siteName;
        website["inLanguage"] = config.defaultLanguage;
        if (!config.defaultDescription.isEmpty()) {
            website["description"] = config.defaultDescription;
        }
        if (!config.organizationName.isEmpty()) {
            website["publisher"] = jsonIdReference_(organizationId);
        }
        graph.append(website);

        if (!config.organizationName.isEmpty()) {
            SwJsonObject organization;
            organization["@type"] = "Organization";
            organization["@id"] = organizationId;
            organization["name"] = config.organizationName;
            organization["url"] = config.organizationUrl;
            if (!config.logoUrl.isEmpty()) {
                SwJsonObject logo;
                logo["@type"] = "ImageObject";
                logo["url"] = absoluteAssetUrl_(config, config.logoUrl);
                organization["logo"] = logo;
            }
            graph.append(organization);
        }

        SwJsonObject webPage;
        webPage["@type"] = page.schemaType;
        webPage["@id"] = pageId;
        webPage["url"] = canonical;
        webPage["name"] = title;
        webPage["description"] = description;
        webPage["inLanguage"] = language;
        webPage["isPartOf"] = jsonIdReference_(websiteId);
        if (!config.organizationName.isEmpty()) {
            webPage["publisher"] = jsonIdReference_(organizationId);
        }
        if (!page.lastModified.isEmpty()) {
            webPage["dateModified"] = page.lastModified;
        }
        if (!page.datePublished.isEmpty()) {
            webPage["datePublished"] = page.datePublished;
        }
        if (!page.authorName.isEmpty()) {
            SwJsonObject author;
            author["@type"] = "Person";
            author["name"] = page.authorName;
            webPage["author"] = author;
        }
        if (!page.applicationCategory.isEmpty()) {
            webPage["applicationCategory"] = page.applicationCategory;
        }
        if (!page.operatingSystem.isEmpty()) {
            webPage["operatingSystem"] = page.operatingSystem;
        }
        graph.append(webPage);

        SwJsonObject root;
        root["@context"] = "https://schema.org";
        root["@graph"] = graph;
        SwString json = SwJsonDocument(root).toJson(SwJsonDocument::JsonFormat::Compact);
        // JSON escaping alone does not neutralize an HTML </script> sequence. Encode the three
        // characters that can cross the script-data boundary before embedding JSON-LD in HTML.
        json.replace("&", "\\u0026");
        json.replace("<", "\\u003C");
        json.replace(">", "\\u003E");
        return json;
    }

    static SwString absoluteAssetUrl_(const SwHttpSeoConfig& config, const SwString& value) {
        if (value.startsWith("https://") || value.startsWith("http://")) {
            return value;
        }
        return joinUrl_(config.siteUrl, value);
    }

    static SwString renderDocument_(const SwHttpSeoConfig& config,
                                    const SwHttpSeoPage& pageValue,
                                    const SwString& bodyHtml,
                                    const SwString& extraHeadHtml) {
        SwHttpSeoPage page = pageValue;
        SwString ignored;
        if (!normalizePage_(page, ignored)) {
            return SwString();
        }

        const SwString canonical = joinUrl_(config.siteUrl, page.path);
        const SwString language = page.language.isEmpty() ? config.defaultLanguage : page.language;
        const SwString description = page.description.isEmpty() ? config.defaultDescription : page.description;
        SwString title = page.title.isEmpty() ? config.defaultTitle : page.title;
        if (config.appendSiteNameToTitle && !config.siteName.isEmpty() && title != config.siteName &&
            !title.endsWith(config.titleSeparator + config.siteName)) {
            title += config.titleSeparator;
            title += config.siteName;
        }
        const SwString socialImage = page.socialImageUrl.isEmpty()
                                         ? config.defaultSocialImageUrl
                                         : page.socialImageUrl;

        SwString output;
        output += "<!doctype html>\n<html lang=\"";
        output += escapeHtml_(language);
        output += "\">\n<head>\n";
        output += "  <meta charset=\"utf-8\">\n";
        output += "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
        output += "  <title>";
        output += escapeHtml_(title);
        output += "</title>\n";
        output += "  <meta name=\"description\" content=\"";
        output += escapeHtml_(description);
        output += "\">\n";
        output += "  <meta name=\"robots\" content=\"";
        output += page.indexable
                      ? "index, follow, max-image-preview:large, max-snippet:-1, max-video-preview:-1"
                      : "noindex, nofollow";
        output += "\">\n";
        output += "  <link rel=\"canonical\" href=\"";
        output += escapeHtml_(canonical);
        output += "\">\n";
        output += "  <meta property=\"og:type\" content=\"";
        output += escapeHtml_(page.openGraphType);
        output += "\">\n";
        output += "  <meta property=\"og:site_name\" content=\"";
        output += escapeHtml_(config.siteName);
        output += "\">\n";
        output += "  <meta property=\"og:title\" content=\"";
        output += escapeHtml_(title);
        output += "\">\n";
        output += "  <meta property=\"og:description\" content=\"";
        output += escapeHtml_(description);
        output += "\">\n";
        output += "  <meta property=\"og:url\" content=\"";
        output += escapeHtml_(canonical);
        output += "\">\n";
        if (!socialImage.isEmpty()) {
            const SwString absoluteImage = absoluteAssetUrl_(config, socialImage);
            output += "  <meta property=\"og:image\" content=\"";
            output += escapeHtml_(absoluteImage);
            output += "\">\n";
            output += "  <meta name=\"twitter:card\" content=\"summary_large_image\">\n";
            output += "  <meta name=\"twitter:image\" content=\"";
            output += escapeHtml_(absoluteImage);
            output += "\">\n";
        } else {
            output += "  <meta name=\"twitter:card\" content=\"summary\">\n";
        }
        output += "  <meta name=\"twitter:title\" content=\"";
        output += escapeHtml_(title);
        output += "\">\n";
        output += "  <meta name=\"twitter:description\" content=\"";
        output += escapeHtml_(description);
        output += "\">\n";
        output += "  <script type=\"application/ld+json\">";
        output += structuredDataJson_(config, page, canonical, title, description, language);
        output += "</script>\n";
        if (!extraHeadHtml.isEmpty()) {
            output += extraHeadHtml;
            if (!extraHeadHtml.endsWith("\n")) {
                output += "\n";
            }
        }
        output += "</head>\n<body>\n";
        output += bodyHtml;
        if (!bodyHtml.endsWith("\n")) {
            output += "\n";
        }
        output += "</body>\n</html>\n";
        return output;
    }

    mutable SwMutex m_mutex;
    SwHttpSeoConfig m_config;
    SwList<SwHttpSeoPage> m_pages;
    bool m_configured = false;
};
