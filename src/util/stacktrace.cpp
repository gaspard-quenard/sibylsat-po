#include "stacktrace.h"

#include <execinfo.h> // For backtrace, backtrace_symbols
#include <dlfcn.h>    // For Dl_info
#include <cxxabi.h>   // For abi::__cxa_demangle
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

namespace StackTrace {

// Function to demangle a C++ symbol name
std::string demangle(const char* symbol) {
    size_t size;
    int status;
    char* demangled = abi::__cxa_demangle(symbol, nullptr, &size, &status);

    if (status == 0 && demangled) {
        std::string result(demangled);
        std::free(demangled);
        return result;
    } else {
        // Demangling failed, return the original symbol
        return std::string(symbol);
    }
}

void print_stacktrace(const std::string& reason) {
    std::cerr << "\n--- Stack Trace ---" << std::endl;
    if (!reason.empty()) {
        std::cerr << "Reason: " << reason << std::endl;
    }

    const int max_frames = 100; // Maximum number of frames to capture
    void* buffer[max_frames];
    int nptrs = backtrace(buffer, max_frames);
    char** strings = backtrace_symbols(buffer, nptrs);

    if (strings == nullptr) {
        std::cerr << "Error: Could not get backtrace symbols." << std::endl;
        return;
    }

    std::cerr << "Obtained " << nptrs << " stack frames:" << std::endl;

    for (int i = 0; i < nptrs; ++i) {
        // Example string format: ./executable(function+0xoffset) [address]
        // We want to extract the function name part for demangling
        std::string line(strings[i]);
        std::string function_symbol;
        std::string address_str;
        size_t open_paren = line.find('(');
        size_t plus_sign = line.find('+');
        size_t close_paren = line.find(')');
        size_t open_bracket = line.find('[');
        size_t close_bracket = line.find(']');

        if (open_paren != std::string::npos && plus_sign != std::string::npos && close_paren != std::string::npos && open_paren < plus_sign && plus_sign < close_paren) {
            function_symbol = line.substr(open_paren + 1, plus_sign - open_paren - 1);
        } else {
            // Fallback if format is unexpected
            function_symbol = line; // Use the whole line as symbol if parsing fails
        }

        if (open_bracket != std::string::npos && close_bracket != std::string::npos && open_bracket < close_bracket) {
             address_str = line.substr(open_bracket + 1, close_bracket - open_bracket - 1);
        }


        std::string demangled_name = demangle(function_symbol.c_str());

        std::cerr << "#" << i << "  ";
        if (!address_str.empty()) {
             std::cerr << address_str << " in ";
        }
        std::cerr << demangled_name;

        // Try to get more info using dladdr if possible (might not always work)
        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_fname) {
             std::cerr << " (" << info.dli_fname << ")";
        }

        std::cerr << std::endl;
    }

    std::free(strings); // Free the memory allocated by backtrace_symbols
    std::cerr << "--- End Stack Trace ---" << std::endl;
}

} // namespace StackTrace
