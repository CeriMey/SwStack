#pragma once

#include "auth/SwHttpAuthTypes.h"

struct SwHttpAuthRenderedTemplate {
    SwString subject;
    SwString textBody;
    SwString htmlBody;
};

class SwHttpAuthTemplateRenderer {
public:
    static SwHttpAuthRenderedTemplate render(const SwHttpAuthMailTemplate& mailTemplate,
                                             const SwString& code,
                                             const SwString& url) {
        SwHttpAuthRenderedTemplate rendered;
        rendered.subject = swHttpAuthDetail::replaceTemplatePlaceholders(mailTemplate.subject, code, url);
        rendered.textBody = swHttpAuthDetail::replaceTemplatePlaceholders(mailTemplate.textBody, code, url);
        rendered.htmlBody =
            swHttpAuthDetail::replaceTemplatePlaceholders(mailTemplate.htmlBody, code, htmlAttributeText_(url));
        return rendered;
    }

    static SwString renderUrl(const SwString& urlTemplate,
                              const SwString& token,
                              const SwString& email = SwString()) {
        return swHttpAuthDetail::replaceUrlTemplateToken(urlTemplate, token, email);
    }

private:
    // Une URL placée dans un attribut HTML y porte ses « & » sous forme d'entité :
    // « &email= » brut est du HTML invalide.
    static SwString htmlAttributeText_(const SwString& value) {
        const std::string input = value.toStdString();
        std::string out;
        out.reserve(input.size() + 16);
        for (std::size_t i = 0; i < input.size(); ++i) {
            switch (input[i]) {
                case '&': out += "&amp;"; break;
                case '"': out += "&quot;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                default: out.push_back(input[i]); break;
            }
        }
        return SwString(out);
    }
};
