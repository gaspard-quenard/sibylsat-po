#ifndef DAG_COMPRESSOR_H
#define DAG_COMPRESSOR_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <utility> // For std::pair

// Represents a unique step (index) within a method's decomposition
struct UnifiedNode {
    int method_id;
    size_t index; // Index within the method's subtask_ids vector

    bool operator==(const UnifiedNode& other) const {
        return method_id == other.method_id && index == other.index;
    }
};

// Hash function for UnifiedNode to use it in unordered_map/set
struct UnifiedNodeHash {
    std::size_t operator()(const UnifiedNode& node) const {
        // Simple hash combination
        std::size_t h1 = std::hash<int>{}(node.method_id);
        std::size_t h2 = std::hash<size_t>{}(node.index); // Use index for hashing
        return h1 ^ (h2 << 1); // Combine hashes
    }
};

// Represents a node in the final compressed DAG
struct CompressedNode {
    int id; // Unique ID for this compressed node
    // Maps method_id to the original index from that method contained in this compressed node
    std::map<int, size_t> original_nodes; 
    bool alive = true; 
};

// Represents the final compressed DAG structure
struct CompressedDAG {
    std::vector<CompressedNode> nodes;
    // Edges represented by pairs of CompressedNode IDs (from_id, to_id)
    std::vector<std::pair<int, int>> edges;
    // Optional: Map to quickly find the compressed node ID for an original node (method_id, index)
    std::unordered_map<UnifiedNode, int, UnifiedNodeHash> node_to_compressed_id;
};

// Structure to hold all nodes and constraints for a single method's DAG
struct MethodDAGInfo {
    std::vector<int> subtask_ids; // All subtask IDs (values) in this method's DAG
    std::vector<std::pair<int, int>> ordering_constraints; // Edges (u_idx, v_idx) means index u_idx < index v_idx
};


/**
 * @brief Compresses multiple DAGs into a single minimal DAG.
 * 
 * Takes multiple DAGs, each defined by its full set of nodes and ordering constraints,
 * and merges them into a single DAG where nodes group compatible original nodes.
 * 
 * @param dags_info_per_method A map where the key is the method ID and the value 
 *                             is a MethodDAGInfo struct containing all node IDs 
 *                             and ordering constraints for that method's DAG.
 * @return CompressedDAG The resulting compressed DAG structure.
 */
CompressedDAG compressDAGs(
    const std::unordered_map<int, MethodDAGInfo>& dags_info_per_method
);

/**
 * @brief Removes transitive edges from a given set of DAG edges.
 * 
 * Given a vector of edges representing a Directed Acyclic Graph (DAG), 
 * this function computes and returns a new vector containing only the 
 * essential (non-transitive) edges. An edge (u, v) is considered transitive 
 * if there exists a path from u to v that does not involve the direct edge (u, v).
 * 
 * @param edges A vector of pairs representing the edges (from_id, to_id) of the DAG.
 * @return std::vector<std::pair<int, int>> A new vector containing only the non-transitive edges.
 */
std::vector<std::pair<int, int>> remove_transitive_edges(
    const std::vector<std::pair<int, int>>& edges
);
void compressed_dag_test();

#endif // DAG_COMPRESSOR_H
