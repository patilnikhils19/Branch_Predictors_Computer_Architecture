#include "pin.H"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <map>
#include <unistd.h> // for pid
#include <stdlib.h>
/* ===================================================================== */
/* Print Help Message                                                    */
/* ===================================================================== */
static INT32 Usage()
{
    cerr << "This pin tool collects a profile of jump/branch/call instructions for an application\n";

    cerr << KNOB_BASE::StringKnobSummary();

    cerr << endl;
    return -1;
}
/* ===================================================================== */

/* ===================================================================== */
/* Commandline Switches */
/* ===================================================================== */

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE,         "pintool",
                            "o", "output.out", "specify profile file name");

KNOB<BOOL>   KnobPid(KNOB_MODE_WRITEONCE,                "pintool",
                            "i", "0", "append pid to output");

KNOB<UINT64> KnobBranchLimit(KNOB_MODE_WRITEONCE,        "pintool",
                            "l", "0", "set limit of dynamic branches simulated/analyzed before quit");

/* ===================================================================== */
/* Global Variables */
/* ===================================================================== */
UINT64 CountSeen = 0;
UINT64 CountTaken = 0;
UINT64 CountCorrect = 0;
UINT64 CountReplaced = 0;
UINT64 BTBHit;
UINT64 BTBMiss;

/* ===================================================================== */
/* Branch predictor                                                      */
/* ===================================================================== */

UINT64 mask = 0x03FF;
#define BTB_SIZE 1024
#define size 10
float History[size]= {1};
float Perceptron[size]={0};

unsigned char BTB_Table[1024];


struct entry_one_bit
{
    bool valid;
    bool prediction;
    UINT64 tag;
    UINT64 ReplaceCount;
   // UINT8 counter;
    float Perceptron[10];
} BTB[BTB_SIZE];

/* initialize the BTB */
VOID BTB_init()
{
    int i;
    for(i = 0; i < BTB_SIZE; i++)
    {
        BTB_Table[i] = 2;
        BTB[i].valid = false;
        BTB[i].prediction = false;
        BTB[i].tag = 0;
        BTB[i].ReplaceCount = 0;
        BTB[i].Perceptron[0] =0;
        BTB[i].Perceptron[1] =0;
        BTB[i].Perceptron[2] =0;
        BTB[i].Perceptron[3] =0;
        BTB[i].Perceptron[4] =0;
        BTB[i].Perceptron[5] =0;
        BTB[i].Perceptron[6] =0;
        BTB[i].Perceptron[7] =0;
        BTB[i].Perceptron[8] =0;
        BTB[i].Perceptron[9] =0;
    }

}

/* see if the given address is in the BTB */
bool BTB_lookup(ADDRINT ins_ptr)
{
    UINT64 index;

    index = mask & ins_ptr;

    if(BTB[index].valid)
        if(BTB[index].tag == ins_ptr)
           {
                BTBHit++;
                return true;
           }
    BTBMiss++;
    return false;
}

/* return the prediction for the given address */
bool BTB_prediction(ADDRINT ins_ptr)
{
    UINT64 index;
    index = mask & ins_ptr;
    int i = 0;
    float  x;
    x = 0;
    for (i=0;i<10;i++)
    {
     x+= History[i] * BTB[index].Perceptron[i];
    }

    if(x > 0)
        {return 1;}
    else
        {return 0;}

}

/* update the BTB entry with the last result */

VOID BTB_update(ADDRINT ins_ptr, bool taken)
{
    UINT64 index;
    index = mask & ins_ptr;
    int   i, t;
    float  x;
    x =0;

    for (i=0;i<10;i++)
    {
     x = x + (History[i] * BTB[index].Perceptron[i]);
    }

    if(taken)
      {
      t=1;
      }
    else
      {
      t=-1;
      }

    if ((BTB_prediction(ins_ptr) != taken) | ( abs(x) < (1.93*10+14)))
    {

    for(i=0;i<10;i++)
     {
    BTB[index].Perceptron[i] = BTB[index].Perceptron[i] + t * History[i];
     }

    }

   for(i=9; i>0; i--)
     {
      History[i] = History[i-1];
     }

    History[0] = t;

   //  BTB[index].History = BTB_History & mask;
   // cout << "history" << BTB[index].History << endl;
}

