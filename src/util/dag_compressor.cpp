#include "util/dag_compressor.h"

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <iostream>
#include <functional> // For std::function
#include <vector>     // Included for test output
#include <map>        // Included for test output
#include <random>     // For randomized testing
#include <chrono>     // For seeding random
#include <numeric>    // For std::iota
#include <stdexcept>  // For std::runtime_error in tests

// Comparator for UnifiedNode (using index) to use in std::set (for deterministic iteration)
struct UnifiedNodeCompare
{
    bool operator()(const UnifiedNode &a, const UnifiedNode &b) const
    {
        if (a.method_id != b.method_id)
        {
            return a.method_id < b.method_id;
        }
        return a.index < b.index; // Compare indices
    }
};

// Hash function for pairs of ints (used for compressed edge set)
struct PairHash
{
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &pair) const
    {
        auto hash1 = std::hash<T1>{}(pair.first);
        auto hash2 = std::hash<T2>{}(pair.second);
        return hash1 ^ (hash2 << 1); // Combine hashes
    }
};

// Helper function to compute reachability using BFS on the original UnifiedNode graph
void compute_original_reachability(
    const UnifiedNode &start_node,
    const std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &adj,
    std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &reachable_nodes)
{
    std::queue<UnifiedNode> q;
    std::unordered_set<UnifiedNode, UnifiedNodeHash> visited;

    q.push(start_node);
    visited.insert(start_node);
    // reachable_nodes[start_node].insert(start_node); // Include self if needed

    while (!q.empty())
    {
        UnifiedNode current = q.front();
        q.pop();

        if (adj.count(current))
        {
            for (const auto &neighbor : adj.at(current))
            {
                if (visited.find(neighbor) == visited.end())
                {
                    visited.insert(neighbor);
                    reachable_nodes[start_node].insert(neighbor); // Record reachability
                    q.push(neighbor);
                }
            }
        }
    }
}

// Helper: Compute nodes reachable FROM start_node_id in compressed graph
void compute_compressed_forward_reachability(
    int start_node_id,
    const std::unordered_map<int, std::unordered_set<int>> &adj,
    std::unordered_set<int> &reachable_nodes) // Output set
{
    std::queue<int> q;
    std::unordered_set<int> visited;

    reachable_nodes.clear();
    q.push(start_node_id);
    visited.insert(start_node_id);
    // reachable_nodes.insert(start_node_id); // Don't include self for path checks

    while (!q.empty())
    {
        int current_id = q.front();
        q.pop();

        if (adj.count(current_id))
        {
            for (const int neighbor_id : adj.at(current_id))
            {
                if (visited.find(neighbor_id) == visited.end())
                {
                    visited.insert(neighbor_id);
                    reachable_nodes.insert(neighbor_id);
                    q.push(neighbor_id);
                }
            }
        }
    }
}

// Helper: Compute nodes that can reach end_node_id in compressed graph (using reverse adj)
void compute_compressed_backward_reachability(
    int end_node_id,
    const std::unordered_map<int, std::unordered_set<int>> &rev_adj,
    std::unordered_set<int> &reaching_nodes) // Output set
{
    std::queue<int> q;
    std::unordered_set<int> visited;

    reaching_nodes.clear();
    q.push(end_node_id);
    visited.insert(end_node_id);
    // reaching_nodes.insert(end_node_id); // Don't include self for path checks

    while (!q.empty())
    {
        int current_id = q.front();
        q.pop();

        if (rev_adj.count(current_id))
        {
            for (const int neighbor_id : rev_adj.at(current_id))
            { // Iterate predecessors
                if (visited.find(neighbor_id) == visited.end())
                {
                    visited.insert(neighbor_id);
                    reaching_nodes.insert(neighbor_id);
                    q.push(neighbor_id);
                }
            }
        }
    }
}

// Helper function to check reachability in the partially built COMPRESSED graph (kept for convenience)
bool is_compressed_reachable(
    int start_node_id,
    int end_node_id,
    const std::unordered_map<int, std::unordered_set<int>> &compressed_adj)
{
    if (start_node_id == end_node_id)
        return true; // Node is reachable from itself

    std::queue<int> q;
    std::unordered_set<int> visited;

    q.push(start_node_id);
    visited.insert(start_node_id);

    while (!q.empty())
    {
        int current_id = q.front();
        q.pop();

        if (compressed_adj.count(current_id))
        {
            for (const int neighbor_id : compressed_adj.at(current_id))
            {
                if (neighbor_id == end_node_id)
                {
                    return true;
                }
                if (visited.find(neighbor_id) == visited.end())
                {
                    visited.insert(neighbor_id);
                    q.push(neighbor_id);
                }
            }
        }
    }
    return false;
}

// Helper function to find a compressed node by ID from a list
const CompressedNode *find_cnode_by_id(int id, const std::vector<CompressedNode> &cnodes_list)
{
    for (const auto &cn : cnodes_list)
    {
        if (cn.id == id)
            return &cn;
    }
    return nullptr;
}

