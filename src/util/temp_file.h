#ifndef TEMP_FILE_H
#define TEMP_FILE_H

#include <string>
#include <cstdio>    // For std::tmpnam, L_tmpnam, std::remove
#include <filesystem>
#include <system_error> // For std::error_code
#include <cstdlib>   // For rand() fallback
#include <vector>    // For std::vector
#include <unistd.h>  // For close()
#include "log.h" // For logging potential cleanup errors (Corrected path)

// Helper RAII class for creating and automatically deleting temporary files.
class TempFile {
public:
    std::string path;

    TempFile() {
        // Generate a unique temporary filename.
        // Generate a unique temporary filename using mkstemp.
        // mkstemp is a POSIX function that creates a unique temporary file and returns a file descriptor.
        // It avoids the race condition issues associated with tmpnam.
        std::filesystem::path temp_path = std::filesystem::temp_directory_path();
        temp_path /= "temp_file_XXXXXX"; // mkstemp requires the template to end with "XXXXXX"
        std::string temp_path_str = temp_path.string();
        
        // Create a modifiable buffer from the string
        std::vector<char> temp_path_buffer(temp_path_str.begin(), temp_path_str.end());
        temp_path_buffer.push_back('\0'); // Ensure null termination
        
        int fd = mkstemp(temp_path_buffer.data());
        if (fd != -1) {
            // mkstemp replaces the "XXXXXX" with a unique string and creates the file.
            close(fd); // Close the file descriptor, we only need the path.
            path = temp_path_buffer.data();
        } else {
            // Fallback if mkstemp fails.
            path = "temp_file_fallback_" + std::to_string(rand());
            Log::w("Warning: mkstemp failed, using less safe fallback filename: %s\n", path.c_str());
        }
    }

    ~TempFile() {
        if (!path.empty()) {
            std::error_code ec;
            // Use std::filesystem::remove for better error handling
            if (!std::filesystem::remove(path, ec)) {
                // Log if removal fails, but don't throw from destructor.
                // Check if the error is simply "file not found", which might be okay.
                if (ec != std::errc::no_such_file_or_directory) {
                     Log::w("Warning: Failed to remove temporary file '%s': %s\n", path.c_str(), ec.message().c_str());
                }
            }
        }
    }

    // Disable copy and move semantics to prevent issues with double deletion.
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
};

#endif // TEMP_FILE_H
