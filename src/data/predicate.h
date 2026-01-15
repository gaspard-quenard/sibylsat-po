#ifndef PREDICATE_H
#define PREDICATE_H

#include <string>

class Predicate
{

private:
    int _id;
    bool _positive;
    std::string _name;

public:
    Predicate(int id, bool positive, std::string name) : _id(id), _positive(positive), _name(name) {}

    // Getter
    const std::string getName() const
    {
        return _name;
    }
};

#endif // PREDICATE_H