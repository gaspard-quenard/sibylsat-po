#include "sat/encoding.h"
#include "util/names.h"
#include "sat/bimander_amo.h"

#include <cmath>

void Encoding::encode(std::vector<PdtNode *> &leaf_nodes)
{
    for (int i = 0; i < leaf_nodes.size(); ++i)
    {
        // Log::i("Encode node %d/%d: %s\n", i, leaf_nodes.size(), TOSTR(*leaf_nodes[i]));
        PdtNode *node = leaf_nodes[i];
        const PdtNode *parent_node = node->getParent();

        const std::vector<int> &current_fact_vars = node->getFactVariables();
        const std::vector<int> &next_fact_vars = i + 1 < leaf_nodes.size() ? leaf_nodes[i + 1]->getFactVariables() : _htn.getFactVarsGoal();

        std::unordered_map<int, std::unordered_set<int>> positive_effs_can_be_implied_by;
        std::unordered_map<int, std::unordered_set<int>> negative_effs_can_be_implied_by;

        // Action implies precondition and effects, get as well all the predicates that can be changed and the actions that can change them
        _stats.begin(STAGE_ACTIONCONSTRAINTS);
        encodeActions(node->getActionAndVariables(), current_fact_vars, next_fact_vars, positive_effs_can_be_implied_by, negative_effs_can_be_implied_by);
        _stats.begin(STAGE_ACTIONCONSTRAINTS);

        // Method implies precondition (Here method precondition is compiled into an action which is the first subtask of the method)
        // encodeMethods(node->getMethodAndVariables(), current_fact_vars, next_fact_vars);

        // action implies prim, method implies not prim
        _stats.begin(STAGE_PRIMITIVENESS);
        encodePrimitivenessOps(node->getActionAndVariables(), node->getMethodAndVariables(), node->getPrimVariable());
        _stats.end(STAGE_PRIMITIVENESS);

        // Encode frame axioms
        _stats.begin(STAGE_FRAMEAXIOMS);
        encodeFrameAxioms(current_fact_vars, next_fact_vars, node->getPrimVariable(), positive_effs_can_be_implied_by, negative_effs_can_be_implied_by);
        _stats.end(STAGE_FRAMEAXIOMS);

        // At most one action or method is selected
        // encodeAtMostOneOp(node->getActionAndVariables());
        // encodeAtMostOneOp(node->getMethodAndVariables());

        // Encode the hierarchy of each ops
        _stats.begin(STAGE_EXPANSIONS);
        encodeHierarchy(node, parent_node);
        _stats.end(STAGE_EXPANSIONS);
    }
}

