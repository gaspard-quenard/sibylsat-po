#ifndef METHOD_H
#define METHOD_H
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "data/predicate.h"


class Method // : public HTNOp
{

private:
    int _id;
    std::string _name;
    int _parent_task_idx;
    std::vector<int> _subtasks_idx;

    std::vector<std::pair<int, int>> _ordering_constraints; // For each subtask idx, the ordering constraints

    std::unordered_set<int> _preconditions_idx;
    std::unordered_set<int> _pos_effs_idx;
    std::unordered_set<int> _neg_effs_idx;
    std::unordered_set<int> _poss_pos_effs_idx;
    std::unordered_set<int> _poss_neg_effs_idx;

public:
    Method(int id, std::string name, int parent_task_idx, std::vector<int> subtasks_idx, std::vector<std::pair<int, int>> ordering_constraints = {})
        : _id(id), _name(name), _parent_task_idx(parent_task_idx), _subtasks_idx(subtasks_idx), _ordering_constraints(ordering_constraints) {}

    const std::string getName() const
    {
        return _name;
    }

    const int getId() const
    {
        return _id;
    }

    const std::vector<int> &getSubtasksIdx() const
    {
        return _subtasks_idx;
    }

    const std::vector<std::pair<int, int>> &getOrderingConstraints() const
    {
        return _ordering_constraints;
    }

    void addSubtask(int subtask_idx)
    {
        _subtasks_idx.push_back(subtask_idx);
    }

    void addOrderingConstraint(int idx_subtask_first, int idx_subtask_second)
    {
        _ordering_constraints.push_back({idx_subtask_first, idx_subtask_second});
    }

    void setPreconditions(std::unordered_set<int> preconditions_idx) { _preconditions_idx = preconditions_idx; }
    void setPositiveEffects(std::unordered_set<int> pos_effs_idx) { _pos_effs_idx = pos_effs_idx; }
    void setNegativeEffects(std::unordered_set<int> neg_effs_idx) { _neg_effs_idx = neg_effs_idx; }
    void setPossiblePositiveEffects(std::unordered_set<int> poss_pos_effs_idx) { _poss_pos_effs_idx = poss_pos_effs_idx; }
    void setPossibleNegativeEffects(std::unordered_set<int> poss_neg_effs_idx) { _poss_neg_effs_idx = poss_neg_effs_idx; }

    const std::unordered_set<int> &getPreconditionsIdx() const { return _preconditions_idx; }
    const std::unordered_set<int> &getPosEffsIdx() const { return _pos_effs_idx; }
    const std::unordered_set<int> &getNegEffsIdx() const { return _neg_effs_idx; }
    const std::unordered_set<int> &getPossPosEffsIdx() const { return _poss_pos_effs_idx; }
    const std::unordered_set<int> &getPossNegEffsIdx() const { return _poss_neg_effs_idx; }


    void addPreconditionIdx(int precondition_idx)
    {
        _preconditions_idx.insert(precondition_idx);
    }

    void removeFirstSubtask()
    {
        if (!_subtasks_idx.empty())
        {
            _subtasks_idx.erase(_subtasks_idx.begin());
            // Remove any ordering constrains related to the first subtask
            for (auto it = _ordering_constraints.begin(); it != _ordering_constraints.end();)
            {
                if (it->first == 0 || it->second == 0)
                {
                    it = _ordering_constraints.erase(it);
                }
                else
                {
                    // Decrease the indices of the remaining ordering constraints
                    if (it->first > 0)
                        --(it->first);
                    if (it->second > 0)
                        --(it->second);
                    ++it;
                }
            }
        }
    }
};

#endif // METHOD_H
