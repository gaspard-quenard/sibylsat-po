#ifndef PLANNER_H
#define PLANNER_H

#include "data/htn_instance.h"
#include "sat/encoding.h"
#include "data/pdt_node.h"
#include "algo/plan_manager.h"


class Planner
{

private:
    HtnInstance &_htn;
    Encoding _enc;
    PdtNode* _root_node;
    PlanManager _plan_manager;
    Statistics& _stats;

    const bool _write_plan;
    const bool _print_var_names;
    const bool _verify_plan;

    const bool _partial_order_problem;
    const bool _po_with_before = _htn.getParams().isNonzero("po_with_before");

    const bool _sibylsat_expansion = _htn.getParams().isNonzero("sibylsat");

    std::vector<int> _leafs_overleafs_vars_to_encode;
    std::vector<int> _previous_nexts_nodes;

    /**
     * Expand the structure with a new layer.
     */
    void expandLayer();

public:
    Planner(HtnInstance& htn): 
    _stats(Statistics::getInstance()),
    _htn(htn), 
    _enc(_htn), 
    _plan_manager(_htn), 
    _print_var_names(_htn.getParams().isNonzero("pvn")),
    _verify_plan(_htn.getParams().isNonzero("vp")),
    _partial_order_problem(_htn.isPartialOrderProblem()),
    _write_plan(_htn.getParams().isNonzero("wp")) {}
    ~Planner() { delete _root_node; }

    int findPlan();
};


#endif // PLANNER_H