#include "WaLoginPage.h"

#include "SwButton.h"
#include "SwFrame.h"
#include "SwLabel.h"
#include "SwLineEdit.h"
#include "SwPainter.h"
#include "SwWidgetPlatformAdapter.h"

#include "WaAvatarCircle.h"

#include <algorithm>
#include <cctype>
#include <string>

static bool waIsValidPseudoKey_(const SwString& pseudo) {
    const std::string s = pseudo.trimmed().toLower().toStdString();
    if (s.empty()) {
        return false;
    }
    for (unsigned char c : s) {
        const bool ok = (std::isalnum(c) != 0) || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static int waCountDigits_(const SwString& s) {
    const std::string in = s.toStdString();
    int n = 0;
    for (unsigned char c : in) {
        if (std::isdigit(c) != 0) {
            ++n;
        }
    }
    return n;
}

static const SwString kTabCss = R"(
    SwButton {
        background-color: rgb(255, 255, 255);
        border-color: rgb(220, 224, 232);
        border-width: 1px;
        border-radius: 14px;
        color: rgb(84, 101, 111);
        font-size: 13px;
        padding: 6px 10px;
    }
    SwButton:hover { background-color: rgb(248, 249, 250); }
    SwButton:pressed { background-color: rgb(236, 238, 240); }
)";

static const SwString kTabSelectedCss = R"(
    SwButton {
        background-color: rgb(220, 252, 231);
        border-color: rgb(134, 239, 172);
        border-width: 1px;
        border-radius: 14px;
        color: rgb(22, 163, 74);
        font-size: 13px;
        padding: 6px 10px;
    }
    SwButton:hover { background-color: rgb(187, 247, 208); }
    SwButton:pressed { background-color: rgb(134, 239, 172); }
)";

static const SwString kPrimaryBtnCss = R"(
    SwButton {
        background-color: rgb(0, 168, 132);
        border-width: 0px;
        border-radius: 14px;
        color: rgb(255, 255, 255);
        font-size: 14px;
        padding: 10px 14px;
    }
    SwButton:hover { background-color: rgb(0, 160, 125); }
    SwButton:pressed { background-color: rgb(0, 150, 115); }
)";

static const SwString kInputCss = R"(
    SwLineEdit {
        background-color: rgb(255, 255, 255);
        border-color: rgb(220, 224, 232);
        border-width: 1px;
        border-radius: 12px;
        padding: 8px 12px;
        color: rgb(17, 27, 33);
    }
    SwLineEdit:focus { border-color: rgb(0, 168, 132); }
)";

WaLoginPage::WaLoginPage(SwWidget* parent)
    : SwWidget(parent) {
    setStyleSheet("SwWidget { background-color: rgba(0, 0, 0, 0); border-width: 0px; }");
    setFocusPolicy(FocusPolicyEnum::Strong);
    buildUi_();
    setFireConfigVisible(false);
    if (m_loginId) {
        m_loginId->setFocus(true);
    }
}

void WaLoginPage::setDatabaseUrl(const SwString& url) {
    if (m_fbUrl) {
        m_fbUrl->setText(url);
    }
}

SwString WaLoginPage::databaseUrl() const {
    return m_fbUrl ? m_fbUrl->getText().trimmed() : SwString();
}

void WaLoginPage::setAuthToken(const SwString& token) {
    if (m_fbToken) {
        m_fbToken->setText(token);
    }
}

SwString WaLoginPage::authToken() const {
    return m_fbToken ? m_fbToken->getText().trimmed() : SwString();
}

void WaLoginPage::setPollIntervalMs(int ms) {
    if (!m_fbPollMs) {
        return;
    }
    if (ms <= 0) {
        m_fbPollMs->setText(SwString());
        return;
    }
    m_fbPollMs->setText(SwString::number(ms));
}

int WaLoginPage::pollIntervalMs() const {
    if (!m_fbPollMs) {
        return 0;
    }
    bool ok = false;
    const int ms = m_fbPollMs->getText().trimmed().toInt(&ok);
    return ok ? ms : 0;
}

void WaLoginPage::setFireConfigVisible(bool on) {
    m_fireConfigVisible = on;
    if (m_fbUrl) m_fbUrl->setVisible(on);
    if (m_fbToken) m_fbToken->setVisible(on);
    if (m_fbPollMs) m_fbPollMs->setVisible(on);
    updateLayout_();
}

void WaLoginPage::setErrorText(const SwString& text) {
    if (!m_error) {
        return;
    }
    m_error->setText(text);
    m_error->setVisible(!text.trimmed().isEmpty());
    update();
}

