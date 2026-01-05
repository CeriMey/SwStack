# WhatsApp (fireBD) - Setup Firebase RTDB

Cette demo utilise uniquement **Realtime Database** via l'API REST (`.json`). Tu n'as **pas besoin** de creer une "application" (Android/iOS/Web) dans Firebase Console pour que ca marche.

Important securite : ne mets jamais une cle **service account** (JSON avec `private_key`) dans un repo ou dans un exe distribue. Si tu l'as deja partagee, considere-la compromise et **regenere/rotate** la cle dans Firebase Console.

## 1) Creer la Realtime Database

Dans Firebase Console :

- **Build -> Realtime Database -> Create database**
- Choisis une region (ex: `europe-west1`)
- Pour un premier test : **Start in test mode**

Regles minimales (temporaire, demo uniquement) :

```json
{
  "rules": {
    ".read": true,
    ".write": true
  }
}
```

## 2) Recuperer l'URL RTDB

Realtime Database -> onglet **Data** : l'URL en haut doit ressembler a :

- `https://<project>-default-rtdb.firebaseio.com`
- ou `https://<project>-default-rtdb.<region>.firebasedatabase.app`

Dans le code, l'URL par defaut est hardcodee ici :

- `exemples/32-WhatsApp/WhatsAppWidget.cpp` (`kWaHardcodedFireBdUrl`)

Si ton URL est differente :

- soit tu recompiles en changeant `kWaHardcodedFireBdUrl`
- soit tu overrides en dev via la ligne de commande :
  - `WhatsApp.exe --firebd-url <DB_URL>`

## 3) Lancer l'app

Lance :

- `build-win/exemples/32-WhatsApp/Release/WhatsApp.exe`

Dans l'app :

- **Creer un compte** : nom, prenom, pseudo, telephone, mot de passe x2
  - pseudo : uniquement lettres/chiffres/`_`/`-`
  - telephone : au moins 6 chiffres
- Puis **se connecter**

La session est memorisee via `SwSettings` et est supprimee quand tu te deconnectes.

## 4) Tester a 2 sur une seule machine

Utilise `--profile` pour isoler `SwSettings` + base locale (2 sessions differentes) :

- `WhatsApp.exe --profile alice`
- `WhatsApp.exe --profile bob`

Ensuite, cree 2 comptes (2 numeros differents) et envoie des messages de l'un a l'autre.

## 5) Erreur "Permission denied"

Tes rules RTDB sont fermees. Mets la RTDB en **test mode** (temporaire) ou utilise un token.

Note : `--firebd-auth <TOKEN>` existe pour le dev, mais evite de mettre un secret dans un exe distribue.

## 6) Script helper

Tu peux utiliser `exemples/32-WhatsApp/run_firebd_demo.ps1` pour lancer deux instances rapidement.

