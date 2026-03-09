# Guide: Create a Custom SwNode Node

This guide explains how to create, configure, build, and deploy a custom node
in the SwNode ecosystem.

---

## 1. Node Architecture

A SwNode node is an **autonomous process** that communicates with other nodes
through IPC (shared memory). Each node is a subclass of `SwRemoteObject` and
exposes:

- **Configs** that can be changed at runtime through IPC (`ipcRegisterConfig`)
- **SHM signals** to publish data to other nodes (`SW_REGISTER_SHM_SIGNAL`)
- **Subscriptions** to receive signals from other nodes

The `main()` entry point is generated automatically by the
`SW_REMOTE_OBJECT_NODE` macro.

---

## 2. File Layout

```
MyProject/
  src/
    MyNode/
      CMakeLists.txt
      MyNode.h
      MyNode.cpp
      MyNodeMain.cpp
      deps.depend          # dependencies to other subprojects
```

### 2.1 Header (`MyNode.h`)

```cpp
#pragma once

#include "SwRemoteObject.h"
#include "SwSharedMemorySignal.h"
#include "SwString.h"

class SwTimer;

class MyNode : public SwRemoteObject {
public:
    MyNode(const SwString& sysName,
           const SwString& nameSpace,
           const SwString& objectName,
           SwObject* parent = nullptr);

private:
    void onTick_();

    // IPC signal: publishes (int seq, SwString message)
    SW_REGISTER_SHM_SIGNAL(heartbeat, int, SwString);

    int periodMs_{1000};
    int seq_{0};
    SwTimer* timer_{nullptr};
};
```

**Key points:**

| Element | Role |
|---------|------|
| `SwRemoteObject` | Required base class for a node |
| `SW_REGISTER_SHM_SIGNAL(name, types...)` | Declares a publishable IPC signal used with `emit name(...)` |
| `ipcRegisterConfig(...)` | Registers a config that can be changed at runtime |

### 2.2 Implementation (`MyNode.cpp`)

```cpp
#include "MyNode.h"

#include "SwDebug.h"
#include "SwTimer.h"

MyNode::MyNode(const SwString& sysName,
               const SwString& nameSpace,
               const SwString& objectName,
               SwObject* parent)
    : SwRemoteObject(sysName, nameSpace, objectName, parent),
      timer_(new SwTimer(this))
{
    // Configs that can be changed through IPC or JSON
    ipcRegisterConfig(int, periodMs_, "period_ms", 1000,
                      [this](const int&) {
                          timer_->stop();
                          timer_->start(periodMs_);
                      });

    // Signal/slot connection
    SwObject::connect(timer_, &SwTimer::timeout, [this]() { onTick_(); });

    // Start
    timer_->start(periodMs_);
}

void MyNode::onTick_() {
    ++seq_;

    // Publish the IPC signal
    const bool ok = emit heartbeat(seq_, SwString("tick"));
    if (!ok) {
        swWarning() << "[MyNode] publish failed";
    }

    if ((seq_ % 10) == 0) {
        swDebug() << "[MyNode] seq=" << seq_;
    }
}
```

### 2.3 Main (`MyNodeMain.cpp`)

```cpp
#include "SwRemoteObjectNode.h"
#include "MyNode.h"

// Automatically generates main() with CLI parsing + JSON config loading
// Args: (NodeType, default_namespace, default_name)
SW_REMOTE_OBJECT_NODE(MyNode, "myapp", "mynode")
```

The `SW_REMOTE_OBJECT_NODE` macro generates a complete `main()` that:

1. Parses CLI arguments (`--sys`, `--ns`, `--name`, `--config_file`, `--duration_ms`)
2. Loads an optional JSON configuration file
3. Instantiates the node with its IPC identifiers
4. Applies JSON params to IPC configs
5. Starts the event loop

### 2.4 Dependencies File (`deps.depend`)

```
../MyUtils
```

Each line is a relative or absolute path to another subproject.
SwBuild builds dependencies **before** the current project.
Comments are supported with `#`.

### 2.5 CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.10)
project(MyNode)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/Zc:preprocessor)
endif()

