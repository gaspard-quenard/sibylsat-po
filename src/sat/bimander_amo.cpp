
#include "bimander_amo.h"

#include <cmath>
#include <cassert>
#include "sat/variable_provider.h"
#include "util/log.h"

BimanderAtMostOne::BimanderAtMostOne(const std::vector<int>& states, size_t numStates, size_t numSubsets)
    : _states(states.begin(), states.begin() + numStates), _num_states(numStates), _num_subsets(numSubsets) {

    // Set up helper variables for a binary number representation
    _num_repr_states = 1;
    size_t num_bins = std::ceil(std::log2(numSubsets));
    while (_num_repr_states < numSubsets) {
        int var = VariableProvider::nextVar();
        Log::d("VARMAP %i (__amo_%i-%i_%i)\n", var, _states[0], _states[_states.size()-1], _bin_num_vars.size());
        _bin_num_vars.push_back(var);
        _num_repr_states *= 2;
    }
}
std::vector<std::vector<int>> BimanderAtMostOne::encode() {
    std::vector<std::vector<int>> cls;

    if (_num_states <= 1) return cls;

    // Divide states into subsets
    size_t groupSize = std::ceil(double(_num_states) / _num_subsets);
    for (size_t i = 0; i < _num_subsets; ++i) {
        size_t start = i * groupSize;
        size_t end = std::min((i + 1) * groupSize, _num_states);
        for (size_t j = start; j < end; ++j) {
            for (size_t k = j + 1; k < end; ++k) {
                cls.push_back({-_states[j], -_states[k]});
            }
        }
    }

    // Linking constraints
    // for (size_t i = 0; i < _num_subsets; ++i) {
    //     size_t start = i * groupSize;
    //     size_t end = std::min((i + 1) * groupSize, _num_states);
    //     auto digitVars = getClause(i, false);
    //     for (size_t j = start; j < end; ++j) {
    //         // If a state variable is true, the corresponding binary variables must match the subset index
    //         for (int digitVar : digitVars) {
    //             std::vector<int> ifStateThenDigit = {-_states[j], digitVar};
    //             cls.push_back(ifStateThenDigit);
    //         }
    //         // If the binary variables match the subset index, the state variable can be true
    //         std::vector<int> ifDigitsThenState = digitVars;
    //         ifDigitsThenState.push_back(_states[j]);
    //         cls.push_back(ifDigitsThenState);
    //     }
    // }
    for (size_t i = 0; i < _num_subsets; i++) {
        size_t start = i * groupSize;
        size_t end = std::min((i + 1) * groupSize, _num_states);
        for (size_t h = start; h < end; h++) {
            for (size_t j = 0; j < _bin_num_vars.size(); j++) {
                // Check if the auxiliary variable B_j must be set to true or false
                // paper: denotes 𝐵𝑗 (or ¯𝐵𝑗 ) if bit 𝑗 of the binary representation of integer i − 1 is 1 (or 0).
            
                bool bit = (i & (1 << j)) != 0;
                int auxVar = bit ? _bin_num_vars[j] : -_bin_num_vars[j];
                cls.push_back({-auxVar, -_states[h]});
            }
        }
    }

    return cls;
}

std::vector<int> BimanderAtMostOne::getClause(int state, bool sign) const {
    assert(!_bin_num_vars.empty());
    std::vector<int> cls(_bin_num_vars.size());
    for (size_t i = 0; i < _bin_num_vars.size(); i++) {
        bool mod = (state & 0x1);
        cls[i] = (!sign ^ mod ? 1 : -1) * _bin_num_vars[i];
        state >>= 1;
    }
    return cls;
}