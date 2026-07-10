# SwQuic, HTTP/3 et WebTransport

## Objectif

Cette note pose la trajectoire d'integration de QUIC, TLS 1.3,
HTTP/3 et WebTransport dans la couche `io` de SwStack.

Le but n'est pas d'ajouter un gros objet `SwQuic` dans le serveur
applicatif. Le but est de construire une pile claire, couche par couche,
afin que `SwHttpApp` puisse exposer les memes routes applicatives sur
HTTP/1.x et HTTP/3, tout en conservant WebSocket sur HTTP/1.x et la voie
Extended CONNECT dediee a WebTransport sur HTTP/3.

## Integration SwHttpApp

`SwHttpApp::listenHttps()` demarre maintenant deux listeners sur le meme
numero de port : HTTPS sur TCP et HTTP/3 sur QUIC/UDP. Le couple certificat
PEM/cle privee qui definit l'identite de l'origine est utilise par les deux
handshakes TLS. Les surcharges multi-certificats conservent la selection SNI
pour HTTP/3.

Le chargeur QUIC accepte les cles X.509 Ed25519, ECDSA P-256/P-384, RSA et
RSA-PSS. Pour une cle RSA-PSS restreinte, il selectionne le schema TLS 1.3
`rsa_pss_pss_sha256`, `sha384` ou `sha512` autorise par la cle. Si aucune de ces
signatures n'est permise, `listenHttps()` echoue avant d'ouvrir les listeners :
il n'y a pas de repli silencieux qui presenterait une identite differente entre
TCP et QUIC.

```cpp
SwHttpApp app;
app.get("/api/status", [](SwHttpContext& ctx) {
    ctx.setHeader("x-protocol", ctx.protocol());
    ctx.json(status);
});

// TCP 443 (HTTPS) + UDP 443 (HTTP/3), memes routes et middlewares.
if (!app.listenHttps("0.0.0.0", 443, "fullchain.pem", "privkey.pem")) {
    // app.lastHttp3Error() fournit le diagnostic QUIC/certificat.
}
```

Les reponses HTTPS annoncent automatiquement l'alternative standard
`Alt-Svc: h3=":443"; ma=86400`. Le protocole effectivement utilise est visible
avec `SwHttpContext::protocol()` et `SwHttpContext::isHttp3()`. Le listener QUIC
est pilote par la readiness UDP et les deadlines QUIC dans la boucle
`SwCoreApplication`; aucun appel manuel a `poll()` n'est requis.

Le dispatch est commun jusque dans les chemins asynchrones : pre-routes
d'authentification/ACL, routes asynchrones, timeouts, limites en vol, filtres de
reponse et metriques passent tous par `SwHttpServer`. Les buffers de requete H3
sont comptes dans le budget global partage avec TCP/TLS, liberes apres la
reponse, et le credit `MAX_STREAMS` est renouvele pour les connexions longues.

Pour un deploiement volontairement TCP-only, appeler
`app.setHttp3Enabled(false)` avant `listenHttps()`.

## Principe d'architecture

La pile visee est la suivante :

```text
SwUdpSocket
  -> SwQuicServer / SwQuicConnection
      -> SwQuicStream / SwQuicDatagramChannel
          -> SwHttp3Server / SwHttp3Connection
              -> SwHttpRouter / SwHttpContext
                  -> routes SwHttpApp / SwAppServer
                      -> SwWebTransportSession
```

`SwUdpSocket` reste une primitive UDP. Il ne doit pas connaitre QUIC,
HTTP/3, WebTransport, les routes HTTP, ni la politique applicative.

QUIC est une couche de transport au-dessus d'UDP. HTTP/3 est une couche
protocolaire au-dessus de QUIC. WebTransport est une capacite applicative
au-dessus de HTTP/3 et de QUIC.

## Ce que QUIC doit posseder

Un vrai QUIC compatible RFC n'est pas seulement un multiplexeur UDP.
Il doit posseder au minimum :

- connection IDs ;
- formats de paquets QUIC ;
- packet number spaces ;
- ACK et retransmission ;
- detection de perte ;
- congestion control ;
- flow control par connexion et par stream ;
- streams unidirectionnels et bidirectionnels ;
- datagrammes non fiables via l'extension QUIC DATAGRAM ;
- timers de handshake, idle timeout et PTO ;
- migration ou, au minimum, refus propre de migration ;
- fermeture propre avec codes d'erreur.

