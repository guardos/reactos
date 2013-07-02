/*
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Virtual DOS Machine
 * FILE:            ntvdm.c
 * PURPOSE:         Virtual DOS Machine
 * PROGRAMMERS:     Aleksandar Andrejevic <theflash AT sdf DOT lonestar DOT org>
 */

/* INCLUDES *******************************************************************/

#include "ntvdm.h"
#include "emulator.h"
#include "bios.h"
#include "dos.h"
#include "timer.h"
#include "pic.h"
#include "ps2.h"

/* PUBLIC VARIABLES ***********************************************************/

BOOLEAN VdmRunning = TRUE;
LPVOID BaseAddress = NULL;
LPCWSTR ExceptionName[] =
{
    L"Division By Zero",
    L"Debug",
    L"Unexpected Error",
    L"Breakpoint",
    L"Integer Overflow",
    L"Bound Range Exceeded",
    L"Invalid Opcode",
    L"FPU Not Available"
};

/* PUBLIC FUNCTIONS ***********************************************************/

VOID DisplayMessage(LPCWSTR Format, ...)
{
    WCHAR Buffer[256];
    va_list Parameters;

    va_start(Parameters, Format);
    _vsnwprintf(Buffer, 256, Format, Parameters);
    MessageBox(NULL, Buffer, L"NTVDM Subsystem", MB_OK);
    va_end(Parameters);
}

BOOL WINAPI ConsoleCtrlHandler(DWORD ControlType)
{
    switch (ControlType)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        {
            /* Perform interrupt 0x23 */
            EmulatorInterrupt(0x23);
            break;
        }
        default:
        {
            /* Stop the VDM if the user logs out or closes the console */
            VdmRunning = FALSE;
        }
    }
    return TRUE;
}

INT wmain(INT argc, WCHAR *argv[])
{
    INT i;
    CHAR CommandLine[128];
    DWORD CurrentTickCount;
    DWORD LastTickCount = GetTickCount();
    DWORD Cycles = 0;
    DWORD LastCyclePrintout = GetTickCount();
    LARGE_INTEGER Frequency, LastTimerTick, Counter;
    LONGLONG TimerTicks;

    /* Set the handler routine */
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

    /* The DOS command line must be ASCII */
    WideCharToMultiByte(CP_ACP, 0, GetCommandLine(), -1, CommandLine, 128, NULL, NULL);

    if (!EmulatorInitialize())
    {
        wprintf(L"FATAL: Failed to initialize the CPU emulator\n");
        return 1;
    }
    
    /* Initialize the performance counter (needed for hardware timers) */
    if (!QueryPerformanceFrequency(&Frequency))
    {
        wprintf(L"FATAL: Performance counter not available\n");
        goto Cleanup;
    }

    /* Initialize the system BIOS */
    if (!BiosInitialize())
    {
        wprintf(L"FATAL: Failed to initialize the VDM BIOS.\n");
        goto Cleanup;
    }

    /* Initialize the VDM DOS kernel */
    if (!DosInitialize())
    {
        wprintf(L"FATAL: Failed to initialize the VDM DOS kernel.\n");
        goto Cleanup;
    }

    /* Start the process from the command line */
    if (!DosCreateProcess(CommandLine, 0))
    {
        DisplayMessage(L"Could not start program: %S", CommandLine);
        return -1;
    }
    
    /* Set the last timer tick to the current time */
    QueryPerformanceCounter(&LastTimerTick);

    /* Main loop */
    while (VdmRunning)
    {
        /* Get the current number of ticks */
        CurrentTickCount = GetTickCount();
        
        /* Get the current performance counter value */
        QueryPerformanceCounter(&Counter);
        
        /* Get the number of PIT ticks that have passed */
        TimerTicks = ((Counter.QuadPart - LastTimerTick.QuadPart)
                     * PIT_BASE_FREQUENCY) / Frequency.QuadPart;
        
        /* Update the PIT */
        for (i = 0; i < TimerTicks; i++) PitDecrementCount();
        LastTimerTick = Counter;
        
        /* Check for console input events every millisecond */
        if (CurrentTickCount != LastTickCount)
        {
            CheckForInputEvents();
            LastTickCount = CurrentTickCount;
        }
        
        /* Continue CPU emulation */
        for (i = 0; (i < STEPS_PER_CYCLE) && VdmRunning; i++)
        {
            EmulatorStep();
            Cycles++;
        }
        
        if ((CurrentTickCount - LastCyclePrintout) >= 1000)
        {
            DPRINT1("NTVDM: %d Instructions Per Second\n", Cycles);
            LastCyclePrintout = CurrentTickCount;
            Cycles = 0;
        }
    }

Cleanup:
    EmulatorCleanup();

    return 0;
}

/* EOF */
