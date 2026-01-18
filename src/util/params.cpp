
#include "assert.h"

#include "util/params.h"
#include "util/log.h"

/**
 * Taken from Hordesat:ParameterProcessor.h by Tomas Balyo.
 */
void Parameters::init(int argc, char **argv)
{
    setDefaults();
    for (int i = 1; i < argc; i++)
    {
        char *arg = argv[i];
        if (arg[0] != '-')
        {
            if (_domain_filename == "")
                _domain_filename = std::string(arg);
            else if (_problem_filename == "")
                _problem_filename = std::string(arg);
            else
            {
                Log::w("Unrecognized parameter %s.", arg);
                printUsage();
                exit(1);
            }
            continue;
        }
        char *eq = strchr(arg, '=');
        if (eq == NULL)
        {
            char *left = arg + 1;
            auto it = _params.find(left);
            if (it != _params.end() && it->second == "0")
                it->second = "1";
            else
                _params[left];
        }
        else
        {
            *eq = 0;
            char *left = arg + 1;
            char *right = eq + 1;
            _params[left] = right;
        }
    }
}

void Parameters::setDefaults()
{
    setParam("cleanup", "1"); // clean up before exit?
    setParam("co", "1");      // colored output
    setParam("s", "0");       // random seed
    setParam("v", "2");       // verbosity
    setParam("vp", "0");      // Verify plan
    setParam("wf", "0");      // output formula to f.cnf
    setParam("wp", "0");      // output plan to plan.txt
    setParam("pvn", "0");     // Print variable names
    setParam("po", "0");      // Partial order encoding
    setParam("mutex", "0");   // Use mutexes during the encoding (enabled by default)
    setParam("precsEffs", "0"); // Compute and use preconditions and effects of methods
    setParam("nsp", "0");     // No split parameters
    setParam("removeMethodPrecAction", "0"); // Remove the special first subtask of the method which contains its preconditions and set instead the preconditions at the method level
    setParam("sibylsat", "0"); // Use the sibylsat expansion
}

void Parameters::printUsage()
{

    Log::setForcePrint(true);

    Log::i("Usage: treerex <domainfile> <problemfile> [options]\n");
    Log::i("  <domainfile>  Path to domain file in HDDL format.\n");
    Log::i("  <problemfile> Path to problem file in HDDL format.\n");
    Log::i("\n");
    Log::i("Option syntax: -OPTION or -OPTION=VALUE .\n");
    Log::i("\n");
    Log::i(" -wf=<0|1>           Write generated formula to text file \"f.cnf\" (with assumptions used in final call)\n");
    Log::i("\n");
    printParams();
    Log::setForcePrint(false);
}

std::string Parameters::getDomainFilename()
{
    return _domain_filename;
}
std::string Parameters::getProblemFilename()
{
    return _problem_filename;
}

void Parameters::printParams()
{
    std::string out = "";
    for (auto it = _params.begin(); it != _params.end(); ++it)
    {
        if (it->second.empty())
        {
            out += "-" + it->first + " ";
        }
        else
        {
            out += "-" + it->first + "=" + it->second + " ";
        }
    }
    Log::i("Called with parameters: %s\n", out.c_str());
}

void Parameters::setParam(const char *name)
{
    _params[name];
}

void Parameters::setParam(const char *name, const char *value)
{
    _params[name] = value;
}

bool Parameters::isSet(const std::string &name) const
{
    return _params.count(name);
}

bool Parameters::isNonzero(const std::string &intParamName) const
{
    return atoi(_params.at(intParamName).c_str()) != 0;
}

std::string Parameters::getParam(const std::string &name, const std::string &defaultValue)
{
    if (isSet(name))
    {
        return _params[name];
    }
    else
    {
        return defaultValue;
    }
}

std::string Parameters::getParam(const std::string &name)
{
    return getParam(name, "ndef");
}

int Parameters::getIntParam(const std::string &name, int defaultValue)
{
    if (isSet(name))
    {
        return atoi(_params[name].c_str());
    }
    else
    {
        return defaultValue;
    }
}

int Parameters::getIntParam(const std::string &name)
{
    assert(isSet(name));
    return atoi(_params[name].c_str());
}

float Parameters::getFloatParam(const std::string &name, float defaultValue)
{
    if (isSet(name))
    {
        return atof(_params[name].c_str());
    }
    else
    {
        return defaultValue;
    }
}

float Parameters::getFloatParam(const std::string &name)
{
    assert(isSet(name));
    return atof(_params[name].c_str());
}
