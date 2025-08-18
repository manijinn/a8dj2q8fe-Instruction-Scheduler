#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <deque>
using namespace std;

int S = 0; // Scheduling queue size.
int N = 0; // Max size for queue sizes.

int cycle = 0; // Cycle that program is on
string filename;
ifstream file;
int tag_counter = 0; // Tag counter. Used to keep track of incrementing tags.

enum state_list {
    IF,     //Fetch State
    ID,     //Dispatch State
    IS,     //Issue State
    EX,     //Execute State
    WB,     //Write Back State
};

class instruction {
    public:
    enum state_list state;
    int tag;
    int address;
    int operation;
    string dest;
    string src1;
    string src2;
    string dest_o; // <operand> is the operand that gets renamed, <operand>_o stores the original register name.
    string src1_o;
    string src2_o;

    // These cycle counters keep track of when an instruction changes their state.
    //exec_latency is the timer for that instruction in the EX state.
    int IF_duration = 0; 
    int IF_cycle = 0;
    int ID_cycle = 0; 
    int IS_cycle = 0; 
    int EX_cycle = 0; 
    int WB_cycle = 0; 
    int exec_latency = 0;

    // These flags tells us the src operands of an instruction are good to go.
    int src1_flag = 1;
    int src2_flag = 1;

    // Keep track of any instruction using this current's instructions registers.
    // In other words, keep track of the instructions dependencies.
    instruction *depend_1_ins = NULL;
    instruction *depend_2_ins = NULL;
    string depend_1_tag = "";
    string depend_2_tag = "";

    instruction (int address, int operation, string dest, string src1, string src2) { 
        this->address = address;
        this->operation = operation;

        this->dest = dest;
        this->src1 = src1;
        this->src2 = src2;
        this->dest_o = dest;
        this->src1_o = src1;
        this->src2_o = src2;

        this->state = IF;
        this->tag = tag_counter;
        this->IF_cycle = cycle;
        this->IF_duration = 1;

        if (operation == 0) {
            exec_latency = 1;
        }
        else if (operation == 1) {
            exec_latency = 2;
        }
        else {
            exec_latency = 5;
        }
    };
};
void FakeRetire();
void Execute();
void Issue();
void Dispatch();
void Fetch();
bool Advance_Cycle();
bool isEmpty();
void init_RF();
void ClearROB();
void RenameOps(instruction *);

// Pointers are useful for a project like this since it allows us to access the data easily anywhere, anytime, between any data structure.
// These vectors store instructions in particular states (e.g. instruction state == Is will be in the issue vector).
vector<instruction*> dispatch(0);
vector<instruction*> issue(0);
vector<instruction*> execute(0);
vector<instruction*> fakeROB(0);
vector<instruction*> disposal(0);

// Register files, one stores a valid bit for that register, the other stores the address of the instruction using that register.
int RF_valid[128];
instruction* RF_ins[128];

int main(int argc, const char * argv[]) {
    S = stoi(argv[1]);
    N = stoi(argv[2]);
    filename = argv[3];
    file.open(filename);

    init_RF();
    do {
        FakeRetire();
        Execute();
        Issue();
        Dispatch();
        if (file.is_open()) {
            Fetch();
        }
    } while(Advance_Cycle());
    cout << "number of instructions = " << tag_counter << endl;
    cout << "number of cycles       = " << cycle << endl;
    cout << std::fixed << std::setprecision(5) << "IPC                    = " << float(tag_counter) / float(cycle) << endl;

    ClearROB();
    return 0;
}

// Initialize register files with appropriate starting values.
void init_RF() {
    for (int i = 0; i < 128; i++) {
        RF_valid[i] = 1; // 1 means resgister is unused or ready.
    }
    for (int i = 0; i < 128; i++) {
        RF_ins[i] = NULL; // 1 means resgister is unused or ready.
    }
}

// Clear the disposal vector which contains all "deleted" instructions from the fakeROB.
void ClearROB() {
    for (instruction *ins : disposal) {
        delete ins;
    }
}

