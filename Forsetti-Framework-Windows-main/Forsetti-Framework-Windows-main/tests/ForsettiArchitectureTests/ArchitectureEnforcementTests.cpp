// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.
//
// Architecture enforcement tests — verify one-way dependency rules (R006).
// Scans #include directives in source files to ensure:
//   - ForsettiCore never includes ForsettiPlatform, ForsettiHostTemplate, or ForsettiModulesExample
//   - ForsettiPlatform never includes ForsettiHostTemplate or ForsettiModulesExample
//   - ForsettiModulesExample never includes ForsettiPlatform or ForsettiHostTemplate

#include "CppUnitTest.h"
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <regex>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
    namespace fs = std::filesystem;

    // Collect all .h and .cpp files in a directory tree
    std::vector<fs::path> collectSourceFiles(const fs::path& root) {
        std::vector<fs::path> files;
        if (!fs::exists(root)) return files;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().string();
            if (ext == ".h" || ext == ".cpp" || ext == ".hpp") {
                files.push_back(entry.path());
            }
        }
        return files;
    }

    // Extract all #include paths from a source file
    std::vector<std::string> extractIncludes(const fs::path& filePath) {
        std::vector<std::string> includes;
        std::ifstream file(filePath);
        std::string line;
        std::regex includeRegex(R"(^\s*#\s*include\s+[<"]([^>"]+)[>"])");

        while (std::getline(file, line)) {
            std::smatch match;
            if (std::regex_search(line, match, includeRegex)) {
                includes.push_back(match[1].str());
            }
        }
        return includes;
    }

    // Check if any include path references a forbidden layer
    bool includesLayer(const std::vector<std::string>& includes, const std::string& layerName) {
        for (const auto& inc : includes) {
            if (inc.find(layerName + "/") == 0 || inc.find(layerName + "\\") == 0) {
                return true;
            }
        }
        return false;
    }

    // Find project root by looking for CMakeLists.txt
    fs::path findProjectRoot() {
        // Try relative paths from typical test execution directories
        std::vector<fs::path> candidates = {
            fs::current_path(),
            fs::current_path() / "..",
            fs::current_path() / "../..",
            fs::current_path() / "../../..",
        };
        for (const auto& dir : candidates) {
            if (fs::exists(dir / "CMakeLists.txt") && fs::exists(dir / "include")) {
                return fs::canonical(dir);
            }
        }
        return fs::current_path();
    }
}

TEST_CLASS(ArchitectureEnforcementTests)
{
public:

    TEST_METHOD(ForsettiCore_MustNotInclude_ForsettiPlatform)
    {
        auto root = findProjectRoot();
        auto coreHeaders = collectSourceFiles(root / "include" / "ForsettiCore");
        auto coreSources = collectSourceFiles(root / "src" / "ForsettiCore");

        std::vector<fs::path> allCoreFiles;
        allCoreFiles.insert(allCoreFiles.end(), coreHeaders.begin(), coreHeaders.end());
        allCoreFiles.insert(allCoreFiles.end(), coreSources.begin(), coreSources.end());

        for (const auto& file : allCoreFiles) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiPlatform"),
                (L"ForsettiCore file includes ForsettiPlatform: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiCore_MustNotInclude_ForsettiHostTemplate)
    {
        auto root = findProjectRoot();
        auto coreHeaders = collectSourceFiles(root / "include" / "ForsettiCore");
        auto coreSources = collectSourceFiles(root / "src" / "ForsettiCore");

        std::vector<fs::path> allCoreFiles;
        allCoreFiles.insert(allCoreFiles.end(), coreHeaders.begin(), coreHeaders.end());
        allCoreFiles.insert(allCoreFiles.end(), coreSources.begin(), coreSources.end());

        for (const auto& file : allCoreFiles) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiHostTemplate"),
                (L"ForsettiCore file includes ForsettiHostTemplate: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiCore_MustNotInclude_ForsettiModulesExample)
    {
        auto root = findProjectRoot();
        auto coreHeaders = collectSourceFiles(root / "include" / "ForsettiCore");
        auto coreSources = collectSourceFiles(root / "src" / "ForsettiCore");

        std::vector<fs::path> allCoreFiles;
        allCoreFiles.insert(allCoreFiles.end(), coreHeaders.begin(), coreHeaders.end());
        allCoreFiles.insert(allCoreFiles.end(), coreSources.begin(), coreSources.end());

        for (const auto& file : allCoreFiles) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiModulesExample"),
                (L"ForsettiCore file includes ForsettiModulesExample: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiPlatform_MustNotInclude_ForsettiHostTemplate)
    {
        auto root = findProjectRoot();
        auto platformHeaders = collectSourceFiles(root / "include" / "ForsettiPlatform");
        auto platformSources = collectSourceFiles(root / "src" / "ForsettiPlatform");

        std::vector<fs::path> allPlatformFiles;
        allPlatformFiles.insert(allPlatformFiles.end(), platformHeaders.begin(), platformHeaders.end());
        allPlatformFiles.insert(allPlatformFiles.end(), platformSources.begin(), platformSources.end());

        for (const auto& file : allPlatformFiles) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiHostTemplate"),
                (L"ForsettiPlatform file includes ForsettiHostTemplate: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiPlatform_MustNotInclude_ForsettiModulesExample)
    {
        auto root = findProjectRoot();
        auto platformHeaders = collectSourceFiles(root / "include" / "ForsettiPlatform");
        auto platformSources = collectSourceFiles(root / "src" / "ForsettiPlatform");

        std::vector<fs::path> allPlatformFiles;
        allPlatformFiles.insert(allPlatformFiles.end(), platformHeaders.begin(), platformHeaders.end());
        allPlatformFiles.insert(allPlatformFiles.end(), platformSources.begin(), platformSources.end());

        for (const auto& file : allPlatformFiles) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiModulesExample"),
                (L"ForsettiPlatform file includes ForsettiModulesExample: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiModulesExample_MustNotInclude_ForsettiPlatform)
    {
        auto root = findProjectRoot();
        auto exampleSources = collectSourceFiles(root / "src" / "ForsettiModulesExample");

        for (const auto& file : exampleSources) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiPlatform"),
                (L"ForsettiModulesExample file includes ForsettiPlatform: " + file.wstring()).c_str());
        }
    }

    TEST_METHOD(ForsettiModulesExample_MustNotInclude_ForsettiHostTemplate)
    {
        auto root = findProjectRoot();
        auto exampleSources = collectSourceFiles(root / "src" / "ForsettiModulesExample");

        for (const auto& file : exampleSources) {
            auto includes = extractIncludes(file);
            Assert::IsFalse(includesLayer(includes, "ForsettiHostTemplate"),
                (L"ForsettiModulesExample file includes ForsettiHostTemplate: " + file.wstring()).c_str());
        }
    }
};
