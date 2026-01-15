
#ifndef BIMANDER_AMO_H
#define BIMANDER_AMO_H

#include <vector>
#include <cstddef>

class BimanderAtMostOne {

private:

    std::vector<int> _states;
    size_t _num_states;
    std::vector<int> _bin_num_vars;
    size_t _num_repr_states;
    size_t _num_subsets;

public:
    BimanderAtMostOne(const std::vector<int>& states, size_t numStates, size_t sizeSubsets);
    std::vector<std::vector<int>> encode();

private:
    std::vector<int> getClause(int state, bool sign) const;

};

#endif