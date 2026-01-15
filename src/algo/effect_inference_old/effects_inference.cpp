#include "effects_inference.h"

#include <algorithm> // For std::set_union, std::set_intersection, std::set_difference, std::sort
#include <iterator>  // For std::inserter
#include <vector>
#include <queue>

// Constructor
EffectsInference::EffectsInference(const HtnInstance &instance) : _instance(instance) {}

// --- Helper functions for set operations ---

std::unordered_set<int> EffectsInference::setUnion(const std::unordered_set<int> &set1, const std::unordered_set<int> &set2)
{
    std::unordered_set<int> result = set1;
    result.insert(set2.begin(), set2.end());
    return result;
}

std::unordered_set<int> EffectsInference::setIntersection(const std::unordered_set<int> &set1, const std::unordered_set<int> &set2)
{
    std::unordered_set<int> result;
    if (set1.size() < set2.size())
    {
        for (const int &elem : set1)
        {
            if (set2.count(elem))
            {
                result.insert(elem);
            }
        }
    }
    else
    {
        for (const int &elem : set2)
        {
            if (set1.count(elem))
            {
                result.insert(elem);
            }
        }
    }
    return result;
}

std::unordered_set<int> EffectsInference::setDifference(const std::unordered_set<int> &set1, const std::unordered_set<int> &set2)
{
    std::unordered_set<int> result;
    for (const int &elem : set1)
    {
        if (set2.find(elem) == set2.end())
        {
            result.insert(elem);
        }
    }
    return result;
}

// --- Placeholder for other member function implementations ---

// Helper function to compute or retrieve ordering info for a method
const EffectsInference::SubtaskOrderingInfo &EffectsInference::getOrderingInfo(int method_id)
{
    // Implementation needed
    // Check cache first
    if (_ordering_info_cache.count(method_id))
    {
        return _ordering_info_cache.at(method_id);
    }

    // Compute if not in cache
    SubtaskOrderingInfo info;
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    const auto &constraints = method.getOrderingConstraints();
    int n = subtasks.size();

    info.adj.resize(n);
    info.rev_adj.resize(n);
    std::vector<int> in_degree(n, 0);
    std::vector<int> out_degree(n, 0); // Needed for cycle detection / LFT-like checks

    // Build graph and check for index validity
    for (const auto &constraint : constraints)
    {
        int u_idx = constraint.first;
        int v_idx = constraint.second;

        if (u_idx < 0 || u_idx >= n || v_idx < 0 || v_idx >= n)
        {
            Log::e("Error: Ordering constraint index out of range (%d or %d) for method %d (%s) with %d subtasks.\n",
                   u_idx, v_idx, method_id, method.getName().c_str(), n);
            info.has_cycle = true; // Mark as problematic
            _ordering_info_cache[method_id] = info;
            return _ordering_info_cache.at(method_id);
        }
        if (u_idx == v_idx)
        {
            Log::d("Warning: Self-loop detected in ordering constraint for subtask index %d in method %d (%s).\n",
                   u_idx, method_id, method.getName().c_str());
            continue; // Ignore self-loops for ordering logic
        }

        // Check for duplicate constraints (optional, but good practice)
        bool already_exists = false;
        for (int neighbor : info.adj[u_idx])
        {
            if (neighbor == v_idx)
            {
                already_exists = true;
                break;
            }
        }
        if (!already_exists)
        {
            info.adj[u_idx].push_back(v_idx);
            info.rev_adj[v_idx].push_back(u_idx);
            in_degree[v_idx]++;
            out_degree[u_idx]++;
        }
    }

    // Cycle Detection (using Kahn's algorithm idea)
    std::queue<int> q;
    for (int i = 0; i < n; ++i)
    {
        if (in_degree[i] == 0)
        {
            q.push(i);
        }
    }
    int count = 0;
    while (!q.empty())
    {
        int u = q.front();
        q.pop();
        count++;
        for (int v : info.adj[u])
        {
            if (--in_degree[v] == 0)
            {
                q.push(v);
            }
        }
    }
    if (count != n)
    {
        Log::d("Warning: Cycle detected in ordering constraints for method %d (%s).\n", method_id, method.getName().c_str());
        info.has_cycle = true;
        // Don't compute successors/predecessors if there's a cycle, as it's ill-defined.
        _ordering_info_cache[method_id] = info;
        return _ordering_info_cache.at(method_id);
    }

    // --- Compute Successors, Predecessors, Parallel ---
    // Only if no cycle was detected
    if (!info.has_cycle)
    {
        // Compute Successors (Transitive Closure) using DFS
        for (int i = 0; i < n; ++i)
        {
            std::vector<bool> visited(n, false);
            transitiveClosureDFS(i, i, info.adj, visited, info.successors[i]);
            info.successors[i].erase(i); // Remove self from successors
        }

        // Compute Predecessors (Transitive Closure on reversed graph) using DFS
        for (int i = 0; i < n; ++i)
        {
            std::vector<bool> visited(n, false);
            transitiveClosureDFS(i, i, info.rev_adj, visited, info.predecessors[i]);
            info.predecessors[i].erase(i); // Remove self from predecessors
        }

        // Compute Parallel Tasks
        for (int i = 0; i < n; ++i)
        {
            for (int j = i + 1; j < n; ++j)
            { // Only check pairs once
                // Check if i is NOT a successor of j AND j is NOT a successor of i
                bool i_successor_of_j = info.successors[j].count(i);
                bool j_successor_of_i = info.successors[i].count(j);

                if (!i_successor_of_j && !j_successor_of_i)
                {
                    info.parallel[i].insert(j);
                    info.parallel[j].insert(i);
                }
            }
        }
    }

    // Store in cache and return
    _ordering_info_cache[method_id] = info;
    return _ordering_info_cache.at(method_id);
}

