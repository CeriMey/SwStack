#include "platform/android/SwAndroidPlatformIntegration.h"

int SwDocumentEditorEmbeddedEntryPoint();

void android_main(android_app* appState) {
    SwAndroidPlatformIntegration::bindAndroidApp(appState);
    (void)SwDocumentEditorEmbeddedEntryPoint();
}
