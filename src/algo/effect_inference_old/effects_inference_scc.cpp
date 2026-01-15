// ============================================================================
//  EffectsInference::calculateAllMethodEffects  –  SCC collapsed version
// ============================================================================

#include "effects_inference.h"

#include <algorithm>
#include <queue>
#include <stack>

// ---------------------------------------------------------------------------
//  Simple dynamic bit-vector (no dependencies)
// ---------------------------------------------------------------------------
struct BitVec
{
    using Block = uint64_t;
    std::vector<Block> v;

    explicit BitVec(size_t bits = 0) : v((bits + 63) >> 6, 0) {}

    void set(int bit)       { v[bit >> 6] |= Block(1) << (bit & 63); }
    bool test(int bit)const { return v[bit >> 6] &  (Block(1) << (bit & 63)); }

    /* bitwise OR  (returns true if anything changed) */
    bool or_with(const BitVec &o)
    {
        bool changed = false;
        for (size_t i = 0; i < v.size(); ++i)
        {
            Block before = v[i];
            v[i] |= o.v[i];
            changed |= (v[i] != before);
        }
        return changed;
    }

    size_t count() const    // popcount (optional)
    {
        size_t s = 0;
        for (Block b : v) s += __builtin_popcountll(b);
        return s;
    }
};

struct EffBits {
    BitVec pos, neg;

    EffBits() = default;
    explicit EffBits(size_t nfluents) : pos(nfluents), neg(nfluents) {}

    bool or_with(const EffBits &o)
    {
        bool ch1 = pos.or_with(o.pos);
        bool ch2 = neg.or_with(o.neg);
        return ch1 | ch2;
    }
};

// ---------- helpers (unchanged except the last step) -----------------------

namespace
{

struct MethodNode
{
    std::vector<int> out;           // edges: caller → callee
};

using Comp      = std::vector<int>;              // one SCC
using CompGraph = std::vector<std::vector<int>>; // condensation DAG

/* -- build graph & locals ------------------------------------------------- */
static void buildGraphAndLocals(const HtnInstance      &inst,
                                std::vector<MethodNode> &graph,
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
            else                       // primitive action → local effects
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
    explicit Tarjan(const std::vector<MethodNode> &g) : G(g)
    {
        int n = (int)G.size();
        idx.assign(n, -1);
        low.assign(n, -1);
        on.assign(n, false);
        comp_of.assign(n, -1);

        for (int v = 0; v < n; ++v)
            if (idx[v] == -1) dfs(v);
    }

    std::vector<Comp> comps;       // list of components
    std::vector<int>  comp_of;     // method → component

private:
    const std::vector<MethodNode> &G;
    std::vector<int> idx, low;
    std::vector<char> on;
    std::vector<int> S;
    int t = 0;

    void dfs(int v)
    {
        idx[v] = low[v] = t++;
        S.push_back(v); on[v] = 1;

        for (int w : G[v].out)
            if (idx[w] == -1) { dfs(w); low[v] = std::min(low[v], low[w]); }
            else if (on[w])     low[v] = std::min(low[v], idx[w]);

        if (low[v] == idx[v])
        {
            comps.emplace_back();
            int w;
            do {
                w = S.back(); S.pop_back(); on[w] = 0;
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
        for (int w : dag[c]) ++indeg[w];

    std::queue<int> q;
    for (int c = 0; c < C; ++c)
        if (indeg[c] == 0) q.push(c);

    std::vector<int> order;
    order.reserve(C);
    while (!q.empty())
    {
        int c = q.front(); q.pop();
        order.push_back(c);
        for (int w : dag[c])
            if (--indeg[w] == 0) q.push(w);
    }
    std::reverse(order.begin(), order.end()); // children first
    return order;
}

} // anonymous namespace

void EffectsInference::calculateAllMethodPossibleEffects()
{
    _possible_effects_cache.clear();

    const int M = _instance.getNumMethods();
    if (M == 0) return;

    /* how many fluents in the domain? */
    const size_t Nf = _instance.getNumPredicates();   // add this accessor

    /* 1. build graph + local primitive bits */
    Log::i("Building effects graph...\n");
    std::vector<MethodNode> G(M);
    std::vector<EffBits>    local(M, EffBits(Nf));

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
                for (int p : a.getPosEffsIdx()) local[m].pos.set(p);
                for (int n : a.getNegEffsIdx()) local[m].neg.set(n);
            }
        }
        std::sort(G[m].out.begin(), G[m].out.end());
        G[m].out.erase(std::unique(G[m].out.begin(), G[m].out.end()),
                       G[m].out.end());
    }

    /* 2. collapse to SCCs  (unchanged) */
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(G);
    const int C = (int)tarjan.comps.size();

    /* 3. condensation DAG */
    CompGraph dag(C);
    for (int v = 0; v < M; ++v)
        for (int w : G[v].out)
            if (tarjan.comp_of[v] != tarjan.comp_of[w])
                dag[tarjan.comp_of[v]].push_back(tarjan.comp_of[w]);

    /* 4. one EffBits per component = union of its members’ locals */
    std::vector<EffBits> compBits(C, EffBits(Nf));
    for (int c = 0; c < C; ++c)
        for (int m : tarjan.comps[c])
            compBits[c].or_with(local[m]);

    /* 5. single bottom-up pass (bitwise OR) */
    Log::i("Bottom-up SCC effects inference...\n");
    for (int c : reverseTopo(dag))        // children processed first
        for (int succ : dag[c])
            compBits[c].or_with(compBits[succ]);

    /* 6. translate back to the public unordered_set cache */
    Log::i("Setting up effects cache...\n");
    for (int m = 0; m < M; ++m)
    {
        const EffBits      &eb = compBits[tarjan.comp_of[m]];
        EffectsSet          es;

        for (size_t b = 0; b < Nf; ++b)
            if (eb.pos.test(b)) es.positive.insert((int)b);
        for (size_t b = 0; b < Nf; ++b)
            if (eb.neg.test(b)) es.negative.insert((int)b);

        _possible_effects_cache[m] = std::move(es);
    }
}