#ifndef STACKTRACE_H
#define STACKTRACE_H

#include <string>

namespace StackTrace {

/**
 * @brief Prints the current stack trace to stderr.
 *
 * This function attempts to retrieve and print the current call stack,
 * including demangled C++ function names where possible. It's designed
 * to be relatively safe to call from signal handlers or exception handlers.
 *
 * @param reason An optional string describing the reason for printing the stack trace (e.g., signal number, exception type).
 */
void print_stacktrace(const std::string& reason = "");

} // namespace StackTrace

#endif // STACKTRACE_H