// Helper function for transitive closure using DFS
void EffectsInference::transitiveClosureDFS(int start_node, int current_node, const std::vector<std::vector<int>> &adj, std::vector<bool> &visited, std::unordered_set<int> &reachable)
{
    visited[current_node] = true;
    if (start_node != current_node)
    { // Don't add the start node itself initially
        reachable.insert(current_node);
    }

    for (int neighbor : adj[current_node])
    {
        if (!visited[neighbor])
        {
            transitiveClosureDFS(start_node, neighbor, adj, visited, reachable);
        }
        // If neighbor was already visited in *this* DFS traversal starting from start_node,
        // it means we've found a path. Add all nodes reachable from the neighbor
        // (which might have been computed in a previous branch of the DFS or a prior call).
        // This handles cases where paths merge.
        else if (reachable.count(neighbor))
        {
            // Optimization: If the neighbor is already known to be reachable,
            // all nodes reachable *from* it are also reachable from the start_node.
            // We can potentially add cached reachability info here if available,
            // but a simple recursive call ensures correctness.
            // Re-add neighbor itself and explore from it again if needed,
            // though the visited check prevents infinite loops in non-cyclic graphs.
            reachable.insert(neighbor); // Ensure neighbor is included
            // If we had a separate cache for reachability per node, we could union it here.
            // Without it, the standard DFS explores correctly.
        }
    }
}

// Recursive function to compute possible effects
EffectsSet EffectsInference::computePossibleEffectsRecursive(int method_id, std::set<int> &recursion_stack)
{
    // 1. Check Cache
    if (_possible_effects_cache.count(method_id))
    {
        return _possible_effects_cache.at(method_id);
    }

    // 2. Check Recursion Stack
    if (recursion_stack.count(method_id))
    {
        Log::d("Warning: Recursion detected for possible effects calculation involving method %d (%s). Returning empty effects for this path.\n",
               method_id, _instance.getMethodById(method_id).getName().c_str());
        return {}; // Return empty set to break the cycle for this path
    }

    // 3. Add to Recursion Stack
    recursion_stack.insert(method_id);

    // 4. Get Method Info and Ordering
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    const SubtaskOrderingInfo &ordering_info = getOrderingInfo(method_id); // Ensures ordering is computed

    // Handle cycles detected during ordering info calculation
    if (ordering_info.has_cycle)
    {
        Log::e("Error: Cannot compute possible effects for method %d (%s) due to cycle in subtask ordering.\n",
               method_id, method.getName().c_str());
        recursion_stack.erase(method_id);        // Remove before returning
        _possible_effects_cache[method_id] = {}; // Cache empty result for cyclic methods
        return {};
    }

    // 5. Compute Union of Effects from Subtasks
    EffectsSet combined_possible_effects;
    for (int subtask_idx = 0; subtask_idx < subtasks.size(); ++subtask_idx)
    {
        int subtask_id = subtasks[subtask_idx];

        if (_instance.isAbstractTask(subtask_id))
        {
            const AbstractTask &abstract_task = _instance.getAbstractTaskById(subtask_id);
            const auto &decomposition_method_ids = abstract_task.getDecompositionMethodsIdx();

            if (decomposition_method_ids.empty())
            {
                Log::d("Warning: Abstract task %d (%s) in method %d (%s) has no decomposition methods. Skipping for possible effects.\n",
                       subtask_id, abstract_task.getName().c_str(), method_id, method.getName().c_str());
                continue;
            }

            // Recursively compute possible effects for all decomposition methods and union them
            for (int decomp_method_id : decomposition_method_ids)
            {
                EffectsSet sub_method_effects = computePossibleEffectsRecursive(decomp_method_id, recursion_stack);
                combined_possible_effects.positive = setUnion(combined_possible_effects.positive, sub_method_effects.positive);
                combined_possible_effects.negative = setUnion(combined_possible_effects.negative, sub_method_effects.negative);
            }
        }
        else
        {
            // It's a primitive task (Action)
            const Action &action = _instance.getActionById(subtask_id);
            const auto &pos_effs = action.getPosEffsIdx();
            const auto &neg_effs = action.getNegEffsIdx();

            combined_possible_effects.positive.insert(pos_effs.begin(), pos_effs.end());
            combined_possible_effects.negative.insert(neg_effs.begin(), neg_effs.end());
        }
    }

    // 6. Remove from Recursion Stack
    //     // recursion_stack.erase(method_id);

    // 7. Store in Cache and Return
    //     // _possible_effects_cache[method_id] = combined_possible_effects;
    return combined_possible_effects;
}

