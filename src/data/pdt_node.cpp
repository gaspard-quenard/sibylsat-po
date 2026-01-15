#include "data/pdt_node.h"
#include "util/names.h"
#include "sat/variable_provider.h"
#include "util/log.h"
#include "util/dag_compressor.h"

void PdtNode::addMethodIdx(int method_idx)
{
    _methods_idx.insert(method_idx);
}

void PdtNode::addParentOfMethod(int method_idx, int parent_method_idx)
{
    _parents_of_method[method_idx].insert(parent_method_idx);
}

void PdtNode::addParentOfAction(int action_idx, int parent_idx, OpType parent_type)
{
    _parents_of_action[action_idx].insert({parent_idx, parent_type});
}

void PdtNode::addActionIdx(int action_idx)
{
    _actions_idx.insert(action_idx);
}

void PdtNode::addActionRepetitionIdx(int action_idx)
{
    _actions_repetition_idx.insert(action_idx);
}

const std::unordered_map<int, std::unordered_set<int>> &PdtNode::getParentsOfMethod() const
{
    return _parents_of_method;
}

const std::unordered_map<int, std::unordered_set<std::pair<int, OpType>, PairHash>> &PdtNode::getParentsOfAction() const
{
    return _parents_of_action;
}

const std::unordered_set<int> &PdtNode::getMethodsIdx() const
{
    return _methods_idx;
}

const std::unordered_set<int> &PdtNode::getActionsIdx() const
{
    return _actions_idx;
}

const std::unordered_set<int> &PdtNode::getActionsRepetitionIdx() const
{
    return _actions_repetition_idx;
}

std::vector<PdtNode *> &PdtNode::getChildren()
{
    return _children;
}

const PdtNode *PdtNode::getParent() const
{
    return _parent;
}

const std::vector<int> &PdtNode::getFactVariables() const
{
    return _fact_variables;
}

const std::unordered_map<int, int> &PdtNode::getMethodAndVariables() const
{
    return _method_variables;
}

const std::unordered_map<int, int> &PdtNode::getActionAndVariables() const
{
    return _action_variables;
}

const int PdtNode::getPrimVariable() const
{
    return _prim_var;
}

const int PdtNode::getLeafOverleafVariable() const
{
    return _leaf_overleaf_var;
}

const std::pair<int, OpType> &PdtNode::getOpSolution() const
{
    return _op_solution;
}

void PdtNode::setOpSolution(int id, OpType type)
{
    _op_solution = {id, type};
}

const std::string PdtNode::getPositionString() const
{
    return std::to_string(_layer) + "_" + std::to_string(_pos);
}

