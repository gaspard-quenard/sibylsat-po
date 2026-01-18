#ifndef PDT_NODE_H
#define PDT_NODE_H

#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <utility>    // For std::pair
#include <functional> // For std::hash

#include "data/htn_instance.h"

enum class OpType
{
    METHOD,
    ACTION
};

enum class OrderingConstrains
{
    SIBLING_NO_ORDERING,
    SIBLING_ORDERING,
    NO_SIBLING_NO_ORDERING,
    NO_SIBLING_ORDERING,
};

// Define a custom hash function for std::pair<int, OpType>
// Needed because std::unordered_set requires a hash function for its elements.
struct PairHash
{
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2> &p) const
    {
        // Hash the first element (int)
        auto hash1 = std::hash<T1>{}(p.first);
        // Hash the second element (OpType enum). Need to cast enum to underlying type (int)
        auto hash2 = std::hash<std::underlying_type_t<T2>>{}(static_cast<std::underlying_type_t<T2>>(p.second));

        // Combine the two hash values. Using boost::hash_combine logic:
        return hash1 ^ (hash2 + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2));
    }
};

class PdtNode
{
private:
    int _layer;
    int _pos;
    int _offset;

    std::unordered_set<int> _methods_idx;
    std::unordered_set<int> _actions_idx;
    std::unordered_set<int> _actions_repetition_idx;

    std::unordered_map<int, std::unordered_set<int>> _parents_of_method;                              // Key: method_idx, Value: set of parent_method_idx
    std::unordered_map<int, std::unordered_set<std::pair<int, OpType>, PairHash>> _parents_of_action; // Key: action_idx, Value: set of {parent_idx, parent_type}

    std::unordered_map<int, int> _method_variables;
    std::unordered_map<int, int> _action_variables;
    std::vector<int> _fact_variables; // Indexed by predicate ID
    int _prim_var;
    int _leaf_overleaf_var = -1; // Variable that indicates if the node is a leaf overleaf (used for PO)

    // For the solution, get the type and id of the op solution
    std::pair<int, OpType> _op_solution; // {id, type}
    int _ts_solution = -1;

    // Only used for PO (TODO could be optimized for all group of methods which share the same ordering and same number of subtasks)
    std::unordered_map<int, int> _parent_method_idx_to_subtask_idx; // Key: method_idx, Value: subtask_idx of this method in this position

    std::unordered_set<PdtNode *> _node_that_must_be_executed_before;
    std::unordered_set<PdtNode *> _node_that_must_be_executed_after;
    int node_executed_var;

    std::unordered_map<const PdtNode *, int> _before_vars; // Var that inidicate that the current node is executed before the other node

    // Try with before and after
    bool _can_be_first_child = true;
    bool _can_be_last_child = true;
    // test
    bool _must_be_first_child = false;
    std::unordered_map<PdtNode *, OrderingConstrains> _possible_next_nodes;
    std::unordered_map<PdtNode *, OrderingConstrains> _possible_previous_nodes;
    std::unordered_map<PdtNode *, int> _possible_next_node_variable;

    std::string _name;

    const PdtNode *_parent;
    std::vector<PdtNode *> _children;

public:
    PdtNode(const PdtNode *parent)
        : _parent(parent)
    {
        if (parent != nullptr)
        {
            _layer = parent->_layer + 1;
            _offset = parent->_children.size();
            _pos = parent->_pos + _offset;
            _name = parent->_name + "->" + std::to_string(_offset);
        }
        else
        {
            _layer = 0;
            _pos = 0;
            _offset = 0;
            _name = "root";
        }
    }
    ~PdtNode()
    {
        for (PdtNode *child : _children)
            delete child;
    }