void Encoding::encodePOWithBefore(std::vector<PdtNode *> &leaf_nodes)
{

    // Number of time steps is set to the number of leaf nodes.
    size_t num_nodes = leaf_nodes.size();
    _num_ts = num_nodes;

    Log::i("Encoding Partial Order with Before Variables for %zu nodes...\n", num_nodes);

    // --- Initialize _node_before_node Variables ---
    // _node_before_node.clear();
    // _node_before_node.resize(num_nodes); // Resize outer vector
    // for (size_t i = 0; i < num_nodes; ++i)
    // {
    //     _node_before_node[i].resize(num_nodes, 0); // Resize inner vector, initialize with 0
    //     for (size_t j = i + 1; j < num_nodes; ++j)
    //     {
    //         int var = VariableProvider::nextVar();
    //         _node_before_node[i][j] = var;
    //         if (_print_var_names)
    //         {
    //             // Use node indices (0 to num_nodes-1) directly in names
    //             std::string var_name = "layer_" + std::to_string(_layer_idx) + "__node_" + leaf_nodes[i]->getName() + "__before__node_" + leaf_nodes[j]->getName();
    //             Log::i("PVN: %d %s\n", var, var_name.c_str());
    //         }
    //     }
    // }

    _stats.beginTiming(TimingStage::ENCODING_BEFORE);
    _stats.begin(STAGE_BEFORE_CLAUSES);

    // --- Encode Ordering Constraints (Next AMO, Next=>Before, Transitivity, Hard Precedence) ---
    for (int i = 0; i < num_nodes; ++i)
    {
        PdtNode *node_i = leaf_nodes[i];
        int pos_i = i; // Use loop index which matches the vector indices

        // --- Encode 'Next' AMO ---
        // At most one predecessor chosen
        std::vector<int> prev_node_to_current_node_vars;
        for (const auto &[prev_node, ordering] : node_i->getPossiblePreviousNodes())
        {
            prev_node_to_current_node_vars.push_back(prev_node->getPossibleNextNodeVariable().at(node_i));
        }
        _stats.begin(STAGE_BEFORE_PREDECESSORS);
        if (prev_node_to_current_node_vars.size() > 0)
        {
            _sat.addClause(prev_node_to_current_node_vars);
        }
        if (prev_node_to_current_node_vars.size() > 1)
        {
            encodeAtMostOne(prev_node_to_current_node_vars);
        }
        _stats.end(STAGE_BEFORE_PREDECESSORS);

        // At most one successor chosen
        std::vector<int> current_node_to_next_node_vars;
        const auto &possible_next_nodes_map = node_i->getPossibleNextNodes();
        const auto &next_node_var_map = node_i->getPossibleNextNodeVariable(); // Map for node_i

        for (const auto &[next_node, ordering] : possible_next_nodes_map)
        {
            int next_node_i_node_k_var = next_node_var_map.at(next_node);
            current_node_to_next_node_vars.push_back(next_node_i_node_k_var);
            int pos_k = next_node->getPos(); // Get the actual position index of the successor

            // --- Encode next => before ---
            // int node_i_before_node_k_var = getBeforeLiteral(pos_i, pos_k);
            int node_i_before_node_k_var = node_i->getBeforeNextNodeVar(next_node);
            _sat.addClause(-next_node_i_node_k_var, node_i_before_node_k_var);

            PdtNode *node_k = leaf_nodes[pos_k];
            if (node_k->getPossibleNextNodes().find(node_i) != node_k->getPossibleNextNodes().end())
            {
                int next_node_k__node_i_var = node_k->getPossibleNextNodeVariable().at(node_i);
                // If i is before k, then k cannot be a next node of i
                _sat.addClause(-node_i_before_node_k_var, -next_node_k__node_i_var);
            }

            // --- Encode Transitivity ---
            _stats.begin(STAGE_BEFORE_TRANSITIVITY);
            // for (const auto &[node_a, ordering] : next_node->getPossibleNextNodes()) {
            // For all other nodes 'a' (pos_a != pos_i)
            
            for (int a = 0; a < num_nodes; ++a)
            {
                if (a == i)
                    continue;  // Skip self
                int pos_a = a; // Use loop index
                if (pos_a == pos_k)
                    continue; // Skip k -> i -> k
            

                PdtNode *node_a = leaf_nodes[pos_a];

                // If i cannot be executed after a or k cannot be executed before a, skip
                if (node_a->getNodeThatMustBeExecutedBefore().find(node_i) != node_a->getNodeThatMustBeExecutedBefore().end() ||
                    node_a->getNodeThatMustBeExecutedBefore().find(next_node) != node_a->getNodeThatMustBeExecutedBefore().end())
                {
                    continue;
                }

                // int node_a_before_node_i_var = getBeforeLiteral(pos_a, pos_i);
                // int node_a_before_node_k_var = getBeforeLiteral(pos_a, pos_k);
                int node_a_before_node_i_var = node_a->getBeforeNextNodeVar(node_i);
                int node_a_before_node_k_var = node_a->getBeforeNextNodeVar(next_node);

                if (node_a_before_node_i_var == -1 || node_a_before_node_k_var == -1)
                {
                    Log::i("Skip...\n");
                    continue; // Skip if the before variable is not defined
                }

                // (a before i) AND next(i, k) => (a before k)
                // _sat.addClause(-node_a_before_node_i_var, -next_node_i_node_k_var, node_a_before_node_k_var);
                // (not a before i) AND next(i, k) => (not a before k)
                _sat.addClause(node_a_before_node_i_var, -next_node_i_node_k_var, -node_a_before_node_k_var);

                // Not sure if it is useful
                // int node_i_before_node_k_var = getBeforeLiteral(pos_i, pos_k);
                // (a before i) AND (i before k) => (a before k)
                _sat.addClause(-node_a_before_node_i_var, -node_i_before_node_k_var, node_a_before_node_k_var);
                // (not a before i) AND (i before k) => (not a before k)
                // _sat.addClause(node_a_before_node_i_var, node_i_before_node_k_var, -node_a_before_node_k_var);
            }
            _stats.end(STAGE_BEFORE_TRANSITIVITY);
        }
        _stats.begin(STAGE_BEFORE_SUCCESSORS);
        if (current_node_to_next_node_vars.size() > 0)
        {
            _sat.addClause(current_node_to_next_node_vars);
        }
        if (current_node_to_next_node_vars.size() > 1)
        {
            encodeAtMostOne(current_node_to_next_node_vars);
        }
        _stats.end(STAGE_BEFORE_SUCCESSORS);

        // --- Encode Hard Precedence ---
        for (PdtNode *prev_node : node_i->getNodeThatMustBeExecutedBefore())
        {
            int pos_prev = prev_node->getPos();
            // int before_lit = getBeforeLiteral(pos_prev, pos_i);
            int before_lit = prev_node->getBeforeNextNodeVar(node_i);
            _sat.addClause(before_lit); // Must be true
        }
        for (PdtNode *next_node : node_i->getNodeThatMustBeExecutedAfter())
        {
            int pos_next = next_node->getPos();
            // int before_lit = getBeforeLiteral(pos_i, pos_next);
            int before_lit = node_i->getBeforeNextNodeVar(next_node);
            _sat.addClause(before_lit); // Must be true
        }
    }

    Log::i("Finished encoding 'before' constraints.\n");

    // Finally, create a new var for leaf overleafs, that will be used for the FA
    // int leaf_overleaf_var = VariableProvider::nextVar();
    // if (_print_var_names)
    // {
    //     // Variable name: "layer_<layer_idx>__leaf_overleaf"
    //     std::string var_name = "layer_" + std::to_string(_layer_idx) + "__leaf_overleaf";
    //     Log::i("PVN: %d %s\n", leaf_overleaf_var, var_name.c_str());
    // }
    // _leaf_overleaf_vars.push_back(leaf_overleaf_var);

    _stats.begin(STAGE_BEFORE_HIERARCHY);
    // Encode special before variables
    Log::i("Encoding special before variables...\n");
    std::unordered_map<const PdtNode *, std::vector<const PdtNode *>> first_children_map;
    std::unordered_map<const PdtNode *, std::vector<const PdtNode *>> children_map;
    // Special hierarchy can be encoded. If next_pos_1__pos_2 is true, then the first child of pos_1 must be before the first child of pos_2
    for (int i = 0; i < leaf_nodes.size(); ++i)
    {
        const PdtNode *node = leaf_nodes[i];
        const PdtNode *parent = node->getParent();
        children_map[parent].push_back(node);
        if (node->canBeFirstChild())
        {
            first_children_map[parent].push_back(node);
        }
    }

    for (const auto &[parent, first_children] : first_children_map)
    {
        for (const auto &[next_node, next_node_var] : parent->getPossibleNextNodeVariable())
        {
            const std::vector<const PdtNode *> &next_node_first_children = first_children_map[next_node];

            // Get all the possible before variables
            std::vector<int> before_vars;
            for (const PdtNode *first_child : first_children)
            {
                for (const PdtNode *next_node_first_child : next_node_first_children)
                {
                    // Get the before variable for the first child of parent and the first child of next_node
                    int before_var = first_child->getBeforeNextNodeVar(next_node_first_child);
                    if (before_var != -1)
                    {
                        before_vars.push_back(before_var);
                    }
                }
            }

            int next_node_vars_2 = parent->getBeforeNextNodeVar(next_node);

            // If the next node is true, then one of the before variables must be true
            if (before_vars.size() > 0)
            {
                _sat.appendClause(-next_node_vars_2);
                for (int before_var : before_vars)
                {
                    _sat.appendClause(before_var);
                }
                _sat.endClause();
            }
        }
    }
    Log::i("Finished encoding special before variables.\n");

    int parent_overleaf_var = _leaf_overleaf_vars.size() > 0 ? _leaf_overleaf_vars.back() : -1;

    // Encode special before variables no task overleaf
    Log::i("Encoding special before variables no task overleaf...\n");
    for (const auto &[parent, children] : children_map)
    {
        for (const auto &[next_node, next_node_var] : parent->getPossibleNextNodeVariable())
        {
            std::vector<const PdtNode *> &next_node_children = children_map[next_node];

            int next_node_vars_2 = parent->getBeforeNextNodeVar(next_node);

            for (const PdtNode *child : children)
            {
                for (const PdtNode *next_node_child : next_node_children)
                {
                    // Get the before variable for the first child of parent and the first child of next_node
                    int before_var = child->getBeforeNextNodeVar(next_node_child);
                    if (before_var != -1)
                    {
                        // If the next node is true, then one of the before variables must be true
                        // Encode the clause: next_node_var AND no_leaf_overleaf => before_vars
                        _sat.addClause(-next_node_vars_2, parent_overleaf_var, before_var);
                    }
                }
            }
        }
    }
    Log::i("Finished encoding special before variables no task overleaf.\n");

    _stats.endTiming(TimingStage::ENCODING_BEFORE);
    _stats.end(STAGE_BEFORE_HIERARCHY);
    _stats.end(STAGE_BEFORE_CLAUSES);


    _stats.beginTiming(TimingStage::ENCODING_MUTEXES);
    _stats.begin(STAGE_MUTEX);
    Log::i("Encoding mutexes...\n");
    if (_encode_mutexes)
    {
        // Encode mutexes for all time steps
        for (int t = 0; t < num_nodes; ++t)
        {
            const std::vector<int> &current_fact_vars = leaf_nodes[t]->getFactVariables();
            // ENcode all groups
            for (const std::vector<int> &group : _htn.getMutex().getMutexGroups())
            {

                std::vector<int> group_vars;
                for (int pred_idx : group)
                {
                    // Get the variable for this predicate at this time step
                    int var = current_fact_vars[pred_idx];
                    group_vars.push_back(var);
                }
                // Encode the at-most-one constraint for this group
                encodeAtMostOne(group_vars);
            }
        }
    }
    Log::i("Finished encoding mutexes.\n");
    _stats.endTiming(TimingStage::ENCODING_MUTEXES);
    _stats.end(STAGE_MUTEX);
    


    int leaf_overleaf_var = VariableProvider::nextVar();
    if (_print_var_names)
    {
        // Variable name: "layer_<layer_idx>__leaf_overleaf"
        std::string var_name = "layer_" + std::to_string(_layer_idx) + "__leaf_overleaf";
        Log::i("PVN: %d %s\n", leaf_overleaf_var, var_name.c_str());
    }
    _leaf_overleaf_vars.push_back(leaf_overleaf_var);


    for (int i = 0; i < leaf_nodes.size(); ++i)
    {
        // Log::i("Encode node %d/%d: %s\n", i, leaf_nodes.size(), TOSTR(*leaf_nodes[i]));
        PdtNode *node = leaf_nodes[i];
        const PdtNode *parent_node = node->getParent();
        // int leaf_overleaf_var = node->getLeafOverleafVariable();

        // action implies prim, method implies not prim
        _stats.begin(STAGE_PRIMITIVENESS);
        encodePrimitivenessOps(node->getActionAndVariables(), node->getMethodAndVariables(), node->getPrimVariable());
        _stats.end(STAGE_PRIMITIVENESS);

        // Encode the hierarchy of each ops
        _stats.beginTiming(TimingStage::ENCODING_HIERARCHY);
        _stats.begin(STAGE_EXPANSIONS);
        encodeHierarchy(node, parent_node);
        _stats.endTiming(TimingStage::ENCODING_HIERARCHY);
        _stats.end(STAGE_EXPANSIONS);

        std::unordered_map<int, std::unordered_set<int>> positive_effs_can_be_implied_by;
        std::unordered_map<int, std::unordered_set<int>> negative_effs_can_be_implied_by;

        const std::vector<int> &current_fact_vars = node->getFactVariables();

        // Encode actions preconditions
        _stats.beginTiming(TimingStage::ENCODING_PREC);
        _stats.begin(STAGE_PREC);
        for (const auto &[action_idx, action_var] : node->getActionAndVariables())
        {
            const Action &action = _htn.getActionById(action_idx);
            for (int precondition_idx : action.getPreconditionsIdx())
            {
                // Action && _ts_to_leaf_node[t][j] => precondition
                // in CNF:
                // (not action_var or not _ts_to_leaf_node[t][j] or current_fact_vars[precondition_idx])
                _sat.addClause(-action_var, current_fact_vars[precondition_idx]);
                // _sat.addClause(-action_var, leaf_overleaf_var, current_fact_vars[precondition_idx]);
            }
        }

        if (_encode_prec_and_effs_methods)
        {
            // Encode methods preconditions
            for (const auto &[method_idx, method_var] : node->getMethodAndVariables())
            {
                const Method &method = _htn.getMethodById(method_idx);
                for (int precondition_idx : method.getPreconditionsIdx())
                {
                    // Method && _ts_to_leaf_node[t][j] => precondition
                    // in CNF:
                    // (not method_var or not _ts_to_leaf_node[t][j] or current_fact_vars[precondition_idx])
                    // _sat.addClause(-method_var, current_fact_vars[precondition_idx]);
                    _sat.addClause(-method_var, leaf_overleaf_var, current_fact_vars[precondition_idx]); 
                    // _sat.addClause(-method_var, leaf_overleaf_var, current_fact_vars[precondition_idx]);
                }
            }
        }
        _stats.endTiming(TimingStage::ENCODING_PREC);
        _stats.end(STAGE_PREC);

        _stats.beginTiming(TimingStage::ENCODING_EFF);
        _stats.begin(STAGE_EFF);
        

        // Encode actions for each possible next action var
        for (const auto &[next_node, next_node_var] : node->getPossibleNextNodeVariable())
        {
            // Encode actions for each possible ts
            const std::vector<int> &next_fact_vars = next_node->getFactVariables();
            for (const auto &[action_idx, action_var] : node->getActionAndVariables())
            {
                const Action &action = _htn.getActionById(action_idx);
                for (int pos_effect_idx : action.getPosEffsIdx())
                {
                    // _sat.addClause(-action_var, -next_node_var, next_fact_vars[pos_effect_idx]);
                    _sat.addClause(-action_var, -next_node_var, leaf_overleaf_var, next_fact_vars[pos_effect_idx]);
                }
                for (int neg_effect_idx : action.getNegEffsIdx())
                {
                    // _sat.addClause(-action_var, -next_node_var, -next_fact_vars[neg_effect_idx]);
                    _sat.addClause(-action_var, -next_node_var, leaf_overleaf_var, -next_fact_vars[neg_effect_idx]);
                }
            }

            if (_encode_prec_and_effs_methods)
            {
                // Encode methods for each possible next action var
                for (const auto &[method_idx, method_var] : node->getMethodAndVariables())
                {
                    // Certified effects can only occurs if there is no leaf overleaf
                    const Method &method = _htn.getMethodById(method_idx);
                    for (int pos_effect_idx : method.getPosEffsIdx())
                    {
                        // _sat.addClause(-method_var, -next_node_var, next_fact_vars[pos_effect_idx]);
                        _sat.addClause(-method_var, -next_node_var, leaf_overleaf_var, next_fact_vars[pos_effect_idx]);
                    }
                    for (int neg_effect_idx : method.getNegEffsIdx())
                    {
                        // _sat.addClause(-method_var, -next_node_var, -next_fact_vars[neg_effect_idx]);
                        _sat.addClause(-method_var, -next_node_var, leaf_overleaf_var, -next_fact_vars[neg_effect_idx]);
                    }
                }
            }
        }
        _stats.endTiming(TimingStage::ENCODING_EFF);
        _stats.end(STAGE_EFF);

        _stats.beginTiming(TimingStage::ENCODING_FIND_FA);
        // Get the positive effects and negative effects of the actions
        for (const auto &[action_idx, action_var] : node->getActionAndVariables())
        {
            const Action &action = _htn.getActionById(action_idx);
            for (int pos_effect_idx : action.getPosEffsIdx())
            {
                positive_effs_can_be_implied_by[pos_effect_idx].insert(action_var);
            }
            for (int neg_effect_idx : action.getNegEffsIdx())
            {
                negative_effs_can_be_implied_by[neg_effect_idx].insert(action_var);
            }
        }

        if (_encode_prec_and_effs_methods)
        {
            // Get the positive effects and negative effects of the methods
            for (const auto &[method_idx, method_var] : node->getMethodAndVariables())
            {
                const Method &method = _htn.getMethodById(method_idx);
                for (int pos_effect_idx : method.getPossPosEffsIdx())
                {
                    positive_effs_can_be_implied_by[pos_effect_idx].insert(method_var);
                }
                for (int neg_effect_idx : method.getPossNegEffsIdx())
                {
                    negative_effs_can_be_implied_by[neg_effect_idx].insert(method_var);
                }
            }
        }

        _stats.endTiming(TimingStage::ENCODING_FIND_FA);

        _stats.beginTiming(TimingStage::ENCODING_FA);
        _stats.begin(STAGE_FRAMEAXIOMS);
        // Encode frame axioms
        // Encode frame axioms for each possible ts
        const int &prim_var = node->getPrimVariable();
        for (const auto &[next_node, next_node_var] : node->getPossibleNextNodeVariable())
        {
            const std::vector<int> &next_fact_vars = next_node->getFactVariables();
            for (int i = 0; i < _htn.getNumPredicates(); i++)
            {
                // If this predicate was true and become false, then either there is a method responsable for this change (so the position is non primtiive)
                // Or an action must be responsible for this change:
                // pred__t and not pred__t+1 => (non prim or negative effect of an action)
                // In CNF:
                // (not pred__t or pred__t+1 or non_prim or action_1_with_negative_effect or action_2_with_negative_effect)
                _sat.appendClause(-current_fact_vars[i], next_fact_vars[i]);
                _sat.appendClause(-next_node_var); // Check only for this node at this ts

                if (!_encode_prec_and_effs_methods)
                {
                    _sat.appendClause(-prim_var);
                }
                // Is there a leaf overleaf ?
                _sat.appendClause(leaf_overleaf_var);
                if (negative_effs_can_be_implied_by.find(i) != negative_effs_can_be_implied_by.end())
                {
                    for (int action_var : negative_effs_can_be_implied_by.at(i))
                    {
                        _sat.appendClause(action_var);
                    }
                }
                _sat.endClause();
                // Do the same things if a predicate was false and become true
                _sat.appendClause(current_fact_vars[i], -next_fact_vars[i]);
                _sat.appendClause(-next_node_var); // Check only for this node at this ts

                if (!_encode_prec_and_effs_methods)
                {
                    _sat.appendClause(-prim_var);
                }
                // Is there a leaf overleaf ?
                _sat.appendClause(leaf_overleaf_var);
                if (positive_effs_can_be_implied_by.find(i) != positive_effs_can_be_implied_by.end())
                {
                    for (int action_var : positive_effs_can_be_implied_by.at(i))
                    {
                        _sat.appendClause(action_var);
                    }
                }
                _sat.endClause();
            }
        }
        _stats.endTiming(TimingStage::ENCODING_FA);
        _stats.end(STAGE_FRAMEAXIOMS);
    }

    _layer_idx++;
}

