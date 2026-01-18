#include "effects_inference.h"

#include <algorithm>
#include <iterator>
#include <vector>
#include <queue>
#include <stack>
#include <numeric>

// ============================================================================
// Constructor
// ============================================================================
EffectsInference::EffectsInference(const HtnInstance &instance) : _instance(instance) {}


// ============================================================================
// PART 1: Subtask Ordering Information
// ============================================================================
// Compute predecessor/successor relationships and parallel task info for methods

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
        std::vector<int> out_degree(n, 0);

        // Build graph from ordering constraints
        for (const auto &constraint : constraints)
        {
            int u_idx = constraint.first;
            int v_idx = constraint.second;

            if (u_idx < 0 || u_idx >= n || v_idx < 0 || v_idx >= n)
            {
                Log::e("Error: Ordering constraint index out of range (%d or %d) for method %d (%s) with %d subtasks.\n",
                       u_idx, v_idx, method_id, method.getName().c_str(), n);
                exit(1);
            }
            if (u_idx == v_idx)
            {
                Log::d("Warning: Self-loop detected in ordering constraint for subtask index %d in method %d (%s).\n",
                       u_idx, method_id, method.getName().c_str());
                exit(1);
            }

            // Check for duplicate constraints
            bool already_exists = std::find(info.adj[u_idx].begin(), info.adj[u_idx].end(), v_idx) != info.adj[u_idx].end();
            if (!already_exists)
            {
                info.adj[u_idx].push_back(v_idx);
                info.rev_adj[v_idx].push_back(u_idx);
                in_degree[v_idx]++;
                out_degree[u_idx]++;
            }
        }

        // Cycle Detection using Kahn's algorithm
        std::queue<int> q;
        std::vector<int> temp_indeg = in_degree;
        for (int i = 0; i < n; ++i)
            if (temp_indeg[i] == 0)
                q.push(i);

        int count = 0;
        while (!q.empty())
        {
            int u = q.front();
            q.pop();
            count++;
            for (int v : info.adj[u])
                if (--temp_indeg[v] == 0)
                    q.push(v);
        }

        if (count != n)
        {
            Log::d("Warning: Cycle detected in ordering constraints for method %d (%s).\n", method_id, method.getName().c_str());
            info.has_cycle = true;
            _ordering_info_cache[method_id] = info;
            continue;
        }

        // Compute successors (transitive closure using DFS)
        for (int i = 0; i < n; ++i)
        {
            std::vector<bool> visited(n, false);
            transitiveClosureDFS(i, i, info.adj, visited, info.successors[i]);
            info.successors[i].erase(i);
        }

        // Compute predecessors (transitive closure on reversed graph)
        for (int i = 0; i < n; ++i)
        {
            std::vector<bool> visited(n, false);
            transitiveClosureDFS(i, i, info.rev_adj, visited, info.predecessors[i]);
            info.predecessors[i].erase(i);
        }

        // Compute parallel tasks
        for (int i = 0; i < n; ++i)
        {
            for (int j = i + 1; j < n; ++j)
            {
                bool i_successor_of_j = info.successors[j].count(i);
                bool j_successor_of_i = info.successors[i].count(j);

                if (!i_successor_of_j && !j_successor_of_i)
                {
                    info.parallel[i].insert(j);
                    info.parallel[j].insert(i);
                }
            }
        }

        _ordering_info_cache[method_id] = info;
    }
}

const EffectsInference::SubtaskOrderingInfo &EffectsInference::getOrderingInfo(int method_id) const
{
    return _ordering_info_cache.at(method_id);
}

void EffectsInference::transitiveClosureDFS(int start_node, int current_node,
                                             const std::vector<std::vector<int>> &adj,
                                             std::vector<bool> &visited,
                                             std::unordered_set<int> &reachable)
{
    visited[current_node] = true;
    if (start_node != current_node)
        reachable.insert(current_node);

    for (int neighbor : adj[current_node])
    {
        if (!visited[neighbor])
            transitiveClosureDFS(start_node, neighbor, adj, visited, reachable);
        else if (reachable.count(neighbor))
            reachable.insert(neighbor);
    }
}

// ============================================================================
// PART 2: Graph Structure Building
// ============================================================================
// Build method information needed for SCC-based analysis

