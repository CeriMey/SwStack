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
        rendered.htmlBody = swHttpAuthDetail::replaceTemplatePlaceholders(mailTemplate.htmlBody, code, url);
        return rendered;
    }

    static SwString renderUrl(const SwString& urlTemplate,
                              const SwString& token) {
        return swHttpAuthDetail::replaceUrlTemplateToken(urlTemplate, token);
    }
};
