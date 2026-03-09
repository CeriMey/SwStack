# coreSw - API (condense)

- Doc Doxygen: https://app.swiiz.io/projects/ViaYG9BRiR7ZO2EenP3myzu7Dkiy67cN7wB/index
- Source: `src/`

## Headers -> classes -> methodes (publiques)

### `src/atomic/thread.h` - Lightweight worker thread hosting its own SwCoreApplication event loop.
- `Thread` - Lightweight worker thread hosting its own SwCoreApplication event loop.; methodes: `Thread(...)`, `~Thread(...)`, `start(...)`, `quit(...)`, `wait(...)`, `isRunning(...)`, `application(...)`, `threadId(...)`, `postTask(...)`, `currentThread(...)`, `adoptCurrentThread(...)`, `attachObject(...)`, `detachObject(...)`, `setStartedCallback(...)`, `setFinishedCallback(...)`

### `src/core/linux_fiber.h` - linux_fiber
- `Fiber` - Fiber; methodes: none

### `src/core/platform/SwPlatform.h` - Platform
- `SwFilePlatform` - File Platform; methodes: `~SwFilePlatform(...)`, `isFile(...)`, `copy(...)`, `getFileMetadata(...)`, `setCreationTime(...)`, `setLastWriteDate(...)`, `setLastAccessDate(...)`, `setAllDates(...)`, `writeMetadata(...)`, `readMetadata(...)`
- `SwDirPlatform` - Dir Platform; methodes: `~SwDirPlatform(...)`, `exists(...)`, `isDirectory(...)`, `normalizePath(...)`, `pathSeparator(...)`, `entryList(...)`, `findFiles(...)`, `absolutePath(...)`, `currentPath(...)`, `mkdir(...)`, `removeRecursively(...)`, `copyDirectory(...)`
- `SwFileInfoPlatform` - File Info Platform; methodes: `~SwFileInfoPlatform(...)`, `exists(...)`, `isFile(...)`, `isDir(...)`, `absoluteFilePath(...)`, `size(...)`, `normalizePath(...)`
- `SwStandardLocationProvider` - Standard Location Provider; methodes: `~SwStandardLocationProvider(...)`, `standardLocation(...)`, `convertPath(...)`

### `src/core/platform/SwPlatformPosix.h` - Platform Posix
- `SwPosixFilePlatform` - Posix File Platform; methodes: `isFile(...)`, `copy(...)`, `getFileMetadata(...)`, `setCreationTime(...)`, `setLastWriteDate(...)`, `setLastAccessDate(...)`, `setAllDates(...)`, `writeMetadata(...)`, `readMetadata(...)`
- `SwPosixDirPlatform` - Posix Dir Platform; methodes: `exists(...)`, `isDirectory(...)`, `normalizePath(...)`, `pathSeparator(...)`, `entryList(...)`, `findFiles(...)`, `absolutePath(...)`, `currentPath(...)`, `mkdir(...)`, `removeRecursively(...)`, `copyDirectory(...)`
- `SwPosixFileInfoPlatform` - Posix File Info Platform; methodes: `exists(...)`, `isFile(...)`, `isDir(...)`, `absoluteFilePath(...)`, `size(...)`, `normalizePath(...)`
- `SwPosixStandardLocationProvider` - Posix Standard Location Provider; methodes: `standardLocation(...)`, `convertPath(...)`

### `src/core/platform/SwPlatformSelector.h` - Platform Selector
- (pas de classe); none

### `src/core/platform/SwPlatformWin.h` - Platform Win
- `SwWinFilePlatform` - Win File Platform; methodes: `isFile(...)`, `copy(...)`, `getFileMetadata(...)`, `setCreationTime(...)`, `setLastWriteDate(...)`, `setLastAccessDate(...)`, `setAllDates(...)`, `writeMetadata(...)`, `readMetadata(...)`
- `SwWinDirPlatform` - Win Dir Platform; methodes: `exists(...)`, `isDirectory(...)`, `normalizePath(...)`, `pathSeparator(...)`, `entryList(...)`, `findFiles(...)`, `absolutePath(...)`, `currentPath(...)`, `mkdir(...)`, `removeRecursively(...)`, `copyDirectory(...)`
- `SwWinFileInfoPlatform` - Win File Info Platform; methodes: `exists(...)`, `isFile(...)`, `isDir(...)`, `absoluteFilePath(...)`, `size(...)`, `normalizePath(...)`
- `SwWinStandardLocationProvider` - Win Standard Location Provider; methodes: `standardLocation(...)`, `convertPath(...)`

### `src/core/StyleSheet.h` - Style Sheet
- `StyleSheet` - Style Sheet; methodes: `StyleSheet(...)`, `parseStyleSheet(...)`, `getStyleProperty(...)`, `parseColor(...)`

### `src/core/Sw.h` - Sw
- `SwFlagSet` - Flag Set; methodes: `SwFlagSet(...)`, `setFlag(...)`, `testFlag(...)`, `raw(...)`, `operator Underlying(...)`
- `SwSize` - Size; methodes: none
- `SwColor` - Color; methodes: none
- `SwRect` - Rect; methodes: none
- `EntryType` - Entry Type; methodes: none
- `CursorType` - Cursor Type; methodes: none
- `FocusPolicyEnum` - Focus Policy Enum; methodes: none
- `EchoModeEnum` - Echo Mode Enum; methodes: none

### `src/core/SwAbstractSlider.h` - Abstract Slider
- `SwAbstractSlider` - Abstract Slider; methodes: `SwAbstractSlider(...)`, `setRange(...)`, `setMinimum(...)`, `setMaximum(...)`, `minimum(...)`, `maximum(...)`, `setValue(...)`, `value(...)`, `setStep(...)`, `step(...)`, `setBufferedValue(...)`, `bufferedValue(...)`, `setAccentColor(...)`, `setTrackColors(...)`, `setBufferedColor(...)`, `setHandleColors(...)`, `orientation(...)`; signaux: `valueChanged(...)`
- `Orientation` - Orientation; methodes: none
- `SwHorizontalSlider` - Horizontal Slider; methodes: `SwHorizontalSlider(...)`
- `SwVerticalSlider` - Vertical Slider; methodes: `SwVerticalSlider(...)`

### `src/core/SwAbstractSocket.h` - Abstract base class for socket-based communication.
- `SwAbstractSocket` - Abstract base class for socket-based communication.; methodes: `SwAbstractSocket(...)`, `~SwAbstractSocket(...)`, `connectToHost(...)`, `waitForConnected(...)`, `waitForBytesWritten(...)`, `close(...)`, `read(...)`, `write(...)`, `isOpen(...)`, `state(...)`; signaux: `connected()`, `disconnected()`, `errorOccurred(...)`, `writeFinished()`

### `src/core/SwAny.h` - Any
- `SwAny` - Any; methodes: `SwAny(...)`, `setTypeName(...)`, `~SwAny(...)`, `registerConversion(...)`, `canConvert(...)`, `getTypeName(...)`, `convert(...)`, `isSerializable(...)`, `registerStringSerialization(...)`, `isMyTypeRegistered(...)`, `isMetaTypeRegistered(...)`, `typeName(...)`, `fromVoidPtr(...)`, `data(...)`, `copyFrom(...)`, `moveFrom(...)`, `store(...)`, `clear(...)`, `void(...)`, `getDynamicDataMap(...)`, `getConversionRules(...)`, `toBool(...)`, `toInt(...)`, `toFloat(...)`, `toDouble(...)`, `toUInt(...)`, `toByteArray(...)`, `toString(...)`, `toJsonValue(...)`, `toJsonObject(...)`, `toJsonArray(...)`, `metaType(...)`, `typeId(...)`

