#pragma once

#include "SwCodeEditor.h"
#include "SwCompleter.h"
#include "SwList.h"
#include "SwString.h"

// ============================================================================
//  Language-specific completion & indentation configuration
// ============================================================================

// ── Completion helpers ─────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swBuildCompletionList_(const char* const* words, int count,
                       const SwString& prefix) {
    SwList<SwCodeEditor::CompletionEntry> result;
    const SwString lowerPrefix = prefix.toLower();
    for (int i = 0; i < count; ++i) {
        const SwString word(words[i]);
        if (word.toLower().startsWith(lowerPrefix) && word != prefix) {
            SwCodeEditor::CompletionEntry entry;
            entry.displayText = word;
            entry.insertText = word;
            result.append(entry);
        }
    }
    return result;
}

// ── C / C++ completions ────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swCppCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "alignas", "alignof", "asm", "auto", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "concept", "const",
        "consteval", "constexpr", "constinit", "const_cast", "continue",
        "co_await", "co_return", "co_yield", "decltype", "default", "delete",
        "do", "double", "dynamic_cast", "else", "enum", "explicit", "export",
        "extern", "false", "final", "float", "for", "friend", "goto", "if",
        "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "nullptr", "operator", "override", "private", "protected", "public",
        "register", "reinterpret_cast", "requires", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
        "using", "virtual", "void", "volatile", "wchar_t", "while",
        // Common STL
        "string", "vector", "map", "unordered_map", "set", "unordered_set",
        "list", "deque", "array", "queue", "stack", "priority_queue",
        "pair", "tuple", "optional", "variant", "any", "shared_ptr",
        "unique_ptr", "weak_ptr", "make_shared", "make_unique",
        "nullptr_t", "size_t", "ptrdiff_t", "int8_t", "int16_t",
        "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        // Preprocessor
        "#include", "#define", "#ifndef", "#ifdef", "#endif", "#pragma",
        "#if", "#elif", "#else", "#undef", "#error", "#warning",
        "include", "define", "ifndef", "ifdef", "endif", "pragma",
        // Common
        "std::cout", "std::cerr", "std::endl", "std::move", "std::forward",
        "std::begin", "std::end", "std::sort", "std::find",
        "std::string", "std::vector", "std::map", "std::function",
        "iostream", "fstream", "sstream", "algorithm", "memory",
        "functional", "utility", "numeric", "cassert", "cstdint"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Python completions ─────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swPythonCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else",
        "except", "finally", "for", "from", "global", "if", "import",
        "in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
        "return", "try", "while", "with", "yield",
        "self", "cls", "super", "__init__", "__main__", "__name__",
        "__str__", "__repr__", "__len__", "__getitem__", "__setitem__",
        "__delitem__", "__iter__", "__next__", "__enter__", "__exit__",
        "__call__", "__eq__", "__ne__", "__lt__", "__gt__", "__le__", "__ge__",
        "__add__", "__sub__", "__mul__", "__truediv__", "__floordiv__",
        "__mod__", "__pow__", "__and__", "__or__", "__xor__",
        // Builtins
        "abs", "all", "any", "bin", "bool", "bytearray", "bytes",
        "callable", "chr", "classmethod", "compile", "complex",
        "delattr", "dict", "dir", "divmod", "enumerate", "eval",
        "exec", "filter", "float", "format", "frozenset", "getattr",
        "globals", "hasattr", "hash", "help", "hex", "id", "input",
        "int", "isinstance", "issubclass", "iter", "len", "list",
        "locals", "map", "max", "memoryview", "min", "next", "object",
        "oct", "open", "ord", "pow", "print", "property", "range",
        "repr", "reversed", "round", "set", "setattr", "slice",
        "sorted", "staticmethod", "str", "sum", "super", "tuple",
        "type", "vars", "zip",
        // Exceptions
        "Exception", "ValueError", "TypeError", "KeyError", "IndexError",
        "AttributeError", "ImportError", "FileNotFoundError", "IOError",
        "RuntimeError", "StopIteration", "OSError", "NotImplementedError",
        // Typing
        "Optional", "Union", "List", "Dict", "Tuple", "Set", "Any",
        "Callable", "Iterator", "Generator", "Sequence", "Mapping",
        "Iterable", "Type", "ClassVar", "Final", "Literal", "TypeVar",
        "Protocol", "TypedDict", "Annotated"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── JavaScript / TypeScript completions ────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swJavaScriptCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "async", "await", "break", "case", "catch", "class", "const",
        "continue", "debugger", "default", "delete", "do", "else",
        "export", "extends", "false", "finally", "for", "function",
        "if", "import", "in", "instanceof", "let", "new", "null",
        "of", "return", "super", "switch", "this", "throw", "true",
        "try", "typeof", "undefined", "var", "void", "while", "with",
        "yield", "from", "as", "static", "get", "set",
        // TypeScript
        "abstract", "declare", "enum", "implements", "interface",
        "module", "namespace", "private", "protected", "public",
        "readonly", "type", "keyof", "infer", "is", "asserts",
        "override", "satisfies",
        // Types
        "string", "number", "boolean", "symbol", "bigint", "any",
        "unknown", "never", "object", "void",
        "Array", "Map", "Set", "WeakMap", "WeakSet", "Promise",
        "Date", "RegExp", "Error", "Function", "Object", "String",
        "Number", "Boolean", "Symbol", "BigInt", "JSON", "Math",
        "Record", "Partial", "Required", "Readonly", "Pick", "Omit",
        "Exclude", "Extract", "NonNullable", "ReturnType", "Parameters",
        // DOM/Node
        "console", "document", "window", "process", "require", "module",
        "exports", "__dirname", "__filename", "setTimeout", "setInterval",
        "clearTimeout", "clearInterval", "fetch", "Response", "Request",
        "Headers", "URL", "URLSearchParams",
        "addEventListener", "removeEventListener", "querySelector",
        "querySelectorAll", "getElementById", "createElement",
        "appendChild", "removeChild", "innerHTML", "textContent",
        "classList", "setAttribute", "getAttribute", "style",
        // Common
        "console.log", "console.error", "console.warn", "console.info",
        "JSON.parse", "JSON.stringify", "Object.keys", "Object.values",
        "Object.entries", "Object.assign", "Array.isArray",
        "Promise.all", "Promise.race", "Promise.resolve", "Promise.reject"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Java completions ───────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swJavaCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "abstract", "assert", "boolean", "break", "byte", "case", "catch",
        "char", "class", "const", "continue", "default", "do", "double",
        "else", "enum", "extends", "final", "finally", "float", "for",
        "goto", "if", "implements", "import", "instanceof", "int",
        "interface", "long", "native", "new", "null", "package", "private",
        "protected", "public", "return", "short", "static", "strictfp",
        "super", "switch", "synchronized", "this", "throw", "throws",
        "transient", "try", "void", "volatile", "while", "true", "false",
        "var", "record", "sealed", "permits", "yield",
        "String", "Integer", "Long", "Double", "Float", "Boolean",
        "Character", "Byte", "Short", "Object", "Class", "Void",
        "System", "System.out", "System.err",
        "System.out.println", "System.out.print",
        "List", "ArrayList", "LinkedList", "Map", "HashMap", "TreeMap",
        "Set", "HashSet", "TreeSet", "Queue", "Stack", "Deque",
        "Collection", "Collections", "Arrays", "Optional", "Stream",
        "Iterator", "Iterable", "Comparable", "Comparator",
        "Runnable", "Callable", "Future", "CompletableFuture",
        "Thread", "StringBuilder", "StringBuffer",
        "Exception", "RuntimeException", "IOException",
        "NullPointerException", "IllegalArgumentException",
        "IllegalStateException", "IndexOutOfBoundsException",
        "@Override", "@Deprecated", "@SuppressWarnings", "@FunctionalInterface",
        "@interface", "@Retention", "@Target", "@Documented"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── C# completions ─────────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swCSharpCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "abstract", "as", "base", "bool", "break", "byte", "case", "catch",
        "char", "checked", "class", "const", "continue", "decimal", "default",
        "delegate", "do", "double", "else", "enum", "event", "explicit",
        "extern", "false", "finally", "fixed", "float", "for", "foreach",
        "goto", "if", "implicit", "in", "int", "interface", "internal",
        "is", "lock", "long", "namespace", "new", "null", "object",
        "operator", "out", "override", "params", "private", "protected",
        "public", "readonly", "ref", "return", "sbyte", "sealed", "short",
        "sizeof", "stackalloc", "static", "string", "struct", "switch",
        "this", "throw", "true", "try", "typeof", "uint", "ulong",
        "unchecked", "unsafe", "ushort", "using", "virtual", "void",
        "volatile", "while", "async", "await", "dynamic", "nameof",
        "var", "when", "where", "yield", "get", "set", "init", "value",
        "record", "required", "with", "not", "and", "or",
        "String", "Int32", "Int64", "Boolean", "Double", "Float",
        "List", "Dictionary", "HashSet", "Queue", "Stack",
        "Task", "Action", "Func", "Predicate", "IEnumerable",
        "IList", "IDictionary", "IDisposable", "IComparable",
        "Console", "Console.WriteLine", "Console.ReadLine",
        "Exception", "ArgumentException", "InvalidOperationException",
        "NullReferenceException", "NotImplementedException",
        "[Serializable]", "[Obsolete]", "[Flags]", "[DllImport]"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Rust completions ───────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swRustCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "as", "async", "await", "break", "const", "continue", "crate",
        "dyn", "else", "enum", "extern", "false", "fn", "for", "if",
        "impl", "in", "let", "loop", "match", "mod", "move", "mut",
        "pub", "ref", "return", "self", "Self", "static", "struct",
        "super", "trait", "true", "type", "unsafe", "use", "where",
        "while", "yield",
        "bool", "char", "f32", "f64", "i8", "i16", "i32", "i64",
        "i128", "isize", "str", "u8", "u16", "u32", "u64", "u128",
        "usize", "String", "Vec", "Box", "Rc", "Arc", "Cell",
        "RefCell", "Mutex", "RwLock", "Option", "Result", "HashMap",
        "HashSet", "BTreeMap", "BTreeSet", "VecDeque", "Path", "PathBuf",
        "Some", "None", "Ok", "Err", "println!", "print!", "eprintln!",
        "eprint!", "format!", "vec!", "todo!", "unimplemented!",
        "unreachable!", "assert!", "assert_eq!", "assert_ne!",
        "dbg!", "cfg!", "include!", "include_str!", "include_bytes!",
        "#[derive(", "#[cfg(", "#[test]", "#[allow(", "#[deny(",
        "#[warn(", "#[inline]", "#[must_use]",
        "derive(Debug", "derive(Clone", "derive(Copy", "derive(PartialEq",
        "derive(Eq", "derive(Hash", "derive(Default", "derive(Serialize",
        "derive(Deserialize"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Go completions ─────────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swGoCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "break", "case", "chan", "const", "continue", "default", "defer",
        "else", "fallthrough", "for", "func", "go", "goto", "if",
        "import", "interface", "map", "package", "range", "return",
        "select", "struct", "switch", "type", "var",
        "bool", "byte", "complex64", "complex128", "error", "float32",
        "float64", "int", "int8", "int16", "int32", "int64", "rune",
        "string", "uint", "uint8", "uint16", "uint32", "uint64",
        "uintptr", "any", "comparable",
        "append", "cap", "clear", "close", "complex", "copy", "delete",
        "imag", "len", "make", "max", "min", "new", "panic", "print",
        "println", "real", "recover",
        "true", "false", "nil", "iota",
        "fmt", "fmt.Println", "fmt.Printf", "fmt.Sprintf", "fmt.Fprintf",
        "fmt.Errorf", "os", "io", "strings", "strconv", "math", "sort",
        "sync", "context", "net", "net/http", "encoding/json", "log",
        "errors", "time", "regexp", "path", "filepath", "bufio"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Shell completions ──────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swShellCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "if", "then", "else", "elif", "fi", "case", "esac", "for",
        "select", "while", "until", "do", "done", "in", "function",
        "return", "exit", "break", "continue", "declare", "typeset",
        "local", "export", "readonly", "unset", "shift", "set",
        "trap", "eval", "exec", "source",
        "echo", "printf", "read", "cd", "pwd", "pushd", "popd",
        "test", "true", "false", "let", "getopts",
        "chmod", "chown", "mkdir", "rmdir", "rm", "cp", "mv", "ln",
        "cat", "grep", "sed", "awk", "find", "xargs", "sort", "uniq",
        "wc", "head", "tail", "cut", "tr", "tee", "basename", "dirname",
        "curl", "wget", "tar", "gzip", "gunzip", "zip", "unzip",
        "ssh", "scp", "rsync", "git", "docker", "make", "cmake",
        "#!/bin/bash", "#!/usr/bin/env bash", "#!/bin/sh",
        "/dev/null", "/dev/stdin", "/dev/stdout", "/dev/stderr",
        "$?", "$!", "$#", "$@", "$*", "$$", "$0"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Batch completions ──────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swBatchCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "if", "else", "for", "in", "do", "goto", "call", "exit",
        "set", "setlocal", "endlocal", "shift", "not", "exist",
        "defined", "equ", "neq", "lss", "leq", "gtr", "geq",
        "errorlevel", "enabledelayedexpansion", "disabledelayedexpansion",
        "echo", "echo.", "echo off", "@echo off", "pause", "cls",
        "type", "copy", "xcopy", "robocopy", "move", "del", "erase",
        "rd", "rmdir", "md", "mkdir", "ren", "rename", "dir",
        "cd", "chdir", "pushd", "popd", "title", "color", "mode",
        "start", "taskkill", "tasklist", "ping", "ipconfig",
        "findstr", "reg", "sc", "net", "where", "powershell",
        "rem", "::", "%~dp0", "%~nx0", "%~f0", "%errorlevel%",
        "%date%", "%time%", "%cd%", "%random%", "%username%",
        "%computername%", "%userprofile%", "%appdata%", "%temp%"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── CMake completions ──────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swCMakeCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "cmake_minimum_required", "project", "add_executable", "add_library",
        "target_link_libraries", "target_include_directories",
        "target_compile_definitions", "target_compile_options",
        "target_compile_features", "target_sources",
        "set", "unset", "option", "list", "string", "math", "file",
        "find_package", "find_library", "find_path", "find_program",
        "include", "include_directories", "link_directories",
        "add_subdirectory", "add_definitions", "add_compile_definitions",
        "add_compile_options", "add_custom_command", "add_custom_target",
        "add_dependencies", "add_test",
        "install", "configure_file", "execute_process",
        "get_filename_component", "get_target_property",
        "set_target_properties", "set_property", "get_property",
        "if", "elseif", "else", "endif",
        "foreach", "endforeach", "while", "endwhile",
        "function", "endfunction", "macro", "endmacro",
        "return", "break", "continue", "message",
        "cmake_policy", "cmake_parse_arguments",
        "enable_testing", "enable_language",
        "fetchcontent_declare", "fetchcontent_makeavailable",
        "PUBLIC", "PRIVATE", "INTERFACE", "REQUIRED", "COMPONENTS",
        "STATIC", "SHARED", "OBJECT", "IMPORTED", "ALIAS",
        "DESTINATION", "TARGETS", "FILES", "DIRECTORY",
        "COMMAND", "WORKING_DIRECTORY", "DEPENDS", "VERBATIM",
        "APPEND", "PARENT_SCOPE", "CACHE", "FORCE",
        "FATAL_ERROR", "WARNING", "STATUS", "AUTHOR_WARNING",
        "CMAKE_SOURCE_DIR", "CMAKE_BINARY_DIR", "CMAKE_CURRENT_SOURCE_DIR",
        "CMAKE_CURRENT_BINARY_DIR", "CMAKE_INSTALL_PREFIX",
        "CMAKE_CXX_STANDARD", "CMAKE_CXX_STANDARD_REQUIRED",
        "CMAKE_C_STANDARD", "CMAKE_BUILD_TYPE",
        "CMAKE_SYSTEM_NAME", "CMAKE_SYSTEM_PROCESSOR",
        "PROJECT_NAME", "PROJECT_VERSION", "PROJECT_SOURCE_DIR",
        "PROJECT_BINARY_DIR"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── XML / HTML completions ─────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swXmlCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "<?xml", "<!DOCTYPE", "<![CDATA[", "xmlns", "xml:lang",
        "encoding", "version", "standalone",
        // HTML tags
        "html", "head", "body", "div", "span", "p", "a", "img",
        "ul", "ol", "li", "table", "tr", "td", "th", "thead", "tbody",
        "form", "input", "button", "select", "option", "textarea",
        "label", "fieldset", "legend",
        "h1", "h2", "h3", "h4", "h5", "h6",
        "header", "footer", "nav", "main", "section", "article", "aside",
        "script", "style", "link", "meta", "title", "base",
        "br", "hr", "pre", "code", "blockquote", "em", "strong",
        "iframe", "video", "audio", "source", "canvas", "svg",
        // Common attributes
        "class", "id", "style", "src", "href", "alt", "title",
        "type", "name", "value", "placeholder", "action", "method",
        "target", "rel", "media", "width", "height",
        "onclick", "onchange", "onsubmit", "onload", "onerror"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── CSS completions ────────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swCssCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "display", "position", "top", "right", "bottom", "left",
        "width", "height", "min-width", "min-height", "max-width", "max-height",
        "margin", "margin-top", "margin-right", "margin-bottom", "margin-left",
        "padding", "padding-top", "padding-right", "padding-bottom", "padding-left",
        "border", "border-width", "border-style", "border-color", "border-radius",
        "background", "background-color", "background-image", "background-size",
        "background-position", "background-repeat",
        "color", "font", "font-family", "font-size", "font-weight", "font-style",
        "text-align", "text-decoration", "text-transform", "line-height",
        "letter-spacing", "word-spacing", "white-space", "overflow",
        "flex", "flex-direction", "flex-wrap", "justify-content", "align-items",
        "align-content", "align-self", "flex-grow", "flex-shrink", "flex-basis",
        "gap", "row-gap", "column-gap", "order",
        "grid", "grid-template-columns", "grid-template-rows",
        "grid-column", "grid-row", "grid-area", "grid-gap",
        "transform", "transition", "animation", "opacity", "visibility",
        "z-index", "cursor", "pointer-events", "user-select",
        "box-shadow", "text-shadow", "filter", "backdrop-filter",
        "none", "block", "inline", "inline-block", "flex", "grid",
        "relative", "absolute", "fixed", "sticky", "static",
        "auto", "inherit", "initial", "unset",
        "solid", "dashed", "dotted", "double",
        "center", "left", "right", "top", "bottom",
        "bold", "normal", "italic",
        "!important",
        "@media", "@keyframes", "@import", "@font-face",
        "@supports", "@layer", "@container",
        ":hover", ":focus", ":active", ":visited",
        ":first-child", ":last-child", ":nth-child",
        "::before", "::after", "::placeholder"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── SQL completions ────────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swSqlCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "SELECT", "FROM", "WHERE", "INSERT", "INTO", "VALUES", "UPDATE",
        "SET", "DELETE", "CREATE", "DROP", "ALTER", "TABLE", "INDEX",
        "VIEW", "DATABASE", "SCHEMA",
        "AND", "OR", "NOT", "IN", "EXISTS", "BETWEEN", "LIKE", "IS",
        "NULL", "TRUE", "FALSE",
        "JOIN", "INNER", "LEFT", "RIGHT", "OUTER", "FULL", "CROSS", "ON",
        "AS", "ORDER", "BY", "ASC", "DESC", "GROUP", "HAVING",
        "LIMIT", "OFFSET", "UNION", "ALL", "DISTINCT",
        "CASE", "WHEN", "THEN", "ELSE", "END",
        "BEGIN", "COMMIT", "ROLLBACK", "TRANSACTION",
        "PRIMARY", "KEY", "FOREIGN", "REFERENCES", "UNIQUE",
        "CHECK", "CONSTRAINT", "DEFAULT", "AUTO_INCREMENT",
        "CASCADE", "RESTRICT",
        "INT", "INTEGER", "VARCHAR", "TEXT", "FLOAT", "DOUBLE",
        "DECIMAL", "BOOLEAN", "DATE", "DATETIME", "TIMESTAMP",
        "CHAR", "BLOB", "JSON", "UUID", "SERIAL", "BIGSERIAL",
        "COUNT", "SUM", "AVG", "MIN", "MAX",
        "COALESCE", "NULLIF", "CAST", "CONVERT",
        "UPPER", "LOWER", "TRIM", "SUBSTRING", "CONCAT", "LENGTH",
        "NOW", "CURRENT_DATE", "CURRENT_TIMESTAMP",
        "ROW_NUMBER", "RANK", "DENSE_RANK", "LAG", "LEAD",
        "OVER", "PARTITION",
        "WITH", "RECURSIVE", "EXPLAIN", "ANALYZE"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Lua completions ────────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swLuaCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "and", "break", "do", "else", "elseif", "end", "false", "for",
        "function", "goto", "if", "in", "local", "nil", "not", "or",
        "repeat", "return", "then", "true", "until", "while",
        "self",
        "assert", "collectgarbage", "dofile", "error", "getmetatable",
        "ipairs", "load", "loadfile", "next", "pairs", "pcall",
        "print", "rawequal", "rawget", "rawlen", "rawset", "require",
        "select", "setmetatable", "tonumber", "tostring", "type",
        "unpack", "xpcall",
        "io.open", "io.read", "io.write", "io.close", "io.lines",
        "os.clock", "os.date", "os.time", "os.exit", "os.execute",
        "string.format", "string.find", "string.match", "string.gsub",
        "string.sub", "string.len", "string.rep", "string.upper",
        "string.lower", "string.byte", "string.char",
        "table.insert", "table.remove", "table.sort", "table.concat",
        "table.unpack", "table.move",
        "math.abs", "math.ceil", "math.floor", "math.max", "math.min",
        "math.sqrt", "math.random", "math.randomseed", "math.pi",
        "math.huge", "math.sin", "math.cos", "math.tan"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── YAML completions ───────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swYamlCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "true", "false", "null", "yes", "no", "on", "off",
        "---", "...",
        "name", "version", "description", "author", "license",
        "dependencies", "devDependencies", "scripts",
        "env", "environment", "services", "volumes", "ports",
        "image", "build", "command", "entrypoint", "restart",
        "depends_on", "networks", "labels", "deploy",
        "apiVersion", "kind", "metadata", "spec", "containers",
        "replicas", "selector", "template", "resources",
        "limits", "requests", "cpu", "memory",
        "steps", "runs-on", "uses", "with", "jobs", "workflow_dispatch",
        "on", "push", "pull_request", "branches", "tags", "paths"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── JSON completions ───────────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swJsonCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "true", "false", "null"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── Markdown completions ───────────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swMarkdownCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "```", "```cpp", "```python", "```javascript", "```java",
        "```bash", "```json", "```yaml", "```html", "```css",
        "```sql", "```rust", "```go", "```csharp", "```typescript",
        "---", "- [ ]", "- [x]"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ── INI / Config completions ───────────────────────────────────────────────

