#ifndef COMMAND_UTILS_H
#define COMMAND_UTILS_H

#include <string>

/**
 * Execute a system command and return its success status.
 * 
 * @param command The command to execute.
 * @param error_message The error message to log if the command fails.
 * @return 0 if successful, non-zero if an error occurs.
 */
int runCommand(const std::string &command, const std::string &error_message);

/**
 * Execute a system command and check if the output contains a specific string.
 * 
 * @param command The command to execute.
 * @param searchString The string to search for in the command output.
 * @return true if the output contains the string, false otherwise.
 */
bool checkCommandOutput(const std::string &command, const std::string &searchString);

#endif // COMMAND_UTILS_H
