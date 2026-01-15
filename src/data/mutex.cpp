#include "data/mutex.h"
#include "util/log.h"
#include "util/names.h"

void Mutex::addMutexGroup(const std::vector<int> &mutex_group)
{
    _mutex_groups.push_back(mutex_group);
    int group_idx = _mutex_groups.size() - 1;
    for (int pred_idx : mutex_group)
    {
        _mutex_map[pred_idx].insert(group_idx);
    }
}

const std::vector<int> &Mutex::getMutexGroup(int group_idx) const
{
    return _mutex_groups[group_idx];
}

const std::unordered_set<int> &Mutex::getMutexGroupsOfPred(int pred_idx)
{
    return _mutex_map[pred_idx];
}

void Mutex::printMutexGroups() const
{
    for (size_t i = 0; i < _mutex_groups.size(); ++i)
    {
        Log::i("Mutex group %zu: ", i);
        for (int pred_idx : _mutex_groups[i])
        {
            Log::i("  %d", pred_idx);
        }
        Log::i("--------------------\n");
    }
}