// Helper: Query global original reachability map safely
bool query_global_original_reachability(
    const UnifiedNode &u, const UnifiedNode &v,
    const std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &global_orig_reach)
{
    if (global_orig_reach.count(u))
    {
        // Check if v exists in the set associated with u
        const auto &reachable_set = global_orig_reach.at(u);
        return reachable_set.count(v);
    }
    return false;
}

// Check if node_to_place can be added to target_c_node based on current compressed state (Version 4 - Refined Path Checks)
bool can_fit(
    const UnifiedNode &node_to_place,
    const CompressedNode &target_c_node,
    const std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &original_adj,
    const std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &original_rev_adj, // Original graph rev adj
    const std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> &original_reachability,
    const std::unordered_map<UnifiedNode, int, UnifiedNodeHash> &node_to_compressed_id, // Current mapping original_node -> compressed_id
    const std::unordered_map<int, std::unordered_set<int>> &compressed_adj,             // Current compressed graph adj
    const std::unordered_map<int, std::unordered_set<int>> &compressed_rev_adj_ref,     // Current compressed graph rev adj
    const std::vector<CompressedNode> &all_current_cnodes                               // List of all compressed nodes so far
)
{
    // 1. Check if method already exists in the target node
    if (target_c_node.original_nodes.count(node_to_place.method_id))
    {
        return false;
    }

    // 2. Check pairwise compatibility with existing nodes in the target
    for (const auto &pair : target_c_node.original_nodes)
    {
        UnifiedNode existing_node_in_target = {pair.first, pair.second};
        bool place_reaches_existing = query_global_original_reachability(node_to_place, existing_node_in_target, original_reachability);
        bool existing_reaches_place = query_global_original_reachability(existing_node_in_target, node_to_place, original_reachability);
        if (place_reaches_existing || existing_reaches_place)
        {
            return false; // Cannot group if ordered originally
        }
    }

    // 3. Path Consistency Checks (Revised v4)
    // Checks if placing node_to_place into target_c_node violates original orderings,
    // considering both existing paths and paths created by new edges this placement implies.

    // Check 3.A: Implications of existing paths ending at target_c_node
    // If PRED_CN -> ... -> target_c_node, this implies p_orig -> node_to_place AND p_orig -> t_orig for all p_orig in PRED_CN, t_orig in target.
    std::unordered_set<int> reaching_target_cn_ids;
    compute_compressed_backward_reachability(target_c_node.id, compressed_rev_adj_ref, reaching_target_cn_ids);
    for (int pred_cn_id : reaching_target_cn_ids)
    {
        const CompressedNode *pred_cn_ptr = find_cnode_by_id(pred_cn_id, all_current_cnodes);
        if (!pred_cn_ptr)
            continue;

        for (const auto &pred_orig_pair : pred_cn_ptr->original_nodes)
        {
            UnifiedNode p_orig = {pred_orig_pair.first, pred_orig_pair.second};
            // Check p_orig -> node_to_place
            if (node_to_place.method_id == p_orig.method_id)
            {
                if (!query_global_original_reachability(p_orig, node_to_place, original_reachability))
                {
                    return false;
                }
            }
            // Check p_orig -> t_orig (for t_orig already in target)
            for (const auto &t_orig_pair : target_c_node.original_nodes)
            {
                UnifiedNode t_orig = {t_orig_pair.first, t_orig_pair.second};
                if (p_orig.method_id == t_orig.method_id)
                {
                    if (!query_global_original_reachability(p_orig, t_orig, original_reachability))
                    {
                        return false;
                    }
                }
            }
        }
    }

    // Check 3.B: Implications of existing paths starting at target_c_node
    // If target_c_node -> ... -> SUCC_CN, this implies node_to_place -> s_orig AND t_orig -> s_orig for all t_orig in target, s_orig in SUCC_CN.
    std::unordered_set<int> reachable_from_target_cn_ids;
    compute_compressed_forward_reachability(target_c_node.id, compressed_adj, reachable_from_target_cn_ids);
    for (int succ_cn_id : reachable_from_target_cn_ids)
    {
        const CompressedNode *succ_cn_ptr = find_cnode_by_id(succ_cn_id, all_current_cnodes);
        if (!succ_cn_ptr)
            continue;

        for (const auto &succ_orig_pair : succ_cn_ptr->original_nodes)
        {
            UnifiedNode s_orig = {succ_orig_pair.first, succ_orig_pair.second};
            // Check node_to_place -> s_orig
            if (node_to_place.method_id == s_orig.method_id)
            {
                if (!query_global_original_reachability(node_to_place, s_orig, original_reachability))
                {
                    return false;
                }
            }
            // Check t_orig -> s_orig (for t_orig already in target)
            for (const auto &t_orig_pair : target_c_node.original_nodes)
            {
                UnifiedNode t_orig = {t_orig_pair.first, t_orig_pair.second};
                if (t_orig.method_id == s_orig.method_id)
                {
                    if (!query_global_original_reachability(t_orig, s_orig, original_reachability))
                    {
                        return false;
                    }
                }
            }
        }
    }

    // Check 3.C: Cycle Prevention (Direct Pred/Succ)
    // Cannot be in the same node as a direct original predecessor or successor.
    if (original_rev_adj.count(node_to_place))
    {
        for (const auto &pred_node : original_rev_adj.at(node_to_place))
        {
            if (node_to_compressed_id.count(pred_node))
            {
                if (node_to_compressed_id.at(pred_node) == target_c_node.id)
                    return false;
            }
        }
    }
    if (original_adj.count(node_to_place))
    {
        for (const auto &succ_node : original_adj.at(node_to_place))
        {
            if (node_to_compressed_id.count(succ_node))
            {
                if (node_to_compressed_id.at(succ_node) == target_c_node.id)
                    return false;
            }
        }
    }

    // Check 3.D: Cycle Prevention (Compressed Graph)
    // If placing node_to_place implies edge P->T or T->S, check if T->...->P or S->...->T exists.
    if (original_rev_adj.count(node_to_place))
    {
        for (const auto &pred_node : original_rev_adj.at(node_to_place))
        {
            if (node_to_compressed_id.count(pred_node))
            {
                int pred_c_id = node_to_compressed_id.at(pred_node);
                if (pred_c_id != target_c_node.id)
                { // Edge P->T implied
                    if (is_compressed_reachable(target_c_node.id, pred_c_id, compressed_adj))
                    {
                        return false; // Cycle T->...->P exists
                    }
                }
            }
        }
    }
    if (original_adj.count(node_to_place))
    {
        for (const auto &succ_node : original_adj.at(node_to_place))
        {
            if (node_to_compressed_id.count(succ_node))
            {
                int succ_c_id = node_to_compressed_id.at(succ_node);
                if (succ_c_id != target_c_node.id)
                { // Edge T->S implied
                    if (is_compressed_reachable(succ_c_id, target_c_node.id, compressed_adj))
                    {
                        return false; // Cycle S->...->T exists
                    }
                }
            }
        }
    }

    // Note: The complex transitive checks from the previous version (checking X->...->P->T and T->S->...->Y)
    // are implicitly covered by checks 3.A and 3.B, because if placing node_to_place into T is valid,
    // then all pairs (p_orig in P, node_to_place) and (node_to_place, s_orig in S) must respect original ordering.
    // If an X->...->P path exists, 3.A ensures x_orig->node_to_place is valid.
    // If a T->...->S path exists, 3.B ensures node_to_place->s_orig is valid.
    // The crucial addition is checking pairs involving nodes *already* in target_c_node (t_orig) against
    // nodes in PRED/SUCC paths (p_orig/s_orig).

    return true; // Passed all checks
}