// Recursive function to compute certified effects
EffectsSet EffectsInference::computeCertifiedEffectsRecursive(int method_id, std::set<int> &recursion_stack)
{
    // 1. Check Cache
    if (_certified_effects_cache.count(method_id))
    {
        return _certified_effects_cache.at(method_id);
    }

    // 2. Check Recursion Stack
    if (recursion_stack.count(method_id))
    {
        Log::d("Warning: Recursion detected for certified effects calculation involving method %d (%s). Returning empty effects for this path.\n",
               method_id, _instance.getMethodById(method_id).getName().c_str());
        return {}; // Return empty set to break the cycle
    }

    // 3. Add to Recursion Stack
    recursion_stack.insert(method_id);

    // 4. Get Method Info and Ordering
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    const SubtaskOrderingInfo &ordering_info = getOrderingInfo(method_id); // Ensures ordering is computed

    // Handle cycles detected during ordering info calculation
    if (ordering_info.has_cycle)
    {
        Log::e("Error: Cannot compute certified effects for method %d (%s) due to cycle in subtask ordering.\n",
               method_id, method.getName().c_str());
        recursion_stack.erase(method_id);         // Remove before returning
        _certified_effects_cache[method_id] = {}; // Cache empty result for cyclic methods
        return {};
    }

    // 5. Compute Certified Effects (Union of contributions)
    EffectsSet final_certified_effects;
    for (int subtask_idx = 0; subtask_idx < subtasks.size(); ++subtask_idx)
    {
        int subtask_id = subtasks[subtask_idx];
        EffectsSet base_effects;

        // --- Get Base Effects of the current subtask ---
        if (_instance.isAbstractTask(subtask_id))
        {
            const AbstractTask &abstract_task = _instance.getAbstractTaskById(subtask_id);
            const auto &decomposition_method_ids = abstract_task.getDecompositionMethodsIdx();

            if (decomposition_method_ids.empty())
            {
                Log::d("Warning: Abstract task %d (%s) in method %d (%s) has no decomposition methods. Cannot contribute certified effects.\n",
                       subtask_id, abstract_task.getName().c_str(), method_id, method.getName().c_str());
                continue; // Skip this subtask
            }

            // Intersect certified effects of all decomposition methods
            bool first_decomp = true;
            for (int decomp_method_id : decomposition_method_ids)
            {
                EffectsSet sub_method_certified = computeCertifiedEffectsRecursive(decomp_method_id, recursion_stack);
                if (first_decomp)
                {
                    base_effects = sub_method_certified;
                    first_decomp = false;
                }
                else
                {
                    base_effects.positive = setIntersection(base_effects.positive, sub_method_certified.positive);
                    base_effects.negative = setIntersection(base_effects.negative, sub_method_certified.negative);
                }
                // If intersection becomes empty, we can potentially stop early for this abstract task
                if (base_effects.isEmpty())
                    break;
            }
            // If after checking all decompositions, the intersection is empty, this abstract task certifies nothing.
            if (base_effects.isEmpty())
                continue;
        }
        else
        {
            // It's a primitive task (Action)
            const Action &action = _instance.getActionById(subtask_id);
            const auto &pos_effs = action.getPosEffsIdx();
            const auto &neg_effs = action.getNegEffsIdx();
            base_effects.positive.insert(pos_effs.begin(), pos_effs.end());
            base_effects.negative.insert(neg_effs.begin(), neg_effs.end());

            // Add all preconditions which are not on the negative effects as positive effects
            const auto &preconditions = action.getPreconditionsIdx();
            for (int precondition : preconditions)
            {
                if (base_effects.negative.count(precondition) == 0)
                {
                    base_effects.positive.insert(precondition);
                }
            }
        }

        // --- Get Possible Effects of Successors/Parallel Tasks ---
        EffectsSet possible_effects_after = getCombinedPossibleEffectsOfSuccessors(subtask_idx, method_id, ordering_info, recursion_stack);

        // --- Calculate Certified Contribution of this Subtask ---
        EffectsSet certified_contribution;
        certified_contribution.positive = setDifference(base_effects.positive, possible_effects_after.negative); // Base positive effect survives if not possibly negated later
        certified_contribution.negative = setDifference(base_effects.negative, possible_effects_after.positive); // Base negative effect survives if not possibly made positive later

        // --- Union with Final Certified Effects ---
        final_certified_effects.positive = setUnion(final_certified_effects.positive, certified_contribution.positive);
        final_certified_effects.negative = setUnion(final_certified_effects.negative, certified_contribution.negative);
    }

    // --- Final Cleanup: Ensure consistency ---
    // Remove any effect that is both certified positive and certified negative (shouldn't happen with correct logic, but good safeguard)
    std::unordered_set<int> conflicting = setIntersection(final_certified_effects.positive, final_certified_effects.negative);
    if (!conflicting.empty())
    {
        Log::d("Warning: Method %d (%s) resulted in conflicting certified effects. Removing conflicts.\n", method_id, method.getName().c_str());
        for (int conflict_pred : conflicting)
        {
            final_certified_effects.positive.erase(conflict_pred);
            final_certified_effects.negative.erase(conflict_pred);
        }
    }

    // 6. Remove from Recursion Stack
    //     // recursion_stack.erase(method_id);

    // 7. Store in Cache and Return
    //     // _certified_effects_cache[method_id] = final_certified_effects;
    return final_certified_effects;
}

