#ifndef HTN_INSTANCE_H
#define HTN_INSTANCE_H

#include <vector>
#include <string>
#include <optional>
#include <fstream>
#include <unordered_set>
#include <map> // Added for std::map
#include "data/action.h"
#include "data/method.h"
#include "data/abstract_task.h"
#include "util/params.h"
#include "data/mutex.h"
#include "util/statistics.h"

class HtnInstance
{
private:
    Parameters &_params;
    Statistics& _stats;

    Mutex _mutex;
    const bool _partial_order_problem = _params.isNonzero("po");

    std::vector<Predicate> _predicates;
    std::vector<Action> _actions;
    std::vector<AbstractTask> _abstr_tasks;
    std::vector<Method> _methods;
    int _id_blank_action = -1;
    int _id_init_action = -2;
    int _id_goal_action = -3;
    Action *_init_action;
    Action *_goal_action;
    Action *_blankAction;

    int _root_task_idx;
    std::unordered_set<int> _init_state;
    std::unordered_set<int> _goal_state;
    std::vector<int> _all_fact_vars_goal;

    std::unordered_map<int, int> _methods_to_precondition_action;

    // For grouping methods by subtask structure (number of subtasks and their ordering constraints)
    std::map<int, int> _method_to_structure_id; // method_id -> structure_id
    // Maps a canonical representation of a structure (num_subtasks, sorted_ordering_constraints) to a unique structure_id
    std::map<std::pair<int, std::vector<std::pair<int, int>>>, int> _canonical_structure_to_id;
    // Maps structure_id to its details {num_subtasks, canonical_ordering_constraints}.
    std::map<int, std::pair<int, std::vector<std::pair<int, int>>>> _structure_id_to_details;
    int _next_structure_id = 0; // Counter for generating unique structure_ids

    /**
     * Parse the domain and problem files using pandaPIparser.
     *
     * @param domain_filename The domain file to parse.
     * @param problem_filename The problem file to parse.
     * @return The filepath of the parsed problem, or std::nullopt if parsing fails.
     */
    std::optional<std::string> parseProblem(const std::string &domain_filename, const std::string &problem_filename);

    /**
     * Ground the parsed problem using pandaPIgrounder.
     *
     * @param parsed_problem_filepath The parsed problem file to ground.
     * @return The filepath of the grounded problem, or std::nullopt if grounding fails.
     */
    std::optional<std::string> groundProblem(const std::string &parsed_problem_filepath);

    /**
     * Load the grounded problem from file and populate internal structures.
     *
     * @param grounded_problem_filepath The grounded problem file to load.
     */
    void loadGroundedProblem(const std::string &grounded_problem_filepath);

    /**
     * Skip lines in the file until a specific target line is found.
     *
     * @param file The file to read from.
     * @param target The target line to search for.
     * @param line_idx The index of the current line (updated to the found line).
     */
    void skipUntil(std::ifstream &file, const std::string &target, int &line_idx);

    /**
     * Read a space-separated list of integers from a file.
     *
     * @param file The file to read from.
     * @param line_idx The current line index (incremented after reading).
     * @return A vector containing the parsed integers.
     */
    std::vector<int> parseIntegerList(std::ifstream &file, int &line_idx);

    /**
     * Extract predicates from the grounded problem and store them in `_predicates`.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractPredicates(std::ifstream &file, int &line_idx);

    /**
     * Extract actions from the grounded problem and store them in `_actions`.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractActions(std::ifstream &file, int &line_idx);

    /**
     * Extract mutexes from the grounded problem and store them in `_mutex`.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractMutexes(std::ifstream &file, int &line_idx);

    /**
     * Extract initial and goal states from the grounded problem.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractInitGoalStates(std::ifstream &file, int &line_idx);

    /**
     * Extract task names and populate `_abstr_tasks` and `_actions`.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractTasksNames(std::ifstream &file, int &line_idx);

    /**
     * Extract the root task index from the grounded problem file.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractInitRootTaskIdx(std::ifstream &file, int &line_idx);

    /**
     * Extract methods and store them in `_methods`.
     *
     * @param file The input file stream.
     * @param line_idx The index of the line to start extraction from.
     */
    void extractMethods(std::ifstream &file, int &line_idx);

