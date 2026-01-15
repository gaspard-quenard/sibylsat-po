#ifndef EFFECTS_INFERENCE_H
#define EFFECTS_INFERENCE_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set> // For recursion detection
#include <optional>
#include <utility> // For std::pair
#include <queue>
#include <bit>

#include "data/htn_instance.h"
#include "data/method.h"
#include "data/action.h"
#include "data/abstract_task.h"
#include "data/mutex.h" // Include Mutex header
#include "util/log.h"   // Include for logging
#include "util/names.h" // Include for TOSTR

struct Sub
{
    int id;
    bool isAbs;
};
struct MethInfo
{
    std::vector<Sub> subtasks;            // in method order
    std::vector<std::vector<int>> later;  // later[idx] = indices ≥ idx
    std::vector<std::vector<int>> before; // transitive closure (indices)
    std::vector<int> topo;                // topological order of idx
    std::vector<int> out;                 // caller → callee
    std::vector<int> absSucc;             // == out (deduplicated)
};

struct BitVec
{
    using Block = uint64_t;
    std::vector<Block> v;

    explicit BitVec(size_t n = 0) : v((n + 63) >> 6, 0) {}

    inline void set(int b) { v[b >> 6] |= Block(1) << (b & 63); }
    inline bool test(int b) const { return v[b >> 6] & (Block(1) << (b & 63)); }

    /* OR / AND / MINUS  (return true if changed) ------------------------ */
    bool or_with(const BitVec &o)
    {
        bool ch = false;
        for (size_t i = 0; i < v.size(); ++i)
        {
            auto x = v[i] | o.v[i];
            ch |= x != v[i];
            v[i] = x;
        }
        return ch;
    }
    bool and_with(const BitVec &o)
    {
        bool ch = false;
        for (size_t i = 0; i < v.size(); ++i)
        {
            auto x = v[i] & o.v[i];
            ch |= x != v[i];
            v[i] = x;
        }
        return ch;
    }
    void minus_with(const BitVec &o)
    {
        for (size_t i = 0; i < v.size(); ++i)
            v[i] &= ~o.v[i];
    }

    bool none() const
    { // true ⇔ every bit is 0
        for (Block b : v)
            if (b)
                return false;
        return true;
    }
    bool any() const { return !none(); }

    /* number of 1-bits in the whole vector */
    std::size_t count() const
    {
        std::size_t s = 0;
#ifdef __GNUG__ // GCC / Clang
        for (Block b : v)
            s += __builtin_popcountll(b);
#else // C++20 alternative
        for (Block b : v)
            s += std::popcount(b);
#endif
        return s;
    }

    // Turn one specific bit off
    inline void clear(int bit) { v[bit >> 6] &= ~(Block(1) << (bit & 63)); }

    /* iterate over all set bits, calling F(int bit) */
    template <class F>
    void for_each_set(F f) const
    {
        for (size_t w = 0; w < v.size(); ++w) // every 64-bit word
        {
            Block word = v[w]; // copy of the word
            while (word)       // until no 1s left
            {
                int b = std::countr_zero(word); // index of lowest 1-bit
                f(int(w * 64 + b));             // call the callback

                word &= word - 1; // clear **that** 1-bit
            }
        }
    }
};

struct EffBits
{
    BitVec pos, neg;
    EffBits() = default;
    explicit EffBits(size_t n) : pos(n), neg(n) {}
    bool or_with(const EffBits &o)
    {
        bool a = pos.or_with(o.pos);
        bool b = neg.or_with(o.neg);
        return a | b;
    }
    bool and_with(const EffBits &o)
    {
        bool a = pos.and_with(o.pos);
        bool b = neg.and_with(o.neg);
        return a | b;
    }
    void minus_with(const EffBits &o)
    {
        pos.minus_with(o.neg); // remove pos cleared by later –
        neg.minus_with(o.pos); // remove neg cleared by later +
    }

    bool none() const { return pos.none() && neg.none(); }
};

// Structure to hold both positive and negative effects
struct EffectsSet
{
    std::unordered_set<int> positive;
    std::unordered_set<int> negative;

    // Helper to check if empty
    bool isEmpty() const
    {
        return positive.empty() && negative.empty();
    }
};

namespace
{

    // struct MethodNode
    // {
    //     std::vector<int> out;           // edges: caller → callee
    // };

    using Comp = std::vector<int>;                   // one SCC
    using CompGraph = std::vector<std::vector<int>>; // condensation DAG

    /* -- build graph & locals ------------------------------------------------- */
    static void buildGraphAndLocals(const HtnInstance &inst,
                                    std::vector<MethInfo> &graph,
                                    std::vector<EffectsSet> &local)
    {
        const int M = inst.getNumMethods();
        graph.resize(M);
        local.resize(M);

        for (int m = 0; m < M; ++m)
        {
            const Method &meth = inst.getMethodById(m);

            for (int tid : meth.getSubtasksIdx())
            {
                if (inst.isAbstractTask(tid))
                {
                    const auto &decs =
                        inst.getAbstractTaskById(tid).getDecompositionMethodsIdx();
                    graph[m].out.insert(graph[m].out.end(), decs.begin(), decs.end());
                }
                else // primitive action → local effects
                {
                    const Action &a = inst.getActionById(tid);
                    local[m].positive.insert(a.getPosEffsIdx().begin(),
                                             a.getPosEffsIdx().end());
                    local[m].negative.insert(a.getNegEffsIdx().begin(),
                                             a.getNegEffsIdx().end());
                }
            }
            std::sort(graph[m].out.begin(), graph[m].out.end());
            graph[m].out.erase(std::unique(graph[m].out.begin(), graph[m].out.end()),
                               graph[m].out.end());
        }
    }