Pour une premiere etape interne VIGIL, il est possible de construire un
multiplexeur UDP inspire de QUIC. Mais s'il n'est pas compatible RFC, il
ne doit pas etre presente comme compatible QUIC, HTTP/3 ou navigateur.

## TLS 1.3 dans QUIC

QUIC n'utilise pas TLS comme `SwSslSocket`.

Avec TCP, TLS chiffre un flux de bytes au-dessus de la socket. Avec QUIC,
TLS 1.3 fournit le handshake, l'authentification, les secrets et les cles,
puis QUIC applique lui-meme la protection des paquets.

Consequences :

- `SwSslServer` et `SwSslSocket` ne sont pas reutilisables directement
  comme transport QUIC ;
- leur modele de certificats et de configuration peut etre reutilise ;
- les primitives crypto bas niveau doivent venir d'un backend solide ;
- il ne faut pas reimplementer AES-GCM, ChaCha20-Poly1305, HKDF,
  X25519 ou la validation de certificats a la main ;
- une couche dediee doit extraire les cles QUIC depuis le handshake TLS
  et alimenter la protection des paquets.

Nom de responsabilite propose :

```text
SwQuicTlsHandshake
SwQuicPacketProtector
SwQuicCryptoKeys
```

## HTTP/3 n'est pas SwHttpSession

`SwHttpSession` est construit autour d'une socket TCP/TLS acceptee et
d'un parser HTTP/1.x. HTTP/3 ne peut pas etre ajoute proprement dans
cette classe.

HTTP/3 doit avoir une session parallele qui transforme les streams QUIC
en requetes et reponses HTTP, puis les envoie vers le meme routeur :

```text
SwHttp3Server
SwHttp3Connection
SwHttp3Stream
SwHttp3FrameCodec
SwQpackDecoder
SwQpackEncoder
```

L'objectif important est de conserver la frontiere applicative :

```text
SwHttpRequest -> SwHttpRouter -> SwHttpResponse
```

Ainsi, une route `GET /api/state` doit pouvoir fonctionner via HTTP/1.x
et via HTTP/3 sans dupliquer la logique applicative.

## WebSocket et WebTransport

Le WebSocket actuel s'appuie sur un upgrade HTTP/1.x et sur une remise de
socket brute via `switchToRawSocket`.

Ce modele ne s'applique pas a WebTransport. WebTransport n'obtient pas une
socket TCP brute. Il ouvre une session sur HTTP/3, puis utilise les streams
QUIC et les datagrammes QUIC.

Il faut donc une abstraction differente :

```text
SwWebTransportSession
SwWebTransportStream
SwWebTransportDatagram
```

API cible possible :

```cpp
SwHttpApp app;

app.listenHttps("0.0.0.0", 443, credentials);
app.listenHttp3("0.0.0.0", 443, credentials);

app.get("/api/state", [](SwHttpContext& context) {
    context.json(...);
});

app.webTransport("/mesh",
                 [](SwWebTransportSession& session,
                    const SwHttpRequest& request) {
                     // Streams fiables + datagrammes non fiables.
                 });
```

Le futur `SwAppServer` peut etre une facade plus large, mais il doit
composer les serveurs au lieu de melanger les responsabilites :

```text
SwAppServer
  owns SwHttpServer
  owns SwHttp3Server
  exposes one route API
```

## Decoupage de classes propose

Transport UDP et demultiplexage :

```text
SwQuicServer
SwQuicConnection
SwQuicConnectionId
SwQuicPacketHeader
SwQuicPacketCodec
```

Fiabilite et controle de flux :

```text
SwQuicAckTracker
SwQuicLossRecovery
SwQuicCongestionControl
SwQuicFlowControl
```

Streams et datagrammes :

```text
SwQuicStream
SwQuicStreamMap
SwQuicDatagramChannel
```

Crypto QUIC :

```text
SwQuicTlsHandshake
SwQuicCryptoKeys
SwQuicPacketProtector
```

HTTP/3 :

