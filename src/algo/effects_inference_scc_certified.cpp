// ============================================================================
//  EffectsInference::calculateAllMethodEffects  –  SCC collapsed version
// ============================================================================

#include "effects_inference.h"

#include <algorithm>
#include <queue>
#include <stack>
#include <numeric>

/*-------------------------------------------------------------------------*/
/*  Build, for every method,                                               */
/*   – sub-tasks (id + “isAbstract”),                                      */
/*   – the set of indices that MAY execute after each subtask              */
/*     (= successors ∪ parallel),                                          */
/*   – the caller → callee edges needed for Tarjan,                        */
/*   – the cached list of “abstract successors” (same as ‘out’ but         */
/*     primitives removed so we need no isAbstract checks later).          */
/*                                                                         */
/*  The routine is cheap: O(|subtasks| + |edges|) in total.                */
/*-------------------------------------------------------------------------*/

void EffectsInference::buildGraphOrderingAndLocalInfo(std::vector<MethInfo> &MI)
{
    const int M = _instance.getNumMethods();
    MI.resize(M);

    /* helper buffer to deduplicate outgoing edges --------------------- */
    std::vector<char> seen(_instance.getNumMethods(), 0);

    for (int m = 0; m < M; ++m)
    {
        MethInfo &info = MI[m];
        const Method &method = _instance.getMethodById(m);
        const auto &subs = method.getSubtasksIdx();
        const int n = (int)subs.size();

        /* ---------- (1)  subtask list -------------------------------- */
        info.subtasks.assign({}, {}); // clear
        info.subtasks.reserve(n);
        for (int id : subs)
            info.subtasks.push_back({id, _instance.isAbstractTask(id)});

        /* ---------- (2)  direct “later” sets (successor ∪ parallel) -- */
        info.later.assign(n, {});
        info.before.assign(n, {});
        const SubtaskOrderingInfo &ord = getOrderingInfo(m);

        for (int i = 0; i < n; ++i)
        {
            auto &vec = info.later[i]; // All the subtasks that may be executed after i

            /* successors (true ordering) */
            if (auto it = ord.successors.find(i); it != ord.successors.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            /* parallel                                                   */
            if (auto it = ord.parallel.find(i); it != ord.parallel.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            /* clean-up (bounds + unique)                                */
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [n](int x)
                                     { return x < 0 || x >= n; }),
                      vec.end());
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        }

        for (int i = 0; i < n; ++i)
        {
            auto &vec = info.before[i]; // All the subtasks that may be before i

            /* predecessors (true ordering) */
            if (auto it = ord.predecessors.find(i); it != ord.predecessors.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            /* parallel                                                   */
            if (auto it = ord.parallel.find(i); it != ord.parallel.end())
                vec.insert(vec.end(), it->second.begin(), it->second.end());

            /* clean-up (bounds + unique)                                */
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [n](int x)
                                     { return x < 0 || x >= n; }),
                      vec.end());
            std::sort(vec.begin(), vec.end());
            vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
        }

        /* ---------- (3)  outgoing caller→callee edges ---------------- */
        info.out.clear();
        for (int i = 0; i < n; ++i)
            if (info.subtasks[i].isAbs)
            {
                const auto &decs =
                    _instance.getAbstractTaskById(info.subtasks[i].id)
                        .getDecompositionMethodsIdx();
                for (int d : decs)
                    if (!seen[d])
                    {
                        seen[d] = 1;
                        info.out.push_back(d);
                    }
            }
        for (int d : info.out)
            seen[d] = 0; // reset flags
        info.absSucc = info.out;

        /* ---------- (4)  topological order of indices ---------------- */
        info.topo.clear();
        if (n != 0)
        {
            std::vector<int> indeg(n, 0);
            for (int i = 0; i < n; ++i)
                for (int j : ord.successors.at(i)) // only true arcs
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
            if ((int)info.topo.size() != n) // cycle safeguard
            {
                info.topo.resize(n);
                std::iota(info.topo.begin(), info.topo.end(), 0);
            }
        }
    }

    /* ---------- (6)  primitive-action caches (unchanged) ------------- */
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

// ---------------------------------------------------------------------------
//  Condensation DAG  –  each strongly-connected component becomes one node
// ---------------------------------------------------------------------------
using CompGraph = std::vector<std::vector<int>>; // dag[src] → dst components