void EffectsInference::buildGraphOrderingAndLocalInfo(std::vector<MethInfo> &MI)
{
    const int M = _instance.getNumMethods();
    MI.resize(M);

    std::vector<char> seen(_instance.getNumMethods(), 0);

    for (int m = 0; m < M; ++m)
    {
        MethInfo &info = MI[m];
        const Method &method = _instance.getMethodById(m);
        const auto &subs = method.getSubtasksIdx();
        const int n = (int)subs.size();

        // 1. Build subtask list with abstract/primitive flags
        info.subtasks.clear();
        info.subtasks.reserve(n);
        for (int id : subs)
            info.subtasks.push_back({id, _instance.isAbstractTask(id)});

        // 2. Compute "later" sets (successors ∪ parallel)
        info.later.assign(n, {});
        info.before.assign(n, {});
        const SubtaskOrderingInfo &ord = getOrderingInfo(m);

        for (int i = 0; i < n; ++i)
        {
            auto &vec = info.later[i];
            if (auto it = ord.successors.find(i); it != ord.successors.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());
            if (auto it = ord.parallel.find(i); it != ord.parallel.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [n](int x) { return x < 0 || x >= n; }),
                      vec.end());
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        }

        for (int i = 0; i < n; ++i)
        {
            auto &vec = info.before[i];
            if (auto it = ord.predecessors.find(i); it != ord.predecessors.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());
            if (auto it = ord.parallel.find(i); it != ord.parallel.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [n](int x) { return x < 0 || x >= n; }),
                      vec.end());
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        }

        // 3. Build caller→callee edges
        info.out.clear();
        for (int i = 0; i < n; ++i)
        {
            if (info.subtasks[i].isAbs)
            {
                const auto &decs = _instance.getAbstractTaskById(info.subtasks[i].id)
                                       .getDecompositionMethodsIdx();
                for (int d : decs)
                {
                    if (!seen[d])
                    {
                        seen[d] = 1;
                        info.out.push_back(d);
                    }
                }
            }
        }
        for (int d : info.out)
            seen[d] = 0;
        info.absSucc = info.out;

        // 4. Compute topological order
        info.topo.clear();
        if (n != 0)
        {
            std::vector<int> indeg(n, 0);
            for (int i = 0; i < n; ++i)
                for (int j : ord.successors.at(i))
                    ++indeg[j];

            std::queue<int> q;
            for (int i = 0; i < n; ++i)
                if (!indeg[i])
                    q.push(i);

            while (!q.empty())
            {
                int v = q.front();
                q.pop();
                info.topo.push_back(v);
                for (int w : ord.successors.at(v))
                    if (--indeg[w] == 0)
                        q.push(w);
            }

            if ((int)info.topo.size() != n)
            {
                info.topo.resize(n);
                std::iota(info.topo.begin(), info.topo.end(), 0);
            }
        }
    }

    // Initialize primitive action caches
    const int Nf = _instance.getNumPredicates();
    const int Na = _instance.getNumActions();

    actionBits.resize(Na, EffBits(Nf));
    actionCertPos.resize(Na, BitVec(Nf));
    actionPrecBits.resize(Na, BitVec(Nf));

    for (int a = 0; a < Na; ++a)
    {
        const Action &act = _instance.getActionById(a);
        EffBits eb(Nf);
        for (int f : act.getPosEffsIdx())
            eb.pos.set(f);
        for (int f : act.getNegEffsIdx())
            eb.neg.set(f);
        actionBits[a] = eb;

        BitVec cp(Nf);
        for (int f : act.getPosEffsIdx())
            cp.set(f);
        for (int f : act.getPreconditionsIdx())
            if (!eb.neg.test(f))
                cp.set(f);
        actionCertPos[a] = std::move(cp);

        BitVec cp2(Nf);
        for (int f : act.getPreconditionsIdx())
            cp2.set(f);
        actionPrecBits[a] = std::move(cp2);
    }
}

// ============================================================================
// PART 3: SCC-based Effects & Preconditions Computation
// ============================================================================

using CompGraph = std::vector<std::vector<int>>;