# Path to the SwStack root
set(CORE_SW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
get_filename_component(CORE_SW_ROOT "${CORE_SW_ROOT}" ABSOLUTE)

add_executable(MyNode
    MyNode.cpp
    MyNodeMain.cpp
)

target_include_directories(MyNode PRIVATE
    ${CORE_SW_ROOT}/src
    ${CORE_SW_ROOT}/src/core
    ${CORE_SW_ROOT}/src/core/runtime
    ${CORE_SW_ROOT}/src/core/object
    ${CORE_SW_ROOT}/src/core/types
    ${CORE_SW_ROOT}/src/core/io
    ${CORE_SW_ROOT}/src/core/fs
    ${CORE_SW_ROOT}/src/core/platform
    ${CORE_SW_ROOT}/src/core/remote
    ${CMAKE_CURRENT_LIST_DIR}
)

if(WIN32)
    target_link_libraries(MyNode PRIVATE crypt32)
endif()

if(NOT WIN32)
    target_link_libraries(MyNode PRIVATE crypto)
endif()
```

**MSVC note:** `/Zc:preprocessor` is required for variadic macros
(`SW_REGISTER_SHM_SIGNAL`, `SW_REMOTE_OBJECT_NODE`).

---

## 3. Receiving Signals From Another Node

To subscribe to a signal published by another node, use
`SwSharedMemorySignal`:

```cpp
#include "SwSharedMemorySignal.h"

// In your node constructor:
auto sig = sw::ipc::openSignal<int, SwString>(
    sysName,       // for example: "pn"
    "pn/capture",  // namespace/object of the source node
    "heartbeat"    // signal name
);

sig->subscribe([this](int seq, const SwString& msg) {
    swDebug() << "Received heartbeat seq=" << seq << " msg=" << msg;
});
```

---

## 4. Exposing RPCs (Remote Method Calls)

A node can expose methods that are callable from other processes through IPC.
On the server side, use `ipcExposeRpc`. On the client side, define a
**proxy object**.

### 4.1 Server Side: Expose RPCs

In your node constructor:

```cpp
class MyNode : public SwRemoteObject {
public:
    MyNode(const SwString& sysName, const SwString& ns, const SwString& name, SwObject* parent = nullptr)
        : SwRemoteObject(sysName, ns, name, parent)
    {
        // Form 1: lambda
        ipcExposeRpc(add, [](int a, int b) -> int {
            return a + b;
        });

        // Form 2: member method
        ipcExposeRpc(greet, this, &MyNode::greet);

        // Form 3: method pointer (the name is inferred automatically)
        ipcExposeRpc(&MyNode::multiply);
    }

    SwString greet(const SwString& who) {
        return SwString("Hello ") + who;
    }

    int multiply(int a, int b) {
        return a * b;
    }
};
```

Supported forms:

| Form | Example |
|------|---------|
| `ipcExposeRpc(name, lambda)` | `ipcExposeRpc(add, [](int a, int b){ return a+b; })` |
| `ipcExposeRpc(name, this, &Class::method)` | `ipcExposeRpc(greet, this, &MyNode::greet)` |
| `ipcExposeRpc(&Class::method)` | `ipcExposeRpc(&MyNode::multiply)` |
| `ipcExposeRpcStr("custom/path", lambda)` | Method name with special characters |

---

## 5. Proxy Object: Call a Remote Node Like a Local Object

The **proxy object** is the client-side counterpart of a `SwRemoteObject`.
It does not start the server node and it does not perform discovery by itself.
Its role is to turn IPC RPC calls into regular C++ methods.

To build a proxy, you need:

- `domain`: the remote node `sysName`
- `object`: `"<nameSpace>/<objectName>"`
- `clientInfo`: a free-form label visible on the server side through `sw::ipc::RpcContext::clientInfo`

### 5.1 Define the Business Interface Proxy

```cpp
#include "SwProxyObject.h"

SW_PROXY_OBJECT_CLASS_BEGIN(MyNodeProxy)
    SW_PROXY_OBJECT_RPC(int, add, int, int)
    SW_PROXY_OBJECT_RPC(SwString, greet, SwString)
    SW_PROXY_OBJECT_RPC(int, multiply, int, int)
SW_PROXY_OBJECT_CLASS_END()
```

The purpose of the proxy class is to reconstruct the **business interface** of
the remote object on the client side.

Instead of scattering raw `RpcMethodClient` calls throughout the codebase, you
define the remote contract once and get:

- typed C++ methods
- a stable local API for callers
- per-RPC error accessors such as `addLastError()`
- interface discovery and validation helpers such as `candidates()`,
  `matchesInterface()`, `missingFunctions()`, and `extraFunctions()`

In practice, use the proxy for the RPCs that define what the remote node
**does** from the application's point of view: business actions, device
commands, queries, and service operations.

Keep the proxy focused on that business contract.
Generic runtime/control RPCs such as `system/saveAsFactory` or
`system/resetFactory` are usually better handled separately when needed,
because they belong to configuration and lifecycle management rather than to
the core object API.

### 5.2 Use the Proxy

```cpp
MyNodeProxy proxy("myapp",        // sysName
                  "myapp/mynode", // "<nameSpace>/<objectName>"
                  "myClient");    // visible on the server side in RpcContext

// Check that the target exposes the expected interface
if (!proxy.matchesInterface()) {
    swWarning() << "Target incompatible: " << proxy.target();
}

// Blocking calls (default timeout: 2000 ms)
int sum = proxy.add(3, 4);
SwString msg = proxy.greet("World");

// Each RPC also exposes its own error accessor
SwString addErr = proxy.addLastError();

// Async call: the callback is invoked only on success
proxy.multiply(6, 7, [](const int& result) {
    swDebug() << "async result: " << result;
});

// Remote interface introspection
SwStringList expected = proxy.interfaceFunctions();
SwStringList remote = proxy.functions();
SwStringList expectedGreetArgs = proxy.interfaceArgType("greet");
SwStringList remoteGreetArgs = proxy.argType("greet");
SwStringList missing = proxy.missingFunctions();
SwStringList extra = proxy.extraFunctions();

// Remote process presence
if (proxy.isAlive()) {
    swDebug() << "Remote PID: " << proxy.remotePid();
}
```

### 5.3 What the Proxy Generates Automatically

| Method | Role |
|--------|------|
| `ClassName::candidates(domain, requireTypeMatch)` | Returns compatible nodes found in the IPC registry |
| `matchesInterface(requireTypeMatch)` | Checks whether the current target exposes all expected RPCs |
| `interfaceFunctions()` | Lists the RPCs declared by the proxy |
| `interfaceArgType(name)` | Returns the argument types expected by the proxy |
| `functions()` | Lists the RPCs exposed by the remote node |
| `argType(name)` | Returns the argument types published by the remote node |
| `missingFunctions()` / `extraFunctions()` | Helps diagnose interface mismatches |
| `remotePid()` / `isAlive()` | Best-effort remote process presence |

### 5.4 Available Macros

| Macro | Description |
|-------|-------------|
| `SW_PROXY_OBJECT_RPC(Ret, name)` | RPC with no arguments, returns `Ret` |
| `SW_PROXY_OBJECT_RPC(Ret, name, T1)` | RPC with 1 argument |
| `SW_PROXY_OBJECT_RPC(Ret, name, T1, T2)` | RPC with 2 arguments |
| `SW_PROXY_OBJECT_RPC(Ret, name, T1, ..., T6)` | Up to 6 arguments |
| `SW_PROXY_OBJECT_VOID(name)` | `void` RPC with no arguments (returns `bool ok`) |
| `SW_PROXY_OBJECT_VOID(name, T1)` | `void` RPC with 1 argument |
| `SW_PROXY_OBJECT_VOID(name, T1, ..., T6)` | Up to 6 arguments |

---

## 6. Discovery and Reactive Factory

In this section, **factory** means a client-side object that creates and
destroys proxy instances based on the nodes currently present in the IPC
registry. This factory does not start any process. It only observes what is
already alive.

### 6.1 Manual Discovery With `candidates()`

Each proxy class generated by `SW_PROXY_OBJECT_CLASS_*` exposes a static
`candidates()` method that returns compatible targets:

```cpp
// Strict matching: RPC name + exact signature
SwStringList strictTargets = MyNodeProxy::candidates("myapp");

// Loose matching: RPC name only, without exact type checking
SwStringList looseTargets = MyNodeProxy::candidates("myapp", false);

for (size_t i = 0; i < strictTargets.size(); ++i) {
    // Format: "<domain>/<nameSpace>/<objectName>"
    swDebug() << "Found: " << strictTargets[i];
}
```

`candidates()` is useful when you want a one-shot snapshot of the system.
If you need a live view, use the reactive factory below.

### 6.2 Reactive Discovery With `SwProxyObjectBrowser`

`SwProxyObjectBrowser<T>` is a **reactive factory** that watches the IPC
registry and emits signals when compatible nodes appear or disappear.

```cpp
#include "SwPointer.h"
#include "SwProxyObjectBrowser.h"

using MyNodeFactory = SwProxyObjectBrowser<MyNodeProxy>;

// Create a factory that watches all MyNodeProxy targets in the "myapp" domain
auto* factory = new MyNodeFactory(
    "myapp",    // IPC domain
    "*",        // filter ("*" = all, "video/*" = video namespace, "*/capture" = capture object)
    "myClient", // client info
    this        // SwObject parent
);

// Emitted when a compatible node appears
SwObject::connect(factory, &MyNodeFactory::remoteAppeared,
    [](SwPointer<MyNodeFactory::Instance> instance) {
        swDebug() << "New node: " << instance->target();
        swDebug() << "  namespace: " << instance->nameSpace();
        swDebug() << "  objectName: " << instance->remoteObjectName();
        swDebug() << "  PID: " << instance->remotePid();

        MyNodeProxy& remote = instance->remote();
        int result = remote.add(1, 2);
        swDebug() << "  add(1,2) = " << result;
    });

// Emitted when a node disappears (process exit, etc.)
SwObject::connect(factory, &MyNodeFactory::remoteDisappeared,
    [](SwPointer<MyNodeFactory::Instance> instance) {
        swWarning() << "Node disappeared: " << instance->target();
    });

// Get the current list
SwList<SwPointer<MyNodeFactory::Instance>> all = factory->instances();
swDebug() << "Active node count: " << all.size();
```

The browser is event-driven: it updates itself from IPC registry events.
`refreshNow()` is still available when you want to force an immediate refresh.

### 6.3 Factory Options

```cpp
// Filter by namespace
factory->setFilter("video/*");    // only the "video" namespace
factory->setFilter("*/capture");  // only objects named "capture"
factory->setFilter("*");          // all targets (default)

// Check that the remote process is alive (OS-level PID check)
factory->setRequireAlive(true);   // default: true

// Check RPC type compatibility (exact signature match)
factory->setRequireTypeMatch(true); // default: true

// Change domain at runtime
factory->setDomain("otherDomain");

// Change the clientInfo injected into newly created proxies
factory->setClientInfo("otherClient");

// Force a manual refresh
factory->refreshNow();

// Pause/resume monitoring
factory->stop();
factory->start();
```

### 6.4 When to Use `candidates()` or the Factory

| Need | Tool |
|------|------|
| Take a one-shot inventory of compatible nodes | `MyNodeProxy::candidates(...)` |
| Maintain a live list and receive events | `SwProxyObjectBrowser<MyNodeProxy>` |
| Call a node you already know | `MyNodeProxy` |

### 6.5 Proxy/Factory Pattern Architecture

```
Process A (server node)            Process B (client)
+--------------------------+       +----------------------------------+
| MyNode : SwRemoteObject  |       | SwProxyObjectBrowser<MyNodeProxy>|
|                          | IPC   |   |                              |
|  ipcExposeRpc(add, ...)  |<----->|   +-> MyNodeProxy::candidates()  |
|  ipcExposeRpc(greet,...) | SHM   |   +-> remoteAppeared signal      |
|  SW_REGISTER_SHM_SIGNAL  |       |   +-> remoteDisappeared signal   |
+--------------------------+       |                                  |
                                   | SwProxyObjectInstance<MyNodeProxy>|
                                   |   +-> remote().add(3, 4)         |
                                   |   +-> remote().greet("World")    |
                                   +----------------------------------+
```

---

## 7. Configuration Factory: Save or Restore Local State

Be careful: in this section, **factory** no longer refers to the browser.
In `SwRemoteObject`, configuration is loaded in layers:

1. `global`
2. `local`: the so-called factory layer
3. `user`: runtime overrides

Every `SwRemoteObject` automatically exposes two system RPCs:

- `system/saveAsFactory`
- `system/resetFactory`

Their behavior is:

| RPC | Effect |
|-----|--------|
| `system/saveAsFactory` | Merges the current `user` layer into the `local` layer and writes the factory file |
| `system/resetFactory` | Deletes the `user` layer and reloads the object from `global + local` |

Important:

- `saveAsFactory()` does **not** delete the user file
- if you want to "commit as factory and then go back to that factory", you will often call `saveAsFactory()` and then `resetFactory()`

### 7.1 Call the Factory With `RpcMethodClient`

```cpp
#include "SwIpcRpc.h"

const SwString sys = "myapp";
const SwString objectFqn = "myapp/mynode";
const SwString clientInfo = "ops";

sw::ipc::RpcMethodClient<bool> saveAsFactory(
    sys, objectFqn, "system/saveAsFactory", clientInfo);

if (!saveAsFactory.call()) {
    swWarning() << "saveAsFactory failed: " << saveAsFactory.lastError();
}

sw::ipc::RpcMethodClient<bool> resetFactory(
    sys, objectFqn, "system/resetFactory", clientInfo);

if (!resetFactory.call()) {
    swWarning() << "resetFactory failed: " << resetFactory.lastError();
}
```

### 7.2 Call the Factory Through `SwApi`

```bash
SwApi node save-as-factory myapp/myapp/mynode
SwApi node reset-factory myapp/myapp/mynode
```

---

## 8. JSON Configuration

### 8.1 `ipcRegisterConfig`

```cpp
ipcRegisterConfig(Type, member_, "config/path", defaultValue, callback);
```

| Parameter | Description |
|-----------|-------------|
| `Type` | C++ type (`int`, `bool`, `SwString`, `double`) |
| `member_` | Member variable to update |
| `"config/path"` | Config path in the JSON tree |
| `defaultValue` | Default value |
| `callback` | Lambda called when the config changes |

### 8.2 JSON Params File

The JSON file passed through `--config_file` or via SwLaunch:

```json
{
  "params": {
    "period_ms": 500
  },
  "options": {
    "reloadOnCrash": true,
    "disconnectTimeoutMs": 5000
  }
}
```

Keys under `"params"` are applied to the node IPC configs.
Keys under `"options"` control SwLaunch behavior.

---

## 9. Build With SwBuild

```bash
# From the project root
SwBuild --root . --scan src
```

SwBuild will:

1. Scan `src/` to find all `CMakeLists.txt`
2. Read `deps.depend` to resolve build order
3. Configure and build each subproject in topological order
4. Install binaries into `install/bin/`

Useful options:

| Option | Description |
|--------|-------------|
| `--clean` | Clean before building |
| `--configure_only` | Generate the CMake project without building |
| `--build_only` | Build without re-configuring |
| `--dry_run` | Print the order without building |
| `--verbose` | Verbose output |

---

## 10. Deploy With SwLaunch

### 10.1 `launch.json` File

```json
{
  "sys": "myapp",
  "duration_ms": 0,
  "nodes": [
    {
      "ns": "myapp",
      "name": "mynode",
      "executable": "install/bin/MyNode",
      "workingDirectory": "install/bin",
      "options": {
        "reloadOnCrash": true,
        "reloadOnDisconnect": true,
        "disconnectTimeoutMs": 5000,
        "restartDelayMs": 1000
      },
      "params": {
        "period_ms": 500
      }
    }
  ]
}
```

### 10.2 Launch

```bash
SwLaunch --config_file myapp.launch.json
```

SwLaunch starts each node as a child process and supervises it:

- **reloadOnCrash**: restart the node if the process crashes (`exit code != 0`)
- **reloadOnDisconnect**: restart if the node disappears from the IPC registry
- **disconnectTimeoutMs**: delay before considering a node offline
- **duration_ms**: execution time (`0` = infinite)

---

## 11. Inspect With SwApi and SwBridge

### 11.1 CLI (`SwApi`)

```bash
# List active IPC domains
SwApi apps

# List nodes in a domain
SwApi nodes --domain myapp

# Ping a node
SwApi ping --target myapp/myapp/mynode

# Inspect signals
SwApi signals --domain myapp

# Read a config
SwApi config --target myapp/myapp/mynode --path period_ms

# Change a config at runtime
SwApi config --target myapp/myapp/mynode --path period_ms --value 200
```

### 11.2 Web Dashboard (`SwBridge`)

```bash
SwBridge --port 9000 --api-key mysecretkey
```

Open `http://localhost:9000/home` in a browser to:

- View nodes in real time
- Edit configs
- Subscribe to signals
- Send pings

---

## 12. Summary

| Step | Command / File |
|------|----------------|
| Create the node | `MyNode.h`, `MyNode.cpp`, `MyNodeMain.cpp` |
| Declare dependencies | `deps.depend` |
| Configure CMake | `CMakeLists.txt` |
| Build | `SwBuild --root . --scan src` |
| Launch | `SwLaunch --config_file launch.json` |
| Inspect | `SwApi apps` / `SwBridge --port 9000` |
| Test | `SwBuildSelfTest`, `SwApiSelfTest`, `SwLaunchSelfTest` |
