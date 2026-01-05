#ifndef DEBUGFLOWMANAGER_H
#define DEBUGFLOWMANAGER_H


#include <QObject>
#include <QEventLoop>
#include <QDebug>
#include <QMap>
#include <QSet>

class DebugFlowManager : public QObject {
    Q_OBJECT

public:
    DebugFlowManager(QObject *parent = nullptr) : QObject(parent) {}

    // Appelée pour démarrer un nœud donné (peut être appelée plusieurs fois pour gérer les forks)
    void start(int nodeId) {
        if (!ongoingNodes.contains(currentBranch)) {
            ongoingNodes[currentBranch] = QSet<int>();
        }
        ongoingNodes[currentBranch].insert(nodeId);
    }

    // Appelée pour attendre à un nœud donné
    void wait(int nodeId) {
        currentNode = nodeId;
        emit nodeWaiting(nodeId);
        eventLoop.exec();
    }

    // Vérifie si un nœud est en cours de traitement dans la branche actuelle
    bool isOngoing(int nodeId) const {
        foreach (QSet<int> nodeList, ongoingNodes) {
            if(nodeList.contains(nodeId)){
                return true;
            }
        }
        return false;
    }

public slots:
    // Slot pour reprendre le flux après un point d'attente
    void resume(int nodeId) {
        if (currentNode == nodeId) {
            eventLoop.quit();
            emit nodeResumed(nodeId);
            ongoingNodes[currentBranch].remove(nodeId);
        } else {
            qWarning() << "Mismatched node ID for resume:" << nodeId;
        }
    }

    // Définir la branche actuelle
    void setBranch(int branchId) {
        currentBranch = branchId;
    }

signals:
    void nodeWaiting(int nodeId);  // Signal émis lorsqu'on attend à un nœud
    void nodeResumed(int nodeId);  // Signal émis lorsqu'on reprend d'un nœud

private:
    QEventLoop eventLoop;
    int currentNode = -1;
    int currentBranch = -1;
    QMap<int, QSet<int>> ongoingNodes;  // Clé : Branch ID, Valeur : Set of ongoing node IDs
};

#endif // DEBUGFLOWMANAGER_H