inline SwList<SwCodeEditor::CompletionEntry>
swIniCompletionProvider_(SwCodeEditor*, const SwString& prefix, size_t, bool) {
    static const char* const words[] = {
        "true", "false", "yes", "no", "on", "off"
    };
    return swBuildCompletionList_(words, static_cast<int>(sizeof(words) / sizeof(words[0])), prefix);
}

// ============================================================================
//  Indentation configuration per language
// ============================================================================

enum class SwIndentStyle {
    CBrace,    // { } based (C, C++, Java, JS, Rust, Go, CSS, etc.)
    Python,    // : based (Python)
    Lua,       // do/then...end based (simplified to same as CBrace for now)
    Xml,       // XML/HTML (same as CBrace for simplicity)
    None       // No special indentation (INI, Markdown, plain text)
};

inline SwIndentStyle swIndentStyleForLanguage_(SwFileLanguage lang) {
    switch (lang) {
    case SwFileLanguage::Cpp:
    case SwFileLanguage::JavaScript:
    case SwFileLanguage::Java:
    case SwFileLanguage::CSharp:
    case SwFileLanguage::Rust:
    case SwFileLanguage::Go:
    case SwFileLanguage::Css:
    case SwFileLanguage::Lua:
        return SwIndentStyle::CBrace;
    case SwFileLanguage::Python:
        return SwIndentStyle::Python;
    case SwFileLanguage::Shell:
    case SwFileLanguage::Batch:
    case SwFileLanguage::CMake:
    case SwFileLanguage::Xml:
    case SwFileLanguage::Json:
    case SwFileLanguage::Yaml:
    case SwFileLanguage::Sql:
        return SwIndentStyle::CBrace;
    case SwFileLanguage::Ini:
    case SwFileLanguage::Markdown:
    case SwFileLanguage::PlainText:
    default:
        return SwIndentStyle::None;
    }
}

inline int swDefaultIndentSize_(SwFileLanguage lang) {
    switch (lang) {
    case SwFileLanguage::Python:
    case SwFileLanguage::Rust:
    case SwFileLanguage::Cpp:
    case SwFileLanguage::Java:
    case SwFileLanguage::CSharp:
    case SwFileLanguage::Go:
    case SwFileLanguage::JavaScript:
    case SwFileLanguage::Css:
    case SwFileLanguage::Lua:
        return 4;
    case SwFileLanguage::Xml:
    case SwFileLanguage::Json:
    case SwFileLanguage::Yaml:
    case SwFileLanguage::CMake:
        return 2;
    case SwFileLanguage::Shell:
    case SwFileLanguage::Batch:
        return 4;
    case SwFileLanguage::Sql:
        return 4;
    default:
        return 4;
    }
}
