
#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

#include <stdlib.h>

class SignalManager
{

private:
    static bool exiting;
    static int numSignals;

public:
    static inline void signalExit()
    {
        exiting = true;
        numSignals++;
        if (numSignals == 1)
        {
            // Forced exit
            exit(0);
        }
    }
    static inline bool isExitSet()
    {
        return exiting;
    }
};

#endif
