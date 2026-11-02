#ifndef PIPELINE_SIMULATOR_H
#define PIPELINE_SIMULATOR_H

#include <iostream>
#include <string>
#include <vector>

struct Operation {
    int id;
    int memoryCycles;
    int calculationCycles;
};

class PipelineSimulator {
public:
    int run(const std::vector<Operation>& ops) {
        int n = static_cast<int>(ops.size());
        if (n == 0) return 0;

        std::vector<int> mem(n), calc(n, 0);
        std::vector<bool> memDone(n, false);
        std::vector<bool> calcStarted(n, false);
        std::vector<bool> done(n, false);

        for (int i = 0; i < n; ++i) {
            mem[i] = ops[i].memoryCycles;
        }

        int nextMem = 0, completed = 0, cycle = 0;

        std::cout << "Cycle | Memory Port | Calculation Port\n";
        std::cout << "---------------------------------------\n";

        while (completed < n) {
            ++cycle;
            std::string memAction = "Idle";
            std::string calcAction = "Idle";

            // Calculation port continues an active calculation.
            for (int i = 0; i < n; ++i) {
                if (calcStarted[i] && !done[i] && calc[i] > 0) {
                    calcAction = "O" + std::to_string(ops[i].id);
                    --calc[i];

                    if (calc[i] == 0) {
                        done[i] = true;
                        ++completed;
                    }
                    break;
                }
            }

            // Start a calculation whose memory phase has completed.
            bool calcPortBusy = calcAction != "Idle";

            if (!calcPortBusy) {
                for (int i = 0; i < n; ++i) {
                    if (memDone[i] && !calcStarted[i] && !done[i]) {
                        calcStarted[i] = true;
                        calcAction = "O" + std::to_string(ops[i].id);

                        if (ops[i].calculationCycles == 0) {
                            done[i] = true;
                            ++completed;
                        } else {
                            calc[i] = ops[i].calculationCycles - 1;

                            if (calc[i] == 0) {
                                done[i] = true;
                                ++completed;
                            }
                        }
                        break;
                    }
                }
            }

            // Memory port processes one operation at a time.
            if (nextMem < n) {
                memAction = "O" + std::to_string(ops[nextMem].id);
                --mem[nextMem];

                if (mem[nextMem] == 0) {
                    memDone[nextMem] = true;
                    ++nextMem;
                }
            }

            std::cout << cycle << "     | "
                      << memAction << "           | "
                      << calcAction << '\n';
        }

        std::cout << "\nTotal cycles: " << cycle << '\n';
        return cycle;
    }
};

#endif