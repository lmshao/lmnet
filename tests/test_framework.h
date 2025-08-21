/**
 * @file test_framework.h
 * @brief Unit Test Framework
 * @author SHAO Liming <lmshao@163.com>
 * @copyright Copyright (c) 2025 SHAO Liming
 * @license MIT
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <cassert>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// Simple test macros
#define TEST(test_suite, test_name)                                                                                    \
    void test_suite##_##test_name();                                                                                   \
    static TestRegistrar test_suite##_##test_name##_registrar(#test_suite, #test_name, test_suite##_##test_name);      \
    void test_suite##_##test_name()

#define EXPECT_TRUE(condition)                                                                                         \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected true but got false" << std::endl;     \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_FALSE(condition)                                                                                        \
    do {                                                                                                               \
        if (condition) {                                                                                               \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected false but got true" << std::endl;     \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_EQ(expected, actual)                                                                                    \
    do {                                                                                                               \
        if ((expected) != (actual)) {                                                                                  \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected " << (expected) << " but got "        \
                      << (actual) << std::endl;                                                                        \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_NE(expected, actual)                                                                                    \
    do {                                                                                                               \
        if ((expected) == (actual)) {                                                                                  \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected not equal to " << (expected)          \
                      << std::endl;                                                                                    \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_GT(val1, val2)                                                                                          \
    do {                                                                                                               \
        if (!((val1) > (val2))) {                                                                                      \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected " << (val1) << " > " << (val2)        \
                      << std::endl;                                                                                    \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

#define EXPECT_GE(val1, val2)                                                                                          \
    do {                                                                                                               \
        if (!((val1) >= (val2))) {                                                                                     \
            std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " - Expected " << (val1) << " >= " << (val2)       \
                      << std::endl;                                                                                    \
            TestRunner::getInstance().recordFailure();                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

// Test structure
struct TestCase {
    std::string suite_name;
    std::string test_name;
    std::function<void()> test_func;

    TestCase(const std::string &suite, const std::string &name, std::function<void()> func)
        : suite_name(suite), test_name(name), test_func(func)
    {
    }
};

// Test runner
class TestRunner {
public:
    static TestRunner &getInstance()
    {
        static TestRunner instance;
        return instance;
    }

    void registerTest(const TestCase &test) { tests_.push_back(test); }

    void recordFailure()
    {
        current_test_failed_ = true;
        total_failures_++;
    }

    int runAllTests()
    {
        std::cout << "Running " << tests_.size() << " tests..." << std::endl;

        int passed = 0;
        int failed = 0;

        for (const auto &test : tests_) {
            current_test_failed_ = false;
            std::cout << "[ RUN      ] " << test.suite_name << "." << test.test_name << std::endl;

            try {
                test.test_func();
                if (!current_test_failed_) {
                    std::cout << "[       OK ] " << test.suite_name << "." << test.test_name << std::endl;
                    passed++;
                } else {
                    std::cout << "[  FAILED  ] " << test.suite_name << "." << test.test_name << std::endl;
                    failed++;
                }
            } catch (const std::exception &e) {
                std::cout << "[  FAILED  ] " << test.suite_name << "." << test.test_name << " - Exception: " << e.what()
                          << std::endl;
                failed++;
            }
        }

        std::cout << "\n[==========] " << tests_.size() << " tests ran." << std::endl;
        std::cout << "[  PASSED  ] " << passed << " tests." << std::endl;
        if (failed > 0) {
            std::cout << "[  FAILED  ] " << failed << " tests." << std::endl;
        }

        return failed == 0 ? 0 : 1;
    }

private:
    std::vector<TestCase> tests_;
    bool current_test_failed_ = false;
    int total_failures_ = 0;
};

// Test registrar
class TestRegistrar {
public:
    TestRegistrar(const std::string &suite_name, const std::string &test_name, std::function<void()> test_func)
    {
        TestRunner::getInstance().registerTest(TestCase(suite_name, test_name, test_func));
    }
};

// Main function for test executables
#define RUN_ALL_TESTS()                                                                                                \
    int main()                                                                                                         \
    {                                                                                                                  \
        return TestRunner::getInstance().runAllTests();                                                                \
    }

#endif // TEST_FRAMEWORK_H