```text
SwHttp3Server
SwHttp3Connection
SwHttp3Stream
SwHttp3FrameCodec
SwQpackEncoder
SwQpackDecoder
```

WebTransport :

```text
SwWebTransportSession
SwWebTransportStream
SwWebTransportRoute
```

La brique `src/core/io/quic` est header-only : chaque classe garde son
propre `.h`, et les fonctions membres sont definies directement dans la
definition de la classe. La cible CMake `SwQuicCore` est donc une cible
`INTERFACE` qui porte seulement les include paths et les dependances de
link, comme `bcrypt` sous Windows.

La contrainte header-only ne doit pas devenir un pretexte pour creer un
gros fichier unique. Les gros fichiers de type `SwQuic.h` ou
`SwHttp3Everything.h` sont a eviter.

## Signalisation SwStack

Les transitions runtime doivent rester dans le modele SwStack :

```cpp
connect(server, &SwQuicServer::newConnection,
        owner, &Owner::onQuicConnection);

connect(connection, &SwQuicConnection::streamOpened,
        http3, &SwHttp3Connection::attachStream);

connect(session, &SwWebTransportSession::datagramReceived,
        handler, &Handler::onDatagram);
```

Le transport emet des evenements. La couche HTTP/3 les interprete. La
couche applicative decide quoi faire. Le transport ne doit pas connaitre
les routes.

## Phasage recommande

Phase 1 : base UDP multiplexee interne

- construire `SwQuicPacketCodec` minimal ;
- construire `SwQuicConnection` sans compatibilite navigateur ;
- valider les timers, ACK, retransmission et streams internes ;
- nommer clairement cette etape comme interne si le wire format n'est pas
  encore compatible QUIC RFC.

Phase 2 : compatibilite QUIC transport

- packet protection ;
- TLS 1.3 handshake pour QUIC ;
- transport parameters ;
- connection IDs ;
- loss recovery conforme ;
- datagram extension.

Phase 3 : HTTP/3

- parser/ecrire les frames HTTP/3 ;
- integrer QPACK ;
- alimenter `SwHttpRouter` avec `SwHttpRequest` ;
- transformer `SwHttpResponse` en frames HTTP/3.

Phase 4 : WebTransport

- route `webTransport(...)` ;
- acceptation de session via HTTP/3 ;
- streams fiables ;
- datagrammes non fiables ;
- gestion Origin / permissions navigateur.

## Anti-patterns a eviter

- Ajouter QUIC directement dans `SwUdpSocket`.
- Reutiliser `SwHttpSession` pour HTTP/3.
- Reutiliser `switchToRawSocket` pour WebTransport.
- Melanger TLS, packet codec, route HTTP et logique applicative dans une
  seule classe.
- Pretendre a la compatibilite navigateur tant que HTTP/3 et WebTransport
  ne sont pas conformes.
- Recoder les primitives crypto bas niveau a la main.

## Validation Cloudflare

Un probe reseau optionnel existe dans :

```text
exemples/72-QuicCloudflareProbe
```

Il construit un paquet QUIC v1 Initial protege, contenant un ClientHello
TLS 1.3 avec ALPN `h3`, puis l'envoie en UDP/443 vers Cloudflare. Il
retire ensuite la protection des paquets Initial serveur et decode les
frames QUIC recues.

Commande de validation :

```powershell
cmake -S . -B build-quic-selftest -DSW_BUILD_exemples_72_QuicCloudflareProbe=ON
cmake --build build-quic-selftest --target QuicCloudflareProbe --config Debug
build-quic-selftest\exemples\72-QuicCloudflareProbe\Debug\QuicCloudflareProbe.exe cloudflare-quic.com cloudflare-quic.com 5000
```

Validation obtenue le 2026-07-05 :

```text
target=cloudflare-quic.com:443 sni=cloudflare-quic.com timeout_ms=8000 initial_bytes=1200
received=1200 from=2606:4700::6812:1b0e:443
unprotected_initial[0]=yes version=0x00000001 packet_number=0 payload_bytes=6
frame[0]=ACK largest=0
received=1200 from=2606:4700::6812:1b0e:443
unprotected_initial[0]=yes version=0x00000001 packet_number=1 payload_bytes=94
frame[0]=CRYPTO offset=0 bytes=90
server_crypto=yes
```

