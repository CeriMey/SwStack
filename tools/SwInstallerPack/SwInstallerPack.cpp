#include "SwInstallerPackSupport.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace swinstaller::pack;

namespace {

static constexpr size_t kDefaultBlobChunkSize = 1024 * 1024;

static void printUsage() {
    std::cerr << "Usage: SwInstallerPack --input <dir> --output <header> "
                 "[--source <cpp>] [--blob-output-dir <dir>] "
                 "[--blob-chunk-size <bytes>] [--blob-count <count>] "
                 "[--symbol <function>] [--name <payload-name>]\n";
}

static bool parseSizeArg(const char* text, size_t& valueOut) {
    if (!text || !*text) {
        return false;
    }
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(std::string(text), &consumed, 10);
        if (consumed != std::string(text).size()) {
            return false;
        }
        valueOut = static_cast<size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

static bool parseArgs(int argc,
                      char* argv[],
                      std::filesystem::path& inputDir,
                      std::filesystem::path& outputHeader,
                      std::filesystem::path& outputSource,
                      std::filesystem::path& blobOutputDir,
                      size_t& blobChunkSize,
                      size_t& expectedBlobCount,
                      SwString& symbol,
                      SwString& payloadName) {
    for (int i = 1; i < argc; ++i) {
        const SwString arg = argv[i];
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            inputDir = std::filesystem::path(argv[++i]);
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputHeader = std::filesystem::path(argv[++i]);
        } else if (arg == "--source" && i + 1 < argc) {
            outputSource = std::filesystem::path(argv[++i]);
        } else if (arg == "--blob-output-dir" && i + 1 < argc) {
            blobOutputDir = std::filesystem::path(argv[++i]);
        } else if (arg == "--blob-chunk-size" && i + 1 < argc) {
            if (!parseSizeArg(argv[++i], blobChunkSize) || blobChunkSize == 0) {
                return false;
            }
        } else if (arg == "--blob-count" && i + 1 < argc) {
            if (!parseSizeArg(argv[++i], expectedBlobCount)) {
                return false;
            }
        } else if (arg == "--symbol" && i + 1 < argc) {
            symbol = argv[++i];
        } else if (arg == "--name" && i + 1 < argc) {
            payloadName = argv[++i];
        } else {
            return false;
        }
    }
    return !inputDir.empty() && !outputHeader.empty();
}

} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path inputDir;
    std::filesystem::path outputHeader;
    std::filesystem::path outputSource;
    std::filesystem::path blobOutputDir;
    size_t blobChunkSize = kDefaultBlobChunkSize;
    size_t expectedBlobCount = 0;
    SwString symbol;
    SwString payloadName;

    if (!parseArgs(argc,
                   argv,
                   inputDir,
                   outputHeader,
                   outputSource,
                   blobOutputDir,
                   blobChunkSize,
                   expectedBlobCount,
                   symbol,
                   payloadName)) {
        printUsage();
        return 1;
    }

    std::error_code ec;
    if (!std::filesystem::exists(inputDir, ec) || !std::filesystem::is_directory(inputDir, ec)) {
        std::cerr << "Input directory does not exist: " << inputDir.string() << "\n";
        return 1;
    }

    if (payloadName.isEmpty()) {
        payloadName = SwString(inputDir.filename().string());
    }
    if (symbol.isEmpty()) {
        symbol = sanitizeSymbol(payloadName);
    } else {
        symbol = sanitizeSymbol(symbol);
    }

    SwList<PackedFile> files;
    SwString err;
    if (!collectFiles(inputDir, files, &err)) {
        std::cerr << err.toStdString() << "\n";
        return 1;
    }

    const std::string headerText = outputSource.empty()
                                       ? generatedHeaderText(symbol, payloadName, files)
                                       : generatedDeclarationHeaderText(symbol);
    if (!writeTextFile(outputHeader, headerText, &err)) {
        std::cerr << err.toStdString() << "\n";
        return 1;
    }

    if (!outputSource.empty()) {
        const SwString headerIncludeName(outputHeader.filename().generic_string());
        const bool splitBlobs = !blobOutputDir.empty();
        const std::string sourceText = splitBlobs
                                           ? generatedManifestSourceText(symbol,
                                                                         payloadName,
                                                                         files,
                                                                         headerIncludeName,
                                                                         blobChunkSize)
                                           : generatedSourceText(symbol,
                                                                 payloadName,
                                                                 files,
                                                                 headerIncludeName);
        if (!writeTextFile(outputSource, sourceText, &err)) {
            std::cerr << err.toStdString() << "\n";
            return 1;
        }

        if (splitBlobs) {
            size_t blobIndex = 0;
            for (size_t i = 0; i < files.size(); ++i) {
                const size_t chunkCount = chunkCountForBytes(files[i].storedData, blobChunkSize);
                for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
                    const SwByteArray chunk = chunkBytes(files[i].storedData, chunkIndex, blobChunkSize);
                    const std::filesystem::path blobSource =
                        blobOutputDir / ("VigilVpnPayloadBlob_" + std::to_string(blobIndex) + ".cpp");
                    const std::string blobText =
                        generatedBlobSourceText(symbol, chunk, blobIndex, headerIncludeName);
                    if (!writeTextFile(blobSource, blobText, &err)) {
                        std::cerr << err.toStdString() << "\n";
                        return 1;
                    }
                    ++blobIndex;
                }
            }
            if (expectedBlobCount > 0 && blobIndex > expectedBlobCount) {
                std::cerr << "Generated more blob sources than CMake declared.\n";
                return 1;
            }
            while (blobIndex < expectedBlobCount) {
                const std::filesystem::path blobSource =
                    blobOutputDir / ("VigilVpnPayloadBlob_" + std::to_string(blobIndex) + ".cpp");
                if (!writeTextFile(blobSource, generatedEmptyBlobSourceText(headerIncludeName), &err)) {
                    std::cerr << err.toStdString() << "\n";
                    return 1;
                }
                ++blobIndex;
            }
        }
    }

    std::cout << "Packed " << files.size() << " files from " << inputDir.string()
              << " into " << outputHeader.string();
    if (!outputSource.empty()) {
        std::cout << " and " << outputSource.string();
    }
    std::cout << "\n";
    return 0;
}
