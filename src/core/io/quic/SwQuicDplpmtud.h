#ifndef SWQUICDPLPMTUD_H
#define SWQUICDPLPMTUD_H

#include <cstdint>

// SwQuicDplpmtud — Datagram Packetization Layer PMTU Discovery (RFC 8899) pour QUIC (RFC 9000 §14.3).
// Machine à états GÉNÉRIQUE : recherche par dichotomie de la plus grande taille de datagramme qui traverse
// le chemin sans fragmentation, via des paquets-sondes PING+PADDING. L'appelant (la connexion) émet la sonde
// de taille nextProbeSize() ; quand un ACK couvre le numéro de paquet de la sonde -> onProbeAcked(size), quand
// la sonde est perdue -> onProbeLost(size) (jusqu'à kMaxProbes retransmissions avant de conclure « trop grand »).
// Détection de trou noir : onBlackholeDetected() rabat le PMTU confirmé à la base et relance la recherche sous
// la taille fautive. Aucun I/O, aucun temps réel ici : entièrement piloté par événements -> testable.
class SwQuicDplpmtud {
public:
    enum class State { Disabled, Searching, Complete, Error };

    static const int kBasePmtu  = 1200; // plancher QUuIC (RFC 9000 14.1) : jamais en dessous
    static const int kMaxProbes = 3;    // retransmissions d'une sonde avant de la déclarer perdue

    // Démarre la recherche entre la base (1200) et maxPmtu (borné par max_udp_payload_size du pair).
    void start(int maxPmtu) {
        m_low  = kBasePmtu;
        m_high = (maxPmtu > kBasePmtu) ? maxPmtu : kBasePmtu;
        m_pmtu = kBasePmtu;
        m_probeSize = 0;
        m_probeCount = 0;
        m_state = (m_high > m_low) ? State::Searching : State::Complete;
    }

    // Taille de la prochaine sonde à émettre (0 si rien à sonder : recherche terminée/désactivée).
    int nextProbeSize() {
        if (m_state != State::Searching) return 0;
        if (m_probeSize == 0) m_probeSize = candidate_();
        return m_probeSize;
    }

    // La sonde de `size` a été acquittée : le chemin supporte au moins `size`.
    void onProbeAcked(int size) {
        if (size != m_probeSize) return; // ACK d'une sonde périmée : ignorer
        if (size > m_pmtu) m_pmtu = size;
        m_low = size;                    // borne basse confirmée
        m_probeSize = 0;
        m_probeCount = 0;
        advance_();
    }

    // La sonde de `size` semble perdue. Après kMaxProbes tentatives -> `size` est jugé trop grand.
    void onProbeLost(int size) {
        if (size != m_probeSize) return;
        if (++m_probeCount >= kMaxProbes) {
            m_high = size - 1;           // plafond : cette taille ne passe pas
            m_probeSize = 0;
            m_probeCount = 0;
            advance_();
        }
        // sinon : la même taille sera re-sondée (retransmission) au prochain nextProbeSize()
    }

    // Un trou noir est détecté (perte soutenue de paquets NON-sondes à la taille confirmée) :
    // on rabat le PMTU à la base et on relance la recherche strictement sous la taille fautive.
    void onBlackholeDetected() {
        m_high = (m_pmtu > kBasePmtu) ? (m_pmtu - 1) : kBasePmtu;
        m_low  = kBasePmtu;
        m_pmtu = kBasePmtu;
        m_probeSize = 0;
        m_probeCount = 0;
        m_state = (m_high > m_low) ? State::Searching : State::Complete;
    }

    int   pmtu()  const { return m_pmtu; }   // PLPMTU confirmé (utilisable dès maintenant)
    State state() const { return m_state; }
    int   probeCount() const { return m_probeCount; }

private:
    // Dichotomie : milieu de l'intervalle ouvert (m_low, m_high].
    int candidate_() const { return m_low + (m_high - m_low + 1) / 2; }

    void advance_() {
        if (m_low >= m_high) { m_state = State::Complete; m_probeSize = 0; return; }
        const int next = candidate_();
        if (next <= m_pmtu) { m_state = State::Complete; m_probeSize = 0; return; }
        m_probeSize = next;
    }

    State m_state = State::Disabled;
    int m_low = kBasePmtu;
    int m_high = kBasePmtu;
    int m_pmtu = kBasePmtu;
    int m_probeSize = 0;
    int m_probeCount = 0;
};

#endif // SWQUICDPLPMTUD_H
