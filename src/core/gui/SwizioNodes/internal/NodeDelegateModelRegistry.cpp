#include "NodeDelegateModelRegistry.h"

namespace SwizioNodes {

NodeDelegateModelRegistry::NodeDelegateModelRegistry() = default;

NodeDelegateModelRegistry::~NodeDelegateModelRegistry() = default;

void NodeDelegateModelRegistry::mergeWith(const NodeDelegateModelRegistry& other)
{
    for (auto const& entry : other.m_registeredItemCreators) {
        m_registeredItemCreators[entry.first] = entry.second;
    }
    for (auto const& entry : other.m_registeredModelsCategory) {
        m_registeredModelsCategory[entry.first] = entry.second;
    }
    m_categories.insert(other.m_categories.begin(), other.m_categories.end());
}

std::unique_ptr<NodeDelegateModel> NodeDelegateModelRegistry::create(SwString const& modelName, const SwString& context)
{
    auto it = m_registeredItemCreators.find(modelName);
    if (it != m_registeredItemCreators.end()) {
        return it->second(context);
    }
    return nullptr;
}

NodeDelegateModelRegistry::RegisteredModelCreatorsMap const& NodeDelegateModelRegistry::registeredModelCreators() const
{
    return m_registeredItemCreators;
}

NodeDelegateModelRegistry::RegisteredModelsCategoryMap const& NodeDelegateModelRegistry::registeredModelsCategoryAssociation() const
{
    return m_registeredModelsCategory;
}

NodeDelegateModelRegistry::CategoriesSet const& NodeDelegateModelRegistry::categories() const { return m_categories; }

} // namespace SwizioNodes

