#ifndef PROCESSPILOT_TEST_UTIL_HPP
#define PROCESSPILOT_TEST_UTIL_HPP

// Minimal test helpers. Unlike assert(), CHECK is never compiled out in
// release builds and reports every failure instead of stopping at the first.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

inline int& testFailures() {
    static int failures = 0;
    return failures;
}

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            std::cerr << __FILE__ << ":" << __LINE__                        \
                      << ": CHECK failed: " #condition "\n";                \
            ++testFailures();                                               \
        }                                                                   \
    } while (0)

inline int finishTests(const char* suite) {
    if (testFailures() == 0) {
        std::cout << suite << ": all tests passed.\n";
        return 0;
    }
    std::cerr << suite << ": " << testFailures() << " check(s) failed.\n";
    return 1;
}

// Creates a fresh temporary directory for a test run.
inline std::string makeTempDir() {
    const char* base = std::getenv("TMPDIR");
    std::string pattern = std::string(base && *base ? base : "/tmp") +
                          "/processpilot-test-XXXXXX";
    if (!mkdtemp(&pattern[0])) {
        std::cerr << "mkdtemp failed\n";
        std::exit(1);
    }
    return pattern;
}

inline std::string writeFile(const std::string& path,
                             const std::string& contents) {
    std::ofstream(path) << contents;
    return path;
}

inline std::string readFile(const std::string& path) {
    std::ifstream file(path);
    std::string contents((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return contents;
}

#endif
