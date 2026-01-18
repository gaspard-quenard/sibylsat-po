import os
import subprocess
import logging
import shlex
import time
import resource # Added for memory limiting
from colorama import init, Fore

TIMEOUT_S = 60
MEMORY_LIMIT_GB = 25 # Limit subprocess memory to 4 GB
MEMORY_LIMIT_BYTES = MEMORY_LIMIT_GB * 1024 * 1024 * 1024

# Function to set memory limit for the child process
def set_memory_limit():
    try:
        resource.setrlimit(resource.RLIMIT_AS, (MEMORY_LIMIT_BYTES, MEMORY_LIMIT_BYTES))
    except Exception as e:
        # Log error if limit setting fails, but don't stop the script
        logging.error(f"Failed to set memory limit: {e}")


PATH_BENCHMARKS = [
    ("Benchmarks/ipc2023-domains/partial-order/Transport", 15),
    ("Benchmarks/ipc2023-domains/partial-order/Barman-BDI", 2),
    ("Benchmarks/ipc2023-domains/partial-order/Rover", 10),
    ("Benchmarks/ipc2023-domains/partial-order/Satellite", 20),
    ("Benchmarks/ipc2023-domains/partial-order/UM-Translog", 10),
    ("Benchmarks/ipc2023-domains/partial-order/PCP", 1),
    ("Benchmarks/ipc2023-domains/partial-order/Woodworking", 10),
    
    # NOT FUNCTIONNAL ??
    # ("Benchmarks/ipc2023-domains/total-order/Woodworking", 10),
    
    # NOT TESTED
    # ("Benchmarks/ipc2023-domains/total-order/Freecell-Learned-ECAI-16", 1),
    # ("Benchmarks/ipc2023-domains/total-order/Childsnack", 10),
    # ("Benchmarks/ipc2023-domains/total-order/Snake", 1),
    # ("Benchmarks/ipc2023-domains/total-order/SharpSAT/SharpSAT", 2),
    # ("Benchmarks/ipc2023-domains/total-order/Multiarm-Blocksworld", 1),
    # ("Benchmarks/ipc2023-domains/total-order/Blocksworld-HPDDL", 1),
    # ("Benchmarks/ipc2023-domains/total-order/Monroe-Partially-Observable", 1),
    # ("Benchmarks/ipc2020-domains/total-order/Elevator-Learned-ECAI-16", 20),
    # ("Benchmarks/ipc2020-domains/total-order/Minecraft-Player", 30),
    # ("Benchmarks/ipc2023-domains/total-order/Logistics-Learned-ECAI-16", 8),
    # ("Benchmarks/ipc2023-domains/total-order/Entertainment", 3),    
]


planner_config = "./build/treerex {domain_path} {problem_path} -po -sibylsat"


