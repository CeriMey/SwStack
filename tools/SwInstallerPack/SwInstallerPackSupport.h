#pragma once

#include "SwByteArray.h"
#include "SwCrypto.h"
#include "SwInstallerPayload.h"
#include "SwList.h"
#include "SwString.h"
#include "third_party/miniz/miniz.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace swinstaller {
namespace pack {

struct PackedFile {
    SwString relativePath;
    SwString checksumSha256;
    SwByteArray originalData;
    SwByteArray storedData;
    bool compressed{false};
};

inline SwString normalizeRelativePath(SwString value) {
    value.replace("\\", "/");
    while (value.startsWith("/")) {
        value.remove(0, 1);
    }
    while (value.contains("//")) {
        value.replace("//", "/");
    }
    return value;
}

inline SwString sanitizeSymbol(SwString value) {
    if (value.isEmpty()) {
        return "EmbeddedPayload";
    }
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                        (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            value[i] = '_';
        }
    }
    if (!((value[0] >= 'a' && value[0] <= 'z') || (value[0] >= 'A' && value[0] <= 'Z') ||
          value[0] == '_')) {
        value.prepend('_');
    }
    return value;
}

inline SwString sha256Hex(const SwByteArray& bytes) {
    const std::vector<unsigned char> digest =
        SwCrypto::generateHashSHA256(std::string(bytes.constData(), bytes.size()));
    static const char* kHex = "0123456789abcdef";
    SwString out;
    out.reserve(digest.size() * 2);
    for (size_t i = 0; i < digest.size(); ++i) {
        out += kHex[(digest[i] >> 4) & 0x0F];
        out += kHex[digest[i] & 0x0F];
    }
    return out;
}

inline SwString cppQuoted(const SwString& value) {
    SwString out = "\"";
    for (size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    out += "\"";
    return out;
}

inline SwString byteArrayInitializer(const SwByteArray& bytes) {
    if (bytes.isEmpty()) {
        return "0x00";
    }
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i != 0) {
            oss << ", ";
        }
        oss << "0x" << std::setw(2)
            << static_cast<unsigned int>(static_cast<unsigned char>(bytes.constData()[i]));
    }
    return SwString(oss.str());
}

inline bool readFileBytes(const std::filesystem::path& path, SwByteArray& out, SwString* errOut) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (errOut) *errOut = SwString("failed to open input file: ") + SwString(path.string());
        return false;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    const std::string data = oss.str();
    out = SwByteArray(data.data(), data.size());
    return true;
}

inline bool writeTextFile(const std::filesystem::path& path,
                          const std::string& text,
                          SwString* errOut) {
    std::error_code ec;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        if (errOut) *errOut = SwString("failed to open output file: ") + SwString(path.string());
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file.good()) {
        if (errOut) *errOut = SwString("failed to write output file: ") + SwString(path.string());
        return false;
    }
    return true;
}

inline bool compressBestEffort(const SwByteArray& source,
                               SwByteArray& stored,
                               bool& compressed,
                               SwString* errOut) {
    compressed = false;
    stored = source;
    if (source.isEmpty()) {
        return true;
    }

    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(source.size()));
    std::vector<unsigned char> buffer(static_cast<size_t>(bound));
    mz_ulong actual = bound;
    const int status = mz_compress2(buffer.data(),
                                    &actual,
                                    reinterpret_cast<const unsigned char*>(source.constData()),
                                    static_cast<mz_ulong>(source.size()),
                                    9);
    if (status != MZ_OK) {
        if (errOut) *errOut = "compression failed";
        return false;
    }
    if (actual < source.size()) {
        stored = SwByteArray(reinterpret_cast<const char*>(buffer.data()), static_cast<size_t>(actual));
        compressed = true;
    }
    return true;
}

inline bool collectFiles(const std::filesystem::path& root,
                         SwList<PackedFile>& files,
                         SwString* errOut) {
    std::vector<std::filesystem::path> discovered;
    for (auto it = std::filesystem::recursive_directory_iterator(root);
         it != std::filesystem::recursive_directory_iterator();
         ++it) {
        if (it->is_regular_file()) {
            discovered.push_back(it->path());
        }
    }
    std::sort(discovered.begin(), discovered.end());

    for (size_t i = 0; i < discovered.size(); ++i) {
        const std::filesystem::path& path = discovered[i];
        SwByteArray original;
        if (!readFileBytes(path, original, errOut)) {
            return false;
        }

        PackedFile packed;
        packed.relativePath = normalizeRelativePath(
            SwString(std::filesystem::relative(path, root).generic_string()));
        packed.originalData = original;
        packed.checksumSha256 = sha256Hex(original);
        if (!compressBestEffort(original, packed.storedData, packed.compressed, errOut)) {
            return false;
        }
        files.append(packed);
    }

    return true;
}

inline std::string generatedHeaderText(const SwString& symbol,
                                       const SwString& payloadName,
                                       const SwList<PackedFile>& files) {
    const SwString safeSymbol = sanitizeSymbol(symbol);
    std::ostringstream oss;
    oss << "#pragma once\n\n";
    oss << "#include \"SwInstallerPayload.h\"\n\n";
    oss << "namespace swinstaller {\nnamespace generated {\n\n";

    for (size_t i = 0; i < files.size(); ++i) {
        oss << "static const unsigned char " << safeSymbol.toStdString() << "_blob_" << i
            << "[] = { " << byteArrayInitializer(files[i].storedData).toStdString() << " };\n";
    }
    if (!files.isEmpty()) {
        oss << "\n";
    }

    oss << "inline const ::swinstaller::SwInstallerEmbeddedPayload& "
        << safeSymbol.toStdString() << "() {\n";
    oss << "    static ::swinstaller::SwInstallerEmbeddedPayload payload = []() {\n";
    oss << "        ::swinstaller::SwInstallerEmbeddedPayload p;\n";
    oss << "        p.payloadId = " << cppQuoted(safeSymbol).toStdString() << ";\n";
    oss << "        p.displayName = " << cppQuoted(payloadName).toStdString() << ";\n";
    for (size_t i = 0; i < files.size(); ++i) {
        oss << "        {\n";
        oss << "            ::swinstaller::SwInstallerEmbeddedFile f;\n";
        oss << "            f.relativePath = " << cppQuoted(files[i].relativePath).toStdString() << ";\n";
        oss << "            f.checksumSha256 = " << cppQuoted(files[i].checksumSha256).toStdString() << ";\n";
        oss << "            f.originalSize = " << static_cast<long long>(files[i].originalData.size()) << ";\n";
        oss << "            f.storedSize = " << static_cast<long long>(files[i].storedData.size()) << ";\n";
        oss << "            f.compressed = " << (files[i].compressed ? "true" : "false") << ";\n";
        oss << "            f.bytes = " << safeSymbol.toStdString() << "_blob_" << i << ";\n";
        oss << "            p.files.append(f);\n";
        oss << "        }\n";
    }
    oss << "        return p;\n";
    oss << "    }();\n";
    oss << "    return payload;\n";
    oss << "}\n\n";
    oss << "} // namespace generated\n";
    oss << "} // namespace swinstaller\n";
    return oss.str();
}

} // namespace pack
} // namespace swinstaller