Le meme probe recoit aussi une reponse de `cloudflare.com`.

Portee exacte de cette validation :

- UDP/443 sortant et retour reseau fonctionnent ;
- le paquet Initial respecte la taille minimale de 1200 octets ;
- les secrets Initial QUIC v1, AES-GCM et header protection sont
  acceptes par un serveur Cloudflare reel ;
- le ClientHello QUIC/TLS contient assez d'information pour provoquer une
  reponse serveur ;
- les paquets Initial serveur sont deproteges ;
- les frames ACK et CRYPTO serveur sont decodees.

Ce n'est pas encore une validation HTTP/3 complete : le client ne retire
pas encore la protection des paquets Handshake, ne poursuit pas encore le
handshake TLS, ne valide pas le certificat et n'envoie pas encore de
requete GET HTTP/3.

## Etat actuel de src/core/io/quic

Mise a jour 2026-07-05.

Couche transport bas niveau, valide par `exemples/71-QuicPacketCodecSelfTest`
(20 cas, tous verts) et par `exemples/72-QuicCloudflareProbe` contre un serveur
Cloudflare reel :

- variable integers, connection IDs, entete long Initial, entete court 1-RTT ;
- codec de paquet Initial, y compris paquets coalesces dans un meme datagramme
  via `decodeInitialPacket(..., &consumed, ...)` ;
- jeu de frames QUIC complet (RFC 9000 section 19) en encodage et decodage :
  PADDING, PING, ACK (avec variante ECN), RESET_STREAM, STOP_SENDING, CRYPTO,
  NEW_TOKEN, STREAM, MAX_DATA, MAX_STREAM_DATA, MAX_STREAMS, DATA_BLOCKED,
  STREAM_DATA_BLOCKED, STREAMS_BLOCKED, NEW_CONNECTION_ID, RETIRE_CONNECTION_ID,
  PATH_CHALLENGE, PATH_RESPONSE, CONNECTION_CLOSE, HANDSHAKE_DONE, DATAGRAM ;
- secrets et cles Initial QUIC v1 conformes aux vecteurs RFC 9001 ;
- protection et deprotection des paquets Initial (vecteurs RFC 9001) ;
- protection et deprotection des paquets 1-RTT a entete court (AES-128-GCM,
  header protection), validees par aller-retour ;
- reassemblage de streams, multiplexage, ACK tracker, serveur UDP loopback.

Mise a jour 2026-07-06 : le handshake TLS 1.3 complet fonctionne.

Cryptographie et handshake, chaque brique validee contre ses vecteurs officiels
(`exemples/73-QuicHttp3FoundationsSelfTest`, `exemples/74-QuicHandshakeSelfTest`) :

- X25519 (`SwQuicX25519`) conforme RFC 7748 ;
- schedule de cles TLS 1.3 (`SwTls13KeySchedule`) conforme RFC 8446 / RFC 8448 ;
- messages TLS 1.3 (`SwTls13Messages`) : ClientHello a cle reelle, parsing
  ServerHello / EncryptedExtensions / Certificate / CertificateVerify / Finished ;
- generateur aleatoire cryptographique (`SwQuicRandom`, BCryptGenRandom) ;
- derivation des cles de paquet par niveau (`SwQuicPacketKeys`) depuis un secret
  de trafic TLS, recoupee avec les vecteurs Initial RFC 9001 ;
- protection des paquets Handshake (`SwQuicPacketProtector::unprotectHandshake`) ;
- pilote de handshake client (`SwQuicHandshakeClient`) : Initial -> Handshake ->
  1-RTT, ECDHE reel, verification du MAC Finished serveur, derivation 1-RTT.

Validation reelle (`exemples/75-QuicHandshakeProbe` contre cloudflare-quic.com,
2026-07-06) : handshake TLS 1.3 **complet**. Le client fait un ClientHello a cle
ephemere X25519 reelle, deprotege le ServerHello, calcule l'ECDHE, deprotege les
4 datagrammes du flight Handshake serveur, extrait le certificat (961 octets),
verifie le Finished serveur, et derive des cles 1-RTT. Sortie `HANDSHAKE COMPLETE`.