void WaLoginPage::resizeEvent(ResizeEvent* event) {
    SwWidget::resizeEvent(event);
    updateLayout_();
}

void WaLoginPage::keyPressEvent(KeyEvent* event) {
    if (event && SwWidgetPlatformAdapter::isReturnKey(event->key())) {
        submit_();
        event->accept();
        return;
    }
    SwWidget::keyPressEvent(event);
}

void WaLoginPage::paintEvent(PaintEvent* event) {
    if (!isVisibleInHierarchy()) {
        return;
    }
    SwPainter* painter = event ? event->painter() : nullptr;
    if (!painter) {
        return;
    }

    const SwRect r = rect();

    // WhatsApp-like top banner.
    const int bannerH = std::min(220, std::max(140, r.height / 4));
    const SwColor banner{0, 168, 132};
    painter->fillRect(SwRect{r.x, r.y, r.width, bannerH}, banner, banner, 0);

    // Soft background below.
    const SwColor bg{240, 242, 245};
    painter->fillRect(SwRect{r.x, r.y + bannerH, r.width, r.height - bannerH}, bg, bg, 0);

    // Paint children (card, inputs, button) on top.
    SwWidget::paintEvent(event);
}

void WaLoginPage::buildUi_() {
    if (m_card) {
        return;
    }

    m_card = new SwFrame(this);
    m_card->setFrameShape(SwFrame::Shape::Box);
    m_card->setStyleSheet(R"(
        SwFrame {
            background-color: rgb(255, 255, 255);
            border-color: rgb(220, 224, 232);
            border-width: 1px;
            border-radius: 18px;
        }
    )");
    m_card->setFocusPolicy(FocusPolicyEnum::NoFocus);

    m_logo = new WaAvatarCircle("W", SwColor{0, 168, 132}, m_card);

    m_title = new SwLabel("WhatsApp", m_card);
    m_title->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(17, 27, 33); font-size: 22px; }");

    m_subtitle = new SwLabel("CrÃ©e un compte ou connecte-toi.", m_card);
    m_subtitle->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(102, 119, 129); font-size: 13px; }");

    // Tabs.
    m_tabLogin = new SwButton("Connexion", m_card);
    m_tabLogin->setStyleSheet(kTabSelectedCss);
    m_tabSignUp = new SwButton("CrÃ©er un compte", m_card);
    m_tabSignUp->setStyleSheet(kTabCss);
    SwObject::connect(m_tabLogin, &SwButton::clicked, [this]() { setMode_(Mode::Login); });
    SwObject::connect(m_tabSignUp, &SwButton::clicked, [this]() { setMode_(Mode::SignUp); });

    // Firebase config.
    m_fbUrl = new SwLineEdit("Firebase RTDB URL (https://...)", m_card);
    m_fbUrl->setStyleSheet(kInputCss);
    m_fbToken = new SwLineEdit("Token (optionnel)", m_card);
    m_fbToken->setStyleSheet(kInputCss);
    m_fbPollMs = new SwLineEdit("Poll ms (optionnel, ex: 750)", m_card);
    m_fbPollMs->setStyleSheet(kInputCss);

    // Standalone app: firebase config is hardcoded, keep fields hidden by default.
    if (m_fbUrl) m_fbUrl->setReadOnly(true);
    if (m_fbToken) m_fbToken->setReadOnly(true);
    if (m_fbPollMs) m_fbPollMs->setReadOnly(true);
    setFireConfigVisible(false);

    // Login form.
    m_loginId = new SwLineEdit("Pseudo ou tÃ©lÃ©phone", m_card);
    m_loginId->setStyleSheet(kInputCss);
    m_loginPass = new SwLineEdit("Mot de passe", m_card);
    m_loginPass->setStyleSheet(kInputCss);
    m_loginPass->setEchoMode(EchoModeEnum::PasswordEcho);

    m_login = new SwButton("Se connecter", m_card);
    m_login->setStyleSheet(kPrimaryBtnCss);

    // Sign-up form.
    m_lastName = new SwLineEdit("Nom", m_card);
    m_lastName->setStyleSheet(kInputCss);
    m_firstName = new SwLineEdit("PrÃ©nom", m_card);
    m_firstName->setStyleSheet(kInputCss);
    m_pseudo = new SwLineEdit("Pseudo", m_card);
    m_pseudo->setStyleSheet(kInputCss);
    m_phone = new SwLineEdit("TÃ©lÃ©phone", m_card);
    m_phone->setStyleSheet(kInputCss);
    m_pass1 = new SwLineEdit("Mot de passe", m_card);
    m_pass1->setStyleSheet(kInputCss);
    m_pass1->setEchoMode(EchoModeEnum::PasswordEcho);
    m_pass2 = new SwLineEdit("Confirmer le mot de passe", m_card);
    m_pass2->setStyleSheet(kInputCss);
    m_pass2->setEchoMode(EchoModeEnum::PasswordEcho);

    m_signup = new SwButton("CrÃ©er le compte", m_card);
    m_signup->setStyleSheet(kPrimaryBtnCss);

    m_error = new SwLabel("", m_card);
    m_error->setStyleSheet("SwLabel { background-color: rgba(0,0,0,0); border-width: 0px; color: rgb(220, 38, 38); font-size: 12px; }");
    m_error->setVisible(false);

    SwObject::connect(m_login, &SwButton::clicked, [this]() { tryLogin_(); });
    SwObject::connect(m_signup, &SwButton::clicked, [this]() { trySignUp_(); });

    setMode_(Mode::Login);
    updateLayout_();
}