static void build_edges(
    const std::unordered_map<int, MethodDAGInfo> &dags_info,
    const std::unordered_map<UnifiedNode, int, UnifiedNodeHash> &n2cid,
    std::set<std::pair<int, int>> &out_edges)
{
    out_edges.clear();
    for (const auto &mp : dags_info)
    {
        int mid = mp.first;
        const auto &info = mp.second;
        for (const auto &e : info.ordering_constraints)
        {
            int cid_u = n2cid.at({mid, static_cast<size_t>(e.first)});
            int cid_v = n2cid.at({mid, static_cast<size_t>(e.second)});
            if (cid_u != cid_v)
                out_edges.insert({cid_u, cid_v});
        }
    }
}

/*  reach[m][u][v] == true  ⇔  in method m, u reaches v  */
static std::vector<std::vector<std::vector<char>>> compute_all_reachability(
    const std::unordered_map<int, MethodDAGInfo> &dags_info)
{
    std::vector<std::vector<std::vector<char>>> reach;
    int max_mid = -1;
    for (auto &p : dags_info)
        max_mid = std::max(max_mid, p.first);
    reach.resize(max_mid + 1);

    for (const auto &mp : dags_info)
    {
        int mid = mp.first;
        int n = static_cast<int>(mp.second.subtask_ids.size());
        reach[mid].assign(n, std::vector<char>(n, 0));

        /* adjacency list */
        std::vector<std::vector<int>> succ(n);
        for (auto [u, v] : mp.second.ordering_constraints)
            succ[u].push_back(v);
        /* DFS or Floyd-Warshall (n ≤ 10 -> use FW) */
        for (int u = 0; u < n; ++u)
            for (int v : succ[u])
                reach[mid][u][v] = 1;
        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i)
                if (reach[mid][i][k])
                    for (int j = 0; j < n; ++j)
                        if (reach[mid][k][j])
                            reach[mid][i][j] = 1;
    }
    return reach;
}