    /**
     * Sort subtasks based on ordering constraints.
     *
     * @param subtasks_id The vector of subtasks to be sorted.
     * @param ordering_constraints The vector of ordering constraints.
     */
    void sortSubtasks(std::vector<int> &subtasks_id, std::vector<std::pair<int, int>> &ordering_constraints);

    /**
     * Use pandaPIplanner to compute the preconditions and effects of methods.
     * And store them in the methods.
     *
     * @param grounded_problem_filepath The path to the grounded problem file.
     *
     * @return True if the preconditions and effects were computed successfully, false otherwise.
     */
    bool getPrecsAndEffsMethods(std::string &grounded_problem_filepath);

    /**
     * Calculate the possible execution time steps [Earliest Start Time, Latest Finish Time] for each subtask based on ordering constraints.
     *
     * @param subtasks_ids Vector of IDs for the subtasks in the method.
     * @param ordering_constraints Vector of pairs representing precedence constraints (u < v).
     * @param method_name The name of the method (for logging).
     * @return A map where keys are subtask indices (0 to n-1) and values are vectors of possible time steps.
     *         Returns an empty map if there are no subtasks or a cycle is detected.
     */
    static std::unordered_map<int, std::vector<int>> calculateSubtaskTimeSteps( // Added static keyword
        const std::vector<int>& subtasks_ids,
        const std::vector<std::pair<int, int>>& ordering_constraints,
        const std::string& method_name);

    /**
     * Calculate the set of subtasks that must *directly* precede each subtask based on the given ordering constraints.
     * It inspects the reversed adjacency list derived from the constraints.
     *
     * @param num_subtasks The total number of subtasks in the method.
     * @param ordering_constraints Vector of pairs representing precedence constraints (u < v), using original indices.
     * @return A map where keys are subtask indices (0 to n-1) and values are vectors of indices of subtasks that *directly* precede them.
     *         Returns an empty map if there are no subtasks or an error occurs (e.g., index out of range).
     */
    std::unordered_map<int, std::vector<int>> calculateDirectPredecessors( // Added static keyword back
        int num_subtasks,
        const std::vector<std::pair<int, int>>& ordering_constraints);


public:
    /**
     * Constructor: Initializes and processes the HTN instance.
     *
     * @param params The parameters for parsing and grounding.
     */
    HtnInstance(Parameters &params);

    Parameters &getParams() const;

    const AbstractTask &getAbstractTaskById(int task_id) const;
    const Action &getActionById(int action_id) const;
    const Method &getMethodById(int method_id) const;
    const Predicate &getPredicateById(int pred_id) const;
    const bool isAbstractTask(int task_id) const;
    const Action &getBlankAction() const;
    const AbstractTask &getRootTask() const;
    const bool isRootTask(const AbstractTask &task) const;
    const std::vector<Predicate> &getPredicates() const;
    // Action getGoalAction() const;
    const std::unordered_set<int> &getInitState() const;
    const std::unordered_set<int> &getGoalState() const;
    const int getNumPredicates() const;
    const std::vector<int> &getFactVarsGoal() const;
    Mutex& getMutex() { return _mutex; }
    const bool isPartialOrderProblem() const { return _partial_order_problem; }
    const int getNumMethods() const; // Added getter
    const int getNumActions() const; // Added getter

    // Getters for method structure information
    int getMethodStructureId(int method_id) const;
    int getNumSubtasksForStructure(int structure_id) const;
    const std::vector<std::pair<int, int>>& getCanonicalOrderingConstraintsForStructure(int structure_id) const;

    void addInitAndGoalActionsToRootMethod();

    const bool methodContainsPreconditionAction(int method_id) const
    {
        return _methods_to_precondition_action.find(method_id) != _methods_to_precondition_action.end();
    }
    const int getPreconditionActionId(int method_id) const
    {
        auto it = _methods_to_precondition_action.find(method_id);
        if (it != _methods_to_precondition_action.end())
        {
            return it->second;
        }
        return -1; // Not found
    }
};

#endif // HTN_INSTANCE_H
