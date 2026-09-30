#pragma once

// Install crash handlers for both SEH (Windows) and std::terminate
// dumpDir: directory where crash dumps will be written (default: "dumps")
void InstallCrashHandlers(const char* dumpDir = "dumps");
void InstallCrashHandlersThisThread();   // stack guarantee for a worker thread (stack-overflow report)
void SetCrashReportHook(void (*f)());   // called by the stack-overflow reporter before the process ends