void Encoding::initalEncode(PdtNode *root_node)
{
    // Encoding the initial state
    encodeInitialState(root_node->getFactVariables(), _htn.getInitState());

    // Encoding the goal state
    encodeGoalState(_htn.getFactVarsGoal(), _htn.getGoalState());

    // Root method must be true
    if (root_node->getActionsIdx().size() != 0 || root_node->getMethodsIdx().size() != 1)
    {
        Log::e("Root node must have only one method and no action");
        exit(1);
    }

    int var_root_method = root_node->getMethodAndVariables().begin()->second;
    _sat.addClause(var_root_method);
}

void Encoding::encodeInitialState(const std::vector<int> &all_pred_vars, const std::unordered_set<int> &init_state)
{
    for (int i = 0; i < _htn.getNumPredicates(); i++)
    {
        if (init_state.find(i) != init_state.end())
        {
            _sat.addClause(all_pred_vars[i]);
        }
        else
        {
            _sat.addClause(-all_pred_vars[i]);
        }
    }
}

void Encoding::encodeGoalState(const std::vector<int> &all_pred_vars, const std::unordered_set<int> &goal_state)
{
    for (int i = 0; i < _htn.getNumPredicates(); i++)
    {
        if (goal_state.find(i) != goal_state.end())
        {
            _sat.addClause(all_pred_vars[i]);
        }
    }
}