/*  Checks rule (B) on the current compressed graph  */
static bool respects_no_new_intra_order(
    const std::vector<CompressedNode> &cns,
    const std::set<std::pair<int, int>> &edges,
    const std::vector<std::vector<std::vector<char>>> &reach)
{
    for (auto [cu, cv] : edges)
    {
        if (cu == cv)
            continue;
        const CompressedNode *A = nullptr;
        const CompressedNode *B = nullptr;
        if (cu < static_cast<int>(cns.size()) && cns[cu].alive)
            A = &cns[cu];
        if (cv < static_cast<int>(cns.size()) && cns[cv].alive)
            B = &cns[cv];
        if (!A || !B)
            continue; // merged away

        for (const auto &pu : A->original_nodes)
        {
            int m = pu.first;
            size_t idx_u = pu.second;
            auto it = B->original_nodes.find(m);
            if (it != B->original_nodes.end())
            {
                size_t idx_v = it->second;
                if (!reach[m][idx_u][idx_v])
                    return false;
            }
        }
    }
    return true;
}

CompressedDAG compressDAGs(
    const std::unordered_map<int, MethodDAGInfo> &dags_info)
{
    /* ------------ 0.  pre-compute reachability per method ---------- */
    auto reach = compute_all_reachability(dags_info);

    /* ------------ 1.  identity compression (always sound) ---------- */
    CompressedDAG R;
    int next_id = 0;
    for (const auto &mp : dags_info)
    {
        int mid = mp.first;
        const auto &info = mp.second;
        for (size_t idx = 0; idx < info.subtask_ids.size(); ++idx)
        {
            CompressedNode cn;
            cn.id = next_id;
            cn.original_nodes.emplace(mid, idx);
            R.node_to_compressed_id.emplace(UnifiedNode{mid, idx}, next_id);
            R.nodes.emplace_back(std::move(cn));
            ++next_id;
        }
    }

    std::set<std::pair<int, int>> edge_set;
    build_edges(dags_info, R.node_to_compressed_id, edge_set);

    /* ------------ 2. greedy merges while they stay sound ----------- */
    bool progress = true;
    while (progress)
    {
        progress = false;

        /* scan live-live pairs, largest first (simple heuristic) */
        std::vector<std::tuple<int, int, int>> candidates; // (-size, cidA, cidB)
        for (size_t i = 0; i < R.nodes.size(); ++i)
            if (R.nodes[i].alive)
                for (size_t j = i + 1; j < R.nodes.size(); ++j)
                    if (R.nodes[j].alive)
                    {
                        const auto &A = R.nodes[i];
                        const auto &B = R.nodes[j];
                        bool disjoint = true;
                        for (const auto &kv : A.original_nodes)
                            if (B.original_nodes.count(kv.first))
                            {
                                disjoint = false;
                                break;
                            }
                        if (!disjoint)
                            continue; // cannot merge
                        int merged_size = static_cast<int>(A.original_nodes.size() + B.original_nodes.size());
                        candidates.emplace_back(-merged_size, static_cast<int>(i),
                                                static_cast<int>(j));
                    }
        std::sort(candidates.begin(), candidates.end()); // biggest first

        for (auto [neg_sz, cidA, cidB] : candidates)
        {
            if (!R.nodes[cidA].alive || !R.nodes[cidB].alive)
                continue;

            /* --- tentatively merge B into A --- */
            // backup
            auto backup_nodesA = R.nodes[cidA].original_nodes;

            for (auto &kv : R.nodes[cidB].original_nodes)
                R.nodes[cidA].original_nodes.insert(kv);
            R.nodes[cidB].alive = false;
            for (auto &kv : R.node_to_compressed_id)
                if (kv.second == cidB)
                    kv.second = cidA;

            build_edges(dags_info, R.node_to_compressed_id, edge_set);

            if (respects_no_new_intra_order(R.nodes, edge_set, reach))
            {
                progress = true; // accept this merge
                break;           // restart outer loop
            }
            else
            {
                /* rollback */
                R.nodes[cidA].original_nodes.swap(backup_nodesA);
                R.nodes[cidB].alive = true;
                for (auto &kv : R.node_to_compressed_id)
                    if (kv.second == cidA && R.nodes[cidA].original_nodes.count(kv.first.method_id) == 0)
                        kv.second = cidB; // restore mapping
            }
        }

        build_edges(dags_info, R.node_to_compressed_id, edge_set);
    }

    build_edges(dags_info, R.node_to_compressed_id, edge_set);

    /* ------------ 3.  finalise result structure ------------------- */
    /* compact live nodes */
    std::vector<CompressedNode> final_nodes;
    for (auto &cn : R.nodes)
        if (cn.alive)
            final_nodes.push_back(cn);

    /* ---- 4. add *sound* transitive edges --------------------------- */
    /*
       Preconditions:
         – R.nodes      holds only the *live* compressed nodes
         – edge_set     already contains all direct edges that passed
                         the rule-B check
         – reach        is the vector<…> computed by compute_all_reachability
    */
    {
        /* helper: id  →  pointer to the live CompressedNode             */
        std::unordered_map<int, const CompressedNode *> id2cn;
        for (const auto &cn : R.nodes)
            id2cn[cn.id] = &cn;

        /* adjacency built from the current edge_set                      */
        std::unordered_map<int, std::vector<int>> adj;
        for (auto [u, v] : edge_set)
            adj[u].push_back(v);

        /* for every source node do a BFS over the *current* edge set     */
        for (const CompressedNode &src_cn : R.nodes)
        {
            const int src = src_cn.id;
            std::unordered_set<int> seen;
            std::queue<int> q;

            if (adj.count(src))
                for (int nxt : adj[src])
                {
                    seen.insert(nxt);
                    q.push(nxt);
                }

            while (!q.empty())
            {
                int cur = q.front();
                q.pop();
                const CompressedNode &dst_cn = *id2cn[cur];

                /* -------- rule-B check for (src → cur) ---------------- */
                bool ok = true;
                for (const auto &[mid, idx_u] : src_cn.original_nodes)
                {
                    auto it = dst_cn.original_nodes.find(mid);
                    if (it != dst_cn.original_nodes.end())
                    {
                        size_t idx_v = it->second;
                        if (!reach[mid][idx_u][idx_v])
                        {
                            ok = false;
                            break;
                        }
                    }
                }
                /* ------------------------------------------------------ */

                if (ok)
                {
                    /* insert the edge only if it is really new           */
                    if (edge_set.insert({src, cur}).second)
                        adj[src].push_back(cur); // extend adjacency
                }

                /* explore further along the existing edges */
                if (adj.count(cur))
                    for (int nxt : adj[cur])
                        if (seen.insert(nxt).second)
                            q.push(nxt);
            }
        }
    }
    /* ---------------------------------------------------------------- */

    std::vector<std::pair<int, int>> final_edges(edge_set.begin(), edge_set.end());

    /* ----  sort final_nodes topologically  --------------------------- */
    {
        /* build in-degree and adjacency from the final edge list */
        std::unordered_map<int, int> indeg;            // id -> in-degree
        std::unordered_map<int, std::vector<int>> adj; // id -> successors
        for (const auto &cn : final_nodes)
            indeg[cn.id] = 0;

        for (auto [u, v] : final_edges)
        {
            if (indeg.count(u) && indeg.count(v))
            { // both live
                adj[u].push_back(v);
                ++indeg[v];
            }
        }

        /* min-heap of “ready” nodes gives deterministic output */
        std::priority_queue<int, std::vector<int>, std::greater<int>> ready;
        for (const auto &kv : indeg)
            if (kv.second == 0)
                ready.push(kv.first);

        /* id -> pointer to the original CompressedNode (for fast lookup) */
        std::unordered_map<int, const CompressedNode *> id2ptr;
        for (const auto &cn : final_nodes)
            id2ptr[cn.id] = &cn;

        std::vector<CompressedNode> topo_nodes;
        topo_nodes.reserve(final_nodes.size());

        while (!ready.empty())
        {
            int id = ready.top();
            ready.pop();

            topo_nodes.push_back(*id2ptr[id]); // copy node

            for (int nxt : adj[id])
                if (--indeg[nxt] == 0)
                    ready.push(nxt);
        }

        /* (Graph is acyclic, but guard against programming mistakes) */
        if (topo_nodes.size() != final_nodes.size())
        {
            // fall back to the original order for any missing nodes
            for (const auto &cn : final_nodes)
                if (indeg[cn.id] >= 0)
                    topo_nodes.push_back(cn);
        }

        final_nodes.swap(topo_nodes);
    }
    /* ----------------------------------------------------------------- */

    R.nodes.swap(final_nodes);
    R.edges.swap(final_edges);
    return R;
}