// Check fakeROB for any instructions in the WB state and remove them.
void FakeRetire() {
    if (fakeROB.empty()) return;

    for (auto it = fakeROB.begin(); it != fakeROB.end();) {
        if ((*it)->state != WB) return;

        cout << (*it)->tag << " fu{" << (*it)->operation << "} src{" << (*it)->src1_o << ","  << (*it)->src2_o << "} dst{" << (*it)->dest_o 
        << "} IF{"  << (*it)->IF_cycle << "," << (*it)->IF_duration 
        << "} ID{"  << (*it)->ID_cycle << "," << (*it)->IS_cycle - (*it)->ID_cycle
        << "} IS{" << (*it)->IS_cycle  << "," << (*it)->EX_cycle - (*it)->IS_cycle
        << "} EX{" << (*it)->EX_cycle << "," << (*it)->WB_cycle - (*it)->EX_cycle
        << "} WB{" << (*it)->WB_cycle << "," << 1 << "}" << endl;
        
        // We don't delete from the fakeROB, instead we push it to a disposal vector to be cleared at the program's end.
        disposal.push_back((*it));
        it = fakeROB.erase(it);
    }
}

// Execute the instructions.
void Execute() {
    if (execute.empty()) return;

    // Scan each instruction in the execute vector. If their exec_latency is at 0, it's done executing.
    // Then. check to see if its dest register is an actual register (not "-1"). If yes, find it in the RF and mark it as available.
    for (auto it = execute.begin(); it != execute.end();) {
        if ((*it)->exec_latency <= 0) {
            if ((*it)->dest_o != "-1") {
                if (RF_ins[stoi((*it)->dest_o)] == (*it)) {
                    RF_valid[stoi((*it)->dest_o)] = 1;
                }
            }
            (*it)->state = WB;
            (*it)->WB_cycle = cycle;
            
            it = execute.erase(it);
        }
        // If the timer != 0, delay the isntructon by another cycle and decrement its counter.
        else {
            (*it)->exec_latency--; 
            it++;
        }
    }
}

// Issue the instructions.
void Issue() {
    int count = 0;
    
    // Sort the instructions by tag. This was specified in the directions, albeit we don't use a temp data structure.
    /*std::sort(issue.begin(), issue.end(), [](instruction *a, instruction *b) {  // Sort by tag, ascending order.
        return a->tag < b->tag;
    });*/
    
    for (auto it = issue.begin(); it != issue.end();) {
        if (count > N) break;
        
        // If the instruction source opearnds are ready (1), or it's dependencies are in the WB state, instruction is good to go.
        if (((*it)->src1_flag == 1 || (*it)->depend_1_ins->state == WB) && ((*it)->src2_flag == 1 || (*it)->depend_2_ins->state == WB)) {
            (*it)->state = EX;
            (*it)->EX_cycle = cycle;
            (*it)->exec_latency--;

            execute.push_back(*it);
            it = issue.erase(it);
            count++;
        }
        // Else, we skip the instruction for now.
        else {
            it++;
        }
    }
}

// Dispatch the instructions
void Dispatch() {
    if (issue.size() < S && !dispatch.empty()) {
        for (auto it = dispatch.begin(); it != dispatch.end();) {    
            
            // We dispatch S amount of insturctions to issue. Or, if we reach an instruction in the IF state.
            if (issue.size() >= S || (*it)->state == IF) break;
            RenameOps(*it);
            
            (*it)->state = IS;
            (*it)->IS_cycle = cycle;
            
            issue.push_back(*it);
            it = dispatch.erase(it);  
            
        }
    }
    // We set N instructions to dispatch (emulates dispatching N instructions).
    int i = 0;
    for (instruction *ins : dispatch) {
        if (i >= N) break;
        if (ins->state == IF) {
            ins->state = ID;
            ins->ID_cycle = cycle;
            i++;
        }
    }
}

// Where the actual renaming takes place.
// If any one of the instruction's register is marked as -1, it doesn't have that register, and so there's nothing to rename.
void RenameOps(instruction *dispatched) {
    if (dispatched->dest_o != "-1") {
        dispatched->dest = to_string(dispatched->tag);
    }
    if (dispatched->src1_o != "-1") {
        dispatched->src1 = to_string(dispatched->tag);
    }
    if (dispatched->src2_o != "-1") {
        dispatched->src2 = to_string(dispatched->tag);
    }
}

