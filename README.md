# SibylSat-PO

## Overview

SibylSat-PO is an incremental SAT-based planner for partially-ordered HTN planning problems.
### Valid Inputs

SibylSat-PO operates on partially-ordered HTN planning problems given as HDDL files [0]. The provided HTN domain may be recursive or non-recursive.

In short, SibylSat-PO supports exactly the HDDL specification from the International Planning Competition (IPC) 2020 provided in [1] and [2].
It handles type systems, STRIPS-style actions with positive and negative conditions, method preconditions, equality and "sort-of" constraints, and universal quantifications in preconditions and effects.
SibylSat-PO _cannot_ handle conditional effects, existential quantifications, or any other extended formalisms going beyond the mentioned concepts.

### Output

SibylSat-PO outputs a plan in accordance to [3]. Basically everything in between "`==>`" and "`<==`" is the found plan, and everything inside that plan in front of the word `root` is the sequence of classical actions to execute. 

## Building

SibylSat-PO use both [pandaPigrounder](https://github.com/panda-planner-dev/pandaPIgrounder) and [pandaPiParser](https://github.com/panda-planner-dev/pandaPIparser) which require the following dependancies:
- `g++` (C++17 support)
- `make`
- `flex`  (version 2.6 or higher)
- `bison` (versions 3.5.1 and 3.7.2 are known to work)
- `zip`
- `zlib1g-dev`
- `gengetopt` (tested with version 2.23)


You can build SibylSat-PO like this:

```
make
```

## Usage

SibylSat uses the HDDL file format.

Execute the planner executable like this:
```bash
./sibylsat-po path/to/domain.hddl path/to/problem.hddl [options]
```

## Benchmarks

SibylSat includes benchmark sets from the totally-ordered HTN [IPC 2023](https://github.com/ipc2023-htn/ipc2023-domains). These benchmarks are included as submodules in the `Benchmarks` folder. To obtain the benchmarks, you will need to initialize the submodules as follows:

```bash
git submodule update --init --recursive
```

The IPC 2023 benchmarks are located in Benchmarks/ipc2023-domains.


## License

The code of SibylSat-PO is published under the GNU GPLv3. Consult the LICENSE file for details.  
The planner uses [pandaPIparser](https://github.com/panda-planner-dev/pandaPIparser) and [pandaPIgrounder](https://github.com/panda-planner-dev/pandaPIgrounder) [0] which are [3-Clause BSD](https://opensource.org/license/bsd-3-clause) licensed.


---

[0] Behnke, G., Höller, D., Schmid, A., Bercher, P., & Biundo, S. (2020). [**On Succinct Groundings of HTN Planning Problems.**](https://www.uni-ulm.de/fileadmin/website_uni_ulm/iui.inst.090/Publikationen/2020/AAAI-BehnkeG.1770.pdf) In AAAI (pp. 9775-9784).

[1] Höller, D., Behnke, G., Bercher, P., Biundo, S., Fiorino, H., Pellier, D., & Alford, R. (2020). [**HDDL: An Extension to PDDL for Expressing Hierarchical Planning Problems.**](https://www.uni-ulm.de/fileadmin/website_uni_ulm/iui.inst.090/Publikationen/2020/Hoeller2020HDDL.pdf) In AAAI (pp. 9883-9891).

[2] Behnke, G. et al. (2020). [**HDDL - Addendum.**](http://gki.informatik.uni-freiburg.de/competition/hddl.pdf) Universität Freiburg.

[3] Behnke, G. et al. (2020). [**Plan verification.**](http://gki.informatik.uni-freiburg.de/ipc2020/format.pdf) Universität Freiburg.