std::vector<std::pair<int, int>> remove_transitive_edges(
    const std::vector<std::pair<int, int>> &edges)
{
    if (edges.empty())
    {
        return {};
    }

    std::unordered_map<int, std::unordered_set<int>> adj;
    std::unordered_set<int> nodes;

    // Build adjacency list and collect nodes
    for (const auto &edge : edges)
    {
        adj[edge.first].insert(edge.second);
        nodes.insert(edge.first);
        nodes.insert(edge.second);
    }

    // Compute all-pairs reachability (can be optimized, but BFS per node is simple)
    // We store reachability info: reachable[start_node] = {set of reachable nodes}
    std::unordered_map<int, std::unordered_set<int>> reachable;
    for (int start_node : nodes)
    {
        std::queue<int> q;
        std::unordered_set<int> visited;

        q.push(start_node);
        visited.insert(start_node);
        // reachable[start_node].insert(start_node); // Typically don't include self in this context

        while (!q.empty())
        {
            int current = q.front();
            q.pop();

            if (adj.count(current))
            {
                for (int neighbor : adj.at(current))
                {
                    if (visited.find(neighbor) == visited.end())
                    {
                        visited.insert(neighbor);
                        reachable[start_node].insert(neighbor); // Record reachability
                        q.push(neighbor);
                    }
                }
            }
        }
    }

    std::vector<std::pair<int, int>> non_transitive_edges;
    std::set<std::pair<int, int>> edge_set(edges.begin(), edges.end()); // For quick lookup

    for (const auto &edge : edges)
    {
        int u = edge.first;
        int v = edge.second;
        bool is_transitive = false;

        // Check if there exists an intermediate node 'w' such that u -> w and w -> ... -> v
        if (adj.count(u))
        {
            for (int w : adj.at(u))
            {
                if (w == v)
                    continue; // Don't consider the direct edge path u -> v

                // Check if w can reach v (using the precomputed reachability)
                if (reachable.count(w) && reachable.at(w).count(v))
                {
                    is_transitive = true;
                    break; // Found an intermediate path, so (u, v) is transitive
                }
            }
        }

        if (!is_transitive)
        {
            non_transitive_edges.push_back(edge);
        }
    }

    return non_transitive_edges;
}

