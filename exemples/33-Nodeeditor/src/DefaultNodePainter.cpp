#include "DefaultNodePainter.hpp"

#include <QtCore/QMargins>
#include <QStyleOptionGraphicsItem> // Pour levelOfDetailFromTransform(...)
#include <cmath>

#include "AbstractGraphModel.hpp"
#include "AbstractNodeGeometry.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "NodeGraphicsObject.hpp"
#include "NodeState.hpp"
#include "StyleCollection.hpp"

namespace {

// Calcul du LOD (Level Of Detail) à partir de la transformation actuelle du painter.
// Plus le LOD est faible, plus l'item est "petit" à l'écran (zoom out important).
inline double computeLOD(QPainter *painter)
{
    // Qt fournit une méthode statique pratique :
    // levelOfDetailFromTransform(painter->worldTransform())
    return QStyleOptionGraphicsItem::levelOfDetailFromTransform(painter->worldTransform());
}

} // namespace


namespace QtNodes {

void DefaultNodePainter::paint(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // Sauvegarder l'état initial du painter
    painter->save();

    // 1) Calcul du LOD
    double lod = computeLOD(painter);

    // 2) Si le LOD est vraiment minuscule, on peut ignorer tout le dessin
    //    (évite de surcharger le CPU/GPU lorsque l'item est très dézoomé)
    if (lod < 0.01)
    {
        painter->restore();
        return;
    }

    // 3) Dessin de la forme générale (rectangle + gradient)
    drawNodeRect(painter, ngo);

    // 4) Dessin des ports (en mode minimal si le LOD est faible)
    drawConnectionPoints(painter, ngo);
    drawFilledConnectionPoints(painter, ngo);

    // 5) Si le LOD dépasse un certain seuil, on affiche texte, labels, etc.
    if (lod > 0.2)
    {
        drawNodeCaption(painter, ngo);
        drawEntryLabels(painter, ngo);
    }

    // 6) Le resize handle, souvent un petit cercle en bas à droite
    drawResizeRect(painter, ngo);

    // Restauration de l'état initial
    painter->restore();
}


void DefaultNodePainter::drawNodeRect(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // On peut récupérer le LOD pour ajuster le style
    double lod = computeLOD(painter);


    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QSize size = geometry.size(nodeId);

    // Récupération du style
    static QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    static NodeStyle nodeStyle(json.object());

    // Pour LOD très faible, on peut simplifier en un rectangle uni
    if (lod < 0.05)
    {
        // Couleur unie = on prend le dégradé du style, mais on peut le fixer
        painter->setBrush(nodeStyle.GradientColor0);
        painter->drawRect(0, 0, size.width(), size.height());
        return;
    }

    auto color = ngo.isSelected()
                     ? nodeStyle.SelectedBoundaryColor
                     : nodeStyle.NormalBoundaryColor;

    // Gestion du "hover"
    if (ngo.nodeState().hovered()) {
        QPen p(color, nodeStyle.HoveredPenWidth);
        painter->setPen(p);
    } else {
        QPen p(color, nodeStyle.PenWidth);
        painter->setPen(p);
    }



    // Sinon, dégradé habituel
    QLinearGradient gradient(QPointF(0.0, 0.0), QPointF(2.0, size.height()));
    gradient.setColorAt(0.0, nodeStyle.GradientColor0);
    gradient.setColorAt(0.10, nodeStyle.GradientColor1);
    gradient.setColorAt(0.90, nodeStyle.GradientColor2);
    gradient.setColorAt(1.0, nodeStyle.GradientColor3);

    painter->setBrush(gradient);

    QRectF boundary(0, 0, size.width(), size.height());
    double const radius = 3.0;
    painter->drawRoundedRect(boundary, radius, radius);
}


void DefaultNodePainter::drawConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // On peut ici vérifier le LOD pour éventuellement zapper certains détails
    double lod = computeLOD(painter);
    if (lod < 0.001) {
        // Trop petit => on n'affiche pas les "cercles" de port
        return;
    }

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    // Récupération du style
    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());
    auto const &connectionStyle = StyleCollection::connectionStyle();

    float diameter = nodeStyle.ConnectionPointDiameter;
    auto reducedDiameter = diameter * 0.6;

    for (PortType portType : {PortType::Out, PortType::In})
    {
        size_t const n = model.nodeData(nodeId,
                                        (portType == PortType::Out)
                                            ? NodeRole::OutPortCount
                                            : NodeRole::InPortCount)
                             .toUInt();

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex)
        {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &dataType =
                model.portData(nodeId, portType, portIndex, PortRole::DataType)
                    .value<NodeDataType>();

            double r = 1.0;
            NodeState const &state = ngo.nodeState();

            // Gestion du "ring" de preview pour un drag de connexion en cours
            if (auto const *cgo = state.connectionForReaction())
            {
                PortType requiredPort = cgo->connectionState().requiredPort();
                if (requiredPort == portType)
                {
                    ConnectionId possibleConnectionId =
                        makeCompleteConnectionId(cgo->connectionId(), nodeId, portIndex);

                    bool const possible = model.connectionPossible(possibleConnectionId);

                    auto cp = cgo->sceneTransform().map(cgo->endPoint(requiredPort));
                    cp = ngo.sceneTransform().inverted().map(cp);

                    auto diff = cp - p;
                    double dist = std::sqrt(QPointF::dotProduct(diff, diff));

                    if (possible)
                    {
                        double const thres = 40.0;
                        r = (dist < thres) ? (2.0 - dist / thres) : 1.0;
                    }
                    else
                    {
                        double const thres = 80.0;
                        r = (dist < thres) ? (dist / thres) : 1.0;
                    }
                }
            }

            // Couleur liée au type de data si activé
            if (connectionStyle.useDataDefinedColors()) {
                painter->setBrush(connectionStyle.normalColor(dataType.id));
            } else {
                painter->setBrush(nodeStyle.ConnectionPointColor);
            }

            painter->drawEllipse(p, reducedDiameter * r, reducedDiameter * r);
        }
    }

    // Reset de l’état “connectionForReaction” si besoin
    if (ngo.nodeState().connectionForReaction()) {
        ngo.nodeState().resetConnectionForReaction();
    }
}