    /* -- Tarjan SCC ----------------------------------------------------------- */
    struct Tarjan
    {
        explicit Tarjan(const std::vector<MethInfo> &g) : G(g)
        {
            int n = (int)G.size();
            idx.assign(n, -1);
            low.assign(n, -1);
            on.assign(n, false);
            comp_of.assign(n, -1);

            for (int v = 0; v < n; ++v)
                if (idx[v] == -1)
                    dfs(v);
        }

        std::vector<Comp> comps;  // list of components
        std::vector<int> comp_of; // method → component

    private:
        const std::vector<MethInfo> &G;
        std::vector<int> idx, low;
        std::vector<char> on;
        std::vector<int> S;
        int t = 0;

        void dfs(int v)
        {
            idx[v] = low[v] = t++;
            S.push_back(v);
            on[v] = 1;

            for (int w : G[v].out)
                if (idx[w] == -1)
                {
                    dfs(w);
                    low[v] = std::min(low[v], low[w]);
                }
                else if (on[w])
                    low[v] = std::min(low[v], idx[w]);

            if (low[v] == idx[v])
            {
                comps.emplace_back();
                int w;
                do
                {
                    w = S.back();
                    S.pop_back();
                    on[w] = 0;
                    comp_of[w] = (int)comps.size() - 1;
                    comps.back().push_back(w);
                } while (w != v);
            }
        }
    };

    /* -- reverse topological order of condensation DAG ------------------------ */
    static std::vector<int> reverseTopo(const CompGraph &dag)
    {
        const int C = (int)dag.size();
        std::vector<int> indeg(C, 0);
        for (int c = 0; c < C; ++c)
            for (int w : dag[c])
                ++indeg[w];

        std::queue<int> q;
        for (int c = 0; c < C; ++c)
            if (indeg[c] == 0)
                q.push(c);

        std::vector<int> order;
        order.reserve(C);
        while (!q.empty())
        {
            int c = q.front();
            q.pop();
            order.push_back(c);
            for (int w : dag[c])
                if (--indeg[w] == 0)
                    q.push(w);
        }
        std::reverse(order.begin(), order.end()); // children first
        return order;
    }

} // anonymous namespace

class EffectsInference
{
private:
    const HtnInstance &_instance;

    /* one EffBits per primitive action (pos,neg only) +  quick mask for
   “certified base” of an action = effects  ∪  (preconds \ negEffects) */
    std::vector<EffBits> actionBits;    // size = #actions
    std::vector<BitVec> actionPrecBits; // size = #actions
    std::vector<BitVec> actionCertPos;  // pre-allocated once

    std::vector<EffBits> _possibleEffBits; // size = #methods
    std::vector<EffBits> _certEffBits;     // size = #methods
    std::vector<BitVec> _precBits;         // size = #methods

    // Caching for computed effects to handle recursion and improve performance
    std::unordered_map<int, EffectsSet> _possible_effects_cache;           // Stores original possible effects
    std::unordered_map<int, EffectsSet> _certified_effects_cache;          // Stores original certified effects
    std::unordered_map<int, std::unordered_set<int>> _preconditions_cache; // Stores mutex-refined possible effects

    // Caching for subtask ordering analysis within a method
    struct SubtaskOrderingInfo
    {
        std::unordered_map<int, std::unordered_set<int>> successors;   // Map subtask_idx -> set of successor subtask_idx
        std::unordered_map<int, std::unordered_set<int>> predecessors; // Map subtask_idx -> set of predecessor subtask_idx
        std::unordered_map<int, std::unordered_set<int>> parallel;     // Map subtask_idx -> set of parallel subtask_idx
        std::vector<std::vector<int>> adj;                             // Adjacency list for transitive closure
        std::vector<std::vector<int>> rev_adj;                         // Reversed adjacency list
        bool has_cycle = false;
    };
    std::unordered_map<int, SubtaskOrderingInfo> _ordering_info_cache; // Map method_id -> ordering info

    // Helper function to compute or retrieve ordering info for a method
    void setOrderingInfoForAllMethods();
    const SubtaskOrderingInfo &getOrderingInfo(int method_id) const;

    // Helper function for transitive closure using DFS
    void transitiveClosureDFS(int u, int current_node, const std::vector<std::vector<int>> &adj, std::vector<bool> &visited, std::unordered_set<int> &reachable);

    // --- Mutex Refinement ---
    void applyMutexRefinementForAllMethodsBits(Mutex &mutex);
    void refineAllPossibleNegativeEffectsWithMutexAndPrecMethodsBits(Mutex &mutex);

    // Test
    void buildGraphOrderingAndLocalInfo(std::vector<MethInfo> &MI);

    // Main function to trigger the computation for all methods
    void calculateAllMethodEffects();

    void calculateAllMethodPossibleEffects();
    void calculateAllMethodCertifiedEffects();
    void calculateAllMethodPreconditionsBits();

public:
    EffectsInference(const HtnInstance &instance);

    void calculateAllMethodsPrecsAndEffs(std::vector<Method> &methods, Mutex *mutex); // Compute preconditions and effects for all methods

    // Getters for the computed effects
    std::optional<EffectsSet> getPossibleEffects(int method_id) const;  // Gets original possible effects
    std::optional<EffectsSet> getCertifiedEffects(int method_id) const; // Gets original certified effects
    std::optional<std::unordered_set<int>> getPreconditions(int method_id) const;

    void clearCaches(); // Clear all caches

    void printAllMethodPrecsAndEffs() const;
};

#endif // EFFECTS_INFERENCE_H