/*  MI … any array with “out” (caller→callee edges)                       */
/*  tarjan.comp_of[v] … component index of method v                       */
/*  tarjan.comps.size()  … number of components                           */
static CompGraph buildCondensation(const std::vector<MethInfo> &MI,
                                   const Tarjan &tarjan)
{
    const int C = (int)tarjan.comps.size();
    CompGraph dag(C);

    /* helper buffer to avoid duplicate edges without std::unordered_set  */
    std::vector<char> mark(C, 0);

    for (int v = 0; v < (int)MI.size(); ++v)
    {
        int srcC = tarjan.comp_of[v];
        for (int w : MI[v].out) // caller → callee
        {
            int dstC = tarjan.comp_of[w];
            if (srcC == dstC)
                continue; // intra-component edge

            if (!mark[dstC]) // add once
            {
                dag[srcC].push_back(dstC);
                mark[dstC] = 1;
            }
        }
        /* clear marks so the next srcC starts with a clean slate */
        for (int dst : dag[srcC])
            mark[dst] = 0;
    }
    return dag;
}

/*  Remove from _possibleEffBits[method] every positive effect
    that lies in a mutex group with ANY certified positive effect
    of the same method (except the certified one itself).              */
void EffectsInference::applyMutexRefinementForAllMethodsBits(Mutex &mutex)
{
    const int num_methods = _instance.getNumMethods();
    int total_removed = 0;
    for (int method_id = 0; method_id < num_methods; ++method_id)
    {
        EffBits &poss = _possibleEffBits[method_id]; // mutate
        const EffBits &cert = _certEffBits[method_id];

        std::size_t removed = 0;

        /* iterate over every certified *positive* effect ------------------- */
        cert.pos.for_each_set([&](int cert_pos_eff)
                              {
            const auto &groups = mutex.getMutexGroupsOfPred(cert_pos_eff);
            for (int gIdx : groups)
            {
                const auto &group = mutex.getMutexGroup(gIdx);
    
                for (int pred : group)                      // each member
                {
                    if (pred == cert_pos_eff) continue;     // keep itself
                    if (poss.pos.test(pred))                // present ?
                    {
                        poss.pos.clear(pred);
                        ++removed;
                        ++total_removed;
                    }
                }
            } });

        // if (removed)
            // Log::i("Mutex refinement pruned %zu possible +effects in method %d (%s)\n",
                //    removed, method_id,
                //    _instance.getMethodById(method_id).getName().c_str());
    }

    Log::i("Mutex refinement pruned %d possible +effects in total\n", total_removed);
}

/*  Remove, for every method, every *possible negative* effect ¬p that is
    mutex with ANY certified precondition p of that same method.          */
void EffectsInference::refineAllPossibleNegativeEffectsWithMutexAndPrecMethodsBits(
    Mutex &mutex)
{
    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates();

    std::size_t total_removed_neg = 0; // statistics

    for (int m = 0; m < M; ++m)
    {
        // Log::i("For method %s\n",
            //    _instance.getMethodById(m).getName().c_str());
        EffBits &poss = _possibleEffBits[m]; // mutate in place
        const BitVec &pre = _precBits[m];    // certified preconditions

        /* --- build a mask of neg-effects to drop -------------------- */
        BitVec dropMask(NF);

        pre.for_each_set([&](int pred) // for every precondition p
                         {
            const auto &groups = mutex.getMutexGroupsOfPred(pred);
            for (int gIdx : groups)
            {
                const auto &group = mutex.getMutexGroup(gIdx);
                for (int q : group)             // every predicate in that group
                    if (q != pred)             // keep the precondition itself 
                        dropMask.set(q);
                    
            } });

        /* --- erase those bits from the possible NEGATIVE set -------- */
        std::size_t before = poss.neg.count();
        poss.neg.minus_with(dropMask);
        std::size_t after = poss.neg.count();

        // if (before != after)
            // Log::i("Mutex refinement using prec pruned %zu possible -effects in method %d (%s)\n",
                //    before - after, m,
                //    _instance.getMethodById(m).getName().c_str());

        total_removed_neg += before - after;
    }

    Log::i("Mutex refinement pruned %zu possible negative effects.\n",
           total_removed_neg);
}

