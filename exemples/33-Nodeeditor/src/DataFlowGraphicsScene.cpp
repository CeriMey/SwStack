#include "DataFlowGraphicsScene.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "UndoCommands.hpp"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsSceneMoveEvent>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QWidgetAction>

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QtGlobal>

#include "core/fs/SwFileInfo.h"
#include "core/fs/SwStandardPaths.h"
#include "core/io/SwFile.h"
#include "core/types/SwByteArray.h"
#include "core/types/SwString.h"

#include <stdexcept>
#include <utility>

namespace {
SwString toSwString(const QString &value)
{
    return SwString(value.toUtf8());
}

QString toQString(const SwString &value)
{
    const std::string &data = value.toStdString();
    return QString::fromUtf8(data.data(), static_cast<int>(data.size()));
}

SwString defaultFlowDir()
{
    SwString home = SwStandardPaths::writableLocation(SwStandardPaths::HomeLocation);
    if (!home.isEmpty()) {
        return home;
    }
    return SwString(QDir::homePath().toUtf8());
}

SwString ensureFlowExtension(const SwString &path)
{
    if (path.isEmpty()) {
        return path;
    }
    if (path.endsWith("flow", Sw::CaseInsensitive)) {
        return path;
    }
    SwString withExt(path);
    withExt += ".flow";
    return withExt;
}

SwByteArray toSwByteArray(const QByteArray &bytes)
{
    return SwByteArray(bytes.constData(), static_cast<size_t>(bytes.size()));
}

QByteArray toQByteArray(const SwString &text)
{
    const std::string &data = text.toStdString();
    return QByteArray(data.data(), static_cast<int>(data.size()));
}
} // namespace

namespace QtNodes {

DataFlowGraphicsScene::DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent)
    : BasicGraphicsScene(graphModel, parent)
    , _graphModel(graphModel)
{
    connect(&_graphModel,
            &DataFlowGraphModel::inPortDataWasSet,
            [this](NodeId const nodeId, PortType const, PortIndex const) { onNodeUpdated(nodeId); });
}

DataFlowGraphicsScene::DataFlowGraphicsScene(std::shared_ptr<NodeDelegateModelRegistry> registry,
                                             QObject *parent)
    : DataFlowGraphicsScene(*new DataFlowGraphModel(QString(), std::move(registry)), parent)
{
    _graphModelOwned.reset(&_graphModel);
}

std::vector<NodeId> DataFlowGraphicsScene::selectedNodes() const
{
    QList<QGraphicsItem *> graphicsItems = selectedItems();

    std::vector<NodeId> result;
    result.reserve(graphicsItems.size());

    for (QGraphicsItem *item : graphicsItems) {
        auto ngo = qgraphicsitem_cast<NodeGraphicsObject *>(item);

        if (ngo != nullptr) {
            result.push_back(ngo->nodeId());
        }
    }

    return result;
}

void DataFlowGraphicsScene::dropItem(QPointF const scenePos, QString itemName)
{
    this->undoStack().push(new CreateCommand(this, itemName, scenePos));
}

QMenu *DataFlowGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu *modelMenu = new QMenu();

    // Add filterbox to the context menu
    auto *txtBox = new QLineEdit(modelMenu);
    txtBox->setPlaceholderText(QStringLiteral("Filter"));
    txtBox->setClearButtonEnabled(true);

    auto *txtBoxAction = new QWidgetAction(modelMenu);
    txtBoxAction->setDefaultWidget(txtBox);

    // 1.
    modelMenu->addAction(txtBoxAction);

    // Add result treeview to the context menu
    QTreeWidget *treeView = new QTreeWidget(modelMenu);
    treeView->header()->close();

    auto *treeViewAction = new QWidgetAction(modelMenu);
    treeViewAction->setDefaultWidget(treeView);

    // 2.
    modelMenu->addAction(treeViewAction);

    auto registry = _graphModel.dataModelRegistry();

    for (auto const &cat : registry->categories()) {
        auto item = new QTreeWidgetItem(treeView);
        item->setText(0, cat);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }

    for (auto const &assoc : registry->registeredModelsCategoryAssociation()) {
        QList<QTreeWidgetItem *> parent = treeView->findItems(assoc.second, Qt::MatchExactly);

        if (parent.count() <= 0)
            continue;

        auto item = new QTreeWidgetItem(parent.first());
        item->setText(0, assoc.first);
    }

    treeView->expandAll();

    connect(treeView,
            &QTreeWidget::itemClicked,
            [this, modelMenu, scenePos](QTreeWidgetItem *item, int) {
                if (!(item->flags() & (Qt::ItemIsSelectable))) {
                    return;
                }

                this->undoStack().push(new CreateCommand(this, item->text(0), scenePos));

                modelMenu->close();
            });

    //Setup filtering
    connect(txtBox, &QLineEdit::textChanged, [treeView](const QString &text) {
        QTreeWidgetItemIterator categoryIt(treeView, QTreeWidgetItemIterator::HasChildren);
        while (*categoryIt)
            (*categoryIt++)->setHidden(true);
        QTreeWidgetItemIterator it(treeView, QTreeWidgetItemIterator::NoChildren);
        while (*it) {
            auto modelName = (*it)->text(0);
            const bool match = (modelName.contains(text, Qt::CaseInsensitive));
            (*it)->setHidden(!match);
            if (match) {
                QTreeWidgetItem *parent = (*it)->parent();
                while (parent) {
                    parent->setHidden(false);
                    parent = parent->parent();
                }
            }
            ++it;
        }
    });

    // make sure the text box gets focus so the user doesn't have to click on it
    txtBox->setFocus();

    // QMenu's instance auto-destruction
    modelMenu->setAttribute(Qt::WA_DeleteOnClose);

    return modelMenu;
}

void DataFlowGraphicsScene::save() const
{
    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    toQString(defaultFlowDir()),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!fileName.isEmpty()) {
        SwString path = ensureFlowExtension(toSwString(fileName));
        SwFile file(path);
        if (file.openBinary(SwFile::Write)) {
            QByteArray const data = QJsonDocument(_graphModel.save()).toJson();
            file.write(toSwByteArray(data));
        }
    }
}

bool DataFlowGraphicsScene::isDurty()
{
    return !undoStack().isClean();
}

void DataFlowGraphicsScene::setAsClean() {
    undoStack().setClean();
}

void DataFlowGraphicsScene::load()
{
    QString fileName = QFileDialog::getOpenFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    toQString(defaultFlowDir()),
                                                    tr("Flow Scene Files (*.flow)"));

    if (fileName.isEmpty())
        return;

    SwString path = toSwString(fileName);
    SwFileInfo info(path.toStdString());
    if (!info.exists())
        return;

    SwFile file(path);
    if (!file.openBinary(SwFile::Read))
        return;

    clearScene();

    SwString const wholeFile = file.readAll();

    _graphModel.load(QJsonDocument::fromJson(toQByteArray(wholeFile)).object());

    Q_EMIT sceneLoaded();
}


QJsonObject DataFlowGraphicsScene::toJson() const
{
    return _graphModel.save();
}

void DataFlowGraphicsScene::fromJson(QJsonObject sceneObject)
{
    clearScene();

    _graphModel.load(sceneObject);

    Q_EMIT sceneLoaded();
}
} // namespace QtNodes
