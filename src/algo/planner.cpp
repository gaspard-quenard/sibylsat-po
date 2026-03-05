#include "algo/planner.h"
#include "util/names.h"

int Planner::findPlan()
{
    std::vector<PdtNode *> leaf_nodes;

    // Initialize Tree
    _root_node = new PdtNode(/*parent=*/nullptr);
    int root_method_idx = _htn.getRootTask().getDecompositionMethodsIdx()[0];
    _root_node->addMethodIdx(root_method_idx);

    leaf_nodes.push_back(_root_node);

    // Assign the SAT variables for the new layer
    for (PdtNode *node : leaf_nodes)
    {
        node->assignSatVariables(_htn, _print_var_names, _partial_order_problem);
    }

    // Encode the initial layer
    _enc.initalEncode(_root_node);

    // Run main loop
    bool solved = false;
    int max_depth = _htn.getParams().getIntParam("D");
    int current_depth = 0;
    std::vector<PdtNode *> new_leaf_nodes;
    while (!solved && current_depth < max_depth)
    {
        current_depth++;
        Log::i("For depth %d\n", current_depth);

        Log::i("  Expanding layer...\n");

        // Expand all the leaf nodes
        _stats.beginTiming(TimingStage::EXPANSION);
        int pos = 0;
        for (PdtNode *node : leaf_nodes)
        {
            if (_partial_order_problem)
            {
                Log::d("Expand node %s\n", TOSTR(*node));
                node->expandPOWithBefore(_htn);
            }
            else
            {
                node->expand(_htn);
            }

            for (PdtNode *child : node->getChildren())
            {
                child->setPos(pos);
                pos++;
                new_leaf_nodes.push_back(child);
            }
        }

        Log::i("Number of leaf nodes: %d\n", new_leaf_nodes.size());

        if (_partial_order_problem)
        {
            Log::i("  Adding ordering constraints between no sibling nodes...\n");
            for (PdtNode *node : new_leaf_nodes)
            {
                node->makeOrderingNoSibling();
            }

            if (_indep_pos)
            {
                Log::i("  Removing ordering constraints between independent nodes...\n");

                // First, compute for each node the full set of possible preconditions and effects
                Log::i("Computing full possible preconditions and effects for each node...\n");
                for (PdtNode *node : new_leaf_nodes)
                {
                    node->computeFullPrecsAndEffs(_htn);
                }

                int total_number_of_independant_found = 0;


                Log::i("Done ! Assigning order between independent nodes...\n");
                for (PdtNode *node : new_leaf_nodes)
                {
                    int current_node_idx = node->getPos();
                    // Get all the next nodes of the current node
                    std::vector<PdtNode*> next_nodes_independants;
                    int num_independent_next_nodes = 0;
                    int num_both_way_next_nodes = 0;
                    // Log::i("Node at pos %d has %d possible next nodes before filtering:\n", current_node_idx, (int)node->getPossibleNextNodes().size());
                    for (const auto &[next_node, _] : node->getPossibleNextNodes())
                    {

                        // If the pos is infererior to this node, pass (already done when we were at this node)
                        if (next_node->getPos() < current_node_idx) continue;

                        // Does this next node can have the current node as possible next node ?
                        if (next_node->getPossibleNextNodes().count(node) > 0)
                        {
                            num_both_way_next_nodes++;
                            // Check if the two nodes are independent (no possible interaction between them)
                            if (node->isIndependentWith(*next_node))
                            {
                                num_independent_next_nodes++;
                                total_number_of_independant_found++;
                                Log::i("  Node %s is independent with next node %s\n", TOSTR(*node), TOSTR(*next_node));

                                next_nodes_independants.push_back(next_node);
                            }
                        }
                    }
                    Log::i("Node at pos %d has %d next nodes. From which %d are independent and %d are both way:\n", current_node_idx, (int)node->getPossibleNextNodes().size(), num_independent_next_nodes, num_both_way_next_nodes);

                    for (PdtNode* next_node : next_nodes_independants)
                    {
                        // Remove the possible ordering between the two nodes (we can choose to execute one before the other, or the opposite)
                        next_node->removePossibleNextNode(node);
                    }
                }

                // Finally, clear the full possible precs/effects for each node to free memory (not needed anymore)
                for (PdtNode *node : new_leaf_nodes)
                {
                    node->clearFullPrecsAndEffs();  
                }

                Log::i("Done ! Total number of independent nodes found: %d\n", total_number_of_independant_found);
            }
            
        }

        _stats.endTiming(TimingStage::EXPANSION);

        Log::i("  Assigning SAT variables...\n");
        // Assign the SAT variables for the new layer
        for (int idx_node = 0; idx_node < new_leaf_nodes.size(); ++idx_node)
        {
            PdtNode *node = new_leaf_nodes[idx_node];
            node->assignSatVariables(_htn, _print_var_names, _partial_order_problem);
            if (_partial_order_problem)
            {
                for (int idx_node_2 = idx_node + 1; idx_node_2 < new_leaf_nodes.size(); ++idx_node_2)
                {
                    PdtNode *node_2 = new_leaf_nodes[idx_node_2];
                    // Create a variable to indicate that idx_node is before_idx_node_2
                    int var = VariableProvider::nextVar();
                    if (_print_var_names)
                    {
                        std::string var_name = "layer_" + std::to_string(current_depth) + "__node_" + node->getName() + "__before__node_" + node_2->getName();
                        Log::i("PVN: %d %s\n", var, var_name.c_str());
                    }
                    // Add the variable to the node
                    bool can_node_before_node_2 = true;
                    bool can_node_2_before_node = true;
                    if (node->getNodeThatMustBeExecutedAfter().find(node_2) != node->getNodeThatMustBeExecutedAfter().end())
                    {
                        can_node_2_before_node = false;
                    }
                    if (node_2->getNodeThatMustBeExecutedAfter().find(node) != node_2->getNodeThatMustBeExecutedAfter().end())
                    {
                        can_node_before_node_2 = false;
                    }
                    if (can_node_before_node_2)
                    {
                        node->addBeforeNextNodeVar(node_2, var);
                    }
                    if (can_node_2_before_node)
                    {
                        node_2->addBeforeNextNodeVar(node, -var);
                    }
                }
            }
        }

        Log::i("  Encoding...\n");
        // Encode the new leaf nodes
        _stats.beginTiming(TimingStage::ENCODING);

        if (_po_v2) {
            _enc.encodePOWithBeforeV2(new_leaf_nodes);
        }
        else if (_partial_order_problem)
        {
            _enc.encodePOWithBefore(new_leaf_nodes);
        }
        else
        {
            _enc.encode(new_leaf_nodes);
        }
        _stats.endTiming(TimingStage::ENCODING);

        // Add assumptions that each leaf node must be primitive
        std::vector<int> prim_vars;
        std::vector<int> leaf_overleaf_vars;
        std::vector<int> previous_next_nodes;
        int last_leaf_overleaf_varfs = _enc.getLastLeafOverleafVar();
        for (PdtNode *node : new_leaf_nodes)
        {
            prim_vars.push_back(node->getPrimVariable());
        }
        if (_partial_order_problem && _sibylsat_expansion)
        {
            // Add the leaf overleaf variable
            _leafs_overleafs_vars_to_encode.push_back(last_leaf_overleaf_varfs);
            for (int previous_leaf_overleaf_var : _leafs_overleafs_vars_to_encode)
            {
                leaf_overleaf_vars.push_back(-previous_leaf_overleaf_var);
            }
            for (int previous_next : _previous_nexts_nodes)
            {
                previous_next_nodes.push_back(previous_next);
            }
        }
        else if (_partial_order_problem)
        {
            // Add the leaf overleaf variable
            // prim_vars.push_back(-last_leaf_overleaf_varfs);
            leaf_overleaf_vars.push_back(-last_leaf_overleaf_varfs);
        }
        _enc.addAssumptions(prim_vars); // All leaf nodes must be primitive
        _enc.addAssumptions(leaf_overleaf_vars); // Enforce that the effects of operations are applied on the next position when leaf overleaf for this layer
        _enc.addAssumptions(previous_next_nodes); // Some order between operation on some positions must be enforced

        Log::i("  Solving with %d clauses, %d prim vars, %d leaf overleaf vars, %d previous next nodes...\n", _stats._num_cls, prim_vars.size(), leaf_overleaf_vars.size(), previous_next_nodes.size());
        // Launch the SAT solver
        int result = _enc.solve();
        // _enc.writeFormula("formula_" + std::to_string(current_depth) + ".cnf");
        Log::i("    Result: %d\n", result);
        solved = (result == 10);

        if (!solved && _sibylsat_expansion)
        {
            Log::i("  UNSAT... Try to find a relaxed solution...\n");
            Log::i("Solving assuming %d leaf overleaf vars and %d previous next nodes...\n", leaf_overleaf_vars.size(), previous_next_nodes.size());
            _enc.addAssumptions(leaf_overleaf_vars);
            _enc.addAssumptions(previous_next_nodes);
            int result = _enc.solve();

            bool relaxed_solved = (result == 10);

            if (!relaxed_solved && previous_next_nodes.size() > 0)
            {
                Log::i("  UNSAT... Now try to relax previous next nodes...\n");
                Log::i("Solving assuming %d leaf overleaf vars without previous next nodes...\n", leaf_overleaf_vars.size());
                _previous_nexts_nodes.clear();
                _enc.addAssumptions(leaf_overleaf_vars);
                result = _enc.solve();
                relaxed_solved = (result == 10);
            }

            if (!relaxed_solved)
            {
                Log::e("UNSAT... No relaxed solution possible for this problem assuming leaf overleaf vars !\n");
                // _leafs_overleafs_vars_to_encode.clear();
                while (_leafs_overleafs_vars_to_encode.size() > 0 && !relaxed_solved)
                {
                    _leafs_overleafs_vars_to_encode.pop_back();
                    Log::i("  Try to find a relaxed solution with %d leaf overleafs...\n", _leafs_overleafs_vars_to_encode.size());
                    std::vector<int> leaf_overleaf_vars;
                    for (int previous_leaf_overleaf_var : _leafs_overleafs_vars_to_encode)
                    {
                        leaf_overleaf_vars.push_back(-previous_leaf_overleaf_var);
                    }
                    _enc.addAssumptions(leaf_overleaf_vars);
                    result = _enc.solve();
                    Log::i("    Result: %d\n", result);
                    relaxed_solved = (result == 10);
                }
            }
            else
            {
                Log::i("Found a relaxed solution !\n");
                // For all leafs nodes, add the next var into the list of previous next nodes
                for (PdtNode *node : new_leaf_nodes)
                {
                    for (const auto &[next_node, var] : node->getPossibleNextNodeVariable())
                    {
                        if (_enc.holds(var))
                        {
                            _previous_nexts_nodes.push_back(var);
                            Log::i("Adding %d to the list of previous next nodes...\n", var);
                        }
                    }
                }
            }
        }

        leaf_nodes = new_leaf_nodes;
        new_leaf_nodes.clear();
    }
    // If solved, extract the plan and verify it
    if (!solved)
    {
        Log::w("No success. Exiting.\n");
        return 1;
    }

    Log::i("Found a solution at layer %i.\n", current_depth);

    // For each node in the PDT, indicate which action or method is true
    _enc.setOpsTrueInTree(/*node=*/_root_node, /*is_po=*/_partial_order_problem);

    // Generate the plan
    if (!_plan_manager.generatePlan(_root_node))
    {
        Log::e("Error: Failed to generate the final plan.\n");
        return 1;
    }
    if (_verify_plan)
    {
        // Verify the plan
        if (!_plan_manager.verifyPlan())
        {
            Log::e("Error: Plan verification failed.\n");
            return 1;
        }
        Log::i("Plan verified successfully.\n");
    }
    // Output the plan
    Log::log_notime(Log::V0_ESSENTIAL, _plan_manager.getPlanString().c_str());
    Log::i("End of solution plan. (counted length of %i)\n", _plan_manager.getPlanSize());
    Log::i("Size of the leaf nodes: %i\n", leaf_nodes.size());
    Log::i("Number of layers: %i\n", current_depth);

    if (_write_plan)
    {
        if (!_plan_manager.outputPlan("plan.txt"))
        {
            Log::e("Error: Failed to write the plan to file.\n");
            return 1;
        }
    }

    return 0;
}