void Encoding::encodeActions(const std::unordered_map<int, int> &map_action_idx_to_var, const std::vector<int> &current_fact_vars, const std::vector<int> &next_fact_vars, std::unordered_map<int, std::unordered_set<int>> &positive_effs_can_be_implied_by, std::unordered_map<int, std::unordered_set<int>> &negative_effs_can_be_implied_by)
{
    for (const auto &[action_idx, action_var] : map_action_idx_to_var)
    {
        const Action &action = _htn.getActionById(action_idx);

        // Action implies preconditions
        for (int precondition_idx : action.getPreconditionsIdx())
        {
            _sat.addClause(-action_var, current_fact_vars[precondition_idx]);
        }

        // Action implies effects
        for (int pos_effect_idx : action.getPosEffsIdx())
        {
            _sat.addClause(-action_var, next_fact_vars[pos_effect_idx]);
            positive_effs_can_be_implied_by[pos_effect_idx].insert(action_var);
        }

        for (int neg_effect_idx : action.getNegEffsIdx())
        {
            _sat.addClause(-action_var, -next_fact_vars[neg_effect_idx]);
            negative_effs_can_be_implied_by[neg_effect_idx].insert(action_var);
        }
    }
}

// void Encoding::encodeMethods(const std::unordered_map<int, int> &map_method_idx_to_var, const std::vector<int> &current_fact_vars, const std::vector<int> &next_fact_vars) {
//     for (const auto &[method_idx, method_var] : map_method_idx_to_var)
//     {
//         const Method &method = _htn.getMethodById(method_idx);
//         const std::vector<int> &preconditions_idx = method.();

