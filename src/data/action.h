#ifndef ACTION_H
#define ACTION_H

#include <vector>

#include "data/predicate.h"

class Action // : public HTNOp
{

private:
    int _id;
    std::string _name;
    std::vector<int> _preconditions_idx;
    std::vector<int> _pos_effs_idx;
    std::vector<int> _neg_effs_idx;

public:
    Action(int id, std::vector<int> preconditions_idx, std::vector<int> pos_effs_idx, std::vector<int> neg_effs_idx) : _id(id), _preconditions_idx(preconditions_idx), _pos_effs_idx(pos_effs_idx), _neg_effs_idx(neg_effs_idx) {}

    void addName(std::string name) { _name = name; }
    
    const std::vector<int> &getPreconditionsIdx() const { return _preconditions_idx; }
    const std::vector<int> &getPosEffsIdx() const { return _pos_effs_idx; }
    const std::vector<int> &getNegEffsIdx() const { return _neg_effs_idx; }
    const std::string getName() const { return _name; }
    const int getId() const { return _id; }
};

#endif // ACTION_H