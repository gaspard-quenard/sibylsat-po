
#ifndef PLAN_MANAGER_H
#define PLAN_MANAGER_H

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <fstream> // Required for std::ostream

#include "data/htn_instance.h"
#include "data/pdt_node.h"

#include <optional>

class PlanManager
{

private:
    HtnInstance &_htn;
    std::string _final_plan_string; // Stores the final plan string after successful generation/conversion
    size_t _size_plan;
    const bool _partial_order_problem = _htn.getParams().isNonzero("po");

    /**
     * Recursively processes a node in the plan decomposition tree to build plan components.
     *
     * @param node The current node in the decomposition tree.
     * @param counter A reference to the counter for generating unique plan IDs.
     * @param parent_task The parent abstract task of the current node.
     * @param actions A vector to store the action lines with their associated time steps.
     * @param abstract_tasks A vector to store the abstract task decomposition lines.
     * @return The plan ID assigned to the operation at this node.
     */
    int processNode(PdtNode *node, int &counter, const AbstractTask *parent_task, std::vector<std::pair<int, std::string>> &actions, std::vector<std::string> &abstract_tasks);

    /**
     * Builds the complete raw plan string from processed actions and tasks.
     *
     * @param actions Vector of action strings. In the form of (timestamp, action_string).
     * @param abstract_tasks Vector of abstract task decomposition strings.
     * @return The complete raw plan string.
     */
    std::string buildPlanRawString(std::vector<std::pair<int, std::string>> &actions, const std::vector<std::string> &abstract_tasks);

    // Internal helper to generate the raw plan string representation
    std::string generateRawPlanString(PdtNode *root_node);

    // Internal helper to run the conversion process from the raw plan to the final plan using pandaPiParser
    std::optional<std::string> convertRawPlanToFinalPlan(const std::string& raw_plan_content);

    // Internal helper to run the verification process using a temporary file
    bool runVerification(const std::string& final_plan_content);

public:
    PlanManager(HtnInstance &htn) : _htn(htn) {}

    /**
     * Generates the final (converted) plan and stores it internally.
     * Handles raw plan generation, conversion via external tool, and temporary file management.
     * Must be called successfully before verifyPlan() or outputPlan().
     *
     * @param root_node The root node of the plan decomposition tree.
     * @return True if generation and conversion were successful, false otherwise.
     */
    bool generatePlan(PdtNode *root_node);

    /**
     * Verifies the internally stored final plan string using an external tool.
     * Requires generatePlan() to have been called successfully first.
     * Handles temporary file management internally.
     *
     * @return True if the plan is valid according to the verifier, false otherwise.
     */
    bool verifyPlan();

    /**
     * Writes the internally stored final plan string to the specified file.
     * Requires generatePlan() to have been called successfully first.
     *
     * @param filename The path to the output file.
     * @return True if writing was successful, false otherwise.
     */
    bool outputPlan(const std::string &filename) const;

    /**
     * Writes the internally stored final plan string to the specified output stream.
     * Requires generatePlan() to have been called successfully first.
     *
     * @param os The output stream (defaults to std::cout).
     */
    void outputPlan(std::ostream &os = std::cout) const;

    /**
     * Gets the internally stored final plan string.
     * Requires generatePlan() to have been called successfully first.
     * Returns an empty string if generatePlan() failed or hasn't been called.
     *
     * @return A const reference to the final plan string.
     */
    const std::string& getPlanString() const;

    const int getPlanSize() const;
};

#endif // PLAN_MANAGER_H