void PdtNode::assignSatVariables(const HtnInstance &htn, const bool print_var_names, const bool is_po)
{
    const int num_predicates = htn.getNumPredicates();

    // Assign a variable to each method
    for (int method_idx : _methods_idx)
    {
        _method_variables[method_idx] = VariableProvider::nextVar();
        if (print_var_names)
        {
            // std::string method_name = htn.getMethodById(method_idx).getName() + "__" + getPositionString();
            std::string method_name = htn.getMethodById(method_idx).getName() + "__" + getName();
            Log::i("PVN: %d %s\n", _method_variables[method_idx], method_name.c_str());
        }
    }

    // Assign a variable to each action
    for (int action_idx : _actions_idx)
    {
        // If only one parent and it is an action, we can use the same variable
        if (_offset == 0 && _parents_of_action[action_idx].size() == 1 && _parents_of_action[action_idx].begin()->second == OpType::ACTION)
        {
            Log::d("Reusing action variable %d for action %s\n", _parent->getActionAndVariables().at(action_idx), TOSTR(htn.getActionById(action_idx)));
            _action_variables[action_idx] = _parent->getActionAndVariables().at(action_idx);
        }
        else
        {
            _action_variables[action_idx] = VariableProvider::nextVar();
            if (print_var_names)
            {
                // std::string action_name = htn.getActionById(action_idx).getName() + "__" + getPositionString();
                std::string action_name = htn.getActionById(action_idx).getName() + "__" + getName();
                Log::i("PVN: %d %s\n", _action_variables[action_idx], action_name.c_str());
            }
        }
    }

    // For each action repetition, get the same variable as the parent action
    // for (int action_idx : _actions_repetition_idx)
    // {
    //     _action_variables[action_idx] = _parent->_action_variables[action_idx];
    // }

    // Assign a variable to each predicate
    // If we are the first child of the parent, we have the same fact variables as the parent
    // bool first_child = _parent != nullptr && _parent->_children[0] == this;
    bool first_child = _parent != nullptr && _offset == 0;
    if (first_child && !is_po)
    // if ((first_child && !is_po) || (is_po && _must_be_first_child))
    {
        Log::i("ICI\n");
        _fact_variables = _parent->_fact_variables;
    }
    else
    {
        for (int pred_id = 0; pred_id < num_predicates; ++pred_id)
        {
            _fact_variables.push_back(VariableProvider::nextVar());
            if (print_var_names)
            {
                std::string pred_name = htn.getPredicateById(pred_id).getName() + "__" + getPositionString();
                Log::i("PVN: %d %s\n", _fact_variables[pred_id], pred_name.c_str());
            }
        }
    }

    // Assign a variable to the prim
    _prim_var = VariableProvider::nextVar();
    if (print_var_names)
    {
        // std::string prim_name = "prim__" + getPositionString();
        std::string prim_name = "prim__" + getName();
        Log::i("PVN: %d %s\n", _prim_var, prim_name.c_str());
    }

    // Assign a variable for all the possible next nodes
    for (const auto &[next_node, ordering] : _possible_next_nodes)
    // for (PdtNode *next_node : _node_that_must_be_executed_after)
    {
        _possible_next_node_variable[next_node] = VariableProvider::nextVar();
        if (print_var_names)
        {
            // std::string node_name = "node_" + node->getPositionString() + "__" + getPositionString();
            std::string node_name = getName() + "--->" + next_node->getName();
            Log::i("PVN: %d %s\n", _possible_next_node_variable[next_node], node_name.c_str());
        }
    }

    if (is_po) {
        // Assign a variable for the leaf overleaf
        _leaf_overleaf_var = VariableProvider::nextVar();
        if (print_var_names)
        {
            // std::string node_name = "node_" + node->getPositionString() + "__" + getPositionString();
            std::string node_name = "leaf_overleaf__" + getName();
            Log::i("PVN: %d %s\n", _leaf_overleaf_var, node_name.c_str());
        }
    }
}

size_t PdtNode::computeNumberOfChildren(HtnInstance &htn)
{
    size_t num_children = 1;
    for (int method_idx : _methods_idx)
    {
        num_children = std::max(num_children, htn.getMethodById(method_idx).getSubtasksIdx().size());
    }
    return num_children;
}

void PdtNode::expand(HtnInstance &htn)
{

    // Get the number of children for this node
    size_t num_children = computeNumberOfChildren(htn);

    // Create the children
    for (size_t i = 0; i < num_children; ++i)
    {
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
    }

    int id_blank_action = htn.getBlankAction().getId();

    // For each action, repeat it for the first children
    for (int action_idx : _actions_idx)
    {
        PdtNode *child = _children[0];
        // Log::i("Repeating action %s\n", TOSTR(htn.getActionById(action_idx)));
        // child->addActionRepetitionIdx(action_idx);
        child->addActionIdx(action_idx);
        child->addParentOfAction(action_idx, action_idx, OpType::ACTION);

        // The action add some blank actions in the remaining children
        for (size_t i = 1; i < num_children; ++i)
        {
            PdtNode *child = _children[i];
            child->addActionIdx(id_blank_action);
            child->addParentOfAction(id_blank_action, action_idx, OpType::ACTION);
        }
    }

    // For each method, if the j-th subtask is an action, add it to the j-th children
    // if the j-th subtask is an abstract task, add all the methods of the abstract task to the j-th children
    for (int method_idx : _methods_idx)
    {
        const Method &method = htn.getMethodById(method_idx);
        Log::d("Children of method %s\n", TOSTR(method));
        for (size_t j = 0; j < method.getSubtasksIdx().size(); ++j)
        {
            PdtNode *jth_child = _children[j];
            int subtask_idx = method.getSubtasksIdx()[j];
            if (htn.isAbstractTask(subtask_idx))
            {
                Log::d("Subtask is the abstract task %s\n", TOSTR(htn.getAbstractTaskById(subtask_idx)));
                const AbstractTask &task = htn.getAbstractTaskById(subtask_idx);
                // Add all the methods of the abstract task to the j-th children
                for (int sub_method_idx : task.getDecompositionMethodsIdx())
                {
                    jth_child->addMethodIdx(sub_method_idx);
                    jth_child->addParentOfMethod(sub_method_idx, method_idx);
                    Log::d("At subtask %d, adding method %s\n", j, TOSTR(htn.getMethodById(sub_method_idx)));
                }
            }
            else
            {
                // Add the action to the j-th child
                jth_child->addActionIdx(subtask_idx);
                jth_child->addParentOfAction(subtask_idx, method_idx, OpType::METHOD);
                Log::d("At subtask %d, adding action %s\n", j, TOSTR(htn.getActionById(subtask_idx)));
            }
        }
        // The method add blank actions in the remaining children
        for (size_t j = method.getSubtasksIdx().size(); j < num_children; ++j)
        {
            PdtNode *child = _children[j];
            child->addActionIdx(id_blank_action);
            child->addParentOfAction(id_blank_action, method_idx, OpType::METHOD);
        }
    }
}

