#include <string>
#include <fstream>
#include <regex>
#include <stdexcept>

#include "project_utils.h"

// Macro to convert preprocessor macro to string
#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

std::filesystem::path getProjectRootDir()
{
    return std::filesystem::path(TO_STRING(PROJECT_ROOT_DIR));
}

std::filesystem::path getProblemProcessingDir()
{
    std::filesystem::path problemProcessingDir = getProjectRootDir() / "ProblemProcessing";

    // Check if the directory exists; if not, create it
    if (!std::filesystem::exists(problemProcessingDir))
    {
        std::filesystem::create_directories(problemProcessingDir);
    }

    return problemProcessingDir;
}

std::string getDomaineNameFromDomainFile(const std::string &domainFile)
{
    std::ifstream file(domainFile);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + domainFile);
    }

    std::string line;
    // Read file line by line
    while (std::getline(file, line))
    {
        // Remove any potential carriage returns
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // Use regex to match the pattern (define (domain name) and capture the domain name
        // This handles any number of spaces between words
        std::regex pattern(R"(\s*\(\s*define\s*\(\s*domain\s+([a-zA-Z0-9_-]+)\s*\))");
        std::smatch matches;

        if (std::regex_search(line, matches, pattern))
        {
            if (matches.size() >= 2)
            {
                return matches[1].str(); // Return the captured domain name
            }
        }
    }

    throw std::runtime_error("No domain definition found in file: " + domainFile);
}

std::vector<std::string> getDomainFileRequirements(const std::string &domainFile)
{
    std::ifstream file(domainFile);
    if (!file.is_open())
    {
        throw std::runtime_error("Could not open file: " + domainFile);
    }

    std::string line;
    // Read file line by line
    while (std::getline(file, line))
    {
        // Remove any potential carriage returns
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        // Use regex to match the pattern (:requirements :<requirement1> :<requirement2> ...)
        // This handles any number of spaces between words
        std::regex pattern(R"(\s*\(\s*:requirements\s+(.*)\))");
        std::smatch matches;

        if (std::regex_search(line, matches, pattern))
        {
            if (matches.size() >= 2)
            {
                std::string requirements = matches[1].str();
                std::vector<std::string> reqs;
                // Split the requirements by space
                std::istringstream iss(requirements);
                for (std::string req; iss >> req;)
                {
                    reqs.push_back(req);
                }
                return reqs; // Return the captured requirements
            }
        }
    }

    throw std::runtime_error("No requirements found in file: " + domainFile);
}