// Helper function to get possible effects of all tasks that *might* execute after a given subtask
EffectsSet EffectsInference::getCombinedPossibleEffectsOfSuccessors(int subtask_idx, int method_id, const SubtaskOrderingInfo &ordering_info, std::set<int> &recursion_stack)
{
    EffectsSet combined_effects;
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    int n = subtasks.size();

    // Combine successors and parallel tasks indices
    std::unordered_set<int> potential_later_indices = ordering_info.successors.at(subtask_idx);
    if (ordering_info.parallel.count(subtask_idx))
    {
        potential_later_indices.insert(ordering_info.parallel.at(subtask_idx).begin(), ordering_info.parallel.at(subtask_idx).end());
    }

    for (int later_idx : potential_later_indices)
    {
        if (later_idx < 0 || later_idx >= n)
            continue; // Should not happen if ordering_info is correct

        int later_task_id = subtasks[later_idx];
        EffectsSet later_task_possible_effects;

        if (_instance.isAbstractTask(later_task_id))
        {
            const AbstractTask &abstract_task = _instance.getAbstractTaskById(later_task_id);
            const auto &decomposition_method_ids = abstract_task.getDecompositionMethodsIdx();

            // Union possible effects of all decomposition methods
            for (int decomp_method_id : decomposition_method_ids)
            {
                // IMPORTANT: Use computePossibleEffectsRecursive here, as we need the *possible* effects of successors
                EffectsSet sub_method_possible = computePossibleEffectsRecursive(decomp_method_id, recursion_stack);
                later_task_possible_effects.positive = setUnion(later_task_possible_effects.positive, sub_method_possible.positive);
                later_task_possible_effects.negative = setUnion(later_task_possible_effects.negative, sub_method_possible.negative);
            }
        }
        else
        {
            // It's a primitive task (Action)
            const Action &action = _instance.getActionById(later_task_id);
            const auto &pos_effs = action.getPosEffsIdx();
            const auto &neg_effs = action.getNegEffsIdx();
            later_task_possible_effects.positive.insert(pos_effs.begin(), pos_effs.end());
            later_task_possible_effects.negative.insert(neg_effs.begin(), neg_effs.end());
        }

        // Union with the overall combined effects
        combined_effects.positive = setUnion(combined_effects.positive, later_task_possible_effects.positive);
        combined_effects.negative = setUnion(combined_effects.negative, later_task_possible_effects.negative);
    }

    return combined_effects;
}

// Helper function to get possible effects of all tasks that *might* execute before or parallel to a given subtask
EffectsSet EffectsInference::getCombinedPossibleEffectsOfPredecessors(int subtask_idx, int method_id, const SubtaskOrderingInfo &ordering_info, std::set<int> &recursion_stack_poss_effects)
{
    EffectsSet combined_effects;
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    int n = subtasks.size();

    // Combine predecessors and parallel tasks indices
    std::unordered_set<int> potential_earlier_indices;
    if (ordering_info.predecessors.count(subtask_idx))
    {
        potential_earlier_indices.insert(ordering_info.predecessors.at(subtask_idx).begin(), ordering_info.predecessors.at(subtask_idx).end());
    }
    if (ordering_info.parallel.count(subtask_idx))
    {
        potential_earlier_indices.insert(ordering_info.parallel.at(subtask_idx).begin(), ordering_info.parallel.at(subtask_idx).end());
    }

    for (int earlier_idx : potential_earlier_indices)
    {
        if (earlier_idx < 0 || earlier_idx >= n)
            continue; // Should not happen

        int earlier_task_id = subtasks[earlier_idx];
        EffectsSet earlier_task_possible_effects;

        if (_instance.isAbstractTask(earlier_task_id))
        {
            const AbstractTask &abstract_task = _instance.getAbstractTaskById(earlier_task_id);
            const auto &decomposition_method_ids = abstract_task.getDecompositionMethodsIdx();

            // Union possible effects of all decomposition methods
            for (int decomp_method_id : decomposition_method_ids)
            {
                // Use computePossibleEffectsRecursive, passing the appropriate recursion stack
                EffectsSet sub_method_possible = computePossibleEffectsRecursive(decomp_method_id, recursion_stack_poss_effects);
                earlier_task_possible_effects.positive = setUnion(earlier_task_possible_effects.positive, sub_method_possible.positive);
                earlier_task_possible_effects.negative = setUnion(earlier_task_possible_effects.negative, sub_method_possible.negative);
            }
        }
        else
        {
            // It's a primitive task (Action)
            const Action &action = _instance.getActionById(earlier_task_id);
            const auto &pos_effs = action.getPosEffsIdx();
            const auto &neg_effs = action.getNegEffsIdx();
            earlier_task_possible_effects.positive.insert(pos_effs.begin(), pos_effs.end());
            earlier_task_possible_effects.negative.insert(neg_effs.begin(), neg_effs.end());
        }

        // Union with the overall combined effects
        combined_effects.positive = setUnion(combined_effects.positive, earlier_task_possible_effects.positive);
        combined_effects.negative = setUnion(combined_effects.negative, earlier_task_possible_effects.negative);
    }

    return combined_effects;
}