void PdtNode::addNodeThatMustBeExecutedBefore(PdtNode *node)
{
    _node_that_must_be_executed_before.insert(node);
}

void PdtNode::addNodeThatMustBeExecutedAfter(PdtNode *node)
{
    _node_that_must_be_executed_after.insert(node);
}

const std::unordered_set<PdtNode *> PdtNode::collectLeafChildren()
{
    std::unordered_set<PdtNode *> leaf_children;

    if (_children.empty())
    {
        leaf_children.insert(this);
        return leaf_children;
    }

    for (PdtNode *child : _children)
    {
        std::unordered_set<PdtNode *> child_leaf_children = child->collectLeafChildren();
        leaf_children.insert(child_leaf_children.begin(), child_leaf_children.end());
    }
    return leaf_children;
}

void PdtNode::expandPO(HtnInstance &htn, bool order_between_child)
{
    // Collect all the nodes that must be executed before the children nodes
    std::unordered_set<PdtNode *> nodes_that_must_be_executed_before;
    for (PdtNode *node : _node_that_must_be_executed_before)
    {
        // Collect all the leaf children of the node
        std::unordered_set<PdtNode *> leaf_children = node->collectLeafChildren();
        nodes_that_must_be_executed_before.insert(leaf_children.begin(), leaf_children.end());
    }

    // Time to crack the number of children and ordering that must be found
    std::unordered_map<int, MethodDAGInfo> dags_info_per_method;

    // TODO instead of doing it for all methods, only instancie one method with identical name for the ordering
    for (int method_idx : _methods_idx)
    {
        MethodDAGInfo dag_info;
        dag_info.subtask_ids = htn.getMethodById(method_idx).getSubtasksIdx();
        dag_info.ordering_constraints = htn.getMethodById(method_idx).getOrderingConstraints();
        dags_info_per_method[method_idx] = dag_info;
    }
    CompressedDAG compressedDAG = compressDAGs(dags_info_per_method);

    size_t num_children = compressedDAG.nodes.size();

    // Create all the children based on the compressed DAG
    for (int idx_child = 0; idx_child < compressedDAG.nodes.size(); ++idx_child)
    {
        const CompressedNode &compressed_node = compressedDAG.nodes[idx_child];

        // Create the child node
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
        // Indicate all the nodes that must be executed before this child
        for (PdtNode *node : nodes_that_must_be_executed_before)
        {
            child->addNodeThatMustBeExecutedBefore(node);
            node->addNodeThatMustBeExecutedAfter(child);
        }
        // Is there other children of this position that must be executed before this child?
        for (const std::pair<int, int> &ordering : compressedDAG.edges)
        {
            if (ordering.second == idx_child)
            {
                Log::d("Child %d must be executed after child %d\n", ordering.second, ordering.first);
                PdtNode *prev_child = _children[ordering.first];
                child->addNodeThatMustBeExecutedBefore(prev_child);
                prev_child->addNodeThatMustBeExecutedAfter(child);
            }
        }
    }

    if (num_children == 0)
    {
        // Only actions in this position, create a single child for them.
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
        child->_can_be_first_child = true; // This child is the first and only.
        child->_can_be_last_child = true;  // This child is the last and only.

        for (PdtNode *node : nodes_that_must_be_executed_before)
        {
            child->addNodeThatMustBeExecutedBefore(node);
            node->addNodeThatMustBeExecutedAfter(child);
        }
        for (int action_idx : _actions_idx)
        {
            child->addActionIdx(action_idx);
            child->addParentOfAction(action_idx, action_idx, OpType::ACTION);
        }
    }

    Log::d("Number of children: %zu\n", num_children);

    int id_blank_action = htn.getBlankAction().getId();

    // Now fill the children with the ops
    for (int idx_child = 0; idx_child < num_children; ++idx_child)
    {
        const CompressedNode &compressed_node = compressedDAG.nodes[idx_child];
        PdtNode *child = _children[idx_child];
        bool is_first_child = idx_child == 0;

        for (int method_idx : _methods_idx)
        {
            // Is this method_idx in the compressed node ? If so, which subtask?
            auto it = compressed_node.original_nodes.find(method_idx);
            if (it != compressed_node.original_nodes.end())
            {
                const Method &method = htn.getMethodById(method_idx);
                Log::d("For parent method %s (%d)...\n", TOSTR(method), method_idx);
                int method_subtask_idx = it->second;
                child->_parent_method_idx_to_subtask_idx[method_idx] = method_subtask_idx;
                int op_idx = method.getSubtasksIdx()[method_subtask_idx];
                if (htn.isAbstractTask(op_idx))
                {
                    Log::d("  Subtask is the abstract task %s\n", TOSTR(htn.getAbstractTaskById(op_idx)));
                    const AbstractTask &task = htn.getAbstractTaskById(op_idx);
                    // Add all the methods of the abstract task to the j-th children
                    for (int sub_method_idx : task.getDecompositionMethodsIdx())
                    {
                        child->addMethodIdx(sub_method_idx);
                        child->addParentOfMethod(sub_method_idx, method_idx);
                        Log::d("    At subtask %d, adding method %s (%d)\n", idx_child, TOSTR(htn.getMethodById(sub_method_idx)), sub_method_idx);
                    }
                }
                else
                {
                    // Add the action to the j-th child
                    child->addActionIdx(op_idx);
                    child->addParentOfAction(op_idx, method_idx, OpType::METHOD);
                    Log::d("    At subtask %d, adding action %s\n", idx_child, TOSTR(htn.getActionById(op_idx)));
                }
            }
            else
            {
                // Add the blank action to the child
                child->addActionIdx(id_blank_action);
                child->addParentOfAction(id_blank_action, method_idx, OpType::METHOD);
                Log::d("Adding blank action %s to child %d\n", TOSTR(htn.getActionById(id_blank_action)), idx_child);
            }
        }

        // Add action repetition to the first child or blank action to the rest
        if (is_first_child)
        {
            for (int action_idx : _actions_idx)
            {
                // child->addActionRepetitionIdx(action_idx);
                child->addActionIdx(action_idx);
                child->addParentOfAction(action_idx, action_idx, OpType::ACTION);
                Log::d("Adding action repetition %s to child %d\n", TOSTR(htn.getActionById(action_idx)), idx_child);
            }
        }
        else
        {
            for (int action_idx : _actions_idx)
            {
                child->addActionIdx(id_blank_action);
                child->addParentOfAction(id_blank_action, action_idx, OpType::ACTION);
                Log::d("Adding blank action %s to child %d\n", TOSTR(htn.getActionById(id_blank_action)), idx_child);
            }
        }
    }
}