// Helper function to print the input DAGs for debugging
void print_input_dags(const std::unordered_map<int, MethodDAGInfo> &dags_info)
{
    std::cerr << "--- Failing Input DAGs ---" << std::endl;
    std::vector<int> method_ids;
    for (const auto &pair : dags_info)
    {
        method_ids.push_back(pair.first);
    }
    std::sort(method_ids.begin(), method_ids.end());

    for (int method_id : method_ids)
    {
        const auto &info = dags_info.at(method_id);
        std::cerr << "Method " << method_id << " (Subtasks: " << info.subtask_ids.size() << "):" << std::endl;
        std::cerr << "  Constraints: ";
        // Sort constraints for consistent output
        // Corrected type: info.ordering_constraints is vector<pair<size_t, size_t>>
        // Assuming MethodDAGInfo::ordering_constraints is std::vector<std::pair<size_t, size_t>>
        std::vector<std::pair<int, int>> sorted_constraints = info.ordering_constraints;
        std::sort(sorted_constraints.begin(), sorted_constraints.end());
        for (const auto &oc : sorted_constraints)
        {
            std::cerr << "{" << oc.first << "," << oc.second << "} ";
        }
        std::cerr << std::endl;
    }
    std::cerr << "--------------------------" << std::endl;
}

// Helper function to print the compressed result for debugging
void print_compressed_dag_result(const CompressedDAG &dag)
{
    std::cerr << "--- Compressed Result ---" << std::endl;
    std::cerr << "Nodes (" << dag.nodes.size() << "):" << std::endl;
    std::vector<CompressedNode> sorted_nodes = dag.nodes;
    std::sort(sorted_nodes.begin(), sorted_nodes.end(),
              [](const CompressedNode &a, const CompressedNode &b)
              { return a.id < b.id; });
    for (const auto &node : sorted_nodes)
    {
        std::cerr << "  Node ID: " << node.id << " contains { ";
        std::map<int, size_t> sorted_originals = node.original_nodes;
        for (const auto &pair : sorted_originals)
        {
            std::cerr << "m" << pair.first << ":" << pair.second << " ";
        }
        std::cerr << "}" << std::endl;
    }

    std::cerr << "Edges (" << dag.edges.size() << "):" << std::endl;
    // Corrected type: dag.edges is vector<pair<int, int>>
    std::vector<std::pair<int, int>> sorted_edges = dag.edges;
    std::sort(sorted_edges.begin(), sorted_edges.end());
    for (const auto &edge : sorted_edges)
    {
        std::cerr << "  " << edge.first << " -> " << edge.second << std::endl;
    }
    std::cerr << "-------------------------" << std::endl;
}