// Recursive function to compute preconditions for a method
std::unordered_set<int> EffectsInference::computePreconditionsRecursive(int method_id, std::set<int> &recursion_stack_prec, std::set<int> &recursion_stack_poss_effects)
{
    // 1. Check Cache
    if (_preconditions_cache.count(method_id))
    {
        return _preconditions_cache.at(method_id);
    }

    // 2. Check Recursion Stack for Preconditions
    if (recursion_stack_prec.count(method_id))
    {
        Log::d("Warning: Recursion detected for precondition calculation involving method %d (%s). Returning empty preconditions for this path.\n",
               method_id, _instance.getMethodById(method_id).getName().c_str());
        return {}; // Return empty set to break the cycle
    }

    // 3. Add to Recursion Stack
    recursion_stack_prec.insert(method_id);

    // 4. Get Method Info and Ordering
    const Method &method = _instance.getMethodById(method_id);
    const auto &subtasks = method.getSubtasksIdx();
    const SubtaskOrderingInfo &ordering_info = getOrderingInfo(method_id); // Ensures ordering is computed

    // Handle cycles detected during ordering info calculation
    if (ordering_info.has_cycle)
    {
        Log::e("Error: Cannot compute preconditions for method %d (%s) due to cycle in subtask ordering.\n",
               method_id, method.getName().c_str());
        recursion_stack_prec.erase(method_id); // Remove before returning
        _preconditions_cache[method_id] = {};  // Cache empty result for cyclic methods
        return {};
    }

    // 5. Compute Preconditions (Union of contributions)
    std::unordered_set<int> final_preconditions;
    for (int subtask_idx = 0; subtask_idx < subtasks.size(); ++subtask_idx)
    {
        int subtask_id = subtasks[subtask_idx];
        std::unordered_set<int> base_preconditions;

        // --- Get Base Preconditions of the current subtask ---
        if (_instance.isAbstractTask(subtask_id))
        {
            const AbstractTask &abstract_task = _instance.getAbstractTaskById(subtask_id);
            const auto &decomposition_method_ids = abstract_task.getDecompositionMethodsIdx();

            if (decomposition_method_ids.empty())
            {
                Log::d("Warning: Abstract task %d (%s) in method %d (%s) has no decomposition methods. Cannot contribute preconditions.\n",
                       subtask_id, abstract_task.getName().c_str(), method_id, method.getName().c_str());
                continue; // Skip this subtask
            }

            // Intersect preconditions of all decomposition methods
            bool first_decomp = true;
            for (int decomp_method_id : decomposition_method_ids)
            {
                // Recursive call needs both stacks
                std::unordered_set<int> sub_method_preconditions = computePreconditionsRecursive(decomp_method_id, recursion_stack_prec, recursion_stack_poss_effects);
                if (first_decomp)
                {
                    base_preconditions = sub_method_preconditions;
                    first_decomp = false;
                }
                else
                {
                    base_preconditions = setIntersection(base_preconditions, sub_method_preconditions);
                }
                // If intersection becomes empty, we can stop early for this abstract task
                if (base_preconditions.empty())
                    break;
            }
            // If after checking all decompositions, the intersection is empty, this abstract task requires nothing initially.
            if (base_preconditions.empty())
                continue;
        }
        else
        {
            // It's a primitive task (Action)
            const Action &action = _instance.getActionById(subtask_id);
            const auto &precs = action.getPreconditionsIdx();
            base_preconditions.insert(precs.begin(), precs.end());
        }

        // --- Get Possible Positive Effects of Predecessors/Parallel Tasks ---
        // Pass the recursion stack for possible effects calculation
        EffectsSet possible_effects_before = getCombinedPossibleEffectsOfPredecessors(subtask_idx, method_id, ordering_info, recursion_stack_poss_effects);

        // --- Calculate Precondition Contribution of this Subtask ---
        // A base precondition is a true precondition for the method if it's NOT possibly added by any preceding/parallel task.
        std::unordered_set<int> precondition_contribution = setDifference(base_preconditions, possible_effects_before.positive);

        // --- Union with Final Preconditions ---
        final_preconditions = setUnion(final_preconditions, precondition_contribution);
    }

    // Add the method's own explicitly defined preconditions
    const auto &method_preconditions = method.getPreconditionsIdx();
    final_preconditions.insert(method_preconditions.begin(), method_preconditions.end());

    // 6. Remove from Recursion Stack
    // recursion_stack_prec.erase(method_id);

    // 7. Store in Cache and Return
    // _preconditions_cache[method_id] = final_preconditions;
    return final_preconditions;
}