void PdtNode::expandPOWithBefore(HtnInstance &htn)
{
    // Collect all the nodes that must be executed before the children nodes
    std::unordered_set<PdtNode *> nodes_that_must_be_executed_before;
    for (PdtNode *node : _node_that_must_be_executed_before)
    {
        // Collect all the leaf children of the node
        std::unordered_set<PdtNode *> leaf_children = node->collectLeafChildren();
        nodes_that_must_be_executed_before.insert(leaf_children.begin(), leaf_children.end());
    }

    // Time to crack the number of children and ordering that must be found
    // Optimized to use method structures
    std::unordered_map<int, MethodDAGInfo> dags_info_per_structure;
    std::unordered_map<int, std::vector<int>> structure_to_method_ids; // Maps structure_id to list of actual method_ids

    for (int method_idx : _methods_idx)
    {
        // Log::i("Method in pos: %s\n", TOSTR(htn.getMethodById(method_idx)));
        int structure_id = htn.getMethodStructureId(method_idx);
        if (structure_id == -1) {
            Log::w("Warning: Could not find structure ID for method %d in PdtNode::expandPOWithBefore. Skipping.\n", method_idx);
            continue; 
        }

        structure_to_method_ids[structure_id].push_back(method_idx);

        if (dags_info_per_structure.find(structure_id) == dags_info_per_structure.end())
        {
            MethodDAGInfo dag_info;
            dag_info.ordering_constraints = htn.getCanonicalOrderingConstraintsForStructure(structure_id);
            int num_subtasks = htn.getNumSubtasksForStructure(structure_id);
            
            if (num_subtasks == -1) { // Error case from getter
                 Log::w("Warning: Could not get num_subtasks for structure_id %d in PdtNode::expandPOWithBefore. Skipping structure.\n", structure_id);
                continue;
            }
            if (htn.getCanonicalOrderingConstraintsForStructure(structure_id).empty() && num_subtasks > 0 && structure_id != -1) {
                 // This check is for the case where getCanonicalOrderingConstraintsForStructure returns the static empty vector due to an error.
                 // However, getNumSubtasksForStructure might have succeeded.
                 // If num_subtasks > 0 but constraints are empty (and it wasn't a valid empty constraints case), it's an issue.
                 // The getter for constraints already logs an error.
                 // We might rely on num_subtasks being -1 if constraints also failed.
                 // For now, let's assume if num_subtasks is valid, we proceed.
            }


            // compressDAGs uses subtask_ids.size(). The actual IDs are not used by compressDAGs itself.
            // It iterates `i` from `0` to `num_subtasks-1`.
            // The `original_nodes[structure_id_key] = i;` line means `i` (the 0-based index) is stored.
            dag_info.subtask_ids.resize(num_subtasks); 
            // Example of filling if needed: for(int i=0; i<num_subtasks; ++i) dag_info.subtask_ids[i] = i;
            
            dags_info_per_structure[structure_id] = dag_info;
        }
    }
    CompressedDAG compressedDAG = compressDAGs(dags_info_per_structure);

    const std::vector<std::pair<int, int>> &non_transitive_edges = remove_transitive_edges(compressedDAG.edges);

    size_t num_children = compressedDAG.nodes.size();

    std::unordered_map<int, int> dag_node_id_to_child_id;

    // First, create the children
    for (size_t i = 0; i < num_children; ++i)
    {
        dag_node_id_to_child_id[compressedDAG.nodes[i].id] = i;
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
        Log::d("Creating child %s\n", TOSTR(*child));
    }

    // Change the ordering to point to the children idx instead of the compressed node idx
    std::vector<std::pair<int, int>> ordering_children;
    std::vector<std::pair<int, int>> non_transitive_ordering_children;
    for (const std::pair<int, int> &ordering : compressedDAG.edges)
    {
        ordering_children.push_back({dag_node_id_to_child_id[ordering.first], dag_node_id_to_child_id[ordering.second]});
    }
    for (const std::pair<int, int> &ordering : non_transitive_edges)
    {
        non_transitive_ordering_children.push_back({dag_node_id_to_child_id[ordering.first], dag_node_id_to_child_id[ordering.second]});
    }

    // Create all the children based on the compressed DAG
    for (int idx_child = 0; idx_child < compressedDAG.nodes.size(); ++idx_child)
    {
        const CompressedNode &compressed_node = compressedDAG.nodes[idx_child];
        std::unordered_set<int> idx_children_not_seen;
        for (int i = 0; i < num_children; ++i)
        {
            if (i != idx_child)
                idx_children_not_seen.insert(i);
        }
        PdtNode *child = _children[idx_child];
        child->_can_be_first_child = true;
        child->_can_be_last_child = true;
        bool must_be_first_child = true;

        // Indicate all the nodes that must be executed before this child
        for (PdtNode *node : nodes_that_must_be_executed_before)
        {
            child->addNodeThatMustBeExecutedBefore(node);
            node->addNodeThatMustBeExecutedAfter(child);
        }
        // Is there other children of this position that must be executed before this child?
        for (const std::pair<int, int> &ordering : ordering_children)
        {
            if (ordering.second == idx_child)
            {
                Log::d("Child %d must be executed after child %d\n", ordering.second, ordering.first);
                PdtNode *prev_child = _children[ordering.first];
                child->addNodeThatMustBeExecutedBefore(prev_child);
                prev_child->addNodeThatMustBeExecutedAfter(child);
                idx_children_not_seen.erase(ordering.first);
                child->_can_be_first_child = false;
                must_be_first_child = false;
            }
            if (ordering.first == idx_child)
            {
                idx_children_not_seen.erase(ordering.second);
                child->_can_be_last_child = false;
            }
        }

        // Make the ordering between the children:

        // If some child must be executed directly after this child:
        for (const std::pair<int, int> &ordering : non_transitive_ordering_children)
        {
            // if (ordering.second == idx_child)
            // {
            //     child->_can_be_first_child = false;
            // }
            if (ordering.first == idx_child)
            {
                PdtNode *next_child = _children[ordering.second];
                Log::d("Possible next child of child %d is child %d\n", ordering.first, ordering.second);
                // child->_possible_next_nodes[next_child] = OrderingConstrains::SIBLING_ORDERING;
                child->addPossibleNextNode(next_child, OrderingConstrains::SIBLING_ORDERING);
                // next_child->_possible_previous_nodes.insert(child);
                // child->_can_be_last_child = false;
            }
        }

        // Children not seen can also be executed directly after this child
        for (int idx_child_not_seen : idx_children_not_seen)
        {
            PdtNode *next_child = _children[idx_child_not_seen];
            Log::d("__ Possible next child of child %d is child %d\n", idx_child, idx_child_not_seen);
            // child->_possible_next_nodes[next_child] = OrderingConstrains::SIBLING_NO_ORDERING;
            child->addPossibleNextNode(next_child, OrderingConstrains::SIBLING_NO_ORDERING);
            must_be_first_child = false;
        }
        child->_must_be_first_child = must_be_first_child;
    }

    if (num_children == 0)
    {
        // Only actions in this position, repeat the action in the first child
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
        for (PdtNode *node : nodes_that_must_be_executed_before)
        {
            child->addNodeThatMustBeExecutedBefore(node);
            node->addNodeThatMustBeExecutedAfter(child);
        }
        for (int action_idx : _actions_idx)
        {
            child->addActionIdx(action_idx);
            child->addParentOfAction(action_idx, action_idx, OpType::ACTION);
        }
    }

    Log::d("Number of children: %zu\n", num_children);

    int id_blank_action = htn.getBlankAction().getId();

    // Now fill the children with the ops
    for (int idx_child = 0; idx_child < num_children; ++idx_child)
    {
        const CompressedNode &compressed_node = compressedDAG.nodes[idx_child];
        PdtNode *child = _children[idx_child];
        bool is_first_child = idx_child == 0;

        // Iterate over all methods relevant to this PdtNode
        for (int actual_method_idx : _methods_idx)
        {
            int structure_id_of_method = htn.getMethodStructureId(actual_method_idx);
            if (structure_id_of_method == -1) {
                 Log::w("Warning: PdtNode::expandPOWithBefore - Method %d has no structure_id. Skipping method for child ops.\n", actual_method_idx);
                continue;
            }

            // Check if this method's structure is represented in the current compressed_node
            // compressed_node.original_nodes maps structure_id to subtask_index_within_that_structure
            auto it_original_node = compressed_node.original_nodes.find(structure_id_of_method);

            if (it_original_node != compressed_node.original_nodes.end())
            {
                // This method's structure contributes to this child node
                int subtask_idx_in_structure = it_original_node->second; // This is the 0-based index from the canonical structure

                const Method &method = htn.getMethodById(actual_method_idx);
                // Ensure subtask_idx_in_structure is valid for this specific method's subtasks
                if (subtask_idx_in_structure < method.getSubtasksIdx().size())
                {
                    Log::d("For parent method %s (%d), using structure %d, subtask_index %d for child %d...\n", 
                           TOSTR(method), actual_method_idx, structure_id_of_method, subtask_idx_in_structure, idx_child);
                    
                    child->_parent_method_idx_to_subtask_idx[actual_method_idx] = subtask_idx_in_structure;
                    int op_idx = method.getSubtasksIdx()[subtask_idx_in_structure];

                    if (htn.isAbstractTask(op_idx))
                    {
                        Log::d("  Child %s: Subtask is the abstract task %s\n", child->getName().c_str(), TOSTR(htn.getAbstractTaskById(op_idx)));
                        const AbstractTask &task = htn.getAbstractTaskById(op_idx);
                        for (int sub_method_idx : task.getDecompositionMethodsIdx())
                        {
                            child->addMethodIdx(sub_method_idx);
                            child->addParentOfMethod(sub_method_idx, actual_method_idx);
                            Log::d("    Child %s: adding method %s (%d) from parent %s\n", 
                                   child->getName().c_str(), TOSTR(htn.getMethodById(sub_method_idx)), sub_method_idx, TOSTR(method));
                        }
                    }
                    else
                    {
                        child->addActionIdx(op_idx);
                        child->addParentOfAction(op_idx, actual_method_idx, OpType::METHOD);
                        Log::d("    Child %s: adding action %s from parent %s\n", 
                               child->getName().c_str(), TOSTR(htn.getActionById(op_idx)), TOSTR(method));
                    }
                }
                else
                {
                     Log::e("Error: PdtNode::expandPOWithBefore - subtask_idx_in_structure %d out of bounds for method %s with %zu subtasks.\n",
                           subtask_idx_in_structure, TOSTR(method), method.getSubtasksIdx().size());
                    // Add blank action as a fallback
                    child->addActionIdx(id_blank_action);
                    child->addParentOfAction(id_blank_action, actual_method_idx, OpType::METHOD);
                }
            }
            else
            {
                // This method's structure does not contribute to this child node (e.g., method has fewer subtasks than this child's position in the compressed DAG,
                // or its structure is simply different and doesn't map to this compressed_node).
                // So, this method places a blank action here.
                child->addActionIdx(id_blank_action);
                child->addParentOfAction(id_blank_action, actual_method_idx, OpType::METHOD);
                Log::d("Child %s: Adding blank action for method %s (%d) as its structure %d is not in compressed_node's original_nodes map.\n",
                       child->getName().c_str(), TOSTR(htn.getMethodById(actual_method_idx)), actual_method_idx, structure_id_of_method);
            }
        }

        // Add action repetitions from the parent PdtNode's _actions_idx.
        // These actions are considered to run "in parallel" or at the "beginning" of the decomposition.
        // If the current child can be a "first child" in the sequence of siblings, it processes these actions.
        // Otherwise, it gets blank actions for them.
        if (is_first_child) 
        {
            for (int action_idx : _actions_idx)
            {
                child->addActionIdx(action_idx);
                child->addParentOfAction(action_idx, action_idx, OpType::ACTION); // Parent is the action itself
                Log::d("Child %s: Adding action repetition %s because it can be a first child\n", 
                       child->getName().c_str(), TOSTR(htn.getActionById(action_idx)));
            }
        }
        else 
        {
            for (int action_idx : _actions_idx)
            {
                child->addActionIdx(id_blank_action);
                child->addParentOfAction(id_blank_action, action_idx, OpType::ACTION);
                Log::d("Child %s: Adding blank action for action_idx %s as it cannot be a first child\n",
                       child->getName().c_str(), TOSTR(htn.getActionById(action_idx)));
            }
        }
    }
}