void run_randomized_soundness_check(int num_iterations = 100, int max_methods = 5, int max_subtasks_per_method = 10, double edge_probability = 0.3)
{
    std::mt19937 rng(std::chrono::steady_clock::now().time_since_epoch().count());
    std::cout << "\n--- Running Randomized Soundness Check ---" << std::endl;

    for (int iter = 0; iter < num_iterations; ++iter)
    {
        if (iter > 0 && iter % 10 == 0)
        {
            std::cout << "Iteration " << iter << "..." << std::endl;
        }
        std::unordered_map<int, MethodDAGInfo> dags_info_per_method_test;
        // Stores reachability for each original method's individual DAG
        std::unordered_map<int, std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash>>
            original_method_reachabilities;

        int num_methods_current = std::uniform_int_distribution<>(1, max_methods)(rng);

        for (int method_id = 0; method_id < num_methods_current; ++method_id)
        {
            MethodDAGInfo current_method_info;
            int num_subtasks = std::uniform_int_distribution<>(2, max_subtasks_per_method)(rng);
            current_method_info.subtask_ids.resize(num_subtasks);
            std::iota(current_method_info.subtask_ids.begin(), current_method_info.subtask_ids.end(), 0);

            std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> current_method_adj;
            for (int u = 0; u < num_subtasks; ++u)
            {
                for (int v = u + 1; v < num_subtasks; ++v)
                { // Ensures u < v for simple acyclicity
                    if (std::uniform_real_distribution<>(0.0, 1.0)(rng) < edge_probability)
                    {
                        // Check if adding u->v would create a cycle with existing edges for this method
                        // (More robust would be full cycle check, but u < v helps)
                        // For simplicity here, we rely on u < v. A full check would involve BFS/DFS on current_method_adj.
                        current_method_info.ordering_constraints.push_back({(size_t)u, (size_t)v});
                        current_method_adj[{method_id, (size_t)u}].insert({method_id, (size_t)v});
                    }
                }
            }
            dags_info_per_method_test[method_id] = current_method_info;

            std::unordered_map<UnifiedNode, std::unordered_set<UnifiedNode, UnifiedNodeHash>, UnifiedNodeHash> reach_for_this_method;
            for (int i = 0; i < num_subtasks; ++i)
            {
                compute_original_reachability({method_id, (size_t)i}, current_method_adj, reach_for_this_method);
            }
            original_method_reachabilities[method_id] = reach_for_this_method;
        }

        if (dags_info_per_method_test.empty())
            continue; // Should not happen with num_methods_current >=1

        CompressedDAG compressed_result = compressDAGs(dags_info_per_method_test);
        std::set<std::pair<int, int>> compressed_edges_set(compressed_result.edges.begin(), compressed_result.edges.end());

        // Verification A: Original orderings preserved
        for (const auto &method_pair : dags_info_per_method_test)
        {
            int method_id = method_pair.first;
            const MethodDAGInfo &method_info = method_pair.second;
            for (const auto &constraint : method_info.ordering_constraints)
            {
                UnifiedNode orig_u = {method_id, constraint.first};
                UnifiedNode orig_v = {method_id, constraint.second};

                if (!compressed_result.node_to_compressed_id.count(orig_u) || !compressed_result.node_to_compressed_id.count(orig_v))
                {
                    std::cerr << "Test Error (Iter " << iter << "): Original node not found in compressed map. Method " << method_id << ", u:" << constraint.first << ", v:" << constraint.second << std::endl;
                    print_input_dags(dags_info_per_method_test);    // Print failing input
                    print_compressed_dag_result(compressed_result); // Print result
                    throw std::runtime_error("Test failed: Original node missing.");
                }
                int cn_u_id = compressed_result.node_to_compressed_id.at(orig_u);
                int cn_v_id = compressed_result.node_to_compressed_id.at(orig_v);

                if (cn_u_id == cn_v_id)
                {
                    std::cerr << "Test Error (Iter " << iter << "): Originally ordered nodes " << method_id << ":" << constraint.first
                              << " and " << method_id << ":" << constraint.second << " ended up in the same compressed node " << cn_u_id << std::endl;
                    print_input_dags(dags_info_per_method_test);    // Print failing input
                    print_compressed_dag_result(compressed_result); // Print result
                    throw std::runtime_error("Test failed: Ordered nodes merged.");
                }
                if (!compressed_edges_set.count({cn_u_id, cn_v_id}))
                {
                    std::cerr << "Test Error (Iter " << iter << "): Original order " << method_id << ":" << constraint.first << " -> " << method_id << ":" << constraint.second
                              << " (CNs " << cn_u_id << "->" << cn_v_id << ") not found in compressed edges." << std::endl;
                    print_input_dags(dags_info_per_method_test);    // Print failing input
                    print_compressed_dag_result(compressed_result); // Print result
                    throw std::runtime_error("Test failed: Original order not preserved.");
                }
            }
        }

        // Verification B: No new intra-method orderings
        for (const auto &edge : compressed_edges_set)
        {
            int cn_a_id = edge.first;
            int cn_b_id = edge.second;
            const CompressedNode *cn_a = find_cnode_by_id(cn_a_id, compressed_result.nodes);
            const CompressedNode *cn_b = find_cnode_by_id(cn_b_id, compressed_result.nodes);

            if (!cn_a || !cn_b)
            {
                std::cerr << "Test Error (Iter " << iter << "): Compressed edge " << cn_a_id << "->" << cn_b_id << " involves non-existent cnode." << std::endl;
                print_input_dags(dags_info_per_method_test);    // Print failing input
                print_compressed_dag_result(compressed_result); // Print result
                throw std::runtime_error("Test failed: Edge with invalid cnode.");
            }

            for (const auto &pair_x : cn_a->original_nodes)
            {
                UnifiedNode node_x = {pair_x.first, pair_x.second};
                for (const auto &pair_y : cn_b->original_nodes)
                {
                    UnifiedNode node_y = {pair_y.first, pair_y.second};

                    if (node_x.method_id == node_y.method_id)
                    { // Same original method
                        int current_method_id = node_x.method_id;
                        if (!original_method_reachabilities.count(current_method_id))
                        {
                            std::cerr << "Test Error (Iter " << iter << "): Missing original reachability for method " << current_method_id << std::endl;
                            print_input_dags(dags_info_per_method_test);    // Print failing input
                            print_compressed_dag_result(compressed_result); // Print result
                            throw std::runtime_error("Test failed: Missing original reachability.");
                        }
                        const auto &specific_method_reach = original_method_reachabilities.at(current_method_id);

                        bool x_reaches_y_orig = specific_method_reach.count(node_x) && specific_method_reach.at(node_x).count(node_y);

                        if (!x_reaches_y_orig)
                        {
                            std::cerr << "Test Error (Iter " << iter << "): Compressed edge " << cn_a_id << "->" << cn_b_id
                                      << " implies new intra-method order for m" << current_method_id << ": "
                                      << node_x.index << " -> " << node_y.index
                                      << ", which was not original." << std::endl;

                            // Print failing input and result
                            print_input_dags(dags_info_per_method_test);
                            print_compressed_dag_result(compressed_result);

                            // Print specific context from previous debug message
                            const auto &m_info = dags_info_per_method_test.at(current_method_id);
                            std::cerr << "  Context: Original constraints for m" << current_method_id << " (subtasks: " << m_info.subtask_ids.size() << "): ";
                            // Sort constraints for consistent output
                            // Corrected type: info.ordering_constraints is vector<pair<size_t, size_t>>
                            std::vector<std::pair<int, int>> sorted_constraints = m_info.ordering_constraints;
                            std::sort(sorted_constraints.begin(), sorted_constraints.end());
                            for (const auto &oc : sorted_constraints)
                            {
                                std::cerr << "{" << oc.first << "," << oc.second << "} ";
                            }
                            std::cerr << std::endl;
                            std::cerr << "    Context: CN_A (ID " << cn_a_id << ") contains: ";
                            for (const auto &item : cn_a->original_nodes)
                                std::cerr << "m" << item.first << ":" << item.second << " ";
                            std::cerr << std::endl;
                            std::cerr << "    Context: CN_B (ID " << cn_b_id << ") contains: ";
                            for (const auto &item : cn_b->original_nodes)
                                std::cerr << "m" << item.first << ":" << item.second << " ";
                            std::cerr << std::endl;

                            throw std::runtime_error("Test failed: New intra-method order created.");
                        }
                    }
                }
            }
        }
    }
    std::cout << "Randomized soundness check passed for " << num_iterations << " iterations." << std::endl;
    std::cout << "--- Randomized Soundness Check Finished ---" << std::endl;
}