### `src/core/SwBackendSsl.h` - /**
- `SwBackendSsl` - /**; methodes: `SwBackendSsl(...)`, `~SwBackendSsl(...)`, `addSearchPath(...)`, `init(...)`, `handshake(...)`, `read(...)`, `write(...)`, `shutdown(...)`, `lastError(...)`
- `IoResult` - Io Result; methodes: none
- `Loader` - Loader; methodes: `load(...)`

### `src/core/SwByteArray.h` - Byte Array
- `SwByteArray` - Byte Array; methodes: `SwByteArray(...)`, `~SwByteArray(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `rbegin(...)`, `rend(...)`, `size(...)`, `length(...)`, `isEmpty(...)`, `isNull(...)`, `capacity(...)`, `constData(...)`, `data(...)`, `toStdString(...)`, `fromStdString(...)`, `at(...)`, `front(...)`, `back(...)`, `clear(...)`, `reserve(...)`, `squeeze(...)`, `resize(...)`, `fill(...)`, `append(...)`, `prepend(...)`, `insert(...)`, `remove(...)`, `replace(...)`, `push_back(...)`, `push_front(...)`, `pop_back(...)`, `pop_front(...)`, `chop(...)`, `truncate(...)`, `chopped(...)`, `truncated(...)`, `left(...)`, `right(...)`, `mid(...)`, `first(...)`, `last(...)`, `repeated(...)`, `reversed(...)`, `trimmed(...)`, `simplified(...)`, `toLower(...)`, `toUpper(...)`, `startsWith(...)`, `endsWith(...)`, `contains(...)`, `indexOf(...)`, `lastIndexOf(...)`, `count(...)`, `split(...)`, `toHex(...)`, `fromHex(...)`, `toBase64(...)`, `fromBase64(...)`, `toLongLong(...)`, `toULongLong(...)`, `toInt(...)`, `toDouble(...)`, `setNum(...)`, `number(...)`, `fromRawData(...)`, `compare(...)`, `swap(...)`

### `src/core/SwCommandLineOption.h` - Command Line Option
- `SwCommandLineOption` - Command Line Option; methodes: `SwCommandLineOption(...)`, `addName(...)`, `getNames(...)`, `setDefaultValue(...)`, `setDefaultValues(...)`, `getDefaultValues(...)`, `isValueRequired(...)`, `getDescription(...)`, `getValueName(...)`

### `src/core/SwCommandLineParser.h` - Command Line Parser
- `SwCommandLineParser` - Command Line Parser; methodes: `SwCommandLineParser(...)`, `setApplicationDescription(...)`, `addOption(...)`, `addHelpOption(...)`, `process(...)`, `isSet(...)`, `value(...)`, `positionalArgumentsList(...)`, `generateHelpText(...)`, `error(...)`

### `src/core/SwCoreApplication.h` - Internal class representing a high-precision timer that executes a callback at specified intervals.
- `_T` - Internal class representing a high-precision timer that executes a callback at specified intervals.; methodes: `_T(...)`, `isReady(...)`, `execute(...)`, `timeUntilReady(...)`
- `SwCoreApplication` - Main application class for managing an event-driven system with fiber-based multitasking.; methodes: `SwCoreApplication(...)`, `~SwCoreApplication(...)`, `instance(...)`, `activeWatchDog(...)`, `desactiveWatchDog(...)`, `getLoadPercentage(...)`, `getLastSecondLoadPercentage(...)`, `postEvent(...)`, `postEventPriority(...)`, `postEventPriorityAndYield(...)`, `addTimer(...)`, `removeTimer(...)`, `exec(...)`, `processEvent(...)`, `hasPendingEvents(...)`, `exit(...)`, `quit(...)`, `defined(...)`, `addWaitFd(...)`, `removeWaitable(...)`, `release(...)`, `generateYieldId(...)`, `yieldFiber(...)`, `unYieldFiber(...)`, `unYieldFiberHighPriority(...)`, `getArgument(...)`, `hasArgument(...)`, `getPositionalArguments(...)`
- `IterationMeasurement` - Iteration Measurement; methodes: none
- `WaitHandleEntry` - Wait Handle Entry; methodes: `WaitHandleEntry(...)`
- `WaitFdEntry` - Wait Fd Entry; methodes: `WaitFdEntry(...)`

### `src/core/SwCrypto.h` - Crypto
- `BcryptWrapper` - Bcrypt Wrapper; methodes: `instance(...)`, `OpenAlgorithmProvider(...)`, `CloseAlgorithmProvider(...)`, `CreateHash(...)`, `HashData(...)`, `FinishHash(...)`, `DestroyHash(...)`, `GetProperty(...)`, `SetProperty(...)`, `GenerateSymmetricKey(...)`, `Encrypt(...)`, `Decrypt(...)`, `DestroyKey(...)`
- `SwCrypto` - Crypto; methodes: `generateHashSHA256(...)`, `generateHashSHA512(...)`, `hashSHA256(...)`, `hashSHA512(...)`, `generateKeyedHashSHA256(...)`, `encryptAES(...)`, `decryptAES(...)`, `base64Encode(...)`, `base64Decode(...)`, `calculateFileChecksum(...)`
- `HashAlgo` - Hash Algo; methodes: none

### `src/core/SwDateTime.h` - Date Time
- `SwDateTime` - Date Time; methodes: `SwDateTime(...)`, `~SwDateTime(...)`, `operator std::time_t(...)`, `setDateTime(...)`, `toTimeT(...)`, `year(...)`, `month(...)`, `day(...)`, `hour(...)`, `minute(...)`, `second(...)`, `toString(...)`, `addDays(...)`, `subtractDays(...)`, `addSeconds(...)`, `subtractSeconds(...)`, `addMinutes(...)`, `subtractMinutes(...)`, `addMonths(...)`, `subtractMonths(...)`, `addYears(...)`, `subtractYears(...)`, `daysInMonth(...)`, `isLeapYear(...)`

### `src/core/SwDebug.h` - Debug
- `SwDebugLevel` - Debug Level; methodes: none
- `SwDebugContext` - Debug Context; methodes: none
- `SwDebug` - Debug; methodes: `instance(...)`, `setAppName(...)`, `setVersion(...)`, `setRemoteEndpoint(...)`, `logMessage(...)`
- `SwDebugIsIterable` - Debug Is Iterable; methodes: none
- `SwDebugIsIterable` - Debug Is Iterable; methodes: none
- `SwDebugIsStdPairImpl` - Debug Is Std Pair Impl; methodes: none
- `SwDebugIsStdPairImpl` - Debug Is Std Pair Impl; methodes: none
- `SwDebugIsStdPair` - Debug Is Std Pair; methodes: none
- `SwDebugMessage` - Debug Message; methodes: `SwDebugMessage(...)`, `~SwDebugMessage(...)`
- `SwDebugScopedTimer` - Debug Scoped Timer; methodes: `SwDebugScopedTimer(...)`, `~SwDebugScopedTimer(...)`
- `SwDebugScopedTimerUs` - Debug Scoped Timer Us; methodes: `SwDebugScopedTimerUs(...)`, `move(...)`, `~SwDebugScopedTimerUs(...)`
- `SwDebugTimedMessage` - Debug Timed Message; methodes: `SwDebugTimedMessage(...)`, `~SwDebugTimedMessage(...)`

### `src/core/SwDir.h` - Dir
- `SwDir` - Dir; methodes: `SwDir(...)`, `~SwDir(...)`, `exists(...)`, `path(...)`, `normalizePath(...)`, `mkpathAbsolute(...)`, `mkpath(...)`, `setPath(...)`, `entryList(...)`, `absolutePath(...)`, `absoluteFilePath(...)`, `mkdir(...)`, `removeRecursively(...)`, `copy(...)`, `findFiles(...)`, `currentPath(...)`, `dirName(...)`

### `src/core/SwEventLoop.h` - Cooperative loop with a sleep delay.
- `SwEventLoop` - A local event loop mechanism built on top of the SwCoreApplication fiber system.; methodes: `SwEventLoop(...)`, `~SwEventLoop(...)`, `exec(...)`, `swsleep(...)`, `quit(...)`, `installRuntime(...)`, `installSlowRuntime(...)`, `uninstallRuntime(...)`, `exit(...)`, `isRuning(...)`

### `src/core/SwFile.h` - File
- `SwFile` - File; methodes: `SwFile(...)`, `~SwFile(...)`, `setFilePath(...)`, `fileName(...)`, `open(...)`, `close(...)`, `write(...)`, `readAll(...)`, `isOpen(...)`, `getDirectory(...)`, `isFile(...)`, `contains(...)`, `readLine(...)`, `atEnd(...)`, `readChunk(...)`, `seek(...)`, `currentPosition(...)`, `readLinesInRangeLazy(...)`, `copy(...)`, `copyByChunk(...)`, `getFileMetadata(...)`, `setCreationTime(...)`, `setLastWriteDate(...)`, `setLastAccessDate(...)`, `setAllDates(...)`, `fileChecksum(...)`, `writeMetadata(...)`, `readMetadata(...)`; signaux: `fileChanged(...)`

### `src/core/SwFileInfo.h` - File Info
- `SwFileInfo` - File Info; methodes: `SwFileInfo(...)`, `~SwFileInfo(...)`, `exists(...)`, `isFile(...)`, `isDir(...)`, `fileName(...)`, `baseName(...)`, `suffix(...)`, `absoluteFilePath(...)`, `size(...)`

### `src/core/SwFlags.h` - Flags
- (pas de classe); none

### `src/core/SwFont.h` - Font
- `SwFont` - Font; methodes: `SwFont(...)`, `~SwFont(...)`, `setFamily(...)`, `getFamily(...)`, `setPointSize(...)`, `getPointSize(...)`, `setWeight(...)`, `getWeight(...)`, `setItalic(...)`, `isItalic(...)`, `setUnderline(...)`, `isUnderline(...)`, `handle(...)`

### `src/core/SwGuiApplication.h` - Gui Application
- `SwGuiApplication` - Gui Application; methodes: `SwGuiApplication(...)`, `~SwGuiApplication(...)`, `instance(...)`, `exec(...)`, `platformIntegration(...)`

### `src/core/SwGuiConsoleApplication.h` - Gui Console Application
- `SwGuiConsoleApplication` - Gui Console Application; methodes: `SwGuiConsoleApplication(...)`, `~SwGuiConsoleApplication(...)`, `setTitle(...)`, `setFooter(...)`, `setStatusMessage(...)`, `addEntry(...)`, `showModal(...)`, `exec(...)`
- `Node` - Node; methodes: none

### `src/core/SwHash.h` - Hash
- `SwHash` - Hash; methodes: `SwHash(...)`, `value(...)`, `take(...)`, `insert(...)`, `find(...)`, `contains(...)`, `count(...)`, `remove(...)`, `isEmpty(...)`, `size(...)`, `reserve(...)`, `clear(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `constFind(...)`, `beginWrap(...)`, `endWrap(...)`, `keys(...)`, `values(...)`
- `IteratorWrapper` - Iterator Wrapper; methodes: `IteratorWrapper(...)`, `key(...)`, `value(...)`, `base(...)`
- `ConstIteratorWrapper` - Const Iterator Wrapper; methodes: `ConstIteratorWrapper(...)`, `key(...)`, `value(...)`, `base(...)`

### `src/core/SwInteractiveConsoleApplication.h` - Interactive Console Application
- `NonBlockingReader` - Non Blocking Reader; methodes: `NonBlockingReader(...)`, `~NonBlockingReader(...)`, `readLineNonBlocking(...)`
- `SwInteractiveConsoleApplication` - /**; methodes: `SwInteractiveConsoleApplication(...)`, `registerCommand(...)`, `addComment(...)`, `waitForNewValue(...)`, `setValue(...)`, `setSingleLineMode(...)`

### `src/core/SwIODescriptor.h` - IODescriptor
- `SwIODescriptor` - IODescriptor; methodes: `SwIODescriptor(...)`, `~SwIODescriptor(...)`, `waitForEvent(...)`, `read(...)`, `write(...)`, `descriptor(...)`, `setDescriptorName(...)`, `descriptorName(...)`
- `SwIODescriptor` - IODescriptor; methodes: `SwIODescriptor(...)`, `~SwIODescriptor(...)`, `waitForEvent(...)`, `read(...)`, `write(...)`, `descriptor(...)`, `setDescriptorName(...)`, `descriptorName(...)`

### `src/core/SwIODevice.h` - IODevice
- `SwIODevice` - IODevice; methodes: `SwIODevice(...)`, `~SwIODevice(...)`, `open(...)`, `close(...)`, `read(...)`, `write(...)`, `isOpen(...)`, `exists(...)`, `startMonitoring(...)`, `stopMonitoring(...)`; signaux: `readyRead()`, `readyWrite()`

### `SwNode/SwComponentContainer/SwComponentContainer.cpp` - Component Container
- `SwComponentContainer` - Component Container; methodes: `SwComponentContainer(...)`, `~SwComponentContainer(...)`, `loadPlugin(...)`, `listPlugins(...)`, `loadComponent(...)`, `unloadComponent(...)`, `listComponentsJson(...)`, `listComponents(...)`
- `ComponentInfo` - Component Info; methodes: none
- `Wanted` - Wanted; methodes: none

### `src/core/remote/SwRemoteObjectComponent.h` - Remote Object Component Plugin
- `ComponentNode` - Component Node; methodes: none
- `AutoRegister` - Auto Register; methodes: `AutoRegister(...)`

### `src/core/remote/SwRemoteObjectComponentRegistry.h` - Remote Object Component Registry
- `SwRemoteObjectComponentRegistry` - Remote Object Component Registry; methodes: `registerComponent(...)`, `contains(...)`, `entry(...)`, `types(...)`, `setPluginPath(...)`
- `Entry` - Entry; methodes: none

### `src/core/remote/SwIpcNoCopyRingBuffer.h` - Ipc No Copy Ring Buffer
- `NoCopyMetaTypeName_` - No Copy Meta Type Name_; methodes: `name(...)`
- `NoCopyMetaTypeName_` - No Copy Meta Type Name_; methodes: `name(...)`
- `ShmMappingDyn` - Shm Mapping Dyn; methodes: `openExisting(...)`, `openOrCreate(...)`, `destroy(...)`, `~ShmMappingDyn(...)`, `data(...)`, `size(...)`, `name(...)`
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: `NoCopyRingBuffer(...)`, `create(...)`, `open(...)`, `streamsInRegistry(...)`, `isValid(...)`, `capacity(...)`, `maxBytesPerItem(...)`, `lastSeq(...)`, `droppedCount(...)`, `maxConsumers(...)`, `consumerTtlMs(...)`, `shmName(...)`, `notifySignalName(...)`, `registrySignalName(...)`, `consumer(...)`, `beginWrite(...)`, `publishCopy(...)`, `acquire(...)`, `readLatest(...)`, `notifier(...)`
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: none
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: none
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: none
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: `WriteLease(...)`, `~WriteLease(...)`, `isValid(...)`, `seq(...)`, `data(...)`, `capacityBytes(...)`, `meta(...)`, `commit(...)`, `cancel(...)`
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: `ReadLease(...)`, `~ReadLease(...)`, `isValid(...)`, `seq(...)`, `data(...)`, `bytes(...)`, `meta(...)`, `publishTimeUs(...)`
- `NoCopyRingBuffer` - No Copy Ring Buffer; methodes: `Consumer(...)`, `~Consumer(...)`, `isValid(...)`, `cursor(...)`, `keepUp(...)`, `acquire(...)`, `readLatest(...)`, `unregister(...)`

### `src/core/remote/SwProxyObject.h` - Proxy Object
- `RpcRequestSpec` - Rpc Request Spec; methodes: none
- `ProxyObjectBase` - Proxy Object Base; methodes: `ProxyObjectBase(...)`, `domain(...)`, `object(...)`, `clientInfo(...)`, `target(...)`, `remotePid(...)`, `isAlive(...)`, `functions(...)`, `argType(...)`
- `ClassName` - Class Name; methodes: `ClassName(...)`, `candidates(...)`, `matchesInterface(...)`, `interfaceFunctions(...)`, `interfaceArgType(...)`, `missingFunctions(...)`, `extraFunctions(...)`

### `src/core/remote/SwProxyObjectBrowser.h` - Proxy Object Browser
- `SwProxyObjectInstance` - Proxy Object Instance; methodes: `SwProxyObjectInstance(...)`, `remote(...)`, `domain(...)`, `objectFqn(...)`, `target(...)`, `nameSpace(...)`, `remoteObjectName(...)`, `remotePid(...)`
- `SwProxyObjectBrowser` - Proxy Object Browser; methodes: `~SwProxyObjectBrowser(...)`, `domain(...)`, `filter(...)`, `clientInfo(...)`, `isActive(...)`, `setDomain(...)`, `setFilter(...)`, `setClientInfo(...)`, `setRequireTypeMatch(...)`, `requireTypeMatch(...)`, `setRequireAlive(...)`, `requireAlive(...)`, `start(...)`, `stop(...)`, `size(...)`, `instances(...)`, `clear(...)`, `refreshNow(...)`; signaux: `remoteAppeared(...)`, `remoteDisappeared(...)`

### `src/core/remote/SwIpcRpc.h` - Ipc Rpc
- `RpcContext` - Rpc Context; methodes: none
- `RpcMethodClient` - Rpc Method Client; methodes: `RpcMethodClient(...)`, `~RpcMethodClient(...)`, `lastError(...)`, `call(...)`, `callAsync(...)`
- `Waiter` - Waiter; methodes: `wake(...)`
- `Pending` - Pending; methodes: `Pending(...)`
- `RpcMethodClient` - Rpc Method Client; methodes: `RpcMethodClient(...)`, `~RpcMethodClient(...)`, `lastError(...)`, `call(...)`, `callAsync(...)`
- `Waiter` - Waiter; methodes: `wake(...)`
- `Pending` - Pending; methodes: `Pending(...)`

### `src/core/SwJoystick.h` - Joystick
- `SwJoystick` - Joystick; methodes: `SwJoystick(...)`, `setRepeatMode(...)`, `repeatMode(...)`, `setRepeatInterval(...)`, `repeatInterval(...)`, `horizontalValue(...)`, `verticalValue(...)`, `magnitude(...)`, `angleDegrees(...)`; signaux: `moved(...)`, `directionChanged(...)`
- `RepeatMode` - Repeat Mode; methodes: none

### `src/core/SwJsonArray.h` - Represents a JSON array capable of holding multiple SwJsonValue elements.
- `SwJsonArray` - Represents a JSON array capable of holding multiple SwJsonValue elements.; methodes: `SwJsonArray(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `append(...)`, `insert(...)`, `remove(...)`, `size(...)`, `isEmpty(...)`, `toJsonString(...)`, `data(...)`

### `src/core/SwJsonDocument.h` - Default constructor for the SwJsonDocument class.
- `SwJsonDocument` - Json Document; methodes: `SwJsonDocument(...)`, `setObject(...)`, `setArray(...)`, `isObject(...)`, `isArray(...)`, `object(...)`, `array(...)`, `toJsonValue(...)`, `find(...)`, `toJson(...)`, `fromJson(...)`, `loadFromJson(...)`
- `JsonFormat` - Json Format; methodes: none

### `src/core/SwJsonObject.h` - Represents a JSON object with key-value pairs.
- `SwJsonObject` - Represents a JSON object with key-value pairs.; methodes: `SwJsonObject(...)`, `contains(...)`, `insert(...)`, `remove(...)`, `size(...)`, `isEmpty(...)`, `value(...)`, `keys(...)`, `values(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `toJsonString(...)`, `data(...)`
- `Iterator` - Iterator; methodes: `Iterator(...)`, `key(...)`, `value(...)`, `base(...)`
- `ConstIterator` - Const Iterator; methodes: `ConstIterator(...)`, `key(...)`, `value(...)`, `base(...)`

### `src/core/SwJsonValue.h` - Represents a versatile JSON value that can hold different types of data.
- `SwJsonValue` - Represents a versatile JSON value that can hold different types of data.; methodes: `SwJsonValue(...)`, `setObject(...)`, `setArray(...)`, `isNull(...)`, `isBool(...)`, `isInt(...)`, `isDouble(...)`, `isString(...)`, `isObject(...)`, `isArray(...)`, `toBool(...)`, `toInt(...)`, `toLongLong(...)`, `toDouble(...)`, `toString(...)`, `toObject(...)`, `toArray(...)`, `toJsonString(...)`, `escapeString(...)`, `isValid(...)`
- `Type` - Type; methodes: none

### `src/core/SwLabel.h` - Label
- `SwLabel` - Label; methodes: `SwLabel(...)`, `paintEvent(...)`, `mouseMoveEvent(...)`, `mousePressEvent(...)`, `mouseReleaseEvent(...)`, `sizeHint(...)`, `minimumSizeHint(...)`

### `src/core/SwLayout.h` - Layout
- `SwAbstractLayout` - Abstract Layout; methodes: `SwAbstractLayout(...)`, `~SwAbstractLayout(...)`, `setParentWidget(...)`, `parentWidget(...)`, `setSpacing(...)`, `spacing(...)`, `setMargin(...)`, `margin(...)`, `updateGeometry(...)`
- `SwBoxLayout` - Box Layout; methodes: `SwBoxLayout(...)`, `~SwBoxLayout(...)`, `addWidget(...)`, `insertWidget(...)`, `removeWidget(...)`
- `Item` - Item; methodes: none
- `SwVerticalLayout` - Vertical Layout; methodes: `SwVerticalLayout(...)`
- `SwHorizontalLayout` - Horizontal Layout; methodes: `SwHorizontalLayout(...)`
- `SwGridLayout` - Grid Layout; methodes: `SwGridLayout(...)`, `setHorizontalSpacing(...)`, `setVerticalSpacing(...)`, `setColumnStretch(...)`, `setRowStretch(...)`, `addWidget(...)`, `removeWidget(...)`
- `Cell` - Cell; methodes: none

### `src/core/SwLibrary.h` - Library
- `SwLibrary` - Library; methodes: `SwLibrary(...)`, `~SwLibrary(...)`, `isLoaded(...)`, `nativeHandle(...)`, `requestedPath(...)`, `path(...)`, `lastError(...)`, `loadAttempts(...)`, `symbolLookups(...)`, `platformPrefix(...)`, `platformSuffix(...)`, `load(...)`, `unload(...)`, `resolve(...)`, `introspectionJson(...)`
- `LoadAttempt` - Load Attempt; methodes: none
- `SymbolLookup` - Symbol Lookup; methodes: none

### `src/core/SwLineEdit.h` - Line Edit
- `SwLineEdit` - Line Edit; methodes: `SwLineEdit(...)`, `~SwLineEdit(...)`, `paintEvent(...)`, `keyPressEvent(...)`, `mousePressEvent(...)`, `mouseDoubleClickEvent(...)`, `mouseMoveEvent(...)`, `mouseReleaseEvent(...)`
- `Padding` - Padding; methodes: none

### `src/core/SwList.h` - List
- `SwList` - List; methodes: `SwList(...)`, `~SwList(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `append(...)`, `prepend(...)`, `push_back(...)`, `insert(...)`, `removeAt(...)`, `clear(...)`, `deleteAll(...)`, `size(...)`, `isEmpty(...)`, `at(...)`, `value(...)`, `data(...)`, `reverse(...)`, `removeDuplicates(...)`, `hasDuplicates(...)`, `filter(...)`, `reserve(...)`, `capacity(...)`, `toVector(...)`, `join(...)`, `first(...)`, `last(...)`, `firstRef(...)`, `lastRef(...)`, `startsWith(...)`, `endsWith(...)`, `mid(...)`, `swap(...)`, `contains(...)`, `count(...)`, `removeAll(...)`, `removeOne(...)`, `removeFirst(...)`, `removeLast(...)`, `replace(...)`, `indexOf(...)`, `lastIndexOf(...)`

### `src/core/SwMainWindow.h` - Constructs the SwMainWindow and registers it with SwGuiApplication.
- `SwMainWindow` - Main Window; methodes: `SwMainWindow(...)`, `~SwMainWindow(...)`, `show(...)`, `hide(...)`, `showMinimized(...)`, `showMaximized(...)`, `showNormal(...)`, `setWindowState(...)`, `setWindowTitle(...)`, `windowTitle(...)`, `setWindowFlags(...)`, `getWindowFlags(...)`
- `WindowState` - Window State; methodes: none

### `src/core/SwMap.h` - Map
- `SwMap` - Map; methodes: `SwMap(...)`, `value(...)`, `insert(...)`, `remove(...)`, `contains(...)`, `isEmpty(...)`, `size(...)`, `clear(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `find(...)`, `constFind(...)`, `keys(...)`, `values(...)`
- `iterator` - iterator; methodes: `iterator(...)`, `key(...)`, `value(...)`, `base(...)`
- `const_iterator` - const_iterator; methodes: `const_iterator(...)`, `key(...)`, `value(...)`, `base(...)`

### `src/core/SwMediaControlWidget.h` - Media Control Widget
- `SwMediaControlWidget` - Media Control Widget; methodes: `SwMediaControlWidget(...)`, `setDurationSeconds(...)`, `setPositionSeconds(...)`, `setBufferedSeconds(...)`, `durationSeconds(...)`, `positionSeconds(...)`, `bufferedSeconds(...)`, `isPlaying(...)`; signaux: `positionChanged(...)`, `previousRequested()`, `playRequested()`, `pauseRequested()`, `stopRequested()`, `nextRequested()`

### `src/core/SwMetaType.h` - Meta Type
- `SwMetaType` - Meta Type; methodes: `fromName(...)`, `id(...)`

### `src/core/SwMutex.h` - Qt-like mutex wrapper with optional recursion support.
- `SwMutex` - Qt-like mutex wrapper with optional recursion support.; methodes: `SwMutex(...)`, `lock(...)`, `tryLock(...)`, `tryLockFor(...)`, `unlock(...)`, `isRecursive(...)`
- `SwMutexLocker` - RAII helper mirroring Qt's QMutexLocker.; methodes: `SwMutexLocker(...)`, `~SwMutexLocker(...)`, `unlock(...)`, `relock(...)`, `mutex(...)`, `isLocked(...)`

### `src/core/SwNetworkAccessManager.h` - Lightweight asynchronous HTTP/HTTPS client built on top of SwTcpSocket.
- `SwNetworkAccessManager` - Lightweight asynchronous HTTP/HTTPS client built on top of SwTcpSocket.; methodes: `SwNetworkAccessManager(...)`, `~SwNetworkAccessManager(...)`, `responseBody(...)`, `responseBodyAsString(...)`, `responseHeaders(...)`, `setRawHeader(...)`, `get(...)`, `abort(...)`, `onReadyRead(...)`, `onDisconnected(...)`, `onError(...)`, `cleanupSocket(...)`, `resetResponseState(...)`, `buildRequest(...)`, `hostHeaderValue(...)`, `processBuffer(...)`, `parseHeaders(...)`, `finishRequest(...)`, `parseUrl(...)`; signaux: `finished(...)`, `errorOccurred(...)`

### `src/core/SwObject.h` - Object
- `index_sequence` - index_sequence; methodes: none
- `make_index_sequence_impl` - make_index_sequence_impl; methodes: none
- `make_index_sequence_impl` - make_index_sequence_impl; methodes: none
- `ISlot` - ISlot; methodes: `~ISlot(...)`, `invoke(...)`, `receiveur(...)`
- `SlotMember` - Slot Member; methodes: `SlotMember(...)`, `invoke(...)`, `receiveur(...)`
- `SlotFunctionReceiver` - Slot Function Receiver; methodes: `SlotFunctionReceiver(...)`, `invoke(...)`, `receiveur(...)`
- `SlotFunction` - Slot Function; methodes: `SlotFunction(...)`, `invoke(...)`, `receiveur(...)`
- `F` - F; methodes: none
- `R` - R; methodes: none
- `R` - R; methodes: none
- `C` - C; methodes: none
- `C` - C; methodes: none
- `Tuple` - Tuple; methodes: none
- `Tuple` - Tuple; methodes: none
- `Func` - Func; methodes: none
- `ReceiverType` - Receiver Type; methodes: none
- `SwObject` - Object; methodes: `SwObject(...)`, `~SwObject(...)`, `staticClassName(...)`, `className(...)`, `threadHandle(...)`, `thread(...)`, `moveToThread(...)`, `classHierarchy(...)`, `deleteLater(...)`, `setParent(...)`, `parent(...)`, `addChild(...)`, `removeChild(...)`, `getChildren(...)`, `addChildEvent(...)`, `removedChildEvent(...)`, `connect(...)`, `disconnect(...)`, `addConnection(...)`, `sender(...)`, `setSender(...)`, `disconnectAllSlots(...)`, `setProperty(...)`, `property(...)`, `propertyExist(...)`; signaux: `destroyed()`, `childRemoved(...)`, `childAdded(...)`
- `SignalKey` - Signal Key; methodes: `SignalKey(...)`
- `BlockingContext` - Blocking Context; methodes: none

### `src/core/SwPainter.h` - Painter
- `SwPainter` - Painter; methodes: `~SwPainter(...)`, `clear(...)`, `fillRect(...)`, `fillRoundedRect(...)`, `drawRect(...)`, `drawText(...)`, `finalize(...)`, `nativeHandle(...)`

### `src/core/SwPair.h` - Pair
- `SwPair` - Pair; methodes: `SwPair(...)`

### `src/core/SwPinOut.h` - Utility class able to enumerate the GPIO/pin capabilities of the host platform.
- `SwPinOut` - Utility class able to enumerate the GPIO/pin capabilities of the host platform.; methodes: `availablePins(...)`, `dumpAvailablePins(...)`
- `PinInfo` - Pin Info; methodes: none

### `src/core/SwPointer.h` - Pointer
- `SwPointerControl` - Pointer Control; methodes: `SwPointerControl(...)`, `object(...)`, `clear(...)`
- `SwPointer` - Pointer; methodes: `SwPointer(...)`, `swap(...)`, `data(...)`, `get(...)`, `isNull(...)`, `clear(...)`

### `src/core/SwProcess.h` - The SwProcess class provides an interface for managing system processes with I/O redirection.
- `ProcessFlags` - Process Flags; methodes: none
- `ProcessFlags` - Process Flags; methodes: none
- `SwProcess` - The SwProcess class provides an interface for managing system processes with I/O redirection.; methodes: `SwProcess(...)`, `~SwProcess(...)`, `start(...)`, `setProgram(...)`, `program(...)`, `setArguments(...)`, `arguments(...)`, `setWorkingDirectory(...)`, `workingDirectory(...)`, `close(...)`, `isOpen(...)`, `read(...)`, `readStdErr(...)`, `write(...)`, `kill(...)`, `terminate(...)`; signaux: `deviceOpened()`, `deviceClosed()`, `processFinished()`, `processTerminated(...)`

### `src/core/SwPushButton.h` - Push Button
- `SwPushButton` - Push Button; methodes: `SwPushButton(...)`, `paintEvent(...)`, `mouseMoveEvent(...)`, `mousePressEvent(...)`, `mouseReleaseEvent(...)`, `sizeHint(...)`, `minimumSizeHint(...)`; signaux: `clicked()`

### `src/core/SwRegularExpression.h` - Regular Expression
- `SwRegularExpressionMatch` - Regular Expression Match; methodes: `SwRegularExpressionMatch(...)`, `hasMatch(...)`, `captured(...)`, `capturedStart(...)`, `capturedEnd(...)`
- `SwRegularExpression` - Regular Expression; methodes: `SwRegularExpression(...)`, `isValid(...)`, `pattern(...)`, `getStdRegex(...)`, `match(...)`, `globalMatch(...)`

### `src/core/remote/SwRemoteObject.h` - Base SwObject that loads a layered JSON configuration and optionally shares updates
- `SwRemoteObject` - Base SwObject that loads a layered JSON configuration and optionally shares updates; methodes: `~SwRemoteObject(...)`, `sysName(...)`, `nameSpace(...)`, `objectName(...)`, `configRootDirectory(...)`, `setConfigRootDirectory(...)`, `configPaths(...)`, `mergedConfig(...)`, `ipcFullName(...)`, `configValue(...)`, `loadConfig(...)`, `saveLocalConfig(...)`, `saveUserConfig(...)`, `setConfigValue(...)`, `enableSharedMemoryConfig(...)`, `disableSharedMemoryConfig(...)`, `ipcDisconnect(...)`; signaux: `configChanged(...)`, `configLoaded(...)`, `remoteConfigReceived(...)`, `remoteConfigValueReceived(...)`
- `ConfigSavePolicy` - Config Save Policy; methodes: none
- `ConfigPaths` - Config Paths; methodes: none
- `IpcConnection` - Ipc Connection; methodes: `~IpcConnection(...)`, `stop(...)`
- `RegisteredConfigEntry` - Registered Config Entry; methodes: none
- `IIpcSubscription` - IIpc Subscription; methodes: `~IIpcSubscription(...)`, `stop(...)`
- `IpcSubscriptionHolder` - Ipc Subscription Holder; methodes: `IpcSubscriptionHolder(...)`, `stop(...)`
- `IpcConnectHelper_` - Ipc Connect Helper_; methodes: none
- `IpcConnectHelper_` - Ipc Connect Helper_; methodes: none
- `IpcConnectScopedHelper_` - Ipc Connect Scoped Helper_; methodes: none
- `IpcConnectScopedHelper_` - Ipc Connect Scoped Helper_; methodes: none
- `IpcExposeRpcHelper_` - Ipc Expose Rpc Helper_; methodes: none
- `IpcExposeRpcHelper_` - Ipc Expose Rpc Helper_; methodes: none
- `IpcExposeRpcHelper_` - Ipc Expose Rpc Helper_; methodes: none

### `src/core/SwSerial.h` - Serial
- `SwSerial` - Serial; methodes: `SwSerial(...)`, `~SwSerial(...)`, `setPortName(...)`, `portName(...)`, `setBaudRate(...)`, `baudRate(...)`, `open(...)`, `close(...)`, `isOpen(...)`, `read(...)`, `write(...)`, `waitForReadyRead(...)`, `waitForBytesWritten(...)`; signaux: `errorOccurred(...)`
- `termios` - termios; methodes: none

### `src/core/SwSerialInfo.h` - Serial Info
- `SwSerialInfo` - Serial Info; methodes: `SwSerialInfo(...)`, `~SwSerialInfo(...)`, `swap(...)`, `isNull(...)`, `isValid(...)`, `isBusy(...)`, `portName(...)`, `systemLocation(...)`, `description(...)`, `manufacturer(...)`, `serialNumber(...)`, `hasVendorIdentifier(...)`, `vendorIdentifier(...)`, `hasProductIdentifier(...)`, `productIdentifier(...)`, `availablePorts(...)`, `standardBaudRates(...)`

### `src/core/SwSettings.h` - Settings
- `SwSettings` - Settings; methodes: `SwSettings(...)`, `~SwSettings(...)`, `setValue(...)`, `value(...)`, `contains(...)`, `remove(...)`, `clear(...)`, `sync(...)`, `isDirty(...)`, `beginGroup(...)`, `endGroup(...)`, `group(...)`

### `src/core/remote/SwSharedMemorySignal.h` - Shared Memory Signal
- `RegistryEntry` - Registry Entry; methodes: none
- `RegistryLayout` - Registry Layout; methodes: none
- `AppEntry` - App Entry; methodes: none
- `AppLayout` - App Layout; methodes: none
- `SubscriberEntry` - Subscriber Entry; methodes: none
- `SubscribersLayout` - Subscribers Layout; methodes: none
- `PidState` - Pid State; methodes: none
- `AppsRegistryTable` - Apps Registry Table; methodes: `registerDomain(...)`, `unregisterCurrentPid(...)`, `snapshot(...)`
- `Mapping` - Mapping; methodes: `Mapping(...)`, `~Mapping(...)`, `layout(...)`
- `ProcessHooks` - Process Hooks; methodes: `trackDomain(...)`, `ensureHeartbeat(...)`
- `RegistryTable` - Registry Table; methodes: `registerSignal(...)`, `snapshot(...)`
- `Mapping` - Mapping; methodes: `Mapping(...)`, `~Mapping(...)`, `layout(...)`
- `ProcessHooks` - Process Hooks; methodes: `ensureHeartbeat(...)`
- `SubscribersRegistryTable` - Subscribers Registry Table; methodes: `registerSubscription(...)`, `unregisterSubscription(...)`, `snapshot(...)`, `listSubscriberPids(...)`
- `Unlocker` - Unlocker; methodes: `Unlocker(...)`, `~Unlocker(...)`
- `Mapping` - Mapping; methodes: `Mapping(...)`, `~Mapping(...)`, `layout(...)`
- `ProcessHooks` - Process Hooks; methodes: `ensureHeartbeat(...)`
- `Encoder` - Encoder; methodes: `Encoder(...)`, `writeBytes(...)`, `size(...)`
- `T` - T; methodes: none
- `Decoder` - Decoder; methodes: `Decoder(...)`, `readBytes(...)`
- `T` - T; methodes: none
- `Codec` - Codec; methodes: `write(...)`, `read(...)`
- `Codec` - Codec; methodes: `write(...)`, `read(...)`
- `Codec` - Codec; methodes: `write(...)`, `read(...)`
- `LoopPollerDispatchRegistry` - Loop Poller Dispatch Registry; methodes: `table(...)`, `registerFn(...)`, `dispatchAll(...)`, `unregisterFn(...)`
- `Table` - Table; methodes: none
- `LoopPoller` - Loop Poller; methodes: `dispatch(...)`, `notifyProcess(...)`, `add(...)`, `remove(...)`, `instance(...)`
- `Task` - Task; methodes: `Task(...)`
- `IpcWakeup` - Ipc Wakeup; methodes: `instance(...)`, `ensureAttached(...)`, `detach(...)`, `notifyPid(...)`
- `index_sequence` - index_sequence; methodes: none
- `make_index_sequence_impl` - make_index_sequence_impl; methodes: none
- `make_index_sequence_impl` - make_index_sequence_impl; methodes: none
- `make_index_sequence` - make_index_sequence; methodes: none
- `tuple_element_decay` - tuple_element_decay; methodes: none
- `Registry` - Registry; methodes: `Registry(...)`, `domain(...)`, `object(...)`
- `ShmLayout` - Shm Layout; methodes: none
- `ShmMapping` - Shm Mapping; methodes: `openOrCreate(...)`, `destroy(...)`, `~ShmMapping(...)`, `layout(...)`, `name(...)`
- `ShmQueueLayout` - Shm Queue Layout; methodes: `initLayout(...)`
- `Slot` - Slot; methodes: none
- `ShmMappingT` - Shm Mapping T; methodes: `openOrCreate(...)`, `destroy(...)`, `~ShmMappingT(...)`, `layout(...)`, `name(...)`
- `RingQueue` - Ring Queue; methodes: `RingQueue(...)`, `~RingQueue(...)`, `shmName(...)`, `wakeEvent(...)`, `push(...)`, `operator(...)`
- `Subscription` - Subscription; methodes: `Subscription(...)`, `~Subscription(...)`, `stop(...)`
- `State` - State; methodes: `State(...)`, `~State(...)`
- `Msg` - Msg; methodes: none
- `State` - State; methodes: `State(...)`, `~State(...)`
- `Msg` - Msg; methodes: none
- `Signal` - Signal; methodes: `Signal(...)`, `~Signal(...)`, `shmName(...)`, `publish(...)`, `operator(...)`, `readLatest(...)`
- `Subscription` - Subscription; methodes: `Subscription(...)`, `~Subscription(...)`, `stop(...)`
- `State` - State; methodes: `State(...)`, `~State(...)`
- `SignalProxy` - Signal Proxy; methodes: `SignalProxy(...)`, `publish(...)`, `operator(...)`, `readLatest(...)`, `shmName(...)`
- `Notifier` - Notifier; methodes: `Notifier(...)`

### `src/core/SwSlider.h` - Slider
- `SwSlider` - Slider; methodes: `SwSlider(...)`, `setOrientation(...)`, `orientation(...)`, `setRange(...)`, `setMinimum(...)`, `setMaximum(...)`, `minimum(...)`, `maximum(...)`, `setStep(...)`, `step(...)`, `setValue(...)`, `value(...)`; signaux: `valueChanged(...)`
- `Orientation` - Orientation; methodes: none

### `src/core/SwStandardLocation.h` - Standard Location
- `SwStandardLocation` - Standard Location; methodes: `standardLocation(...)`, `convertPath(...)`

### `src/core/SwStandardLocationDefs.h` - Standard Location Defs
- `SwStandardPathType` - Standard Path Type; methodes: none
- `SwStandardLocationId` - Standard Location Id; methodes: none

### `src/core/SwStandardPaths.h` - Standard Paths
- `SwStandardPaths` - Standard Paths; methodes: `writableLocation(...)`, `writableLocations(...)`, `standardLocations(...)`, `displayName(...)`, `setTestModeEnabled(...)`, `isTestModeEnabled(...)`, `findExecutable(...)`, `locate(...)`, `locateAll(...)`

### `src/core/SwString.h` - String
- `SwString` - String; methodes: `SwString(...)`, `operator std::string(...)`, `size(...)`, `length(...)`, `isEmpty(...)`, `clear(...)`, `isInt(...)`, `isFloat(...)`, `toStdString(...)`, `reserve(...)`, `toInt(...)`, `toFloat(...)`, `toDouble(...)`, `number(...)`, `toBase64(...)`, `deBase64(...)`, `fromBase64(...)`, `encryptAES(...)`, `decryptAES(...)`, `split(...)`, `contains(...)`, `reversed(...)`, `startsWith(...)`, `endsWith(...)`, `compare(...)`, `indexOf(...)`, `lastIndexOf(...)`, `firstIndexOf(...)`, `trimmed(...)`, `toUpper(...)`, `toLower(...)`, `fromWString(...)`, `fromWCharArray(...)`, `replace(...)`, `remove(...)`, `arg(...)`, `count(...)`, `simplified(...)`, `mid(...)`, `left(...)`, `right(...)`, `first(...)`, `last(...)`, `append(...)`, `prepend(...)`, `substr(...)`, `erase(...)`, `insert(...)`, `toUtf8(...)`, `toWChar(...)`, `toStdWString(...)`, `toLatin1(...)`, `fromLatin1(...)`, `fromUtf8(...)`, `resize(...)`, `data(...)`, `begin(...)`, `end(...)`, `utf16Size(...)`, `utf32Size(...)`, `chop(...)`
- `hash` - hash; methodes: `operator(...)`

### `src/core/SwStyle.h` - Style
- `WidgetState` - Widget State; methodes: none
- `WidgetStateHelper` - Widget State Helper; methodes: `isState(...)`, `setState(...)`, `clearState(...)`
- `WidgetStyle` - Widget Style; methodes: none
- `SwStyle` - Style; methodes: `SwStyle(...)`, `drawControl(...)`, `drawBackground(...)`
- `BoxShadow` - Box Shadow; methodes: none
- `Padding` - Padding; methodes: none

### `src/core/SwTcpServer.h` - Tcp Server
- `SwTcpServer` - Tcp Server; methodes: `SwTcpServer(...)`, `~SwTcpServer(...)`, `listen(...)`, `close(...)`, `nextPendingConnection(...)`; signaux: `newConnection()`
- `SwTcpServer` - Tcp Server; methodes: `SwTcpServer(...)`, `~SwTcpServer(...)`, `listen(...)`, `close(...)`, `nextPendingConnection(...)`; signaux: `newConnection()`
- `pollfd` - pollfd; methodes: none

### `src/core/SwTcpSocket.h` - Provides an implementation of a TCP socket using the Winsock2 library.
- `SwTcpSocket` - Provides an implementation of a TCP socket using the Winsock2 library.; methodes: `SwTcpSocket(...)`, `~SwTcpSocket(...)`, `useSsl(...)`, `isUsingSsl(...)`, `isRemoteClosed(...)`, `connectToHost(...)`, `waitForConnected(...)`, `close(...)`, `read(...)`, `write(...)`, `waitForBytesWritten(...)`, `shutdownWrite(...)`, `adoptSocket(...)`
- `addrinfo` - addrinfo; methodes: none
- `TlsState` - Tls State; methodes: none
- `SwTcpSocket` - Tcp Socket; methodes: `SwTcpSocket(...)`, `~SwTcpSocket(...)`, `useSsl(...)`, `isRemoteClosed(...)`, `connectToHost(...)`, `waitForConnected(...)`, `waitForBytesWritten(...)`, `shutdownWrite(...)`, `close(...)`, `read(...)`, `write(...)`, `adoptSocket(...)`
- `addrinfo` - addrinfo; methodes: none
- `pollfd` - pollfd; methodes: none
- `TlsState` - Tls State; methodes: none

### `src/core/SwThread.h` - High-level wrapper around the atomic::Thread, offering a Qt-like API.
- `SwThread` - High-level wrapper around the atomic::Thread, offering a Qt-like API.; methodes: `SwThread(...)`, `~SwThread(...)`, `start(...)`, `quit(...)`, `wait(...)`, `isRunning(...)`, `postTask(...)`, `application(...)`, `threadId(...)`, `handle(...)`, `currentThread(...)`, `adoptCurrentThread(...)`, `fromHandle(...)`; signaux: `started()`, `finished()`, `terminated()`

### `src/core/SwTimer.h` - Provides a timer implementation for periodic or single-shot execution of tasks, similar to QTimer.
- `SwTimer` - Provides a timer implementation for periodic or single-shot execution of tasks, similar to QTimer.; methodes: `SwTimer(...)`, `~SwTimer(...)`, `setInterval(...)`, `interval(...)`, `setSingleShot(...)`, `isSingleShot(...)`, `start(...)`, `stop(...)`, `isActive(...)`, `remainingTime(...)`, `setTimerType(...)`, `timerType(...)`, `singleShot(...)`; signaux: `timeout()`
- `TimerType` - Timer Type; methodes: none

### `src/core/SwUdpSocket.h` - Udp Socket
- `SocketState` - Socket State; methodes: none
- `SocketError` - Socket Error; methodes: none

### `src/core/SwVector.h` - Vector
- `SwVector` - Vector; methodes: `SwVector(...)`, `~SwVector(...)`, `begin(...)`, `end(...)`, `cbegin(...)`, `cend(...)`, `at(...)`, `front(...)`, `back(...)`, `data(...)`, `isEmpty(...)`, `size(...)`, `count(...)`, `clear(...)`, `reserve(...)`, `capacity(...)`, `push_back(...)`, `append(...)`, `emplace_back(...)`, `erase(...)`, `removeAt(...)`, `remove(...)`, `resize(...)`

### `src/core/SwVideoWidget.h` - Video Widget
- `SwVideoRenderer` - Video Renderer; methodes: `~SwVideoRenderer(...)`, `render(...)`
- `SwFallbackVideoRenderer` - Fallback Video Renderer; methodes: `render(...)`
- `SwWin32VideoRenderer` - Win 32 Video Renderer; methodes: `render(...)`
- `SwD2DVideoRenderer` - D 2 DVideo Renderer; methodes: `SwD2DVideoRenderer(...)`, `isSupported(...)`, `render(...)`
- `SwVideoWidget` - Widget that hosts a media pipeline and paints decoded video frames.; methodes: `SwVideoWidget(...)`, `~SwVideoWidget(...)`, `setVideoSource(...)`, `videoSource(...)`, `setVideoDecoder(...)`, `videoDecoder(...)`, `setRenderer(...)`, `renderer(...)`, `start(...)`, `stop(...)`, `setScalingMode(...)`, `scalingMode(...)`, `setBackgroundColor(...)`, `backgroundColor(...)`, `setFrameArrivedCallback(...)`, `hasFrame(...)`, `currentFrame(...)`, `lastFrameTime(...)`, `paintEvent(...)`, `resizeEvent(...)`
- `ScalingMode` - Scaling Mode; methodes: none

### `src/core/SwWidget.h` - Widget
- `EventType` - Event Type; methodes: none
- `Event` - Event; methodes: `Event(...)`, `type(...)`, `accept(...)`, `ignore(...)`, `isAccepted(...)`
- `ResizeEvent` - Resize Event; methodes: `ResizeEvent(...)`, `width(...)`, `height(...)`
- `PaintEvent` - Paint Event; methodes: `PaintEvent(...)`, `painter(...)`, `paintRect(...)`
- `MouseEvent` - Mouse Event; methodes: `MouseEvent(...)`, `x(...)`, `y(...)`, `setX(...)`, `setY(...)`, `getDeltaX(...)`, `setDeltaX(...)`, `getDeltaY(...)`, `setDeltaY(...)`, `getSpeedX(...)`, `setSpeedX(...)`, `getSpeedY(...)`, `setSpeedY(...)`
- `KeyEvent` - Key Event; methodes: `KeyEvent(...)`, `key(...)`, `isCtrlPressed(...)`, `isShiftPressed(...)`, `isAltPressed(...)`
- `SwWidget` - Widget; methodes: `SwWidget(...)`, `~SwWidget(...)`, `addChild(...)`, `removeChild(...)`, `setLayout(...)`, `layout(...)`, `show(...)`, `hide(...)`, `update(...)`, `move(...)`, `resize(...)`, `width(...)`, `height(...)`, `setMinimumSize(...)`, `setMaximumSize(...)`, `rect(...)`, `geometry(...)`, `frameGeometry(...)`, `pos(...)`, `sizeHint(...)`, `minimumSizeHint(...)`, `getChildUnderCursor(...)`, `getToolSheet(...)`; signaux: `resized(...)`, `moved(...)`, `visibilityChanged()`

### `src/core/SwWidgetInterface.h` - Widget Interface
- `SwWidgetInterface` - Widget Interface; methodes: `SwWidgetInterface(...)`, `~SwWidgetInterface(...)`, `show(...)`, `hide(...)`, `update(...)`, `move(...)`, `resize(...)`, `paintEvent(...)`, `mousePressEvent(...)`, `mouseReleaseEvent(...)`, `mouseDoubleClickEvent(...)`, `mouseMoveEvent(...)`, `keyPressEvent(...)`, `getToolSheet(...)`, `frameGeometry(...)`, `sizeHint(...)`, `minimumSizeHint(...)`

### `src/core/SwWidgetPlatformAdapter.h` - Widget Platform Adapter
- `SwWidgetPlatformHandle` - Widget Platform Handle; methodes: none
- `SwWidgetPlatformAdapter` - Widget Platform Adapter; methodes: `fromNativeHandle(...)`, `setCursor(...)`, `invalidateRect(...)`, `characterIndexAtPosition(...)`, `textWidthUntil(...)`, `isBackspaceKey(...)`, `isDeleteKey(...)`, `isLeftArrowKey(...)`, `isRightArrowKey(...)`, `isHomeKey(...)`, `isEndKey(...)`, `isCapsLockKey(...)`, `matchesShortcutKey(...)`, `translateCharacter(...)`, `isShiftModifierActive(...)`, `defined(...)`, `finishSyntheticExpose(...)`
- `LinuxExposeTracker` - Linux Expose Tracker; methodes: none
- `Key` - Key; methodes: none

### `src/media/SwFileVideoSource.h` - File Video Source
- `SwFileVideoSource` - File Video Source; methodes: `SwFileVideoSource(...)`, `~SwFileVideoSource(...)`, `name(...)`, `start(...)`, `stop(...)`, `setLoop(...)`

### `src/media/SwHttpMjpegSource.h` - Http Mjpeg Source
- `SwHttpMjpegSource` - Http Mjpeg Source; methodes: `SwHttpMjpegSource(...)`, `~SwHttpMjpegSource(...)`, `name(...)`, `initialize(...)`, `start(...)`, `stop(...)`

### `src/media/SwMediaFoundationH264Decoder.h` - Media Foundation H 264 Decoder
- `SwMediaFoundationDecoderBase` - Media Foundation Decoder Base; methodes: `SwMediaFoundationDecoderBase(...)`, `~SwMediaFoundationDecoderBase(...)`, `name(...)`, `open(...)`, `feed(...)`, `flush(...)`
- `SwMediaFoundationH264Decoder` - Media Foundation H 264 Decoder; methodes: `SwMediaFoundationH264Decoder(...)`
- `SwMediaFoundationH265Decoder` - Media Foundation H 265 Decoder; methodes: `SwMediaFoundationH265Decoder(...)`
- `SwMediaFoundationAv1Decoder` - Media Foundation Av 1 Decoder; methodes: `SwMediaFoundationAv1Decoder(...)`
- `SwMediaFoundationH264Decoder` - Media Foundation H 264 Decoder; methodes: `name(...)`, `feed(...)`
- `SwMediaFoundationH265Decoder` - Media Foundation H 265 Decoder; methodes: `name(...)`, `feed(...)`
- `SwMediaFoundationAv1Decoder` - Media Foundation Av 1 Decoder; methodes: `name(...)`, `feed(...)`

### `src/media/SwMediaFoundationMovieSource.h` - Media Foundation Movie Source
- `SwMediaFoundationMovieSource` - Media Foundation Movie Source; methodes: `SwMediaFoundationMovieSource(...)`, `~SwMediaFoundationMovieSource(...)`, `name(...)`, `initialize(...)`, `start(...)`, `stop(...)`, `setLoop(...)`
- `MovieCaptureThread` - Movie Capture Thread; methodes: `MovieCaptureThread(...)`
- `SwMediaFoundationMovieSource` - Media Foundation Movie Source; methodes: `SwMediaFoundationMovieSource(...)`, `name(...)`, `initialize(...)`, `start(...)`, `stop(...)`

### `src/media/SwMediaFoundationVideoSource.h` - Media Foundation Video Source
- `SwMediaFoundationVideoSource` - Media Foundation Video Source; methodes: `SwMediaFoundationVideoSource(...)`, `~SwMediaFoundationVideoSource(...)`, `name(...)`, `initialize(...)`, `start(...)`, `stop(...)`
- `CaptureThread` - Capture Thread; methodes: `CaptureThread(...)`
- `SwMediaFoundationVideoSource` - Media Foundation Video Source; methodes: `SwMediaFoundationVideoSource(...)`, `name(...)`, `initialize(...)`, `start(...)`, `stop(...)`

### `src/media/SwRtspUdpSource.h` - Rtsp Udp Source
- `SwRtspUdpSource` - Rtsp Udp Source; methodes: `SwRtspUdpSource(...)`, `base64Decode(...)`, `parseH265Fmtp(...)`, `~SwRtspUdpSource(...)`, `name(...)`, `initialize(...)`, `setLocalAddress(...)`, `setUseTcpTransport(...)`, `forceLocalBind(...)`, `start(...)`, `stop(...)`
- `RtspRequest` - Rtsp Request; methodes: none
- `RtspStep` - Rtsp Step; methodes: none
- `PollWorkerThread` - Poll Worker Thread; methodes: `PollWorkerThread(...)`, `requestStop(...)`, `setInterval(...)`
- `TsDemux` - Ts Demux; methodes: `reset(...)`, `hasStartCodeH264Idr(...)`, `hasStartCodeHevcIdr(...)`, `isHevc(...)`, `feed(...)`, `parsePAT(...)`, `parsePMT(...)`, `parsePts(...)`, `handlePES(...)`

### `src/media/SwVideoDecoder.h` - Video Decoder
- `SwVideoDecoder` - Video Decoder; methodes: `~SwVideoDecoder(...)`, `name(...)`, `open(...)`, `feed(...)`, `flush(...)`, `setFrameCallback(...)`
- `SwPassthroughVideoDecoder` - Passthrough Video Decoder; methodes: `name(...)`, `feed(...)`
- `SwVideoDecoderFactory` - Video Decoder Factory; methodes: `instance(...)`, `registerDecoder(...)`, `acquire(...)`, `create(...)`
- `Entry` - Entry; methodes: none

### `src/media/SwVideoFrame.h` - Video Frame
- `SwVideoFrame` - Video Frame; methodes: `SwVideoFrame(...)`, `allocate(...)`, `fromCopy(...)`, `wrapExternal(...)`, `isValid(...)`, `width(...)`, `height(...)`, `pixelFormat(...)`, `planeCount(...)`, `planeData(...)`, `planeStride(...)`, `planeHeight(...)`, `setTimestamp(...)`, `timestamp(...)`, `setColorSpace(...)`, `colorSpace(...)`, `setRotation(...)`, `rotation(...)`, `setAspectRatio(...)`, `aspectRatio(...)`, `formatInfo(...)`, `clear(...)`, `fill(...)`, `buffer(...)`, `bufferSize(...)`

### `src/media/SwVideoPacket.h` - Video Packet
- `SwVideoPacket` - Video Packet; methodes: `SwVideoPacket(...)`, `codec(...)`, `setCodec(...)`, `payload(...)`, `setPayload(...)`, `pts(...)`, `dts(...)`, `setPts(...)`, `setDts(...)`, `isKeyFrame(...)`, `setKeyFrame(...)`, `setRawFormat(...)`, `rawFormat(...)`, `carriesRawFrame(...)`
- `Codec` - Codec; methodes: none

### `src/media/SwVideoSource.h` - Video Source
- `SwVideoSource` - Video Source; methodes: `~SwVideoSource(...)`, `name(...)`, `start(...)`, `stop(...)`, `setPacketCallback(...)`
- `SwVideoPipeline` - Video Pipeline; methodes: `setSource(...)`, `setDecoder(...)`, `setDecoderHint(...)`, `setFrameCallback(...)`, `useDecoderFactory(...)`, `start(...)`, `stop(...)`

### `src/media/SwVideoTypes.h` - Describes how a pixel format is laid out in memory.
- `SwVideoPixelFormat` - Video Pixel Format; methodes: none
- `SwVideoColorSpace` - Video Color Space; methodes: none
- `SwVideoRotation` - Video Rotation; methodes: none
- `SwVideoFormatInfo` - Describes how a pixel format is laid out in memory.; methodes: `isValid(...)`, `isPlanar(...)`, `isPacked(...)`

### `src/platform/SwPlatformFactory.h` - Platform Factory
- (pas de classe); fonctions: `SwCreateDefaultPlatformIntegration(...)`

### `src/platform/SwPlatformIntegration.h` - Platform Integration
- `SwMouseButton` - Mouse Button; methodes: none
- `SwPixelFormat` - Pixel Format; methodes: none
- `SwPlatformPoint` - Platform Point; methodes: `SwPlatformPoint(...)`
- `SwPlatformSize` - Platform Size; methodes: `SwPlatformSize(...)`
- `SwPlatformRect` - Platform Rect; methodes: `SwPlatformRect(...)`
- `SwKeyEvent` - Key Event; methodes: none
- `SwMouseEvent` - Mouse Event; methodes: none
- `SwWindowCallbacks` - Window Callbacks; methodes: none
- `SwPlatformImage` - Platform Image; methodes: `~SwPlatformImage(...)`, `size(...)`, `format(...)`, `pitch(...)`, `pixels(...)`, `clear(...)`
- `SwPlatformPainter` - Platform Painter; methodes: `~SwPlatformPainter(...)`, `begin(...)`, `end(...)`, `flush(...)`, `drawImage(...)`, `fillRect(...)`
- `SwPlatformWindow` - Platform Window; methodes: `~SwPlatformWindow(...)`, `show(...)`, `hide(...)`, `setTitle(...)`, `resize(...)`, `move(...)`, `requestUpdate(...)`, `nativeHandle(...)`
- `SwPlatformIntegration` - Platform Integration; methodes: `~SwPlatformIntegration(...)`, `initialize(...)`, `shutdown(...)`, `createWindow(...)`, `createPainter(...)`, `createImage(...)`, `processPlatformEvents(...)`, `wakeUpGuiThread(...)`, `availableScreens(...)`, `clipboardText(...)`, `setClipboardText(...)`, `name(...)`

### `src/platform/win/SwWin32Painter.h` - Win 32 Painter
- `SwWin32Painter` - Win 32 Painter; methodes: `SwWin32Painter(...)`, `clear(...)`, `fillRect(...)`, `fillRoundedRect(...)`, `drawRect(...)`, `drawText(...)`, `nativeHandle(...)`

### `src/platform/win/SwWin32PlatformIntegration.h` - Win 32 Platform Integration
- `SwVirtualGraphicsEngine` - Virtual Graphics Engine; methodes: `~SwVirtualGraphicsEngine(...)`, `initialize(...)`, `shutdown(...)`, `render(...)`
- `SwGdiPlusEngine` - Gdi Plus Engine; methodes: `instance(...)`, `initialize(...)`, `shutdown(...)`, `render(...)`
- `SwWin32WindowCallbacks` - Win 32 Window Callbacks; methodes: none
- `SwWin32PlatformIntegration` - Win 32 Platform Integration; methodes: `SwWin32PlatformIntegration(...)`, `~SwWin32PlatformIntegration(...)`, `initialize(...)`, `shutdown(...)`, `createWindow(...)`, `createPainter(...)`, `createImage(...)`, `processPlatformEvents(...)`, `wakeUpGuiThread(...)`, `availableScreens(...)`, `clipboardText(...)`, `setClipboardText(...)`, `name(...)`, `registerWindow(...)`, `deregisterWindow(...)`, `WindowProc(...)`, `registerWindowClass(...)`, `unregisterWindowClass(...)`

### `src/platform/win/SwWindows.h` - Windows
- (pas de classe); macros: `NOMINMAX`, `WIN32_LEAN_AND_MEAN`

### `src/platform/x11/SwX11Painter.h` - X 11 Painter
- `SwX11Painter` - X 11 Painter; methodes: `SwX11Painter(...)`, `~SwX11Painter(...)`, `clear(...)`, `fillRect(...)`, `fillRoundedRect(...)`, `drawRect(...)`, `drawText(...)`, `finalize(...)`

### `src/platform/x11/SwX11PlatformIntegration.h` - X 11 Platform Integration
- `SwX11PlatformImage` - X 11 Platform Image; methodes: `SwX11PlatformImage(...)`, `size(...)`, `format(...)`, `pitch(...)`, `pixels(...)`, `clear(...)`
- `SwX11PlatformPainter` - X 11 Platform Painter; methodes: `SwX11PlatformPainter(...)`, `begin(...)`, `end(...)`, `flush(...)`, `drawImage(...)`, `fillRect(...)`, `targetWindow(...)`
- `SwX11PlatformWindow` - X 11 Platform Window; methodes: `SwX11PlatformWindow(...)`, `~SwX11PlatformWindow(...)`, `show(...)`, `hide(...)`, `setTitle(...)`, `resize(...)`, `move(...)`, `requestUpdate(...)`, `nativeHandle(...)`, `handle(...)`, `display(...)`, `callbacks(...)`, `setCallbacks(...)`, `updateSize(...)`
- `SwX11PlatformIntegration` - X 11 Platform Integration; methodes: `SwX11PlatformIntegration(...)`, `~SwX11PlatformIntegration(...)`, `initialize(...)`, `shutdown(...)`, `createWindow(...)`, `createPainter(...)`, `createImage(...)`, `processPlatformEvents(...)`, `wakeUpGuiThread(...)`, `availableScreens(...)`, `name(...)`, `clipboardText(...)`, `setClipboardText(...)`, `display(...)`, `deleteWindowAtom(...)`, `registerWindow(...)`, `unregisterWindow(...)`, `findWindow(...)`
- `SelectionWaitContext` - Selection Wait Context; methodes: `SelectionWaitContext(...)`
- `SwX11PlatformIntegration` - X 11 Platform Integration; methodes: `initialize(...)`, `shutdown(...)`, `createWindow(...)`, `createPainter(...)`, `createImage(...)`, `processPlatformEvents(...)`, `wakeUpGuiThread(...)`, `availableScreens(...)`, `clipboardText(...)`, `setClipboardText(...)`, `name(...)`