// Main function to trigger the computation for all methods
void EffectsInference::calculateAllMethodEffects()
{
    int num_methods = _instance.getNumMethods();
    // for (int i = 0; i < num_methods; ++i)
    // {
    //     std::set<int> recursion_stack_poss;
    //     std::set<int> recursion_stack_cert;
    //     // Compute possible effects (handles caching internally)
    //     EffectsSet combined_possible_effects = computePossibleEffectsRecursive(i, recursion_stack_poss);
    //     _possible_effects_cache[i] = combined_possible_effects;
    // }
    Log::i("Calculating possible effects for all methods...\n");
    calculateAllMethodPossibleEffects();
    Log::i("Done !\n");
    for (int i = 0; i < num_methods; ++i)
    {
        if (i % 100 == 0) {
            Log::i("%d/%d\n", i, num_methods);
        }
        // Compute certified effects (handles caching internally)
        std::set<int> recursion_stack_cert;
        EffectsSet combined_certified_effects = computeCertifiedEffectsRecursive(i, recursion_stack_cert);
        _certified_effects_cache[i] = combined_certified_effects;

        auto certified_effects = getCertifiedEffects(i);
        auto possible_effects = getPossibleEffects(i);
        int num_removed = 0;
        if (possible_effects.has_value() && certified_effects.has_value())
        {
            const auto &possible = possible_effects.value();
            const auto &certified = certified_effects.value();

            // Remove conflicts
            std::unordered_set<int> conflicting_pos = setIntersection(possible.positive, certified.negative);
            std::unordered_set<int> conflicting_neg = setIntersection(possible.negative, certified.positive);

            for (int conflict : conflicting_pos)
            {
                Log::d("Removing possible positive effect %s of method %d (%s) because it is certified negative.\n",
                       TOSTR(_instance.getPredicateById(conflict)), i, _instance.getMethodById(i).getName().c_str());
                _possible_effects_cache[i].positive.erase(conflict);
                num_removed++;
            }
            for (int conflict : conflicting_neg)
            {
                Log::d("Removing possible negative effect %s of method %d (%s) because it is certified positive.\n",
                       TOSTR(_instance.getPredicateById(conflict)), i, _instance.getMethodById(i).getName().c_str());
                _possible_effects_cache[i].negative.erase(conflict);
                num_removed++;
            }
        }
    }
    Log::i("Finished calculating initial possible and certified effects for all methods.\n");
}

void EffectsInference::calculateAllMethodPreconditions()
{
    Log::i("Starting calculation of preconditions for all methods...\n");
    int num_methods = _instance.getNumMethods();
    int num_precs = 0;
    for (int i = 0; i < num_methods; ++i)
    {
        std::set<int> recursion_stack_prec;
        std::set<int> recursion_stack_poss_effects; // Separate stack for nested possible effects calls
        // Compute preconditions (handles caching internally)
        std::unordered_set<int> preconditions = computePreconditionsRecursive(i, recursion_stack_prec, recursion_stack_poss_effects);
        _preconditions_cache[i] = preconditions;

        // To debug, print the method previous preconditions and the preconditions found now
        const Method &method = _instance.getMethodById(i);
        const auto &previous_preconditions = method.getPreconditionsIdx();
        for (int new_prec : preconditions)
        {
            if (previous_preconditions.count(new_prec) == 0)
            {
                Log::d("New precondition %s found for method %d (%s).\n",
                       TOSTR(_instance.getPredicateById(new_prec)), i, method.getName().c_str());
                num_precs++;
            }
        }
    }
    Log::i("Finished calculating preconditions for all methods.\n");
    Log::i("Total number of additinnal preconditions found: %d\n", num_precs);
}

// --- Mutex Refinement Implementation ---