namespace {

static CompGraph buildCondensation(const std::vector<MethInfo> &MI,
                                   const Tarjan &tarjan)
{
    const int C = (int)tarjan.comps.size();
    CompGraph dag(C);
    std::vector<char> mark(C, 0);

    for (int v = 0; v < (int)MI.size(); ++v)
    {
        int srcC = tarjan.comp_of[v];
        for (int w : MI[v].out)
        {
            int dstC = tarjan.comp_of[w];
            if (srcC == dstC)
                continue;

            if (!mark[dstC])
            {
                dag[srcC].push_back(dstC);
                mark[dstC] = 1;
            }
        }
        for (int dst : dag[srcC])
            mark[dst] = 0;
    }
    return dag;
}

} // anonymous namespace

void EffectsInference::calculateAllMethodPossibleEffects()
{
    _possible_effects_cache.clear();

    const int M = _instance.getNumMethods();
    if (M == 0)
        return;

    const size_t Nf = _instance.getNumPredicates();

    Log::i("Building effects graph...\n");
    std::vector<MethInfo> G(M);
    std::vector<EffBits> local(M, EffBits(Nf));

    // Build graph and extract local primitive effects
    for (int m = 0; m < M; ++m)
    {
        const Method &meth = _instance.getMethodById(m);
        for (int tid : meth.getSubtasksIdx())
        {
            if (_instance.isAbstractTask(tid))
            {
                const auto &D =
                    _instance.getAbstractTaskById(tid).getDecompositionMethodsIdx();
                G[m].out.insert(G[m].out.end(), D.begin(), D.end());
            }
            else
            {
                const Action &a = _instance.getActionById(tid);
                for (int p : a.getPosEffsIdx())
                    local[m].pos.set(p);
                for (int n : a.getNegEffsIdx())
                    local[m].neg.set(n);
            }
        }
        std::sort(G[m].out.begin(), G[m].out.end());
        G[m].out.erase(std::unique(G[m].out.begin(), G[m].out.end()),
                       G[m].out.end());
    }

    // Collapse to SCCs
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(G);
    const int C = (int)tarjan.comps.size();

    // Build condensation DAG
    CompGraph dag(C);
    for (int v = 0; v < M; ++v)
    {
        for (int w : G[v].out)
        {
            if (tarjan.comp_of[v] != tarjan.comp_of[w])
            {
                bool found = std::find(dag[tarjan.comp_of[v]].begin(),
                                       dag[tarjan.comp_of[v]].end(),
                                       tarjan.comp_of[w]) != dag[tarjan.comp_of[v]].end();
                if (!found)
                    dag[tarjan.comp_of[v]].push_back(tarjan.comp_of[w]);
            }
        }
    }

    // Aggregate local effects per component
    std::vector<EffBits> compBits(C, EffBits(Nf));
    for (int c = 0; c < C; ++c)
        for (int m : tarjan.comps[c])
            compBits[c].or_with(local[m]);

    // Bottom-up propagation through DAG
    Log::i("Bottom-up SCC effects inference...\n");
    for (int c : reverseTopo(dag))
        for (int succ : dag[c])
            compBits[c].or_with(compBits[succ]);

    // Store in bitset format
    Log::i("Setting up effects cache...\n");
    _possibleEffBits.resize(M);
    for (int m = 0; m < M; ++m)
    {
        const EffBits &eb = compBits[tarjan.comp_of[m]];
        _possibleEffBits[m] = eb;
    }
}

