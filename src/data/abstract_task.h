#ifndef ABSTRACT_TASK_H
#define ABSTRACT_TASK_H

#include "data/predicate.h"

class AbstractTask // : public HTNOp
{

private:
    int _id;
    std::string _name;
    std::vector<int> _decomposition_methods_idx;

public:
    AbstractTask(int id, std::string name) : _id(id), _name(name) {}

    const std::string getName() const
    {
        return _name;
    }

    void addDecompositionMethod(int method_idx)
    {
        _decomposition_methods_idx.push_back(method_idx);
    }

    const std::vector<int> &getDecompositionMethodsIdx() const
    {
        return _decomposition_methods_idx;
    }

    const int getId() const
    {
        return _id;
    }
};

#endif // ABSTRACT_TASK_H