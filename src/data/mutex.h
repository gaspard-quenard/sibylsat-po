#ifndef MUTEX_H
#define MUTEX_H

#include <unordered_set>
#include <unordered_map>
#include <vector>

class Mutex
{

private:
    std::vector<std::vector<int>> _mutex_groups;          // Vector of mutex groups, each group is a set of predicate indices which are mutually exclusive
    std::unordered_map<int, std::unordered_set<int>> _mutex_map; // Key: predicate index, Value: group of mutexes which contain the predicate

public:
    Mutex() = default;
    ~Mutex() = default;
    void addMutexGroup(const std::vector<int> &mutex_group);
    const std::vector<int> &getMutexGroup(int group_idx) const;
    const std::vector<std::vector<int>> &getMutexGroups() const { return _mutex_groups; }
    const std::unordered_set<int> &getMutexGroupsOfPred(int pred_idx);
    void printMutexGroups() const;
};

#endif // MUTEX_H