void EffectsInference::calculateAllMethodCertifiedEffects()
{
    _certified_effects_cache.clear();

    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates();
    if (M == 0)
        return;

    // Build method graph
    Log::i("Building cert effects graph...\n");
    std::vector<MethInfo> MI(M);
    buildGraphOrderingAndLocalInfo(MI);

    // Compute SCCs and condensation DAG
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(MI);
    CompGraph dag = buildCondensation(MI, tarjan);
    auto order = reverseTopo(dag);

    // Initialize certified effects bitsets
    std::vector<EffBits> cert(M, EffBits(NF));
    EffBits tmp(NF), base(NF), later(NF), inter(NF);

    // Bottom-up inference over SCC components
    Log::i("Bottom-up SCC cert effects inference...\n");
    for (int C : order)
    {
        bool changed;
        do
        {
            changed = false;

            for (int m : tarjan.comps[C])
            {
                EffBits newCert(NF);
                const auto &info = MI[m];
                const int n = (int)info.subtasks.size();
                std::vector<EffBits> laterEff(n, EffBits(NF));

                // Compute effects that come after each subtask
                for (int k = (int)info.topo.size() - 1; k >= 0; --k)
                {
                    int i = info.topo[k];
                    EffBits acc(NF);
                    const Sub &si = info.subtasks[i];

                    if (si.id < 0)
                        continue;

                    for (int j : info.later[i])
                    {
                        const Sub &sj = info.subtasks[j];

                        if (sj.id < 0)
                            continue;

                        if (!sj.isAbs)
                        {
                            acc.or_with(actionBits[sj.id]);
                        }
                        else
                        {
                            const auto &decs = _instance.getAbstractTaskById(sj.id)
                                                   .getDecompositionMethodsIdx();
                            for (int d : decs)
                                acc.or_with(_possibleEffBits[d]);
                        }
                    }
                    laterEff[i] = std::move(acc);
                }

                // Analyse each subtask
                for (size_t idx = 0; idx < info.subtasks.size(); ++idx)
                {
                    const Sub &s = info.subtasks[idx];

                    if (s.id < 0)
                        continue;

                    // 1. Base effects
                    base = EffBits(NF);
                    if (!s.isAbs)
                    {
                        base.pos.or_with(actionBits[s.id].pos);
                        base.neg.or_with(actionBits[s.id].neg);
                        base.pos.or_with(actionCertPos[s.id]);
                    }
                    else
                    {
                        bool first = true;
                        for (int d : _instance.getAbstractTaskById(s.id)
                                         .getDecompositionMethodsIdx())
                        {
                            if (first)
                            {
                                base = cert[d];
                                first = false;
                            }
                            else
                            {
                                base.and_with(cert[d]);
                            }
                            if (base.pos.none() && base.neg.none())
                                break;
                        }
                        if (first)
                            continue;
                    }

                    // 2. Later effects that might kill base effects
                    later = laterEff[idx];

                    // 3. Keep only what survives
                    tmp = base;
                    tmp.minus_with(later);
                    newCert.or_with(tmp);
                }

                if (cert[m].or_with(newCert))
                    changed = true;
            }
        } while (changed);
    }

    Log::i("Setting certified effects in cache..\n");
    _certEffBits = std::move(cert);
}

void EffectsInference::calculateAllMethodPreconditionsBits()
{
    _preconditions_cache.clear();

    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates();
    if (M == 0)
        return;

    // Build method graph
    Log::i("Building precondition graph...\n");
    std::vector<MethInfo> MI(M);
    buildGraphOrderingAndLocalInfo(MI);

    // Compute SCCs and condensation DAG
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(MI);
    CompGraph dag = buildCondensation(MI, tarjan);
    auto order = reverseTopo(dag);

    // Initialize precondition bitsets
    std::vector<BitVec> prec(M, BitVec(NF));
    BitVec base(NF), tmp(NF), before(NF), inter(NF);

    // Bottom-up inference over SCC components
    Log::i("Bottom-up SCC precondition inference...\n");
    for (int C : order)
    {
        bool changed;
        do
        {
            changed = false;

            for (int m : tarjan.comps[C])
            {
                BitVec newCert(NF);
                const auto &info = MI[m];
                const int n = (int)info.subtasks.size();
                std::vector<BitVec> beforeEff(n, BitVec(NF));

                // Compute effects that come before each subtask
                for (int k = (int)info.topo.size() - 1; k >= 0; --k)
                {
                    int i = info.topo[k];
                    BitVec acc(NF);
                    const Sub &si = info.subtasks[i];

                    if (si.id < 0)
                        continue;

                    for (int j : info.before[i])
                    {
                        const Sub &sj = info.subtasks[j];

                        if (sj.id < 0)
                            continue;

                        if (!sj.isAbs)
                        {
                            acc.or_with(actionBits[sj.id].pos);
                        }
                        else
                        {
                            const auto &decs = _instance.getAbstractTaskById(sj.id)
                                                   .getDecompositionMethodsIdx();
                            for (int d : decs)
                                acc.or_with(_possibleEffBits[d].pos);
                        }
                    }
                    beforeEff[i] = std::move(acc);
                }

                // Analyse each subtask
                for (size_t idx = 0; idx < info.subtasks.size(); ++idx)
                {
                    const Sub &s = info.subtasks[idx];

                    if (s.id < 0)
                        continue;

                    // 1. Base preconditions
                    base = BitVec(NF);
                    if (!s.isAbs)
                    {
                        base.or_with(actionPrecBits[s.id]);
                    }
                    else
                    {
                        bool first = true;
                        for (int d : _instance.getAbstractTaskById(s.id)
                                         .getDecompositionMethodsIdx())
                        {
                            if (first)
                            {
                                base = prec[d];
                                first = false;
                            }
                            else
                            {
                                base.and_with(prec[d]);
                            }
                            if (base.none())
                                break;
                        }
                        if (first)
                            continue;
                    }

                    // 2. Earlier effects that might provide preconditions
                    before = beforeEff[idx];

                    // 3. Keep only what is not provided earlier
                    tmp = base;
                    tmp.minus_with(before);
                    newCert.or_with(tmp);
                }

                if (prec[m].or_with(newCert))
                    changed = true;
            }
        } while (changed);
    }

    Log::i("Setting preconditions in cache..\n");
    _precBits = std::move(prec);
}