if __name__ == "__main__":


    # Initialize colorama
    init(autoreset=True)

    # Initialize logging
    logging.basicConfig(
        format='%(asctime)s %(levelname)s:%(message)s', level=logging.DEBUG)

    # Add the option to verify the plan if not already present
    if (not "-vp=1" in planner_config):
        print(f"{Fore.MAGENTA}Automatically added the option '-vp=1' to verify the plan{Fore.RESET}")
        planner_config += " -vp=1"

    initial_time_all = time.time()

    number_instances_checked = 0

    number_of_benchmarks = len(PATH_BENCHMARKS)
    idx_benchmark = 0

    for (path_benchmark, highest_instance_to_solve) in PATH_BENCHMARKS:

        idx_benchmark += 1

        name_benchmark = path_benchmark.split('/')[-1]
        logging.info(
            f"Test benchmark {name_benchmark} ({idx_benchmark}/{number_of_benchmarks})")
        

        # Load all the problems
        all_files_in_benchmark = sorted(os.listdir(path_benchmark))

        full_path_benchmark = os.path.abspath(path_benchmark)

        # Remove all files which do not end with hddl
        files_in_benchmark = [f for f in all_files_in_benchmark if f.endswith("hddl") or f.endswith("pddl")]

        # Get all the domain files
        domain_files = [f for f in files_in_benchmark if "domain" in f.lower() and f.endswith("hddl")]

        # Remove all the files which can contains the "domain" keyword with any case
        files_in_benchmark = [f for f in files_in_benchmark if not "domain" in f.lower()]

        # Keep only the first 2 level of each benchmark
        num_level_to_keep = min(highest_instance_to_solve, len(files_in_benchmark))

        

        initial_time = time.time()
        for i in range(num_level_to_keep):
            files_in_benchmark[i] = files_in_benchmark[i]

            # Check if the domain file exists
            domain_file_name = "domain.hddl"
            if not domain_file_name in domain_files:
                # Check if the domain is the name of the problem + "-domain.hddl"
                domain_file_name = files_in_benchmark[i].split('.')[0] + "-domain.hddl"
                if not domain_file_name in domain_files:
                    logging.error(f"{Fore.RED}Benchmark {name_benchmark} is NOK because domain file is not found{Fore.RESET}")
                    exit(1)

            # Launch planner to test is OK
            command = planner_config.format(domain_path=os.path.join(full_path_benchmark, domain_file_name), problem_path=os.path.join(full_path_benchmark, files_in_benchmark[i]))
            print(command)
            try:
                # Execute the command with timeout and memory limit
                output = subprocess.run(
                    shlex.split(command),
                    check=False,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    universal_newlines=True,
                    timeout=TIMEOUT_S,
                    preexec_fn=set_memory_limit # Set memory limit before execution
                )
                return_code = output.returncode
            except subprocess.TimeoutExpired:
                logging.error(f"{Fore.YELLOW}Benchmark {name_benchmark}, Problem {files_in_benchmark[i]} TIMEOUT ({TIMEOUT_S}s){Fore.RESET}")
                # Decide if timeout should be considered NOK or just skipped
                # For now, let's treat it as NOK for safety, similar to how the original script handles errors.
                # If you want to just skip and continue, remove the exit(1) below.
                exit(1) 
            except MemoryError: # This might catch memory errors in the parent, preexec_fn handles child
                 logging.error(f"{Fore.RED}Benchmark {name_benchmark}, Problem {files_in_benchmark[i]} Parent process Memory Error{Fore.RESET}")
                 exit(1)
            # Note: If the child process exceeds the memory limit set by setrlimit,
            # it will likely be killed by the OS (e.g., with SIGKILL).
            # subprocess.run might return a negative return code like -9 (SIGKILL).

            if output.returncode < 0: # Check for signals like SIGKILL (-9)
                 logging.error(f"{Fore.RED}Benchmark {name_benchmark}, Problem {files_in_benchmark[i]} likely killed due to memory limit (return code: {output.returncode}){Fore.RESET}")
                 exit(1)

            if (return_code != 0):
                logging.error(f"{Fore.RED}Benchmark {name_benchmark} is NOK{Fore.RESET}")
                exit(1)

            # Get the size of the plan from the output (line with End of solution plan. (counted length of <size_plan>))
            output_str = output.stdout
            output_str = output_str.split('\n')
            
            # Confirm that the plan has been verified as correct (contains the line "Plan has been verified by pandaPIparser")
            verified = False
            for line in output_str:
                if "Plan has been verified by pandaPIparser" in line:
                    verified = True
                    break
            if not verified:
                logging.error(f"{Fore.RED}Benchmark {name_benchmark} is NOK because the plan of problem {files_in_benchmark[i]} has not been verified{Fore.RESET}")
                exit(1)
            
            
            size_plan = 0
            for line in output_str:
                if "End of solution plan. (counted length of" in line:
                    size_plan = int(line.split(' ')[-1][:-1])
                    break

            # Print the size of the plan
            # logging.info(f"Size of the plan: {size_plan}")
            number_instances_checked += 1

        final_time = time.time()
        logging.info(
            f"{Fore.GREEN}Benchmark {name_benchmark} is OK{Fore.RESET} (done in {round(final_time - initial_time, 2)} s)")
        


    final_time_all = time.time()
    # Get the time with 2 decimals
    time_all = round(final_time_all - initial_time_all, 2)

    logging.info(
        f"{Fore.GREEN}All the tests are OK{Fore.RESET} (Check {number_instances_checked} problems) (done in {time_all} s)")