void compressed_dag_test()
{
    // Test with two methods with some ordering constraints
    MethodDAGInfo method0_orig;
    method0_orig.subtask_ids = {0, 1, 2, 3};
    method0_orig.ordering_constraints = {{0, 1}, {0, 2}, {1, 3}, {2, 3}};

    MethodDAGInfo method1_orig;
    method1_orig.subtask_ids = {0, 1, 2, 3};
    method1_orig.ordering_constraints = {{0, 1}, {1, 2}, {1, 3}};

    std::unordered_map<int, MethodDAGInfo> dags_info_per_method;
    dags_info_per_method[0] = method0_orig;
    dags_info_per_method[1] = method1_orig;

    // Compress the DAGs
    std::cout << "--- Running compressed_dag_test ---" << std::endl;
    CompressedDAG compressed_dag = compressDAGs(dags_info_per_method);

    // Print the resulting compressed DAG
    std::cout << "Compressed Nodes (" << compressed_dag.nodes.size() << "):" << std::endl;
    // Sort nodes by ID for consistent output
    std::sort(compressed_dag.nodes.begin(), compressed_dag.nodes.end(),
              [](const CompressedNode &a, const CompressedNode &b)
              { return a.id < b.id; });
    for (const auto &node : compressed_dag.nodes)
    {
        std::cout << "  Node ID: " << node.id << " contains { ";
        // Sort original nodes within compressed node for consistent output
        std::map<int, size_t> sorted_originals = node.original_nodes;
        for (const auto &pair : sorted_originals)
        {
            std::cout << "m" << pair.first << ":" << pair.second << " ";
        }
        std::cout << "}" << std::endl;
    }

    std::cout << "Compressed Edges (" << compressed_dag.edges.size() << "):" << std::endl;
    // Sort edges for consistent output
    std::sort(compressed_dag.edges.begin(), compressed_dag.edges.end());
    for (const auto &edge : compressed_dag.edges)
    {
        std::cout << "  " << edge.first << " -> " << edge.second << std::endl;
    }

    std::cout << "--- compressed_dag_test finished ---" << std::endl;

    // Call the randomized test suite
    // You might want to adjust parameters for quicker/more thorough testing.
    // e.g., run_randomized_soundness_check(100, 5, 10, 0.3); for a decent check
    // For a very quick check: run_randomized_soundness_check(10, 3, 5, 0.4);
    try
    {
        // run_randomized_soundness_check(100, 30, 6, 0.3); // Use simplified parameters from user feedback
    }
    catch (const std::exception &e)
    {
        std::cerr << "ERROR during randomized check: " << e.what() << std::endl;
        // Decide if you want to exit with error from here or let main handle it
        // exit(1); // Or rethrow, or just let it fall through if main has other things
    }

    int a = 0; // Keep breakpoint possibility
    // exit(0);   // Comment out if main should continue or if run_randomized_soundness_check handles exit on error
}

// If this file is compiled into a library and compressed_dag_test is not main:
// You might need a separate main.cpp that calls compressed_dag_test().
// For example:
// int main() {
//     compressed_dag_test();
//     return 0;
// }
