#ifndef PROJECT_UTILS_H
#define PROJECT_UTILS_H

#include <filesystem>
#include <vector>

// Function to get the project root directory
std::filesystem::path getProjectRootDir();

// Function to get or create the problem processing directory
std::filesystem::path getProblemProcessingDir();

// Get the domain name as defined in the (define (domain <name_domain>) part of the domain file
std::string getDomaineNameFromDomainFile(const std::string &domainFile);

// Get the requirements of the domain file as a vector of strings (e.g: (:requirements :typing :hierarchy :fluents) -> [":typing", ":hierarchy", ":fluents"])
std::vector<std::string> getDomainFileRequirements(const std::string &domainFile);

#endif // PROJECT_UTILS_H