// Optional function to refine possible effects using mutexes after initial calculation
void EffectsInference::refineAllPossibleEffectsWithMutex(Mutex &mutex)
{
    Log::i("Starting mutex refinement for possible effects...\n");
    int num_methods = _instance.getNumMethods();
    int total_removed_pos = 0;
    int total_removed_neg = 0;

    // Ensure initial calculations are done (or at least attempted)
    if (_possible_effects_cache.empty() || _certified_effects_cache.empty())
    {
        Log::d("Warning: Initial effect calculation might not have been run before mutex refinement.\n");
        // Optionally, call calculateAllMethodEffects() here, but it's better design
        // for the user to call it explicitly first.
    }

    for (int method_id = 0; method_id < num_methods; ++method_id)
    {
        // Apply refinement for the current method
        EffectsSet refined_effects = applyMutexRefinement(method_id, mutex);

        // Log differences (optional)
        if (_possible_effects_cache.count(method_id))
        {
            const auto &original_possible = _possible_effects_cache.at(method_id);
            total_removed_pos += (original_possible.positive.size() - refined_effects.positive.size());
            total_removed_neg += (original_possible.negative.size() - refined_effects.negative.size());
        }

        _possible_effects_cache[method_id] = refined_effects; // Update the original cache as well
    }
    Log::i("Finished mutex refinement. Removed %d possible positive and %d possible negative effects across all methods.\n",
           total_removed_pos, total_removed_neg);
}

void EffectsInference::refineAllPossibleNegativeEffectsWithMutexAndPrecMethods(Mutex &mutex)
{
    // Log::i("Starting mutex refinement for possible effects using precondition and mutex...\n");
    int num_methods = _instance.getNumMethods();
    int total_removed_pos = 0;
    int total_removed_neg = 0;

    // Ensure initial calculations are done (or at least attempted)
    if (_possible_effects_cache.empty() || _certified_effects_cache.empty())
    {
        Log::d("Warning: Initial effect calculation might not have been run before mutex refinement.\n");
        // Optionally, call calculateAllMethodEffects() here, but it's better design
        // for the user to call it explicitly first.
    }

    for (int method_id = 0; method_id < num_methods; ++method_id)
    {

        std::unordered_set<int> pos_effects_to_remove;

        // Get the method
        const Method &method = _instance.getMethodById(method_id);

        // for (int j = 0; j < method.getPreconditionsIdx().size(); j++)
        for (const int &precondition : method.getPreconditionsIdx())
        {
            // int precondition = method.getPreconditionsIdx()[j];
            // Get all the mutex groups of the precondition
            const auto &mutex_groups = mutex.getMutexGroupsOfPred(precondition);
            for (int group_idx : mutex_groups)
            {
                const auto &group = mutex.getMutexGroup(group_idx);
                for (int poss_neg_eff : _possible_effects_cache[method_id].negative)
                {
                    if (poss_neg_eff == precondition)
                        continue; // Don't remove the precondition itself
                    // Check if the possible negative effect is in the same mutex group
                    bool in_group = false;
                    for (int group_member : group)
                    {
                        if (group_member == poss_neg_eff)
                        {
                            in_group = true;
                            break;
                        }
                    }

                    if (in_group)
                    {
                        // Mark for removal
                        if (_possible_effects_cache[method_id].negative.find(poss_neg_eff) != _possible_effects_cache[method_id].negative.end())
                        {
                            if (pos_effects_to_remove.find(poss_neg_eff) == pos_effects_to_remove.end())
                            {
                                Log::d("Mutex Refinement: Marking possible negative effect %s of method %d (%s) for removal due to precondition %s.\n",
                                       TOSTR(_instance.getPredicateById(poss_neg_eff)), method_id, _instance.getMethodById(method_id).getName().c_str(), TOSTR(_instance.getPredicateById(precondition)));
                                pos_effects_to_remove.insert(poss_neg_eff);
                                total_removed_neg++;
                            }
                        }
                    }
                }
            }
        }
        // Remove the marked effects from the possible effects
        for (int poss_neg_eff : pos_effects_to_remove)
        {
            _possible_effects_cache[method_id].negative.erase(poss_neg_eff);
        }
    }

    Log::i("Finished mutex refinement using preconditions. Removed %d possible positive and %d possible negative effects across all methods.\n",
           total_removed_pos, total_removed_neg);
}

