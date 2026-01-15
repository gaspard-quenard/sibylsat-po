#include "sat/variable_provider.h"

int VariableProvider::_running_var_id = 1;

int VariableProvider::nextVar()
{
    return _running_var_id++;
}

int VariableProvider::getMaxVar()
{
    return _running_var_id - 1;
}