    void addMethodIdx(int method_idx);
    void addActionIdx(int action_idx);
    void addActionRepetitionIdx(int action_idx);
    void addParentOfMethod(int method_idx, int parent_method_idx);
    void addParentOfAction(int action_idx, int parent_idx, OpType parent_type);
    const std::unordered_set<int> &getMethodsIdx() const;
    const std::unordered_set<int> &getActionsIdx() const;
    const std::unordered_set<int> &getActionsRepetitionIdx() const;
    std::vector<PdtNode *> &getChildren();
    const PdtNode *getParent() const;
    const std::unordered_map<int, std::unordered_set<int>> &getParentsOfMethod() const;
    const std::unordered_map<int, std::unordered_set<std::pair<int, OpType>, PairHash>> &getParentsOfAction() const;

    const std::vector<int> &getFactVariables() const;
    const std::unordered_map<int, int> &getMethodAndVariables() const;
    const std::unordered_map<int, int> &getActionAndVariables() const;
    const int getPrimVariable() const;
    const int getLeafOverleafVariable() const;
    const std::string getPositionString() const;

    // Getters and Setters for the solution
    void setOpSolution(int id, OpType type);
    void setTsSolution(int ts) { _ts_solution = ts; }
    const int getTsSolution() const { return _ts_solution; }
    const std::pair<int, OpType> &getOpSolution() const;

    void assignSatVariables(const HtnInstance &htn, const bool print_var_names, const bool is_po);

    size_t computeNumberOfChildren(HtnInstance &htn);
    void expand(HtnInstance &htn);
    void expandPOWithBefore(HtnInstance &htn);
    void createChildren(HtnInstance &htn);
    void addNodeThatMustBeExecutedBefore(PdtNode *node);
    void addNodeThatMustBeExecutedAfter(PdtNode *node);
    const std::unordered_set<PdtNode *> &getNodeThatMustBeExecutedBefore() const
    {
        return _node_that_must_be_executed_before;
    }
    const std::unordered_set<PdtNode *> &getNodeThatMustBeExecutedAfter() const
    {
        return _node_that_must_be_executed_after;
    }

    void setPos(int pos)
    {
        _pos = pos;
    }

    const int &getPos() const
    {
        return _pos;
    }

    const int &getLayerIdx() const
    {
        return _layer;
    }

    const std::unordered_set<PdtNode *> collectLeafChildren();
    const std::string &getName() const
    {
        return _name;
    }
    int getBaseTimeStep() const
    {
        return _node_that_must_be_executed_before.size();
    }
    int getEndTimeStep(int numTs) const
    {
        return numTs - _node_that_must_be_executed_after.size();
    }
    bool canBeExecutedAtTimeStep(int t, int numTs) const
    {
        return t >= getBaseTimeStep() && t < getEndTimeStep(numTs);
    }

    const std::unordered_map<PdtNode *, OrderingConstrains> &getPossibleNextNodes() const
    {
        return _possible_next_nodes;
    }

    const std::unordered_map<PdtNode *, OrderingConstrains> &getPossiblePreviousNodes() const
    {
        return _possible_previous_nodes;
    }

    const std::unordered_map<PdtNode *, int> &getPossibleNextNodeVariable() const
    {
        return _possible_next_node_variable;
    }

    void addPossibleNextNode(PdtNode *next_node, OrderingConstrains ordering)
    {
        _possible_next_nodes[next_node] = ordering;
        next_node->_possible_previous_nodes[this] = ordering;
    }

    const int getParentMethodIdxToSubtaskIdx(int parent_method_idx) const
    {
        if (_parent_method_idx_to_subtask_idx.find(parent_method_idx) == _parent_method_idx_to_subtask_idx.end())
        {
            return -1;
        }
        return _parent_method_idx_to_subtask_idx.at(parent_method_idx);
    }

    void addBeforeNextNodeVar(PdtNode *next_node, int var)
    {
        _before_vars[next_node] = var;
    }

    bool canBeFirstChild() const
    {
        return _can_be_first_child;
    }

    bool canBeLastChild() const
    {
        return _can_be_last_child;
    }

    int getBeforeNextNodeVar(const PdtNode *next_node) const
    {
        if (_before_vars.find(next_node) == _before_vars.end())
        {
            return -1;
        }
        return _before_vars.at(next_node);
    }

    void makeOrderingNoSibling();
};

#endif // PDT_NODE_H