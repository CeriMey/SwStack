#pragma once

/**
 * @file src/core/gui/SwizioNodes/internal/NodeDelegateModelRegistry.h
 * @ingroup core_swizio_nodes
 * @brief Declares the public interface exposed by NodeDelegateModelRegistry in the CoreSw
 * node-editor layer.
 *
 * This header belongs to the CoreSw node-editor layer. It contains the graph, geometry, style,
 * and scene infrastructure used by the embedded node editor.
 *
 * Within that layer, this file focuses on the node delegate model registry interface. The
 * declarations exposed here define the stable surface that adjacent code can rely on while the
 * implementation remains free to evolve behind the header.
 *
 * This header mainly contributes module-level utilities, helper declarations, or namespaced types
 * that are consumed by the surrounding subsystem.
 *
 * Model-oriented declarations here define the data contract consumed by views, delegates, or
 * algorithms, with an emphasis on stable roles, ownership, and update flow rather than on
 * presentation details.
 *
 * Most declarations here are extension points or internal contracts that coordinate graph
 * editing, visualization, and interaction.
 *
 */


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

    /**
     * @brief Constructs a `NodeDelegateModelRegistry` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    NodeDelegateModelRegistry();
    /**
     * @brief Destroys the `NodeDelegateModelRegistry` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~NodeDelegateModelRegistry();

    /**
     * @brief Constructs a `NodeDelegateModelRegistry` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    NodeDelegateModelRegistry(NodeDelegateModelRegistry const&) = delete;
    /**
     * @brief Constructs a `NodeDelegateModelRegistry` instance.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    NodeDelegateModelRegistry(NodeDelegateModelRegistry&&) = default;

    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    NodeDelegateModelRegistry& operator=(NodeDelegateModelRegistry const&) = delete;
    /**
     * @brief Performs the `operator=` operation.
     * @return The requested operator =.
     */
    NodeDelegateModelRegistry& operator=(NodeDelegateModelRegistry&&) = default;

public:
    template<typename ModelType>
    /**
     * @brief Performs the `registerModel` operation.
     * @param creator Value passed to the method.
     * @param category Value passed to the method.
     */
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
    /**
     * @brief Performs the `registerModel` operation.
     * @param category Value passed to the method.
     */
    void registerModel(SwString const& category = "Nodes")
    {
        RegistryItemCreator creator = [](const SwString& context) { return RegistryItemPtr(new ModelType(context)); };
        registerModel<ModelType>(std::move(creator), category);
    }

    /**
     * @brief Performs the `mergeWith` operation.
     * @param other Value passed to the method.
     */
    void mergeWith(const NodeDelegateModelRegistry& other);

    /**
     * @brief Creates the requested create.
     * @param modelName Value passed to the method.
     * @param context Value passed to the method.
     * @return The resulting create.
     */
    std::unique_ptr<NodeDelegateModel> create(SwString const& modelName, const SwString& context);

    /**
     * @brief Returns the current registered Model Creators.
     * @return The current registered Model Creators.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    RegisteredModelCreatorsMap const& registeredModelCreators() const;

    /**
     * @brief Returns the current registered Models Category Association.
     * @return The current registered Models Category Association.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    RegisteredModelsCategoryMap const& registeredModelsCategoryAssociation() const;

    /**
     * @brief Returns the current categories.
     * @return The current categories.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
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
    /**
     * @brief Performs the `computeName` operation.
     * @param true_type Value passed to the method.
     * @return The requested compute Name.
     */
    static SwString computeName(std::true_type, RegistryItemCreator const&)
    {
        return ModelType::Name();
    }

    template<typename ModelType>
    /**
     * @brief Performs the `computeName` operation.
     * @param false_type Value passed to the method.
     * @param creator Value passed to the method.
     * @return The requested compute Name.
     */
    static SwString computeName(std::false_type, RegistryItemCreator const& creator)
    {
        return creator("")->name();
    }
};

} // namespace SwizioNodes
