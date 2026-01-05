#pragma once

#include "SwWidget.h"

#include "SwString.h"

class SwButton;
class SwFrame;
class SwLabel;
class SwLineEdit;
class WaAvatarCircle;

class WaLoginPage final : public SwWidget {
    SW_OBJECT(WaLoginPage, SwWidget)

public:
    explicit WaLoginPage(SwWidget* parent = nullptr);

    void setDatabaseUrl(const SwString& url);
    SwString databaseUrl() const;

    void setAuthToken(const SwString& token);
    SwString authToken() const;

    void setPollIntervalMs(int ms);
    int pollIntervalMs() const;

    void setFireConfigVisible(bool on);
    bool fireConfigVisible() const { return m_fireConfigVisible; }

    void setErrorText(const SwString& text);

signals:
    DECLARE_SIGNAL(loginRequested, SwString, SwString);
    DECLARE_SIGNAL(signUpRequested, SwString, SwString, SwString, SwString, SwString);

protected:
    void resizeEvent(ResizeEvent* event) override;
    void keyPressEvent(KeyEvent* event) override;
    void paintEvent(PaintEvent* event) override;

private:
    enum class Mode {
        Login,
        SignUp
    };

    void buildUi_();
    void updateLayout_();
    void setMode_(Mode mode);
    void submit_();
    void tryLogin_();
    void trySignUp_();
    SwString requireFireUrlOrShowError_();

    SwFrame* m_card{nullptr};
    WaAvatarCircle* m_logo{nullptr};
    SwLabel* m_title{nullptr};
    SwLabel* m_subtitle{nullptr};

    SwButton* m_tabLogin{nullptr};
    SwButton* m_tabSignUp{nullptr};
    Mode m_mode{Mode::Login};

    // Shared config (stored in SwSettings by the app).
    SwLineEdit* m_fbUrl{nullptr};
    SwLineEdit* m_fbToken{nullptr};
    SwLineEdit* m_fbPollMs{nullptr};
    bool m_fireConfigVisible{false};

    // Login.
    SwLineEdit* m_loginId{nullptr};
    SwLineEdit* m_loginPass{nullptr};
    SwButton* m_login{nullptr};

    // Sign up.
    SwLineEdit* m_lastName{nullptr};
    SwLineEdit* m_firstName{nullptr};
    SwLineEdit* m_pseudo{nullptr};
    SwLineEdit* m_phone{nullptr};
    SwLineEdit* m_pass1{nullptr};
    SwLineEdit* m_pass2{nullptr};
    SwButton* m_signup{nullptr};

    SwLabel* m_error{nullptr};
};