// ============================================================================
// PART 4: Mutex Refinement
// ============================================================================

void EffectsInference::applyMutexRefinementForAllMethodsBits(Mutex &mutex)
{
    const int num_methods = _instance.getNumMethods();
    int total_removed = 0;

    for (int method_id = 0; method_id < num_methods; ++method_id)
    {
        EffBits &poss = _possibleEffBits[method_id];
        const EffBits &cert = _certEffBits[method_id];
        std::size_t removed = 0;

        // Remove possible positive effects mutex with certified positive effects
        cert.pos.for_each_set([&](int cert_pos_eff) {
            const auto &groups = mutex.getMutexGroupsOfPred(cert_pos_eff);
            for (int gIdx : groups)
            {
                const auto &group = mutex.getMutexGroup(gIdx);
                for (int pred : group)
                {
                    if (pred == cert_pos_eff)
                        continue;
                    if (poss.pos.test(pred))
                    {
                        poss.pos.clear(pred);
                        ++removed;
                        ++total_removed;
                    }
                }
            }
        });
    }

    Log::i("Mutex refinement pruned %d possible +effects in total\n", total_removed);
}

void EffectsInference::refineAllPossibleNegativeEffectsWithMutexAndPrecMethodsBits(
    Mutex &mutex)
{
    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates();
    std::size_t total_removed_neg = 0;

    for (int m = 0; m < M; ++m)
    {
        EffBits &poss = _possibleEffBits[m];
        const BitVec &pre = _precBits[m];

        BitVec dropMask(NF);

        // Build mask of negative effects to remove
        pre.for_each_set([&](int pred) {
            const auto &groups = mutex.getMutexGroupsOfPred(pred);
            for (int gIdx : groups)
            {
                const auto &group = mutex.getMutexGroup(gIdx);
                for (int q : group)
                {
                    if (q != pred)
                        dropMask.set(q);
                }
            }
        });

        // Remove negative effects in the mask
        std::size_t before = poss.neg.count();
        poss.neg.minus_with(dropMask);
        std::size_t after = poss.neg.count();

        total_removed_neg += before - after;
    }

    Log::i("Mutex refinement pruned %zu possible negative effects.\n",
           total_removed_neg);
}

// ============================================================================
// PART 5: Main Orchestration & Cache Management
// ============================================================================

void EffectsInference::calculateAllMethodEffects()
{
    int num_methods = _instance.getNumMethods();
    Log::i("Calculating possible effects for all methods...\n");
    calculateAllMethodPossibleEffects();
    Log::i("Done !\n");

    // Compute certified effects
    int num_removed_2 = 0;
    calculateAllMethodCertifiedEffects();

    // Remove impossible effects: if method cert makes –f, then +f is impossible
    for (int i = 0; i < num_methods; ++i)
    {
        std::size_t before = _possibleEffBits[i].pos.count() + _possibleEffBits[i].neg.count();
        _possibleEffBits[i].pos.minus_with(_certEffBits[i].neg);
        _possibleEffBits[i].neg.minus_with(_certEffBits[i].pos);
        std::size_t after = _possibleEffBits[i].pos.count() + _possibleEffBits[i].neg.count();
        num_removed_2 += (before - after);
    }

    Log::i("Number of possible effects removed (bitset): %d\n", num_removed_2);
    Log::i("Finished calculating initial possible and certified effects for all methods.\n");
}

