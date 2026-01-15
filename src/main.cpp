
#include <iostream>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <cstdlib>
#include <sys/wait.h>
#include <exception>
#include <execinfo.h>
#include <signal.h>
#include <exception>

#include "util/log.h"
#include "util/params.h"
#include "util/timer.h"
#include "util/signal_manager.h"
#include "util/statistics.h"
#include "util/stacktrace.h" // Include the stacktrace utility
#include "data/htn_instance.h"
#include "algo/planner.h"
#include "util/dag_compressor.h"

#ifndef TREEREX_VERSION
#define TREEREX_VERSION "(dbg)"
#endif

#ifndef IPASIRSOLVER
#define IPASIRSOLVER "(unknown)"
#endif


const char* TREEREX_ASCII = 
" A#######B                      A######B                \n"
"    A#B    A#####B  A######B A######B A#B     A#B A######B A#B    A#B \n"
"    A#B    A#B    A#B A#B      A#B      A#B     A#B A#B       A#B  A#B  \n"
"    A#B    A#B    A#B A#####B  A#####B  A######B  A#####B    A##B   \n"
"    A#B    A#####B  A#B      A#B      A#B   A#B   A#B        A##B   \n"
"    A#B    A#B   A#B  A#B      A#B      A#B    A#B  A#B       A#B  A#B  \n"
"    A#B    A#B    A#B A######B A######B A#B     A#B A######B A#B    A#B \n";
                                                

void outputBanner(bool colors) {

    const char* ascii = TREEREX_ASCII;

    for (size_t i = 0; i < strlen(ascii); i++) {
        char c = ascii[i];
        switch (c) {
        case 'A': if (colors) std::cout << Modifier(Code::FG_GREEN).str(); break;
        case 'B': if (colors) std::cout << Modifier(Code::FG_CYAN).str(); break;
        case 'C': if (colors) std::cout << Modifier(Code::FG_LIGHT_BLUE).str(); break;
        case 'D': if (colors) std::cout << Modifier(Code::FG_LIGHT_YELLOW).str(); break;
        default : std::cout << std::string(1, c);
        }
    }
    std::cout << Modifier(Code::FG_DEFAULT).str();
}



void handleSignal(int signum) {
    
    // Print stack trace if signal is SIGSEGV or SIGABRT
    if (signum == SIGSEGV || signum == SIGABRT) {
        Log::e("Received signal %d (%s)\n", signum, strsignal(signum));
        StackTrace::print_stacktrace("Signal handler");

        // exit immediately
        std::exit(EXIT_FAILURE);
    }
    SignalManager::signalExit();
}

void run(Parameters& params) {

    Statistics::getInstance().beginTiming(TimingStage::TOTAL);

    // Parse, ground the problem and create the relevant HDDL structures
    HtnInstance htn(params);

    // Create the planner and find a plan
    Planner planner(htn);
    int result = planner.findPlan();

    Statistics::getInstance().endTiming(TimingStage::TOTAL);
    Statistics::getInstance().printStats();

    if (result == 0 && !params.isNonzero("cleanup")) {
        // Exit directly -- avoid to clean up :)
        Log::i("Exiting happily (no cleaning up).\n");
        exit(result);
    }
    Log::i("Exiting happily.\n");
    return;
}

int main(int argc, char** argv) {

    // Set the custom terminate handler early
    // std::set_terminate(my_terminate_handler);

    // Register signal handlers
    signal(SIGTERM, handleSignal); // Termination request
    signal(SIGINT, handleSignal);  // Interrupt (Ctrl+C)
    signal(SIGSEGV, handleSignal); // Segmentation fault
    signal(SIGABRT, handleSignal); // Abort signal (often from assert or std::terminate)


    Timer::init();

    // TO TEST 
    compressed_dag_test();
    // END TEST

    Parameters params;
    params.init(argc, argv);

    int verbosity = params.getIntParam("v");
    Log::init(verbosity, /*coloredOutput=*/params.isNonzero("co"));

    if (verbosity >= Log::V2_INFORMATION) {
        outputBanner(params.isNonzero("co"));
        Log::log_notime(Log::V0_ESSENTIAL, "T r e e R e x");
        Log::log_notime(Log::V0_ESSENTIAL, "  version %s\n", TREEREX_VERSION);
        Log::log_notime(Log::V0_ESSENTIAL, "using SAT solver %s\n", IPASIRSOLVER);
        Log::log_notime(Log::V0_ESSENTIAL, "\n");
        
    }

    if (params.isSet("h") || params.isSet("help")) {
        params.printUsage();
        exit(0);
    }

    if (params.getProblemFilename() == "") {
        Log::w("Please specify both a domain file and a problem file. Use -h for help.\n");
        exit(1);
    }

    // Removed the try...catch block. Let uncaught exceptions trigger std::terminate.
    run(params);

    return 0; // Normal exit if run() completes without uncaught exceptions
}