/* insert a new branch in the table */
VOID BTB_insert(ADDRINT ins_ptr)
{
    UINT64 index;

    index = mask & ins_ptr;

    if(BTB[index].valid)
    {
        BTB[index].ReplaceCount++;
        CountReplaced++;
    }

    BTB[index].valid = true;
    BTB[index].prediction = true;  // Missed branches always enter as taken/true
    BTB[index].tag = ins_ptr;
    //BTB[index].counter = 2;
}

/* ===================================================================== */


/* ===================================================================== */

VOID WriteResults(bool limit_reached)
{
    int i;

    string output_file = KnobOutputFile.Value();
    if(KnobPid) output_file += "." + decstr(getpid());

    std::ofstream out(output_file.c_str());

    if(limit_reached)
        out << "Reason: limit reached\n";
    else
        out << "Reason: fini\n";
    out << "Count Seen: " << CountSeen << endl;
    out << "Count Taken: " << CountTaken << endl;
    out << "Count Correct: " << CountCorrect << endl;
    out << "Count Replaced: " << CountReplaced << endl;
    out << "BTB Hit: " << BTBHit << endl;
    out << "BTB Miss: " << BTBMiss << endl;
    out << "BTB Miss Rate: " << (float) 100*BTBMiss/(BTBMiss+BTBHit) << endl;
    out << "Prediction Accuracy Percentage: " << (float) 100*CountCorrect/CountSeen << endl;
    for(i = 0; i < BTB_SIZE; i++)
    {
        out << "BTB entry: " << i << ";" << BTB[i].valid << ";" << BTB[i].ReplaceCount << endl;
    }
    out.close();
}

/* ===================================================================== */

VOID br_predict(ADDRINT ins_ptr, INT32 taken)
{
    CountSeen++;
    if (taken)
        CountTaken++;

    if(BTB_lookup(ins_ptr))
    {
        if(BTB_prediction(ins_ptr) == taken) CountCorrect++;
        BTB_update(ins_ptr, taken);
    }
    else
    {
        if(!taken) CountCorrect++;
        else BTB_insert(ins_ptr);
    }

    if(CountSeen == KnobBranchLimit.Value())
    {
        WriteResults(true);
        exit(0);
    }
}


//  IARG_INST_PTR
// ADDRINT ins_ptr

/* ===================================================================== */

VOID Instruction(INS ins, void *v)
{
// The subcases of direct branch and indirect branch are
// broken into "call" or "not call".  Call is for a subroutine
// These are left as subcases in case the programmer wants
// to extend the statistics to see how sub cases of branches behave

    if( INS_IsRet(ins) )
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
            IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
    }
    else if( INS_IsSyscall(ins) )
    {
        INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
            IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
    }
    else if (INS_IsDirectBranchOrCall(ins))
    {
        if( INS_IsCall(ins) ) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
                IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
        }
        else {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
                IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
        }
    }
    else if( INS_IsIndirectBranchOrCall(ins) )
    {
        if( INS_IsCall(ins) ) {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
                IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
    }
        else {
            INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR) br_predict,
                IARG_INST_PTR, IARG_BRANCH_TAKEN,  IARG_END);
        }
    }

}

/* ===================================================================== */

#define OUT(n, a, b) out << n << " " << a << setw(16) << CountSeen. b  << " " << setw(16) << CountTaken. b << endl

VOID Fini(int n, void *v)
{
    WriteResults(false);
}


/* ===================================================================== */


/* ===================================================================== */

int main(int argc, char *argv[])
{

    if( PIN_Init(argc,argv) )
    {
        return Usage();
    }

    BTB_init(); // Initialize hardware structures

    INS_AddInstrumentFunction(Instruction, 0);
    PIN_AddFiniFunction(Fini, 0);

    // Never returns

    PIN_StartProgram();

    return 0;
}

/* ===================================================================== */
/* eof */
/* ===================================================================== */
