#include "util/command_utils.h"
#include "util/log.h"
#include <cstdlib>
#include <filesystem>
#include <array>

int runCommand(const std::string &command, const std::string &error_message)
{
    Log::d("Executing command: %s\n", command.c_str());
    int ret = std::system(command.c_str());
    if (ret != 0)
    {
        Log::e("Error: %s\n", error_message.c_str());
    }
    return ret;
}

bool checkCommandOutput(const std::string &command, const std::string &searchString)
{
    std::array<char, 128> buffer;
    std::string result;
    std::shared_ptr<FILE> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe)
        throw std::runtime_error("popen() failed!");

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr)
    {
        result += buffer.data();
    }

    return result.find(searchString) != std::string::npos;
}