//         // Method implies preconditions
//         for (int precondition_idx : preconditions_idx)
//         {
//             _sat.addClause(-method_var, current_fact_vars[precondition_idx]);
//         }
//     }
// }

void Encoding::encodePrimitivenessOps(const std::unordered_map<int, int> &map_action_idx_to_var, const std::unordered_map<int, int> &map_method_idx_to_var, const int &prim_var)
{
    for (const auto &[action_idx, action_var] : map_action_idx_to_var)
    {
        _sat.addClause(-action_var, prim_var);
    }
    for (const auto &[method_idx, method_var] : map_method_idx_to_var)
    {
        _sat.addClause(-method_var, -prim_var);
    }
}

void Encoding::encodeFrameAxioms(const std::vector<int> &current_fact_vars, const std::vector<int> &next_fact_vars, const int &prim_var, const std::unordered_map<int, std::unordered_set<int>> &positive_effs_can_be_implied_by, const std::unordered_map<int, std::unordered_set<int>> &negative_effs_can_be_implied_by)
{
    for (int i = 0; i < _htn.getNumPredicates(); i++)
    {
        // If this predicate was true and become false, then either there is a method responsable for this change (so the position is non primtiive)
        // Or an action must be responsible for this change:
        // pred__t and not pred__t+1 => (non prim or negative effect of an action)
        // In CNF:
        // (not pred__t or pred__t+1 or non_prim or action_1_with_negative_effect or action_2_with_negative_effect)
        _sat.appendClause(-current_fact_vars[i], next_fact_vars[i]);
        _sat.appendClause(-prim_var);
        if (negative_effs_can_be_implied_by.find(i) != negative_effs_can_be_implied_by.end())
        {
            for (int action_var : negative_effs_can_be_implied_by.at(i))
            {
                _sat.appendClause(action_var);
            }
        }
        _sat.endClause();
        // Do the same things if a predicate was false and become true
        _sat.appendClause(current_fact_vars[i], -next_fact_vars[i]);
        _sat.appendClause(-prim_var);
        if (positive_effs_can_be_implied_by.find(i) != positive_effs_can_be_implied_by.end())
        {
            for (int action_var : positive_effs_can_be_implied_by.at(i))
            {
                _sat.appendClause(action_var);
            }
        }
        _sat.endClause();
    }
}

