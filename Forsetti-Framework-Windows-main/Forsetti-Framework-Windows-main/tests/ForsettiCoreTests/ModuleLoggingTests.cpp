// Forsetti Framework for Windows
// Copyright (c) 2026 James Daley. All Rights Reserved.
// Proprietary and Confidential. Patent Pending.

#include "CppUnitTest.h"
#include "TestHelpers.h"
#include "ForsettiCore/ForsettiLogger.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace Forsetti;
using namespace Forsetti::Tests;

TEST_CLASS(ModuleLoggingTests)
{
public:

    TEST_METHOD(RecordingLogger_CapturesEntry)
    {
        RecordingLogger logger;
        logger.log(LogLevel::Info, "hello");
        Assert::AreEqual(size_t(1), logger.entries.size());
        Assert::AreEqual(std::string("hello"), logger.entries[0].message);
    }

    TEST_METHOD(RecordingLogger_CapturesLevel)
    {
        RecordingLogger logger;
        logger.log(LogLevel::Error, "oops");
        Assert::IsTrue(logger.entries[0].level == LogLevel::Error);
    }

    TEST_METHOD(RecordingLogger_CapturesSourceModuleID)
    {
        RecordingLogger logger;
        logger.log(LogLevel::Debug, "debug msg", "com.test.module");
        Assert::AreEqual(std::string("com.test.module"), logger.entries[0].sourceModuleID);
    }

    TEST_METHOD(RecordingLogger_MultipleEntries)
    {
        RecordingLogger logger;
        logger.log(LogLevel::Info, "first");
        logger.log(LogLevel::Warning, "second");
        logger.log(LogLevel::Error, "third");
        Assert::AreEqual(size_t(3), logger.entries.size());
    }

    TEST_METHOD(RecordingLogger_EmptyModuleID)
    {
        RecordingLogger logger;
        logger.log(LogLevel::Info, "no module");
        Assert::AreEqual(std::string(""), logger.entries[0].sourceModuleID);
    }

    TEST_METHOD(LogLevelToString_AllLevels)
    {
        Assert::AreEqual(std::string("DEBUG"), logLevelToString(LogLevel::Debug));
        Assert::AreEqual(std::string("INFO"), logLevelToString(LogLevel::Info));
        Assert::AreEqual(std::string("WARNING"), logLevelToString(LogLevel::Warning));
        Assert::AreEqual(std::string("ERROR"), logLevelToString(LogLevel::Error));
    }

    TEST_METHOD(ConsoleLogger_DoesNotThrow)
    {
        ConsoleLogger logger;
        // Just verify it doesn't throw
        logger.log(LogLevel::Info, "test message", "test.module");
        logger.log(LogLevel::Error, "error message", "", {{"key", "value"}});
        Assert::IsTrue(true);
    }

    TEST_METHOD(RecordingLogger_MetadataIgnoredInStorage)
    {
        // RecordingLogger doesn't store metadata, but it shouldn't crash
        RecordingLogger logger;
        logger.log(LogLevel::Info, "with metadata", "mod.id",
                   {{"key1", "val1"}, {"key2", "val2"}});
        Assert::AreEqual(size_t(1), logger.entries.size());
        Assert::AreEqual(std::string("with metadata"), logger.entries[0].message);
    }
};
