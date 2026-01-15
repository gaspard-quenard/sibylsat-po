#include "effects_inference.h"

#include <algorithm> // For std::set_union, std::set_intersection, std::set_difference, std::sort
#include <iterator>  // For std::inserter
#include <vector>
#include <queue>

// Constructor
EffectsInference::EffectsInference(const HtnInstance &instance) : _instance(instance) {}

// --- Helper functions for set operations ---
// --- Placeholder for other member function implementations ---

void EffectsInference::setOrderingInfoForAllMethods()
{
    for (int method_id = 0; method_id < _instance.getNumMethods(); ++method_id)
    {
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
                exit(1);
            }
            if (u_idx == v_idx)
            {
                Log::d("Warning: Self-loop detected in ordering constraint for subtask index %d in method %d (%s).\n",
                       u_idx, method_id, method.getName().c_str());
                // continue; // Ignore self-loops for ordering logic
                exit(1);
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
            exit(1);
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
    }
}

// Helper function to compute or retrieve ordering info for a method
const EffectsInference::SubtaskOrderingInfo &EffectsInference::getOrderingInfo(int method_id) const
{
    // // Implementation needed
    // // Check cache first
    // if (_ordering_info_cache.count(method_id))
    // {
    //     return _ordering_info_cache.at(method_id);
    // }

    // // Compute if not in cache
    // SubtaskOrderingInfo info;
    // const Method &method = _instance.getMethodById(method_id);
    // const auto &subtasks = method.getSubtasksIdx();
    // const auto &constraints = method.getOrderingConstraints();
    // int n = subtasks.size();

    // info.adj.resize(n);
    // info.rev_adj.resize(n);
    // std::vector<int> in_degree(n, 0);
    // std::vector<int> out_degree(n, 0); // Needed for cycle detection / LFT-like checks

    // // Build graph and check for index validity
    // for (const auto &constraint : constraints)
    // {
    //     int u_idx = constraint.first;
    //     int v_idx = constraint.second;

    //     if (u_idx < 0 || u_idx >= n || v_idx < 0 || v_idx >= n)
    //     {
    //         Log::e("Error: Ordering constraint index out of range (%d or %d) for method %d (%s) with %d subtasks.\n",
    //                u_idx, v_idx, method_id, method.getName().c_str(), n);
    //         info.has_cycle = true; // Mark as problematic
    //         _ordering_info_cache[method_id] = info;
    //         return _ordering_info_cache.at(method_id);
    //     }
    //     if (u_idx == v_idx)
    //     {
    //         Log::d("Warning: Self-loop detected in ordering constraint for subtask index %d in method %d (%s).\n",
    //                u_idx, method_id, method.getName().c_str());
    //         continue; // Ignore self-loops for ordering logic
    //     }

    //     // Check for duplicate constraints (optional, but good practice)
    //     bool already_exists = false;
    //     for (int neighbor : info.adj[u_idx])
    //     {
    //         if (neighbor == v_idx)
    //         {
    //             already_exists = true;
    //             break;
    //         }
    //     }
    //     if (!already_exists)
    //     {
    //         info.adj[u_idx].push_back(v_idx);
    //         info.rev_adj[v_idx].push_back(u_idx);
    //         in_degree[v_idx]++;
    //         out_degree[u_idx]++;
    //     }
    // }

    // // Cycle Detection (using Kahn's algorithm idea)
    // std::queue<int> q;
    // for (int i = 0; i < n; ++i)
    // {
    //     if (in_degree[i] == 0)
    //     {
    //         q.push(i);
    //     }
    // }
    // int count = 0;
    // while (!q.empty())
    // {
    //     int u = q.front();
    //     q.pop();
    //     count++;
    //     for (int v : info.adj[u])
    //     {
    //         if (--in_degree[v] == 0)
    //         {
    //             q.push(v);
    //         }
    //     }
    // }
    // if (count != n)
    // {
    //     Log::d("Warning: Cycle detected in ordering constraints for method %d (%s).\n", method_id, method.getName().c_str());
    //     info.has_cycle = true;
    //     // Don't compute successors/predecessors if there's a cycle, as it's ill-defined.
    //     _ordering_info_cache[method_id] = info;
    //     return _ordering_info_cache.at(method_id);
    // }

    // // --- Compute Successors, Predecessors, Parallel ---
    // // Only if no cycle was detected
    // if (!info.has_cycle)
    // {
    //     // Compute Successors (Transitive Closure) using DFS
    //     for (int i = 0; i < n; ++i)
    //     {
    //         std::vector<bool> visited(n, false);
    //         transitiveClosureDFS(i, i, info.adj, visited, info.successors[i]);
    //         info.successors[i].erase(i); // Remove self from successors
    //     }

    //     // Compute Predecessors (Transitive Closure on reversed graph) using DFS
    //     for (int i = 0; i < n; ++i)
    //     {
    //         std::vector<bool> visited(n, false);
    //         transitiveClosureDFS(i, i, info.rev_adj, visited, info.predecessors[i]);
    //         info.predecessors[i].erase(i); // Remove self from predecessors
    //     }

    //     // Compute Parallel Tasks
    //     for (int i = 0; i < n; ++i)
    //     {
    //         for (int j = i + 1; j < n; ++j)
    //         { // Only check pairs once
    //             // Check if i is NOT a successor of j AND j is NOT a successor of i
    //             bool i_successor_of_j = info.successors[j].count(i);
    //             bool j_successor_of_i = info.successors[i].count(j);

    //             if (!i_successor_of_j && !j_successor_of_i)
    //             {
    //                 info.parallel[i].insert(j);
    //                 info.parallel[j].insert(i);
    //             }
    //         }
    //     }
    // }

    // // Store in cache and return
    // _ordering_info_cache[method_id] = info;
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

// Main function to trigger the computation for all methods
void EffectsInference::calculateAllMethodEffects()
{
    int num_methods = _instance.getNumMethods();
    Log::i("Calculating possible effects for all methods...\n");
    calculateAllMethodPossibleEffects();
    Log::i("Done !\n");
    int num_pos = 0;
    int num_neg = 0;

    int num_removed_2 = 0;
    calculateAllMethodCertifiedEffects();
    for (int i = 0; i < num_methods; ++i)
    {
        /*  +f is impossible if the method is certified to make –f
        –f is impossible if the method is certified to make +f          */
        std::size_t before = _possibleEffBits[i].pos.count() + _possibleEffBits[i].neg.count();

        _possibleEffBits[i].pos.minus_with(_certEffBits[i].neg); //   pos  &=  ~cert.neg
        _possibleEffBits[i].neg.minus_with(_certEffBits[i].pos); //   neg  &=  ~cert.pos

        std::size_t after = _possibleEffBits[i].pos.count() + _possibleEffBits[i].neg.count();

        num_removed_2 += (before - after);
    }
    Log::i("Number of possible effects removed (bitset): %d\n", num_removed_2);

    Log::i("Number of cert positive effects: %d\n", num_pos);
    Log::i("Number of cert negative effects: %d\n", num_neg);

    Log::i("Finished calculating initial possible and certified effects for all methods.\n");
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

    Log::i("Set ordering info for all methods...\n");
    setOrderingInfoForAllMethods();

    // Calculate all method effects
    Log::i("Calculating all method effects...\n");
    calculateAllMethodEffects();
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        // Refine possible effects with mutex
        Log::i("Refining all possible effects with mutex...\n");
        applyMutexRefinementForAllMethodsBits(*mutex);
        Log::i("Done !\n");
    }

    Log::i("Calculating all method preconditions...\n");
    calculateAllMethodPreconditionsBits();
    // int num_precs = 0;
    // for (int i = 0; i < methods.size(); ++i)
    // {
        // const Method &method = methods[i];
        // num_precs += _precBits[i].count();
    // }
    // Log::i("Total number of additional preconditions found: %d\n", num_precs);
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        // Refine possible effects with mutex and preconditions
        Log::i("Refining all possible negative effects with mutex and preconditions...\n");
        refineAllPossibleNegativeEffectsWithMutexAndPrecMethodsBits(*mutex);
        Log::i("Done !\n");
    }


    // Transofrm all _possibleEffBits, _certEffBits and _preBits to the _cached values
    Log::i("Transforming all effects and preconditions to methods...\n");
    _preconditions_cache.clear();
    _certified_effects_cache.clear();
    _possible_effects_cache.clear();
    const int Nf = _instance.getNumPredicates();
    for (int i = 0; i < methods.size(); i++)
    {
        EffectsSet          es;
        const EffBits &eb = _certEffBits[i];

        for (size_t b = 0; b < Nf; ++b)
            if (eb.pos.test(b)) es.positive.insert((int)b);
        for (size_t b = 0; b < Nf; ++b)
            if (eb.neg.test(b)) es.negative.insert((int)b);


        _certified_effects_cache[i] = std::move(es);

        const EffBits &eb_pos = _possibleEffBits[i];
        EffectsSet es_pos;
        for (size_t b = 0; b < Nf; ++b)
            if (eb_pos.pos.test(b)) es_pos.positive.insert((int)b);
        for (size_t b = 0; b < Nf; ++b)
            if (eb_pos.neg.test(b)) es_pos.negative.insert((int)b);
        _possible_effects_cache[i] = std::move(es_pos);

        const BitVec &eb_prec = _precBits[i];
        for (size_t b = 0; b < Nf; ++b)
            if (eb_prec.test(b)) _preconditions_cache[i].insert((int)b);
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


void EffectsInference::printAllMethodPrecsAndEffs() const
{
    Log::i("Printing all methods preconditions and effects:\n");
    for (int i = 0; i < _instance.getNumMethods(); ++i)
    {
        const Method &method = _instance.getMethodById(i);
        Log::i("Method %s (id: %d):\n", TOSTR(method), i);
        Log::i("  Preconditions (%d):\n", method.getPreconditionsIdx().size());
        for (const int &prec : method.getPreconditionsIdx())
        {
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(prec)));
        }
        // Log::i("\n");
        Log::i("  Possible Positive Effects (%d):\n", method.getPossPosEffsIdx().size());
        for (const int &eff : method.getPossPosEffsIdx())
        {
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));
        }
        // Log::i("\n");
        Log::i("  Possible Negative Effects (%d):\n", method.getPossNegEffsIdx().size());
        for (const int &eff : method.getPossNegEffsIdx())
        {
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));
        }
        // Log::i("\n");
        Log::i("  Certified Positive Effects (%d):\n", method.getPosEffsIdx().size());
        for (const int &eff : method.getPosEffsIdx())
        {
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));
        }
        // Log::i("\n");
        Log::i("  Certified Negative Effects (%d):\n", method.getNegEffsIdx().size());
        for (const int &eff : method.getNegEffsIdx())
        {
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));
        }
        Log::i("\n");
    }
    Log::i("Finished printing all methods preconditions and effects.\n");
}