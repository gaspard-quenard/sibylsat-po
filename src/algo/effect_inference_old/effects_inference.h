#ifndef EFFECTS_INFERENCE_H
#define EFFECTS_INFERENCE_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set> // For recursion detection
#include <optional>
#include <utility> // For std::pair

#include "data/htn_instance.h"
#include "data/method.h"
#include "data/action.h"
#include "data/abstract_task.h"
#include "data/mutex.h" // Include Mutex header
#include "util/log.h" // Include for logging
#include "util/names.h" // Include for TOSTR

// Structure to hold both positive and negative effects
struct EffectsSet {
    std::unordered_set<int> positive;
    std::unordered_set<int> negative;

    // Helper to check if empty
    bool isEmpty() const {
        return positive.empty() && negative.empty();
    }
};

class EffectsInference {
private:
    const HtnInstance& _instance;

    // Caching for computed effects to handle recursion and improve performance
    std::unordered_map<int, EffectsSet> _possible_effects_cache;    // Stores original possible effects
    std::unordered_map<int, EffectsSet> _certified_effects_cache;   // Stores original certified effects
    std::unordered_map<int, std::unordered_set<int>> _preconditions_cache; // Stores mutex-refined possible effects

    // Caching for subtask ordering analysis within a method
    struct SubtaskOrderingInfo {
        std::unordered_map<int, std::unordered_set<int>> successors; // Map subtask_idx -> set of successor subtask_idx
        std::unordered_map<int, std::unordered_set<int>> predecessors; // Map subtask_idx -> set of predecessor subtask_idx
        std::unordered_map<int, std::unordered_set<int>> parallel;     // Map subtask_idx -> set of parallel subtask_idx
        std::vector<std::vector<int>> adj; // Adjacency list for transitive closure
        std::vector<std::vector<int>> rev_adj; // Reversed adjacency list
        bool has_cycle = false;
    };
    std::unordered_map<int, SubtaskOrderingInfo> _ordering_info_cache; // Map method_id -> ordering info

    // Helper function to compute or retrieve ordering info for a method
    const SubtaskOrderingInfo& getOrderingInfo(int method_id);

    // Helper function for transitive closure using DFS
    void transitiveClosureDFS(int u, int current_node, const std::vector<std::vector<int>>& adj, std::vector<bool>& visited, std::unordered_set<int>& reachable);

    // Recursive function to compute possible effects
    EffectsSet computePossibleEffectsRecursive(int method_id, std::set<int>& recursion_stack);

    // Recursive function to compute certified effects
    EffectsSet computeCertifiedEffectsRecursive(int method_id, std::set<int>& recursion_stack);

    std::unordered_set<int> computePreconditionsRecursive(int method_id, std::set<int> &recursion_stack_prec, std::set<int> &recursion_stack_poss_effects);

    // Helper function to get possible effects of all tasks that *might* execute after a given subtask
    EffectsSet getCombinedPossibleEffectsOfSuccessors(int subtask_idx, int method_id, const SubtaskOrderingInfo& ordering_info, std::set<int>& recursion_stack);
    EffectsSet getCombinedPossibleEffectsOfPredecessors(int subtask_idx, int method_id, const SubtaskOrderingInfo &ordering_info, std::set<int> &recursion_stack_poss_effects);

    // Helper functions for set operations
    static std::unordered_set<int> setUnion(const std::unordered_set<int>& set1, const std::unordered_set<int>& set2);
    static std::unordered_set<int> setIntersection(const std::unordered_set<int>& set1, const std::unordered_set<int>& set2);
    static std::unordered_set<int> setDifference(const std::unordered_set<int>& set1, const std::unordered_set<int>& set2);

    // --- Mutex Refinement ---
    // Helper function to apply mutex filtering for a single method
    EffectsSet applyMutexRefinement(int method_id, Mutex& mutex);


public:
    EffectsInference(const HtnInstance& instance);

    void calculateAllMethodsPrecsAndEffs(std::vector<Method>& methods, Mutex* mutex); // Compute preconditions and effects for all methods


    // Main function to trigger the computation for all methods
    void calculateAllMethodEffects();

    // TEST SCC
    void calculateAllMethodPossibleEffects();
    // END TEST

    // Should be called after calculateAllMethodEffects() and refineAllPossibleEffectsWithMutex()
    void calculateAllMethodPreconditions();

    // Optional function to refine possible effects using mutexes after initial calculation
    void refineAllPossibleEffectsWithMutex(Mutex& mutex);

    // If a precondition is mutex with a possible negative effect, remove it
    void refineAllPossibleNegativeEffectsWithMutexAndPrecMethods(Mutex& mutex);

    // Getters for the computed effects
    std::optional<EffectsSet> getPossibleEffects(int method_id) const;    // Gets original possible effects
    std::optional<EffectsSet> getCertifiedEffects(int method_id) const;   // Gets original certified effects
    std::optional<std::unordered_set<int>> getPreconditions(int method_id) const;

    void clearCaches(); // Clear all caches
};

#endif // EFFECTS_INFERENCE_H