void EffectsInference::calculateAllMethodCertifiedEffects()
{
    _certified_effects_cache.clear(); // still keep the public map

    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates(); // ← add accessor
    if (M == 0)
        return;

    /* ---------- 1. build method graph & ordering-info cache ------------ */
    // identical to the possible-effects pass, but we ALSO keep, for every
    // method, its list of subtasks and the “successors ∪ parallel”
    Log::i("Building cert effects graph...\n");
    std::vector<MethInfo> MI(M);
    buildGraphOrderingAndLocalInfo(MI); // your own routine

    /* ---------- 2. Tarjan SCC & condensation DAG ----------------------- */
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(MI);                             // as before
    CompGraph dag = buildCondensation(MI, tarjan); // as before
    auto order = reverseTopo(dag);                 // children first

    /* ---------- 3. keep EffBits per method, initially empty ------------ */
    std::vector<EffBits> cert(M, EffBits(NF));

    /* ---------- 4. iterate bottom-up over components ------------------- */
    EffBits tmp(NF), base(NF), later(NF), inter(NF);

    Log::i("Bottom-up SCC cert effects inference...\n");
    for (int C : order) // already children→parents
    {
        // Log::i("SCC %d/%d\n", C, (int)tarjan.comps.size());
        bool changed;
        do
        { // fix-point inside the SCC
            changed = false;

            for (int m : tarjan.comps[C])
            {
                // Log::i("For method %s\n", _instance.getMethodById(m).getName().c_str());
                EffBits newCert(NF); // rebuild from scratch
                const auto &info = MI[m];

                const int n = (int)info.subtasks.size();
                std::vector<EffBits> laterEff(n, EffBits(NF));

                for (int k = (int)info.topo.size() - 1; k >= 0; --k)
                {
                    int i = info.topo[k]; // real sub-task index
                    EffBits acc(NF);      // accumulator for i
                    const Sub &si = info.subtasks[i];

                    if (si.id < 0)
                        continue; // skip special actions (goal, init, blank)

                    for (int j : info.later[i]) // all tasks that *may* follow i
                    {
                        const Sub &sj = info.subtasks[j];

                        if (sj.id < 0)
                            continue; // skip special actions (goal, init, blank)

                        if (!sj.isAbs) // ---------- primitive action
                        {
                            acc.or_with(actionBits[sj.id]); //  its own effects
                        }
                        else // ---------- abstract task
                        {
                            const auto &decs =
                                _instance.getAbstractTaskById(sj.id).getDecompositionMethodsIdx();
                            for (int d : decs) //  union all decomposition methods
                                acc.or_with(_possibleEffBits[d]);
                        }
                    }
                    laterEff[i] = std::move(acc); // store result for index i
                }

                /* ---------- analyse every sub-task -------------------- */
                for (size_t idx = 0; idx < info.subtasks.size(); ++idx)
                {
                    const Sub &s = info.subtasks[idx];

                    if (s.id < 0)
                        continue; // skip special actions (goal, init, blank)

                    // ---- 1. base effects of this sub-task ------------
                    base = EffBits(NF);
                    if (!s.isAbs)
                    {
                        base.pos.or_with(actionBits[s.id].pos);
                        base.neg.or_with(actionBits[s.id].neg);
                        base.pos.or_with(actionCertPos[s.id]); // precond trick
                    }
                    else
                    {
                        // intersection of certified effects of all decomposition methods
                        bool first = true;
                        for (int d : _instance.getAbstractTaskById(s.id).getDecompositionMethodsIdx())
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
                            // if (base.pos.v==0 && base.neg.v==0) break; // empty early
                            if (base.pos.none() && base.neg.none())
                                break;
                        }
                        if (first)
                            continue; // no decomposition → nothing certified
                    }

                    // ---- 2. effects that *might* come later ----------
                    later = laterEff[idx];

                    // ---- 3. keep only what survives ------------------
                    tmp = base;            // copy
                    tmp.minus_with(later); // remove killed bits
                    newCert.or_with(tmp);  // union into method
                }

                if (cert[m].or_with(newCert)) // grow?
                    changed = true;
            }
        } while (changed);
    }

    Log::i("Setting certified effects in cache..\n");
    /* ---------- 5. expose in the public unordered_set cache ------------ */
    // for (int m = 0; m < M; ++m)
    // {
    //     EffectsSet es;
    //     const EffBits &eb = cert[m];

    //     for (size_t b = 0; b < NF; ++b)
    //         if (eb.pos.test(b))
    //             es.positive.insert((int)b);
    //     for (size_t b = 0; b < NF; ++b)
    //         if (eb.neg.test(b))
    //             es.negative.insert((int)b);
    //     _certified_effects_cache[m] = std::move(es);
    // }

    _certEffBits = std::move(cert); // store for later use
}

