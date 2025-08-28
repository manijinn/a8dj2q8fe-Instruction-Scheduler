# a8dj2q8fe-Instruction-Scheduler
Instruction scheduling simulator program utilizing Tomasulo's algorithm. 
Made for a graduate Computer Architecture course. The program was tested with 10^4 addresses, but can handle much more if memory allows. It uses Tomasulo's algorithm and runs on a simulated superscalar processor. As expected with Tomasulo's algorithm, it issues and executes only those instructions which have their source operands free.
To use the script, run it with ./sim S N.
S is the size of the schedlung queue, and N is the instruction flow. Instruction flow means N instructions will be fetched and dispatched and N + 1 instructions will be issued to N + 1 functional units.