Mise a jour 2026-07-06 (suite) : transport cable, PKI reelle, session HTTP/3.

Transport de bout en bout dans `SwQuicConnection` (modele sans-IO :
`receiveDatagram` / `buildDatagrams` / `nextTimeoutMs` / `onTimeout`), valide par
`exemples/71-QuicPacketCodecSelfTest` (aller-retour 1-RTT protege, ACK ->
loss recovery, flow control, fermeture propre) :

- espaces de numeros de paquet separes Initial / Handshake / Application, avec
  reconstruction du numero tronque (RFC 9000 annexe A.3, `expandPacketNumber`) ;
- deprotection par niveau branchee dans la reception (Initial/Handshake/1-RTT) ;
- chemin d'envoi reel : trames STREAM depuis un buffer par stream, ACK, PING,
  PATH_RESPONSE (echo automatique de PATH_CHALLENGE), MAX_DATA / MAX_STREAM_DATA
  a l'ouverture de fenetre, RESET_STREAM sur STOP_SENDING, CONNECTION_CLOSE ;
- loss recovery, NewReno et flow control connexion+stream **cables** (plus en
  isolation) ; ack_delay applique avec l'exposant du pair ; retransmission des
  trames perdues ;
- timers PTO (backoff exponentiel), idle timeout et ACK delay via
  `nextTimeoutMs` / `onTimeout` ;
- `SwQuicServer` route par connection ID (et non plus par 4-uple UDP) et purge
  l'`AckTracker` ;
- transport parameters (`SwQuicTransportParameters`, RFC 9000 s.18 + DATAGRAM
  RFC 9221) encodes/decodes et appliques aux limites de flow control.

Handshake client complet avec **authentification serveur reelle**
(`exemples/75-QuicHandshakeProbe` contre cloudflare-quic.com, 2026-07-06,
`server_authenticated=yes`) :

- reassemblage CRYPTO hors-ordre (via `SwQuicStream`) : tient sur reseau reel ;
- EncryptedExtensions parses : ALPN `h3` + transport parameters du serveur
  appliques ;
- validation X.509 via CNG/CryptoAPI (`SwQuicCertificateVerifier`) : chaine
  jusqu'a une racine de confiance Windows, correspondance du hostname (SNI), et
  verification de la signature CertificateVerify (Ed25519, RSA-PSS et ECDSA
  P-256/384 ; OpenSSL fournit Ed25519/RSA-PSS sous Windows) ;
- resultat live Cloudflare : chaine de 3 certificats validee, ALPN `h3`,
  transport params (initial_max_data=10 Mo, idle 180 s), cles 1-RTT.

Couche session HTTP/3 (`SwHttp3Server`, RFC 9114) validee de bout en bout par
`exemples/76-Http3ServerSelfTest` par-dessus deux `SwQuicConnection` en loopback :

- ouverture du control stream serveur + SETTINGS, demux des streams
  unidirectionnels (control / push / QPACK encoder-decoder) ;
- stream de requete client -> HEADERS (QPACK statique) + DATA -> `SwHttpRequest`
  passe a un handler -> `SwHttpResponse` reencode en HEADERS + DATA ;
- WebTransport Extended CONNECT (RFC 9220) accepte via `SwHttp3Server`, framing
  session/streams/datagrammes RFC 9297 par `SwWebTransportSession`.

Briques HTTP/3 (RFC 9114 / RFC 9204 / RFC 7541) : `SwHttp3FrameCodec`,
`SwQpackStaticTable`, `SwHpackHuffman`, `SwQpackEncoder`, `SwQpackDecoder`,
`SwHttp3Connection` (helper client sans etat), `SwHttp3Server` (session serveur).

Mise a jour 2026-07-06 (suite) : handshake serveur, migration, durcissement RFC.

Handshake **cote serveur** (`SwQuicHandshakeServer` + `SwQuicServerCredential`)
ecrit et valide de bout en bout par `exemples/77-QuicHandshakeLoopbackSelfTest`
(les deux vrais drivers client<->serveur, sans echafaudage) :