// Helper function to apply mutex filtering for a single method
EffectsSet EffectsInference::applyMutexRefinement(int method_id, Mutex &mutex)
{
    // Get the original computed effects (ensure they exist)
    auto possible_opt = getPossibleEffects(method_id);
    auto certified_opt = getCertifiedEffects(method_id);

    if (!possible_opt.has_value() || !certified_opt.has_value())
    {
        Log::d("Warning: Cannot refine method %d (%s) as original effects were not computed (possibly due to cycle or error).\n",
               method_id, _instance.getMethodById(method_id).getName().c_str());
        return {}; // Return empty if original computation failed
    }

    const EffectsSet &possible = possible_opt.value();
    const EffectsSet &certified = certified_opt.value();

    // Start with a copy of the original possible effects
    EffectsSet refined = possible;
    int removed_pos_count = 0;
    int removed_neg_count = 0;

    // --- Refine Possible Positive Effects ---
    std::unordered_set<int> pos_effects_to_remove;
    for (int cert_pos_eff : certified.positive)
    {
        const auto &mutex_groups = mutex.getMutexGroupsOfPred(cert_pos_eff);
        for (int group_idx : mutex_groups)
        {
            const auto &group = mutex.getMutexGroup(group_idx);
            for (int poss_pos_eff : refined.positive)
            {
                if (poss_pos_eff == cert_pos_eff)
                    continue; // Don't remove the certified effect itself

                // Check if the possible effect is in the same mutex group
                bool in_group = false;
                for (int group_member : group)
                {
                    if (group_member == poss_pos_eff)
                    {
                        in_group = true;
                        break;
                    }
                }

                if (in_group)
                {
                    // Mark for removal
                    if (pos_effects_to_remove.find(poss_pos_eff) == pos_effects_to_remove.end())
                    {
                        Log::d("Mutex Refinement: Marking possible positive effect %s of method %d (%s) for removal due to certified positive %s.\n",
                               TOSTR(_instance.getPredicateById(poss_pos_eff)), method_id, TOSTR(_instance.getMethodById(method_id)), TOSTR(_instance.getPredicateById(cert_pos_eff)));
                        pos_effects_to_remove.insert(poss_pos_eff);
                        removed_pos_count++;
                    }
                }
            }
        }
    }

    // Perform removals
    for (int pred_id : pos_effects_to_remove)
    {
        refined.positive.erase(pred_id);
    }

    // if (removed_pos_count > 0 || removed_neg_count > 0)
    // {
    //     Log::i("Mutex Refinement for method %d (%s): Removed %d possible positive and %d possible negative effects.\n",
    //            method_id, _instance.getMethodById(method_id).getName().c_str(), removed_pos_count, removed_neg_count);
    // }

    return refined;
}

// Getters for the computed effects
std::optional<EffectsSet> EffectsInference::getPossibleEffects(int method_id) const
{
    // Returns original, unrefined possible effects
    if (_possible_effects_cache.count(method_id))
    {
        return _possible_effects_cache.at(method_id);
    }
    return std::nullopt; // Not computed or doesn't exist
}

std::optional<EffectsSet> EffectsInference::getCertifiedEffects(int method_id) const
{
    // Returns original certified effects
    if (_certified_effects_cache.count(method_id))
    {
        return _certified_effects_cache.at(method_id);
    }
    return std::nullopt; // Not computed or doesn't exist
}

// Getter for computed preconditions
std::optional<std::unordered_set<int>> EffectsInference::getPreconditions(int method_id) const
{
    if (_preconditions_cache.count(method_id))
    {
        return _preconditions_cache.at(method_id);
    }
    return std::nullopt; // Not computed or doesn't exist
}

void EffectsInference::clearCaches()
{
    _possible_effects_cache.clear();
    _certified_effects_cache.clear();
    _preconditions_cache.clear(); // Clear the new cache
    _ordering_info_cache.clear(); // Also clear ordering info cache if recomputing
}

void EffectsInference::calculateAllMethodsPrecsAndEffs(std::vector<Method> &methods, Mutex *mutex)
{
    Log::i("Calculating all methods preconditions and effects...\n");

    // Calculate all method effects
    Log::i("Calculating all method effects...\n");
    calculateAllMethodEffects();
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        // Refine possible effects with mutex
        Log::i("Refining all possible effects with mutex...\n");
        refineAllPossibleEffectsWithMutex(*mutex);
        Log::i("Done !\n");
    }

    Log::i("Calculating all method preconditions...\n");
    calculateAllMethodPreconditions();
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        // Refine possible effects with mutex and preconditions
        Log::i("Refining all possible negative effects with mutex and preconditions...\n");
        refineAllPossibleNegativeEffectsWithMutexAndPrecMethods(*mutex);
        Log::i("Done !\n");
    }
    Log::i("Finished calculating all methods preconditions and effects. Set all values in the methods.\n");
    for (int i = 0; i < methods.size(); i++)
    {
        Method &method = methods[i];
        if (_possible_effects_cache.count(i))
        {
            method.setPossiblePositiveEffects(_possible_effects_cache.at(i).positive);
            method.setPossibleNegativeEffects(_possible_effects_cache.at(i).negative);
        }
        if (_certified_effects_cache.count(i))
        {
            method.setPositiveEffects(_certified_effects_cache.at(i).positive);
            method.setNegativeEffects(_certified_effects_cache.at(i).negative);
        }
        if (_preconditions_cache.count(i))
        {
            // We may already have preconditions, so add the new preconditios instead of replacing them
            for (const int &precondition : _preconditions_cache.at(i))
            {
                method.addPreconditionIdx(precondition);
            }
        }
    }

    Log::i("Done !\n");
    Log::i("Cleared caches.\n");
    clearCaches();
    Log::i("Finished calculating all methods preconditions and effects.\n");
}