void Encoding::encodeAtMostOne(const std::vector<int> &vars)
{
    if (vars.size() < 100)
    {
        for (int i = 0; i < vars.size(); i++)
        {
            for (int j = i + 1; j < vars.size(); j++)
            {
                _sat.addClause(-vars[i], -vars[j]);
            }
        }
    }
    else
    {
        size_t size_amo = vars.size();
        auto bamo = BimanderAtMostOne(vars, size_amo, (size_t)std::sqrt(size_amo));
        for (const auto &c : bamo.encode())
            _sat.addClause(c);
    }
}

void Encoding::encodeHierarchy(const PdtNode *cur_node, const PdtNode *parent_node)
{
    // For each parent op, get the possible children
    std::unordered_map<int, std::unordered_set<int>> possible_children_var_per_op_var;

    _stats.beginTiming(TimingStage::TEST_3);
    for (const auto &[child_method, possible_parent] : cur_node->getParentsOfMethod())
    {
        int var_child = cur_node->getMethodAndVariables().at(child_method);
        _sat.appendClause(-var_child);
        for (const int& parent_method : possible_parent)
        {
            const int& var_parent = parent_node->getMethodAndVariables().at(parent_method);
            possible_children_var_per_op_var[var_parent].insert(var_child);
            _sat.appendClause(var_parent);
        }
        _sat.endClause();
    }
    _stats.endTiming(TimingStage::TEST_3);
    _stats.beginTiming(TimingStage::TEST_2);
    // Iterate through the actions in the current node and their potential parents
    for (const auto &[child_action_idx, possible_parents_set] : cur_node->getParentsOfAction())
    {
        const int& var_child = cur_node->getActionAndVariables().at(child_action_idx);
        _sat.appendClause(-var_child);
        // Iterate through the set of parent pairs (ID, Type) for this child action
        for (const auto &parent_pair : possible_parents_set)
        {
            int parent_idx = parent_pair.first;
            OpType parent_type = parent_pair.second;

            int var_parent;
            // Get the parent variable based on its type
            if (parent_type == OpType::ACTION)
            {
                var_parent = parent_node->getActionAndVariables().at(parent_idx);
            }
            else // OpType::METHOD
            {
                var_parent = parent_node->getMethodAndVariables().at(parent_idx);
            }
            // Add the child variable to the set associated with the parent variable
            possible_children_var_per_op_var[var_parent].insert(var_child);
            _sat.appendClause(var_parent);
        }
        _sat.endClause();
    }
    _stats.endTiming(TimingStage::TEST_2);
    _stats.beginTiming(TimingStage::TEST_4);
    bool encode_at_most_one_on_children_instead_of_all_ops = true;
    int num_ops = cur_node->getMethodAndVariables().size() + cur_node->getActionAndVariables().size();
    int half = num_ops / 2;
    for (const auto &[parent_var, children_var] : possible_children_var_per_op_var)
    {
        // If children_var > 1/2, then encode at most one for all ops
        if (children_var.size() > half)
        {
            encode_at_most_one_on_children_instead_of_all_ops = false;
            Log::d("Encode at most one for all ops because there is a parent with %d children and there are %d ops\n", children_var.size(), num_ops);
            break;
        }
    }
    _stats.endTiming(TimingStage::TEST_4);

    // Each parent implies at least one child
    _stats.beginTiming(TimingStage::TEST_1);
    for (const auto &[parent_var, children_var] : possible_children_var_per_op_var)
    {
        
        _sat.appendClause(-parent_var);
        std::vector<int> children;
        for (const int& child_var : children_var)
        {
            _sat.appendClause(child_var);
            if (encode_at_most_one_on_children_instead_of_all_ops) {
                children.push_back(child_var);
            }
        }
        _sat.endClause();

        if (encode_at_most_one_on_children_instead_of_all_ops) {
            encodeAtMostOne(children);
        }
    }
    _stats.endTiming(TimingStage::TEST_1);

    _stats.beginTiming(TimingStage::TEST_5);
    if (!encode_at_most_one_on_children_instead_of_all_ops) {
        // Encode at most one for all ops
        std::vector<int> all_ops;
        for (const auto &[op_idx, op_var] : cur_node->getMethodAndVariables())
        {
            all_ops.push_back(op_var);
        }
        for (const auto &[op_idx, op_var] : cur_node->getActionAndVariables())
        {
            all_ops.push_back(op_var);
        }
        encodeAtMostOne(all_ops);
    }
    _stats.endTiming(TimingStage::TEST_5);
}