// Get instructions. We fetch N instructions at a time, but dispatch holds 2N instructions.
void Fetch() {
    // For reference:
    // <PC> <operation type> <dest reg #> <src1 reg #> <src2 reg #>
    int address;
    int operation;
    int tag;
    string dest;
    string src1;
    string src2;
    string line;
    int count = 0;

    // TEST without fakeROB
    // Check if the dispatch queue is full. If yes, we skip fetching for the time-being.
    while (dispatch.size() < 2*N && count < N) {
        if (file.eof()) { // If the file has reached the end, we close it.
            file.close();
            return;
        }
        getline(file, line);
        if (line.empty()) { // Each trace file has a weird empty line at the end which throws off the entire code, so we check for empty lines.
            file.close();
            return;
        }
        stringstream ss(line);
        string temp;
        ss >> temp;
        address = stoi(temp, nullptr, 16); // Interpret string as hex and store it as integer, might have to change this to store as actual hex.
        ss >> temp;
        operation = stoi(temp);
        ss >> dest;
        ss >> src1;
        ss >> src2;

        instruction *ins = new instruction(address, operation, dest, src1, src2);

        if (ins->src1 != "-1") {
            if (RF_valid[stoi(ins->src1)] == 0) {   // Access RF to see if that register is being used, 0 = yes
                ins->depend_1_tag = RF_ins[stoi(ins->src1)]->tag;   // Store tag of dependency into current instruction.
                ins->depend_1_ins = RF_ins[stoi(ins->src1)];        // Similarily, store the address of dependency into instruction.
                ins->src1_flag = 0;     // Set src1 flag to 0 meaning it's not ready.
            }
            if (RF_valid[stoi(ins->src1)] == 1) {   // src1 register is not being used.
                ins->depend_1_tag = "self";     // We set the dependency of the instruction to itself. Though doing so is inconsequential in the sim.
                ins->depend_1_ins = NULL;       // We set the dependency address of the instruction to NULL. Though doing so is inconsequential in the sim.
                ins->src1_flag = 1;     // Instruction's src1 is good to go!
            }
        }

        // The code is the same as above for src1, but for src2 instead.
        if (ins->src2 != "-1") {
            if (RF_valid[stoi(ins->src2)] == 0) { // access RF to see if that register is being used, 0 = yes
                ins->depend_2_tag = RF_ins[stoi(ins->src2)]->tag; // store tag into varaible
                ins->depend_2_ins = RF_ins[stoi(ins->src2)];
                ins->src2_flag = 0;
            }
            if (RF_valid[stoi(ins->src2)] == 1) {
                ins->depend_2_tag = "self"; 
                ins->depend_2_ins = NULL;
                ins->src2_flag = 1;
            }
        }

        // Here we set the the insturciton as the latest user of the des register by inserting it's address into RF_ins file.
        // We also thus mark the register as 0 (being used) in the RF.
        RF_valid[stoi(ins->dest)] = 0;
        RF_ins[stoi(ins->dest)] = ins;
            
        // We push the instruction to the dispatch vector and the fakeROB. It is now in the pipeline.
        ins->IF_cycle = cycle;
        dispatch.push_back(ins);    // Add to dispatch queue.
        fakeROB.push_back(ins);     // Add to fakeROB
        tag_counter++;
        count++;
    }
}

bool Advance_Cycle() {
    if(isEmpty()) //If there are no more instructions left, end calculations
        return false;
    else { //If there are more instructions to calculate, increment to next cycle and continue
        cycle++;
        return true;
    }
}

//Checks if fakeROB empty. This means there are no longer any instructons left in the pipeline!
bool isEmpty() {
    if (fakeROB.size() != 0) return false;
    if (execute.size() != 0) return false;
    if (issue.size() != 0) return false;
    if (dispatch.size() != 0) return false;
    if (file.is_open()) return false;
    
    return true;
}