void EffectsInference::calculateAllMethodPreconditionsBits()
{
    _preconditions_cache.clear(); // still keep the public map

    const int M = _instance.getNumMethods();
    const int NF = _instance.getNumPredicates(); // ← add accessor
    if (M == 0)
        return;

    /* ---------- 1. build method graph & ordering-info cache ------------ */
    // identical to the possible-effects pass, but we ALSO keep, for every
    // method, its list of subtasks and the “successors ∪ parallel”
    Log::i("Building precondition graph...\n");
    std::vector<MethInfo> MI(M);
    buildGraphOrderingAndLocalInfo(MI); // your own routine

    /* ---------- 2. Tarjan SCC & condensation DAG ----------------------- */
    Log::i("Collapsing SCCs...\n");
    Tarjan tarjan(MI);                             // as before
    CompGraph dag = buildCondensation(MI, tarjan); // as before
    auto order = reverseTopo(dag);                 // children first

    /* ---------- 3. keep EffBits per method, initially empty ------------ */
    std::vector<BitVec> prec(M, BitVec(NF)); // size = #methods

    /* ---------- 4. iterate bottom-up over components ------------------- */
    BitVec base(NF), tmp(NF), before(NF), inter(NF);

    Log::i("Bottom-up SCC precondition inference...\n");
    for (int C : order) // already children→parents
    {
        // Log::i("SCC %d/%d\n", C, (int)tarjan.comps.size());
        bool changed;
        do
        { // fix-point inside the SCC
            changed = false;

            for (int m : tarjan.comps[C])
            {
                // Log::i("For method %s\n", _instance.getMethodById(m).getName().c_str());
                BitVec newCert(NF); // rebuild from scratch
                const auto &info = MI[m];

                const int n = (int)info.subtasks.size();
                std::vector<BitVec> beforeEff(n, BitVec(NF)); // size = #sub-tasks

                for (int k = (int)info.topo.size() - 1; k >= 0; --k)
                {
                    int i = info.topo[k]; // real sub-task index
                    BitVec acc(NF);       // accumulator for i
                    const Sub &si = info.subtasks[i];

                    if (si.id < 0)
                        continue; // skip special actions (goal, init, blank)

                    for (int j : info.before[i]) // all tasks that *may* be before i
                    {
                        const Sub &sj = info.subtasks[j];

                        if (sj.id < 0)
                            continue; // skip special actions (goal, init, blank)

                        if (!sj.isAbs) // ---------- primitive action
                        {
                            acc.or_with(actionBits[sj.id].pos); //  its own effects
                        }
                        else // ---------- abstract task
                        {
                            const auto &decs =
                                _instance.getAbstractTaskById(sj.id).getDecompositionMethodsIdx();
                            for (int d : decs) //  union all decomposition methods
                                acc.or_with(_possibleEffBits[d].pos);
                        }
                    }
                    beforeEff[i] = std::move(acc); // store result for index i
                }

                /* ---------- analyse every sub-task -------------------- */
                for (size_t idx = 0; idx < info.subtasks.size(); ++idx)
                {
                    const Sub &s = info.subtasks[idx];

                    if (s.id < 0)
                        continue; // skip special actions (goal, init, blank)

                    // ---- 1. base preconditions of this sub-task ------------
                    base = BitVec(NF);
                    if (!s.isAbs)
                    {
                        base.or_with(actionPrecBits[s.id]);
                    }
                    else
                    {
                        // intersection of preconditions of all decomposition methods
                        bool first = true;
                        for (int d : _instance.getAbstractTaskById(s.id).getDecompositionMethodsIdx())
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
                            continue; // no decomposition → nothing certified
                    }

                    // ---- 2. effects that *might* come later ----------
                    before = beforeEff[idx];

                    // ---- 3. keep only what survives ------------------
                    tmp = base;             // copy
                    tmp.minus_with(before); // remove killed bits
                    newCert.or_with(tmp);   // union into method
                }

                if (prec[m].or_with(newCert)) // grow?
                    changed = true;
            }
        } while (changed);
    }

    Log::i("Setting preconditions in cache..\n");
    /* ---------- 5. expose in the public unordered_set cache ------------ */
    // for (int m = 0; m < M; ++m)
    // {
    //     std::unordered_set<int> pre;
    //     const BitVec &eb = prec[m];
    //     for (size_t b = 0; b < NF; ++b)
    //         if (eb.test(b))
    //             pre.insert((int)b);

    //     _preconditions_cache[m] = std::move(pre);
    // }

    _precBits = std::move(prec); // store for later use
}