void Encoding::addAssumptions(const std::vector<int> &assumptions)
{
    for (int assumption : assumptions)
    {
        _sat.assume(assumption);
    }
}

int Encoding::solve()
{
    return _sat.solve();
}

void Encoding::setOpsTrueInTree(PdtNode *node, bool is_po)
{
    Log::i("For node %s\n", TOSTR(*node));
    // Get the op true in the node
    int num_ops_true = 0;
    for (const auto &[op_idx, op_var] : node->getMethodAndVariables())
    {
        if (_sat.holds(op_var))
        {
            // If it is a leaf node, return an error
            if (node->getChildren().size() == 0)
            {
                throw std::runtime_error("An op true in the leaf node cannot be a method");
            }
            node->setOpSolution(op_idx, OpType::METHOD);
            Log::i("  Method %s is true\n", TOSTR(_htn.getMethodById(op_idx)));
            num_ops_true++;
        }
    }
    for (const auto &[op_idx, op_var] : node->getActionAndVariables())
    {
        if (_sat.holds(op_var))
        {
            node->setOpSolution(op_idx, OpType::ACTION);
            if (is_po && node->getChildren().size() == 0)
            {
                assignTsToLeafNode(node);
                Log::i("Set ts %d solution for action %s (var %d)\n", node->getTsSolution(), TOSTR(_htn.getActionById(op_idx)), op_var);
            }
            Log::i("  Action %s is true\n", TOSTR(_htn.getActionById(op_idx)));
            num_ops_true++;
        }
    }
    if (num_ops_true != 1)
    {
        throw std::runtime_error("There must be one and only one op true in the node");
    }
    // Recursively extract the op true for each child
    for (PdtNode *child : node->getChildren())
    {
        setOpsTrueInTree(child, is_po);
    }
}

