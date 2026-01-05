#include "NodeDelegateModelRegistry.hpp"

#include <QtCore/QFile>
#include <QtWidgets/QMessageBox>

using QtNodes::NodeDataType;
using QtNodes::NodeDelegateModel;
using QtNodes::NodeDelegateModelRegistry;

std::unique_ptr<NodeDelegateModel> NodeDelegateModelRegistry::create(QString const &modelName, const QString &context)
{
    auto it = _registeredItemCreators.find(modelName);

    if (it != _registeredItemCreators.end()) {
        return it->second(context);
    }

    return nullptr;
}

NodeDelegateModelRegistry::RegisteredModelCreatorsMap const &
NodeDelegateModelRegistry::registeredModelCreators() const
{
    return _registeredItemCreators;
}

NodeDelegateModelRegistry::RegisteredModelsCategoryMap const &
NodeDelegateModelRegistry::registeredModelsCategoryAssociation() const
{
    return _registeredModelsCategory;
}

NodeDelegateModelRegistry::CategoriesSet const &NodeDelegateModelRegistry::categories() const
{
    return _categories;
}