void WaLoginPage::updateLayout_() {
    if (!m_card) {
        return;
    }

    const SwRect r = rect();

    const int cardW = std::min(620, std::max(380, r.width - 64));
    const int cardH = std::min(600, std::max(440, r.height - 120));

    const int cardX = (r.width - cardW) / 2;
    const int cardY = 120 + std::max(0, (r.height - 120 - cardH) / 2);

    m_card->move(cardX, cardY);
    m_card->resize(cardW, cardH);

    const SwRect cr = m_card->frameGeometry();
    const int pad = 20;
    const int gap = 10;

    if (m_logo) { m_logo->move(pad, pad); m_logo->resize(44, 44); }

    const int textX = pad + 56;
    const int textW = std::max(0, cr.width - textX - pad);

    if (m_title) { m_title->move(textX, pad + 2); m_title->resize(textW, 26); }
    if (m_subtitle) { m_subtitle->move(textX, pad + 30); m_subtitle->resize(textW, 18); }

    int y = pad + 72;
    const int fieldW = std::max(0, cr.width - 2 * pad);

    // Tabs row.
    const int tabH = 36;
    const int tabW = (fieldW - gap) / 2;
    if (m_tabLogin) { m_tabLogin->move(pad, y); m_tabLogin->resize(tabW, tabH); }
    if (m_tabSignUp) { m_tabSignUp->move(pad + tabW + gap, y); m_tabSignUp->resize(fieldW - tabW - gap, tabH); }
    y += tabH + 12;

    // Firebase config (optional, usually hidden in the standalone app).
    if (m_fireConfigVisible) {
        const int configH = 38;
        if (m_fbUrl) { m_fbUrl->move(pad, y); m_fbUrl->resize(fieldW, configH); y += configH + 8; }
        if (m_fbToken) { m_fbToken->move(pad, y); m_fbToken->resize(fieldW, configH); y += configH + 8; }
        if (m_fbPollMs) { m_fbPollMs->move(pad, y); m_fbPollMs->resize(fieldW, configH); y += configH + 12; }
    }

    const int btnH = 44;
    const int rowH = 40;

    if (m_mode == Mode::Login) {
        if (m_loginId) { m_loginId->move(pad, y); m_loginId->resize(fieldW, rowH); y += rowH + 10; }
        if (m_loginPass) { m_loginPass->move(pad, y); m_loginPass->resize(fieldW, rowH); y += rowH + 14; }
        if (m_login) {
            const int btnW2 = std::min(220, fieldW);
            m_login->move(cr.width - pad - btnW2, cr.height - pad - btnH);
            m_login->resize(btnW2, btnH);
        }
    } else {
        if (m_lastName) { m_lastName->move(pad, y); m_lastName->resize(fieldW, rowH); y += rowH + 8; }
        if (m_firstName) { m_firstName->move(pad, y); m_firstName->resize(fieldW, rowH); y += rowH + 8; }
        if (m_pseudo) { m_pseudo->move(pad, y); m_pseudo->resize(fieldW, rowH); y += rowH + 8; }
        if (m_phone) { m_phone->move(pad, y); m_phone->resize(fieldW, rowH); y += rowH + 8; }
        if (m_pass1) { m_pass1->move(pad, y); m_pass1->resize(fieldW, rowH); y += rowH + 8; }
        if (m_pass2) { m_pass2->move(pad, y); m_pass2->resize(fieldW, rowH); y += rowH + 14; }

        if (m_signup) {
            const int btnW2 = std::min(240, fieldW);
            m_signup->move(cr.width - pad - btnW2, cr.height - pad - btnH);
            m_signup->resize(btnW2, btnH);
        }
    }

    if (m_error) {
        const int errH = 18;
        m_error->move(pad, cr.height - pad - btnH - 10 - errH);
        m_error->resize(fieldW, errH);
    }
}