int Encoding::assignTsToLeafNode(PdtNode *leaf_node)
{
    // Log::i("Checking node %s\n", TOSTR(*leaf_node));
    // If the ts is already assigned, return it
    if (leaf_node->getTsSolution() != -1)
    {
        return leaf_node->getTsSolution();
    }
    if (leaf_node->getChildren().size() != 0)
    {
        throw std::runtime_error("The node is not a leaf node");
    }

    // Is it the init node ?
    if (leaf_node->getPossiblePreviousNodes().size() == 0)
    {
        Log::i("Lead node %s is init\n", TOSTR(*leaf_node));
        leaf_node->setTsSolution(0);
        return 0;
    }

    // Get the previous node of this node
    PdtNode *prev_node = nullptr;
    for (const auto &[prev, ordering] : leaf_node->getPossiblePreviousNodes())
    {
        if (_sat.holds(prev->getPossibleNextNodeVariable().at(leaf_node)))
        {
            // Log::i("Node %s is previous of %s\n", TOSTR(*prev), TOSTR(*leaf_node));
            prev_node = prev;
            break;
        }
    }
    if (prev_node == nullptr)
    {
        throw std::runtime_error("The previous node is null");
    }

    int prev_node_ts = assignTsToLeafNode(prev_node);
    int current_node_ts = prev_node_ts + 1;
    leaf_node->setTsSolution(current_node_ts);
    return current_node_ts;
}