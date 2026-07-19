set(SW_AUTH_SOURCES
    SwAuthAccountService.cpp
    SwAuthLoginCompletionService.cpp
    SwAuthMfaService.cpp
    SwAuthSessionService.cpp
    federated/SwFederatedAuthProviderRegistry.cpp
    federated/SwFederatedAuthService.cpp
    federated/SwFederatedAuthTransactionStore.cpp
    federated/SwFederatedIdentityStore.cpp
    federated/SwOidcJwtValidator.cpp
    federated/SwOidcLoginFlow.cpp
    federated/SwOidcProvider.cpp
    federated/SwOidcStaticClientCredential.cpp
)

add_library(SwAuth STATIC ${SW_AUTH_SOURCES})
target_compile_features(SwAuth PUBLIC cxx_std_17)
target_include_directories(SwAuth PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    "${CMAKE_CURRENT_LIST_DIR}/../../core"
    "${CMAKE_CURRENT_LIST_DIR}/../io"
    "${CMAKE_CURRENT_LIST_DIR}/../object"
    "${CMAKE_CURRENT_LIST_DIR}/../remote"
    "${CMAKE_CURRENT_LIST_DIR}/../runtime"
    "${CMAKE_CURRENT_LIST_DIR}/../fs"
    "${CMAKE_CURRENT_LIST_DIR}/../hw"
    "${CMAKE_CURRENT_LIST_DIR}/../platform"
    "${CMAKE_CURRENT_LIST_DIR}/../storage"
    "${CMAKE_CURRENT_LIST_DIR}/../types"
)
if(DEFINED SW_AUTH_OPENSSL_INCLUDE AND NOT SW_AUTH_OPENSSL_INCLUDE STREQUAL "")
    target_include_directories(SwAuth PUBLIC "${SW_AUTH_OPENSSL_INCLUDE}")
endif()
if(DEFINED SW_AUTH_CRYPTO_LIBRARY AND NOT SW_AUTH_CRYPTO_LIBRARY STREQUAL "")
    target_link_libraries(SwAuth PUBLIC ${SW_AUTH_CRYPTO_LIBRARY})
elseif(TARGET OpenSSL::Crypto)
    target_link_libraries(SwAuth PUBLIC OpenSSL::Crypto)
else()
    find_package(OpenSSL REQUIRED)
    target_link_libraries(SwAuth PUBLIC OpenSSL::Crypto)
endif()
