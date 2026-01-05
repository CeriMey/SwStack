#pragma once

// Minimal export macro shim for miniz when built as a static library.
// We keep it empty by default, but allow DLL builds if ever needed.

#ifndef MINIZ_EXPORT
  #if defined(_WIN32) && defined(MINIZ_DLL)
    #if defined(MINIZ_EXPORTS)
      #define MINIZ_EXPORT __declspec(dllexport)
    #else
      #define MINIZ_EXPORT __declspec(dllimport)
    #endif
  #else
    #define MINIZ_EXPORT
  #endif
#endif

