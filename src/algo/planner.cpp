#include "algo/planner.h"
#include "util/names.h"

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

    // Set SAT variables for the initial layer

    // Encode the inital layer
    _enc.initalEncode(_root_node);

    // Run main loop
    bool solved = false;
    int max_depth = 50;
    int current_depth = 0;
    std::vector<PdtNode *> new_leaf_nodes;
    while (!solved && current_depth < max_depth)
    {
        current_depth++;
        Log::i("For depth %d\n", current_depth);

        Log::i("  Expanding layer...\n");

        // if (_partial_order_problem)
        // {
        //     // First, create all the children nodes
        //     for (PdtNode *node : leaf_nodes)
        //     {
        //         node->createChildren(_htn);
        //         for (PdtNode *child : node->getChildren())
        //         {
        //             new_leaf_nodes.push_back(child);
        //         }
        //     }

        //     // Next, populate each children based on its parent
        //     for (PdtNode *node : leaf_nodes)
        //     {
        //         node->expandPO(_htn, new_leaf_nodes);
        //     }
        // }
        // else
        // {
        // Expand all the leaf nodes
        _stats.beginTiming(TimingStage::EXPANSION);
        int pos = 0;
        for (PdtNode *node : leaf_nodes)
        {
            if (_partial_order_problem)
            {
                Log::d("Expand node %s\n", TOSTR(*node));
                // node->expandPO(_htn, /*order_between_children=*/current_depth > 1);
                if (_po_with_before)
                {
                    node->expandPOWithBefore(_htn);
                }
                else
                {
                    node->expandPO(_htn, /*order_between_children=*/current_depth > 1);
                }
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

        if (_po_with_before)
        {
            Log::i("  Adding ordering constraints between no sibling nodes...\n");
            for (PdtNode *node : new_leaf_nodes)
            {
                node->makeOrderingNoSibling();
            }

            // To debug, print all the next nodes
            // for (const PdtNode *node : new_leaf_nodes)
            // {
            //     Log::i("Next nodes for %s\n", TOSTR(*node));
            //     for (const auto &[next_node, ordering] : node->getPossibleNextNodes())
            //     {
            //         Log::i("  %s (%s)\n", TOSTR(*next_node), TOSTR(ordering));
            //     }
            // }
        }

        _stats.endTiming(TimingStage::EXPANSION);
        // }

        Log::i("  Assigning SAT variables...\n");
        // Assign the SAT variables for the new layer
        for (int idx_node = 0; idx_node < new_leaf_nodes.size(); ++idx_node)
        {
            PdtNode *node = new_leaf_nodes[idx_node];
            // Log::i("Assigning SAT variables for node %d/%d: %s\n", idx_node, new_leaf_nodes.size(), TOSTR(*node));
            // Log::i("%s\n", TOSTR(*node));
            node->assignSatVariables(_htn, _print_var_names, _partial_order_problem);
            if (_po_with_before)
            {
                for (int idx_node_2 = idx_node + 1; idx_node_2 < new_leaf_nodes.size(); ++idx_node_2)
                {
                    PdtNode *node_2 = new_leaf_nodes[idx_node_2];
                    // Create a variable to indicate that idx_node is before_idx_node_2
                    int var = VariableProvider::nextVar();
                    if (_print_var_names)
                    {
                        // Use node indices (0 to num_nodes-1) directly in names
                        std::string var_name = "layer_" + std::to_string(current_depth) + "__node_" + node->getName() + "__before__node_" + node_2->getName();
                        Log::i("PVN: %d %s\n", var, var_name.c_str());
                    }
                    // Add the variable to the node
                    // node->addBeforeNextNodeVar(node_2, var);
                    // node_2->addBeforeNextNodeVar(node, -var); // <--- Slow down a lot when adding this variable

                    bool can_node_before_node_2 = true;
                    bool can_node_2_before_node = true;
                    if (node->getNodeThatMustBeExecutedAfter().find(node_2) != node->getNodeThatMustBeExecutedAfter().end())
                    {
                        can_node_2_before_node = false;
                    }
                    if (node_2->getNodeThatMustBeExecutedAfter().find(node) != node_2->getNodeThatMustBeExecutedAfter().end())
                    {
                        // Why does it slow so much when adding this variable ?
                        // node_2->addBeforeNextNodeVar(node, -var);
                        // node->addBeforeNextNodeVar(node_2, var);
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
            // if (_partial_order_problem)
            // {
            //     // To debug, indicate for each node, all the nodes that must be executed before
            //     Log::i("Previous of node: %s\n", TOSTR(*node));
            //     for (PdtNode *prev_ndoe : node->getNodeThatMustBeExecutedBefore())
            //     {
            //         Log::i("  %s\n", TOSTR(*prev_ndoe));
            //     }
            //     Log::i("-----------------------\n");
            // }
        }

        Log::i("  Encoding...\n");
        // Encode the new leaf nodes
        _stats.beginTiming(TimingStage::ENCODING);
        if (_partial_order_problem)
        {
            if (_po_with_before)
            {
                _enc.encodePOWithBefore(new_leaf_nodes);
            }
            else
            {
                _enc.encodePO(new_leaf_nodes);
            }
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
            // prim_vars.push_back(-last_leaf_overleaf_varfs);
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
        _enc.addAssumptions(prim_vars);
        _enc.addAssumptions(leaf_overleaf_vars); // Can be kept
        _enc.addAssumptions(previous_next_nodes);

        // Log::i("  Solving with %d clauses...\n", _stats._num_cls);
        Log::i("  Solving with %d clauses, %d prim vars, %d leaf overleaf vars, %d previous next nodes...\n", _stats._num_cls, prim_vars.size(), leaf_overleaf_vars.size(), previous_next_nodes.size());
        // Launch the SAT solver
        int result = _enc.solve();
        // _enc.writeFormula("formula_" + std::to_string(current_depth) + ".cnf");
        Log::i("    Result: %d\n", result);
        solved = (result == 10);

        if (!solved && _sibylsat_expansion)
        {
            Log::i("  Failed to find a solution. Try to find a relaxed solution...\n");
            Log::i("Still considering that leaf overleaf cannot happen...\n");
            _enc.addAssumptions(leaf_overleaf_vars);
            _enc.addAssumptions(previous_next_nodes);
            int result = _enc.solve();

            bool relaxed_solved = (result == 10);

            if (!relaxed_solved && previous_next_nodes.size() > 0)
            {
                // while (!relaxed_solved && previous_next_nodes.size() > 0)
                // {
                //     Log::i("Failed to resolve the problem while settings the previous next nodes... Relaxing all assumptions which cause false...\n");
                //     std::vector<int> new_previous_next_nodes;
                //     int size_before_relaxing = previous_next_nodes.size();
                //     for (int previous_next : previous_next_nodes)
                //     {
                //         if (_enc.causeFail(previous_next))
                //         {
                //             Log::i("Relaxing %d\n", previous_next);
                //         }
                //         else
                //         {
                //             Log::i("Keeping %d\n", previous_next);
                //             new_previous_next_nodes.push_back(previous_next);
                //         }
                //         // next_previous_next_nodes.push_back(-previous_next);
                //     }
                //     previous_next_nodes = new_previous_next_nodes;
                //     Log::i("Keep only %d of the %d previous next nodes...\n", previous_next_nodes.size(), size_before_relaxing);
                //     if (previous_next_nodes.size() == size_before_relaxing)
                //     {
                //         Log::i("No more previous next nodes to relax...\n");
                //         _enc.addAssumptions(previous_next_nodes);
                //         relaxed_solved = false;
                //         break;
                //     }
                //     _previous_nexts_nodes.clear();
                //     _previous_nexts_nodes = previous_next_nodes;
                //     _enc.addAssumptions(leaf_overleaf_vars);
                //     _enc.addAssumptions(previous_next_nodes);
                //     result = _enc.solve();
                // }

                _previous_nexts_nodes.clear();
                _enc.addAssumptions(leaf_overleaf_vars);
                result = _enc.solve();
                relaxed_solved = (result == 10);
            }

            if (!relaxed_solved)
            {
                Log::e("No solution possible for this problem !\n");
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
                // if (relaxed_solved) {
                //     for (PdtNode *node : new_leaf_nodes)
                // {
                //     for (const auto &[next_node, var] : node->getPossibleNextNodeVariable())
                //     {
                //         if (_enc.holds(var))
                //         {
                //             _previous_nexts_nodes.push_back(var);
                //             // int before_var = node->getBeforeNextNodeVar(next_node); // TODO COULD BE RELAXED ?
                //             // _previous_nexts_nodes.push_back(before_var);
                //             Log::i("Adding %d to the list of previous next nodes...\n", var);
                //         }
                //     }
                // }
                // }

                // return 1;
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
                            // int before_var = node->getBeforeNextNodeVar(next_node); // TODO COULD BE RELAXED ?
                            // _previous_nexts_nodes.push_back(before_var);
                            Log::i("Adding %d to the list of previous next nodes...\n", var);
                        }
                    }
                }
                // _leafs_overleafs_vars_to_encode.push_back(_enc.getLastLeafOverleafVar());
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