// ============================================================================
// Public API: Getters and Main Entry Point
// ============================================================================

std::optional<EffectsSet> EffectsInference::getPossibleEffects(int method_id) const
{
    if (_possible_effects_cache.count(method_id))
        return _possible_effects_cache.at(method_id);
    return std::nullopt;
}

std::optional<EffectsSet> EffectsInference::getCertifiedEffects(int method_id) const
{
    if (_certified_effects_cache.count(method_id))
        return _certified_effects_cache.at(method_id);
    return std::nullopt;
}

std::optional<std::unordered_set<int>> EffectsInference::getPreconditions(int method_id) const
{
    if (_preconditions_cache.count(method_id))
        return _preconditions_cache.at(method_id);
    return std::nullopt;
}

void EffectsInference::clearCaches()
{
    _possible_effects_cache.clear();
    _certified_effects_cache.clear();
    _preconditions_cache.clear();
    _ordering_info_cache.clear();
}

void EffectsInference::calculateAllMethodsPrecsAndEffs(std::vector<Method> &methods, Mutex *mutex)
{
    Log::i("Calculating all methods preconditions and effects...\n");

    Log::i("Set ordering info for all methods...\n");
    setOrderingInfoForAllMethods();

    Log::i("Calculating all method effects...\n");
    calculateAllMethodEffects();
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        Log::i("Refining all possible effects with mutex...\n");
        applyMutexRefinementForAllMethodsBits(*mutex);
        Log::i("Done !\n");
    }

    Log::i("Calculating all method preconditions...\n");
    calculateAllMethodPreconditionsBits();
    Log::i("Done !\n");

    if (mutex != nullptr)
    {
        Log::i("Refining all possible negative effects with mutex and preconditions...\n");
        refineAllPossibleNegativeEffectsWithMutexAndPrecMethodsBits(*mutex);
        Log::i("Done !\n");
    }

    // Transform bitset representation to cached sets and apply to methods
    Log::i("Transforming all effects and preconditions to methods...\n");
    _preconditions_cache.clear();
    _certified_effects_cache.clear();
    _possible_effects_cache.clear();

    const int Nf = _instance.getNumPredicates();
    for (int i = 0; i < methods.size(); i++)
    {
        // Store certified effects
        EffectsSet es;
        const EffBits &eb = _certEffBits[i];
        for (size_t b = 0; b < Nf; ++b)
        {
            if (eb.pos.test(b))
                es.positive.insert((int)b);
            if (eb.neg.test(b))
                es.negative.insert((int)b);
        }
        _certified_effects_cache[i] = std::move(es);

        // Store possible effects
        const EffBits &eb_pos = _possibleEffBits[i];
        EffectsSet es_pos;
        for (size_t b = 0; b < Nf; ++b)
        {
            if (eb_pos.pos.test(b))
                es_pos.positive.insert((int)b);
            if (eb_pos.neg.test(b))
                es_pos.negative.insert((int)b);
        }
        _possible_effects_cache[i] = std::move(es_pos);

        // Store preconditions
        const BitVec &eb_prec = _precBits[i];
        for (size_t b = 0; b < Nf; ++b)
            if (eb_prec.test(b))
                _preconditions_cache[i].insert((int)b);
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
            for (const int &precondition : _preconditions_cache.at(i))
                method.addPreconditionIdx(precondition);
        }
    }

    printAllMethodPrecsAndEffs();

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
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(prec)));

        Log::i("  Possible Positive Effects (%d):\n", method.getPossPosEffsIdx().size());
        for (const int &eff : method.getPossPosEffsIdx())
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));

        Log::i("  Possible Negative Effects (%d):\n", method.getPossNegEffsIdx().size());
        for (const int &eff : method.getPossNegEffsIdx())
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));

        Log::i("  Certified Positive Effects (%d):\n", method.getPosEffsIdx().size());
        for (const int &eff : method.getPosEffsIdx())
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));

        Log::i("  Certified Negative Effects (%d):\n", method.getNegEffsIdx().size());
        for (const int &eff : method.getNegEffsIdx())
            Log::i("    %s\n", TOSTR(_instance.getPredicateById(eff)));

        Log::i("\n");
    }
    Log::i("Finished printing all methods preconditions and effects.\n");
}