#pragma once

#include "Export.hpp"
#include "NodeDelegateModel.h"

#include "core/types/SwString.h"

#include <functional>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace SwizioNodes {

/// Class uses map for storing models (name, model)
class SWIZIO_NODES_PUBLIC NodeDelegateModelRegistry
{
public:
    using RegistryItemPtr = std::unique_ptr<NodeDelegateModel>;
    using RegistryItemCreator = std::function<RegistryItemPtr(const SwString& context)>;
    using RegisteredModelCreatorsMap = std::unordered_map<SwString, RegistryItemCreator>;
    using RegisteredModelsCategoryMap = std::unordered_map<SwString, SwString>;
    using CategoriesSet = std::set<SwString>;

    NodeDelegateModelRegistry();
    ~NodeDelegateModelRegistry();

    NodeDelegateModelRegistry(NodeDelegateModelRegistry const&) = delete;
    NodeDelegateModelRegistry(NodeDelegateModelRegistry&&) = default;

    NodeDelegateModelRegistry& operator=(NodeDelegateModelRegistry const&) = delete;
    NodeDelegateModelRegistry& operator=(NodeDelegateModelRegistry&&) = default;

public:
    template<typename ModelType>
    void registerModel(RegistryItemCreator creator, SwString const& category = "Nodes")
    {
        SwString const name = computeName<ModelType>(HasStaticMethodName<ModelType>{}, creator);
        if (!m_registeredItemCreators.count(name)) {
            m_registeredItemCreators[name] = std::move(creator);
            m_categories.insert(category);
            m_registeredModelsCategory[name] = category;
        }
    }

    template<typename ModelType>
    void registerModel(SwString const& category = "Nodes")
    {
        RegistryItemCreator creator = [](const SwString& context) { return RegistryItemPtr(new ModelType(context)); };
        registerModel<ModelType>(std::move(creator), category);
    }

    void mergeWith(const NodeDelegateModelRegistry& other);

    std::unique_ptr<NodeDelegateModel> create(SwString const& modelName, const SwString& context);

    RegisteredModelCreatorsMap const& registeredModelCreators() const;

    RegisteredModelsCategoryMap const& registeredModelsCategoryAssociation() const;

    CategoriesSet const& categories() const;

private:
    RegisteredModelsCategoryMap m_registeredModelsCategory;
    CategoriesSet m_categories;
    RegisteredModelCreatorsMap m_registeredItemCreators;

private:
    template<typename T, typename = void>
    struct HasStaticMethodName : std::false_type
    {};

    template<typename T>
    struct HasStaticMethodName<T, typename std::enable_if<std::is_same<decltype(T::Name()), SwString>::value>::type>
        : std::true_type
    {};

    template<typename ModelType>
    static SwString computeName(std::true_type, RegistryItemCreator const&)
    {
        return ModelType::Name();
    }

    template<typename ModelType>
    static SwString computeName(std::false_type, RegistryItemCreator const& creator)
    {
        return creator("")->name();
    }
};

} // namespace SwizioNodes