void DefaultNodePainter::drawFilledConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo) const
{
    double lod = computeLOD(painter);
    // On peut décider de ne pas dessiner si le LOD est trop faible
    if (lod < 0.05) {
        return;
    }

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    auto diameter = nodeStyle.ConnectionPointDiameter;
    auto const &connectionStyle = StyleCollection::connectionStyle();

    for (PortType portType : {PortType::Out, PortType::In})
    {
        size_t const n = model.nodeData(nodeId,
                                        (portType == PortType::Out)
                                            ? NodeRole::OutPortCount
                                            : NodeRole::InPortCount)
                             .toUInt();

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex)
        {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &connected = model.connections(nodeId, portType, portIndex);
            if (!connected.empty())
            {
                // Couleur définie par type de données ou style par défaut
                auto const &dataType =
                    model.portData(nodeId, portType, portIndex, PortRole::DataType)
                        .value<NodeDataType>();

                if (connectionStyle.useDataDefinedColors())
                {
                    QColor const c = connectionStyle.normalColor(dataType.id);
                    painter->setPen(c);
                    painter->setBrush(c);
                }
                else
                {
                    painter->setPen(nodeStyle.FilledConnectionPointColor);
                    painter->setBrush(nodeStyle.FilledConnectionPointColor);
                }

                painter->drawEllipse(p, diameter * 0.4, diameter * 0.4);
            }
        }
    }
}


void DefaultNodePainter::drawNodeCaption(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // Pour la caption, on peut également skip si le LOD est trop faible
    double lod = computeLOD(painter);
    // Ici on décide qu'en-deçà de 0.2 on ne dessine pas le texte
    if (lod < 0.2) {
        return;
    }

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    if (!model.nodeData(nodeId, NodeRole::CaptionVisible).toBool())
        return;

    QString const name = model.nodeData(nodeId, NodeRole::Caption).toString();

    QFont f = painter->font();
    f.setBold(true);

    QPointF position = geometry.captionPosition(nodeId);

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    painter->setFont(f);
    painter->setPen(nodeStyle.FontColor);
    painter->drawText(position, name);

    f.setBold(false);
    painter->setFont(f);
}


void DefaultNodePainter::drawEntryLabels(QPainter *painter, NodeGraphicsObject &ngo) const
{
    double lod = computeLOD(painter);
    // Si le LOD est trop faible, pas de labels
    if (lod < 0.2) {
        return;
    }

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
    NodeStyle nodeStyle(json.object());

    for (PortType portType : {PortType::Out, PortType::In})
    {
        unsigned int n = model.nodeData<unsigned int>(
            nodeId,
            (portType == PortType::Out)
                ? NodeRole::OutPortCount
                : NodeRole::InPortCount
            );

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex)
        {
            auto const &connected = model.connections(nodeId, portType, portIndex);

            QPointF p = geometry.portTextPosition(nodeId, portType, portIndex);

            if (connected.empty())
                painter->setPen(nodeStyle.FontColorFaded);
            else
                painter->setPen(nodeStyle.FontColor);

            QString s;
            // Affichage de la légende du port
            if (model.portData<bool>(nodeId, portType, portIndex, PortRole::CaptionVisible))
            {
                s = model.portData<QString>(nodeId, portType, portIndex, PortRole::Caption);
            }
            else
            {
                auto portData = model.portData(nodeId, portType, portIndex, PortRole::DataType);
                s = portData.value<NodeDataType>().name;
            }

            painter->drawText(p, s);
        }
    }
}


void DefaultNodePainter::drawResizeRect(QPainter *painter, NodeGraphicsObject &ngo) const
{
    // Pas forcément un gros impact sur les perfs, mais on peut aussi conditionner
    double lod = computeLOD(painter);
    if (lod < 0.05)
    {
        return;
    }

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    if (model.nodeFlags(nodeId) & NodeFlag::Resizable) {
        painter->setBrush(Qt::gray);
        painter->drawEllipse(geometry.resizeHandleRect(nodeId));
    }
}

} // namespace QtNodes
