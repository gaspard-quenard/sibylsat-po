#include "util/names.h"

#include <vector>

std::vector<Predicate> *g_predicates;
std::vector<Action> *g_actions;
std::vector<AbstractTask> *g_abstr_tasks;
std::vector<Method> *g_methods;
const Action *g_blankAction;
const Action *g_init_action;
const Action *g_goal_action;

namespace Names
{
    void init(std::vector<Predicate> &predicates, std::vector<Action> &actions, std::vector<AbstractTask> &abstr_tasks, std::vector<Method> &methods, const Action *blankAction, const Action *init_action, const Action *goal_action)
    {
        g_predicates = &predicates;
        g_actions = &actions;
        g_abstr_tasks = &abstr_tasks;
        g_methods = &methods;
        g_blankAction = blankAction;
        g_init_action = init_action;
        g_goal_action = goal_action;
    }

    std::string to_string(const Action &a)
    {
        std::string out;
        // First, print all the preconditions
        // for (int idx : a.getPreconditionsIdx()) {
        //     out += g_predicates->at(idx).getName() + " ";
        // }
        // out += " { " + a.getName() + " } ";
        out += a.getName();
        // Next, print all the positive effects
        // for (int idx : a.getPosEffsIdx()) {
        //     out += g_predicates->at(idx).getName() + " ";
        // }
        // // And finally, print all the negative effects
        // for (int idx : a.getNegEffsIdx()) {
        //     out += "not " + g_predicates->at(idx).getName() + " ";
        // }
        return out;
    }

    std::string to_string(const AbstractTask &at)
    {
        std::string out = at.getName();
        // out + " { ";
        // for (int idx : at.getDecompositionMethodIdx()) {
        //     out += g_methods->at(idx).getName() + " ";
        // }
        // out += "}";
        return out;
    }

    std::string to_string(const Method &m)
    {
        std::string out = m.getName();
        // out +=  " { ";
        // for (int idx : m.getSubtasksIdx()) {
        //     if (idx > g_actions->size()) {
        //         out += g_abstr_tasks->at(idx - g_actions->size()).getName() + " ";
        //     } else {
        //         out += g_actions->at(idx).getName() + " ";
        //     }
        // }
        // out += "}";
        return out;
    }

    std::string to_string(const Predicate &p)
    {
        return p.getName();
    }

    std::string to_string(const PdtNode &node)
    {
        // std::string out = "Node__" + node.getPositionString() + "\n";
        // std::string out = node.getName() + "\n";
        // out += "Methods: \n";
        // for (int idx : node.getMethodsIdx()) {
        //     out += to_string(g_methods->at(idx)) + "\n";
        // }
        // out += "Actions: \n";
        // for (int idx : node.getActionsIdx()) {
        //     if (idx == g_blankAction->getId()) {
        //         out += to_string(*g_blankAction) + "\n";
        //     } else {
        //         out += to_string(g_actions->at(idx)) + "\n";
        //     }
        // }
        // return out;
        return node.getName();
    }

    std::string to_string(const OrderingConstrains &oc)
    {
        switch (oc)
        {
        case OrderingConstrains::SIBLING_NO_ORDERING:
            return "SIBLING_NO_ORDERING";
        case OrderingConstrains::SIBLING_ORDERING:
            return "SIBLING_ORDERING";
        case OrderingConstrains::NO_SIBLING_NO_ORDERING:
            return "NO_SIBLING_NO_ORDERING";
        case OrderingConstrains::NO_SIBLING_ORDERING:
            return "NO_SIBLING_ORDERING";
        default:
            return "UNKNOWN";
        }
    }
}