void PdtNode::makeOrderingNoSibling()
{
    if (_parent == nullptr)
    {
        return;
    }
    // Get all the possible next nodes of the parent
    for (const auto &[next_node, ordering] : _parent->_possible_next_nodes)
    {
        // Multiple case here:
        // If the parent has an ordering with this next node, then all the children of the parent
        // must be executed before this next node. As such, only the last child of the parent can
        // have an ordering with the first child of the next node.
        if (ordering == OrderingConstrains::SIBLING_ORDERING)
        {
            // If we are not one of the last children, continue
            if (!_can_be_last_child)
            {
                continue;
            }

            // For all the first children of the next node...
            for (PdtNode *next_node_child : next_node->_children)
            {
                // If this is the first child of the next node, then we can have an ordering with this child
                if (next_node_child->_can_be_first_child)
                {
                    // _possible_next_nodes[next_node_child] = OrderingConstrains::SIBLING_ORDERING;
                    addPossibleNextNode(next_node_child, OrderingConstrains::SIBLING_ORDERING);
                }
            }
        }
        else
        {
            // For all the children of the next node...
            for (PdtNode *next_node_child : next_node->_children)
            {
                // _possible_next_nodes[next_node_child] = OrderingConstrains::NO_SIBLING_NO_ORDERING;
                addPossibleNextNode(next_node_child, OrderingConstrains::NO_SIBLING_NO_ORDERING);
            }
        }
    }
}

void PdtNode::createChildren(HtnInstance &htn)
{

    // Get the number of children for this node
    size_t num_children = computeNumberOfChildren(htn);

    // Create the children
    for (size_t i = 0; i < num_children; ++i)
    {
        PdtNode *child = new PdtNode(this);
        _children.push_back(child);
    }
}