- parsing du ClientHello (`SwTls13Messages::parseClientHello`) : random,
  session_id, cipher suites, key_share x25519, ALPN, transport parameters ;
- ECDHE, ServerHello, EncryptedExtensions (ALPN h3 + transport params serveur
  dont original_destination_connection_id et initial_source_connection_id) ;
- Certificate + CertificateVerify **reellement signe** : credential ECDSA P-256
  auto-signe genere via CNG (`SwQuicEcdsaCredential::createSelfSigned`), cert
  X.509 DER construit a la main, signature verifiee par le client ;
- Finished serveur, verification du Finished client, cles handshake et 1-RTT
  identiques des deux cotes.

Migration de connexion (RFC 9000 section 9), validee par un test loopback
Wi-Fi -> cellulaire dans `exemples/71` :

- routage par connection ID (deja en place) + detection du changement d'adresse
  dans `SwQuicServer` ;
- validation de chemin : `SwQuicConnection::onPeerAddressChanged` emet un
  PATH_CHALLENGE, `SwQuicConnection` repond au PATH_CHALLENGE et valide sur le
  PATH_RESPONSE correspondant ;
- limite d'anti-amplification 3x sur le chemin non valide (RFC 9000 8.1/9.3) ;
- reset du controleur de congestion et du RTT a la validation (RFC 9000 9.4) ;
- emission de NEW_CONNECTION_ID (`issueNewConnectionId`).

Durcissement issu d'une revue adversariale multi-agents (16 defauts confirmes,
14 corriges) :

- PTO : les probes ne sont plus bloquees par la congestion (RFC 9002 6.2.4), et
  max_ack_delay n'est inclus que pour l'espace Application (6.2.1) ;
- ACK Delay reel mesure (fin du delai fixe a max_ack_delay) ;
- RESET_STREAM sur STOP_SENDING declare la final size reellement envoyee (4.5) ;
- paquets indechiffrables : discard silencieux au lieu d'echouer le datagramme
  (RFC 9000 12.2), les paquets coalesces derriere sont toujours traites ;
- HTTP/3 : FIN nu (requete a moitie fermee) desormais traitee ; les trailers
  n'ecrasent plus les pseudo-headers de requete ;
- serveur : datagrammes Initial < 1200 ignores + limite 3x anti-amplification,
  Initial serveur pad a 1200, session_id > 32 rejete, ALPN client valide,
  quic_transport_parameters exige + verification initial_source_connection_id ;
- client : enforcement de TLS 1.3 via supported_versions et d'ALPN h3.

Mise a jour 2026-07-10 : les deux points de durcissement ci-dessus sont
implementes (ACK du Finished client, `HANDSHAKE_DONE`, fragmentation du flight
certificat). `SwQuicHttp3Server` pilote maintenant le handshake et les sessions
sur une vraie socket UDP, avec tickets de reprise et 0-RTT lies au SNI. Une
requete forcee en QUIC depuis Chrome 149 a atteint `SwHttpApp` et a retourne
`HTTP/3`, ce qui valide l'interoperabilite navigateur du chemin HTTP/3 courant.
Les reponses `body`, les listes de chunks et les fichiers sont emises
progressivement sous forme de frames DATA bornees (jusqu'a 64 Kio), avec
reprise apres ACK et ordonnancement equitable entre les streams. Le self-test
`HttpAppHttp3SelfTest` valide notamment un fichier de plus de 17 Mio sans le
materialiser dans le writer HTTP/3.

Ce qui reste hors de cette compatibilite HTTP/3 de base :

- QPACK dynamique et ses streams d'instructions (la table statique utilisee
  ici est interoperable) ;
- Retry cote serveur et key update 1-RTT ;
- agilite de suites (ChaCha20-Poly1305, AES-256) ; seul AES-128-GCM est gere ;
- validation navigateur WebTransport separee : le GET HTTP/3 Chrome ne vaut
  pas validation complete d'une session WebTransport.

## References standards

- QUIC transport : RFC 9000.
- TLS dans QUIC : RFC 9001.
- QUIC loss detection/congestion : RFC 9002.
- HTTP/3 : RFC 9114.
- QUIC DATAGRAM : RFC 9221.
- WebTransport over HTTP/3 : draft IETF WEBTRANS.
