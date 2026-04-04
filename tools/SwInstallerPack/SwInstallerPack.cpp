#include "SwInstallerPackSupport.h"

#include <filesystem>
#include <iostream>

using namespace swinstaller::pack;

namespace {

static void printUsage() {
    std::cerr << "Usage: SwInstallerPack --input <dir> --output <header> "
                 "[--symbol <function>] [--name <payload-name>]\n";
}

static bool parseArgs(int argc,
                      char* argv[],
                      std::filesystem::path& inputDir,
                      std::filesystem::path& outputHeader,
                      SwString& symbol,
                      SwString& payloadName) {
    for (int i = 1; i < argc; ++i) {
        const SwString arg = argv[i];
        if ((arg == "--input" || arg == "-i") && i + 1 < argc) {
            inputDir = std::filesystem::path(argv[++i]);
        } else if ((arg == "--output" || arg == "-o") && i + 1 < argc) {
            outputHeader = std::filesystem::path(argv[++i]);
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
    SwString symbol;
    SwString payloadName;

    if (!parseArgs(argc, argv, inputDir, outputHeader, symbol, payloadName)) {
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

    const std::string headerText = generatedHeaderText(symbol, payloadName, files);
    if (!writeTextFile(outputHeader, headerText, &err)) {
        std::cerr << err.toStdString() << "\n";
        return 1;
    }

    std::cout << "Packed " << files.size() << " files from " << inputDir.string()
              << " into " << outputHeader.string() << "\n";
    return 0;
}
