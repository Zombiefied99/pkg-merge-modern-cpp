// pkgmerge.cpp

// Remove this line unless your Visual Studio project uses precompiled headers.
 #include "stdafx.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

using std::map;
using std::string;
using std::vector;

struct Package
{
    int part = 0;
    fs::path file;
    vector<Package> parts;

    bool operator<(const Package& rhs) const
    {
        return part < rhs.part;
    }
};

constexpr std::array<unsigned char, 4> PKG_MAGIC = {
    0x7F, 0x43, 0x4E, 0x54
};

bool hasPkgMagic(const fs::path& file)
{
    std::ifstream input(file, std::ios::binary);

    if (!input)
    {
        return false;
    }

    std::array<unsigned char, 4> magic{};

    input.read(
        reinterpret_cast<char*>(magic.data()),
        static_cast<std::streamsize>(magic.size())
    );

    return input.gcount() == static_cast<std::streamsize>(magic.size()) &&
        magic == PKG_MAGIC;
}

bool appendFile(
    const fs::path& source,
    std::ofstream& destination,
    int partNumber)
{
    std::ifstream input(source, std::ios::binary);

    if (!input)
    {
        std::cerr
            << "[error] could not open part file: "
            << source.string()
            << '\n';

        return false;
    }

    std::error_code error;
    const std::uintmax_t totalSize = fs::file_size(source, error);

    if (error)
    {
        std::cerr
            << "[error] could not determine file size for: "
            << source.string()
            << '\n';

        return false;
    }

    constexpr std::size_t BUFFER_SIZE = 512 * 1024;
    vector<char> buffer(BUFFER_SIZE);

    std::uintmax_t copied = 0;

    while (input)
    {
        input.read(
            buffer.data(),
            static_cast<std::streamsize>(buffer.size())
        );

        const std::streamsize bytesRead = input.gcount();

        if (bytesRead <= 0)
        {
            break;
        }

        destination.write(buffer.data(), bytesRead);

        if (!destination)
        {
            std::cerr
                << "\n[error] failed writing part "
                << partNumber
                << '\n';

            return false;
        }

        copied += static_cast<std::uintmax_t>(bytesRead);

        const double percentage =
            totalSize == 0
            ? 100.0
            : static_cast<double>(copied) /
            static_cast<double>(totalSize) * 100.0;

        std::cout
            << "\r\t[work] merged "
            << copied
            << "/"
            << totalSize
            << " bytes ("
            << static_cast<int>(percentage)
            << "%) for part "
            << partNumber
            << "..."
            << std::flush;
    }

    std::cout << "done\n";
    return true;
}

void mergePackages(map<string, Package>& packages)
{
    for (auto& root : packages)
    {
        Package& package = root.second;

        std::sort(package.parts.begin(), package.parts.end());

        const std::size_t pieceCount = package.parts.size();

        std::cout
            << "[work] beginning to merge "
            << pieceCount
            << (pieceCount == 1 ? " piece" : " pieces")
            << " for package "
            << root.first
            << "...\n";

        const fs::path mergedFile =
            package.file.parent_path() /
            (root.first + "-merged.pkg");

        std::error_code error;

        if (fs::exists(mergedFile, error))
        {
            fs::remove(mergedFile, error);

            if (error)
            {
                std::cerr
                    << "[error] could not remove existing output file: "
                    << mergedFile.string()
                    << '\n';

                continue;
            }
        }

        std::cout
            << "\t[work] copying root package file to new file..."
            << std::flush;

        fs::copy_file(
            package.file,
            mergedFile,
            fs::copy_options::overwrite_existing,
            error
        );

        if (error)
        {
            std::cerr
                << "\n[error] failed to copy root package: "
                << error.message()
                << '\n';

            continue;
        }

        std::cout << "done\n";

        std::ofstream output(
            mergedFile,
            std::ios::binary | std::ios::app
        );

        if (!output)
        {
            std::cerr
                << "[error] could not open merged file: "
                << mergedFile.string()
                << '\n';

            continue;
        }

        bool success = true;

        for (const Package& part : package.parts)
        {
            if (!appendFile(part.file, output, part.part))
            {
                success = false;
                break;
            }
        }

        output.close();

        if (success)
        {
            std::cout
                << "[success] created "
                << mergedFile.string()
                << '\n';
        }
    }
}

int main(int argc, char* argv[])
{
    fs::path directory;

#ifndef _DEBUG
    if (argc != 2)
    {
        std::cout
            << "No PKG directory supplied.\n"
            << "Usage: pkg-merge.exe <directory>\n";

        return 1;
    }

    directory = argv[1];
#else
    directory =
        R"(E:\Code\Go\src\github.com\Tustin\pkg-merge\Debug\pkgs)";
#endif

    std::error_code error;

    if (!fs::is_directory(directory, error))
    {
        std::cerr
            << "[error] argument '"
            << directory.string()
            << "' is not a directory\n";

        return 1;
    }

    map<string, Package> packages;
    vector<Package> packageParts;

    for (const fs::directory_entry& entry :
        fs::directory_iterator(directory))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }

        const fs::path& path = entry.path();
        const string fileName = path.filename().string();

        if (path.extension() != ".pkg")
        {
            std::cout
                << "[warn] '"
                << fileName
                << "' is not a PKG file. Skipping...\n";

            continue;
        }

        if (fileName.find("-merged") != string::npos)
        {
            continue;
        }

        const std::size_t underscorePosition =
            fileName.find_last_of('_');

        const std::size_t extensionPosition =
            fileName.find_last_of('.');

        if (underscorePosition == string::npos ||
            extensionPosition == string::npos ||
            underscorePosition >= extensionPosition)
        {
            std::cout
                << "[warn] '"
                << fileName
                << "' does not have the expected name format. "
                << "Skipping...\n";

            continue;
        }

        const string titleId =
            fileName.substr(0, underscorePosition);

        const string partText =
            fileName.substr(
                underscorePosition + 1,
                extensionPosition - underscorePosition - 1
            );

        char* end = nullptr;
        const long parsedPart =
            std::strtol(partText.c_str(), &end, 10);

        if (end == partText.c_str() || *end != '\0')
        {
            std::cout
                << "[warn] '"
                << partText
                << "' is not a valid part number. Skipping...\n";

            continue;
        }

        Package package;
        package.part = static_cast<int>(parsedPart);
        package.file = path;

        if (hasPkgMagic(path))
        {
            package.part = 0;

            packages[titleId] = package;

            std::cout
                << "[success] found root PKG file for "
                << titleId
                << '\n';
        }
        else
        {
            packageParts.push_back(package);

            std::cout
                << "[success] found piece "
                << package.part
                << " for PKG file "
                << titleId
                << '\n';
        }
    }

    // Attach each part to its corresponding root package.
    for (const Package& part : packageParts)
    {
        const string fileName = part.file.filename().string();
        const std::size_t underscorePosition =
            fileName.find_last_of('_');

        const string titleId =
            fileName.substr(0, underscorePosition);

        const auto package = packages.find(titleId);

        if (package == packages.end())
        {
            std::cout
                << "[warn] no root PKG was found for part file '"
                << fileName
                << "'. Skipping...\n";

            continue;
        }

        package->second.parts.push_back(part);
    }

    if (packages.empty())
    {
        std::cerr << "[error] no root PKG files were found\n";
        return 1;
    }

    mergePackages(packages);

    std::cout << "\n[success] completed\n";
    return 0;
}