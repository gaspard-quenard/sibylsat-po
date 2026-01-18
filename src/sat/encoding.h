#ifndef ENCODING_H
#define ENCODING_H

#include "data/htn_instance.h"
#include "sat/sat_interface.h"
#include "data/pdt_node.h"

class Encoding
{
private:
    HtnInstance &_htn;
    Statistics &_stats;
    SatInterface _sat;

    // For PO
    std::vector<int> _leaf_overleaf_vars;
    int _num_ts;

    const bool _encode_prec_and_effs_methods = _htn.getParams().isNonzero("sibylsat");

    int _layer_idx = 0;

    const bool _print_var_names = _htn.getParams().isNonzero("pvn");
    const bool _encode_mutexes = _htn.getParams().isNonzero("mutex");

    void encodeInitialState(const std::vector<int> &all_pred_vars, const std::unordered_set<int> &init_state);
    void encodeGoalState(const std::vector<int> &all_pred_vars, const std::unordered_set<int> &goal_state);
    void encodeActions(const std::unordered_map<int, int> &map_action_idx_to_var, const std::vector<int> &current_fact_vars, const std::vector<int> &next_fact_vars, std::unordered_map<int, std::unordered_set<int>> &positive_effs_can_be_implied_by, std::unordered_map<int, std::unordered_set<int>> &negative_effs_can_be_implied_by);
    void encodePrimitivenessOps(const std::unordered_map<int, int> &map_action_idx_to_var, const std::unordered_map<int, int> &map_method_idx_to_var, const int &prim_var);
    void encodeFrameAxioms(const std::vector<int> &current_fact_vars, const std::vector<int> &next_fact_vars, const int &prim_var, const std::unordered_map<int, std::unordered_set<int>> &positive_effs_can_be_implied_by, const std::unordered_map<int, std::unordered_set<int>> &negative_effs_can_be_implied_by);
    void encodeAtMostOne(const std::vector<int> &vars);
    void encodeHierarchy(const PdtNode *cur_node, const PdtNode *parentNode);

public:
    Encoding(HtnInstance &htn) : _htn(htn), _sat(htn.getParams()), _stats(Statistics::getInstance()) {}

    void initalEncode(PdtNode *root);
    void encode(std::vector<PdtNode *> &leaf_nodes);
    void encodePOWithBefore(std::vector<PdtNode *> &leaf_nodes);

    void writeFormula(std::string filename)
    {
        _sat.print_formula(filename);
    }

    // Indicate for each node, which action or method is true
    void setOpsTrueInTree(PdtNode *node, bool is_po);

    void addAssumptions(const std::vector<int> &assumptions);
    void setPhase(int var, int phase)
    {
        _sat.setPhase(var, phase);
    }
    int getLastLeafOverleafVar()
    {
        return _leaf_overleaf_vars.back();
    }
    bool holds(int lit)
    {
        return _sat.holds(lit);
    }
    bool causeFail(int lit)
    {
        return _sat.didAssumptionFail(lit);
    }
    int solve();

    int assignTsToLeafNode(PdtNode *leaf_node);
};

#endif // ENCODING_H