void WaLoginPage::setMode_(Mode mode) {
    m_mode = mode;

    if (m_tabLogin) m_tabLogin->setStyleSheet(mode == Mode::Login ? kTabSelectedCss : kTabCss);
    if (m_tabSignUp) m_tabSignUp->setStyleSheet(mode == Mode::SignUp ? kTabSelectedCss : kTabCss);

    const bool login = (mode == Mode::Login);
    if (m_loginId) m_loginId->setVisible(login);
    if (m_loginPass) m_loginPass->setVisible(login);
    if (m_login) m_login->setVisible(login);

    if (m_lastName) m_lastName->setVisible(!login);
    if (m_firstName) m_firstName->setVisible(!login);
    if (m_pseudo) m_pseudo->setVisible(!login);
    if (m_phone) m_phone->setVisible(!login);
    if (m_pass1) m_pass1->setVisible(!login);
    if (m_pass2) m_pass2->setVisible(!login);
    if (m_signup) m_signup->setVisible(!login);

    setErrorText(SwString());
    updateLayout_();
    if (login && m_loginId) {
        m_loginId->setFocus(true);
    } else if (!login && m_lastName) {
        m_lastName->setFocus(true);
    }
}

void WaLoginPage::submit_() {
    if (m_mode == Mode::Login) {
        tryLogin_();
    } else {
        trySignUp_();
    }
}

SwString WaLoginPage::requireFireUrlOrShowError_() {
    const SwString url = databaseUrl();
    if (url.isEmpty()) {
        setErrorText("Firebase URL manquante.");
        if (m_fbUrl && m_fireConfigVisible) {
            m_fbUrl->setFocus(true);
        }
        return SwString();
    }
    return url;
}

void WaLoginPage::tryLogin_() {
    if (!m_loginId || !m_loginPass) {
        return;
    }

    if (requireFireUrlOrShowError_().isEmpty()) {
        return;
    }

    const SwString id = m_loginId->getText().trimmed();
    if (id.isEmpty()) {
        setErrorText("Pseudo ou tÃ©lÃ©phone manquant.");
        m_loginId->setFocus(true);
        return;
    }
    if (id.contains("@")) {
        setErrorText("Email non supporte. Utilise un pseudo ou un telephone.");
        m_loginId->setFocus(true);
        return;
    }

    const SwString pass = m_loginPass->getText().trimmed();
    if (pass.isEmpty()) {
        setErrorText("Mot de passe manquant.");
        m_loginPass->setFocus(true);
        return;
    }

    setErrorText(SwString());
    emit loginRequested(id, pass);
}

void WaLoginPage::trySignUp_() {
    if (!m_lastName || !m_firstName || !m_pseudo || !m_phone || !m_pass1 || !m_pass2) {
        return;
    }

    if (requireFireUrlOrShowError_().isEmpty()) {
        return;
    }

    const SwString lastName = m_lastName->getText().trimmed();
    const SwString firstName = m_firstName->getText().trimmed();
    const SwString pseudo = m_pseudo->getText().trimmed();
    const SwString phone = m_phone->getText().trimmed();
    const SwString p1 = m_pass1->getText();
    const SwString p2 = m_pass2->getText();

    if (lastName.isEmpty()) { setErrorText("Nom manquant."); m_lastName->setFocus(true); return; }
    if (firstName.isEmpty()) { setErrorText("PrÃ©nom manquant."); m_firstName->setFocus(true); return; }
    if (pseudo.isEmpty()) { setErrorText("Pseudo manquant."); m_pseudo->setFocus(true); return; }
    if (phone.isEmpty()) { setErrorText("TÃ©lÃ©phone manquant."); m_phone->setFocus(true); return; }
    if (!waIsValidPseudoKey_(pseudo)) { setErrorText("Pseudo invalide (lettres/chiffres/_/-)."); m_pseudo->setFocus(true); return; }
    if (waCountDigits_(phone) < 6) { setErrorText("Numero de telephone invalide."); m_phone->setFocus(true); return; }
    if (p1.trimmed().isEmpty()) { setErrorText("Mot de passe manquant."); m_pass1->setFocus(true); return; }
    if (p1 != p2) { setErrorText("Les mots de passe ne correspondent pas."); m_pass2->setFocus(true); return; }

    setErrorText(SwString());
    emit signUpRequested(firstName, lastName, pseudo, phone, p1);
}

