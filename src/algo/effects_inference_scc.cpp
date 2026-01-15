// ============================================================================
//  EffectsInference::calculateAllMethodEffects  –  SCC collapsed version
// ============================================================================

#include "effects_inference.h"

#include <algorithm>
#include <queue>
#include <stack>

void EffectsInference::calculateAllMethodPossibleEffects()
{
    _possible_effects_cache.clear();

    const int M = _instance.getNumMethods();
    if (M == 0) return;

    /* how many fluents in the domain? */
    const size_t Nf = _instance.getNumPredicates();   // add this accessor

    /* 1. build graph + local primitive bits */
    Log::i("Building effects graph...\n");
    std::vector<MethInfo> G(M);
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
    _possibleEffBits.resize(M);
    for (int m = 0; m < M; ++m)
    {
        const EffBits      &eb = compBits[tarjan.comp_of[m]];

        _possibleEffBits[m] = eb; // store for later use
        EffectsSet          es;

        // for (size_t b = 0; b < Nf; ++b)
        //     if (eb.pos.test(b)) es.positive.insert((int)b);
        // for (size_t b = 0; b < Nf; ++b)
        //     if (eb.neg.test(b)) es.negative.insert((int)b);

        // _possible_effects_cache[m] = std::move(es);
    }
}