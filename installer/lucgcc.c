/* lucgcc.exe - LUC installer "silent-fail" helper.
 *
 * WHY THIS EXISTS
 * Inno Setup's Exec() spawns children with CREATE_DEFAULT_ERROR_MODE
 * (Projects/Src/Setup.InstFunc.pas, InstExec: "dwCreationFlags :=
 * CREATE_DEFAULT_ERROR_MODE"), so an error mode set in the installer is
 * NOT inherited and a broken toolchain (cc1.exe unable to load
 * libgmp-10.dll / libisl-23.dll / libmpc-3.dll / libmpfr-6.dll) pops one
 * "System Error" dialog per missing DLL - a dialog storm during the
 * install-time build.
 *
 * HOW IT FIXES IT
 * This tiny helper runs BELOW that hop: it sets
 * SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX
 * for ITSELF, then launches the rest of its own command line via
 *     cmd.exe /C "<rest>"
 * with creation flags 0.  Child processes inherit the caller's error
 * mode unless CREATE_DEFAULT_ERROR_MODE is passed (gcc's pex-win32.c
 * uses flags 0), so cmd -> gcc -> cc1 and the produced exes all inherit
 * the suppressed mode and fail silently; their stderr still reaches the
 * caller (build_log.txt).  Exit code = cmd's exit code.
 *
 * The outer quotes around <rest> are essential: cmd's /C quote rule
 * strips the first and last quote character of the line, restoring
 * <rest> verbatim even when it contains several quoted paths (same
 * trick Inno Setup itself uses for .bat files).
 *
 * Build (static so the helper itself never needs mingw runtime DLLs):
 *   gcc -O2 -s -static -o lucgcc.exe lucgcc.c
 */

#ifndef LUCGCCTest
#include <windows.h>
#endif

/* Skip argv[0] on our own command line and return a pointer to the rest.
   Handles quoted argv[0] and the C-runtime backslash rules (a backslash
   before a quote escapes it), so paths like C:\dir with \" from batch
   files are parsed correctly. */
static const char *after_arg0(const char *s)
{
    int bs;
    if (*s == '"') {
        s++;
        for (;;) {
            if (*s == 0)
                break;
            bs = 0;
            while (*s == '\\') { bs++; s++; }
            if (*s == '"') {
                if (bs % 2 == 0) { s++; break; }  /* unescaped close quote */
                s++;                              /* literal quote char */
            }
            if (*s)
                s++;
        }
    } else {
        while (*s && *s != ' ' && *s != '\t')
            s++;
    }
    while (*s == ' ' || *s == '\t')
        s++;
    return s;
}

#ifdef LUCGCCTest
/* ---- Linux test build: verify the argv[0] skipping rules ---- */
#include <stdio.h>
#include <string.h>
static int fails = 0;
static void chk(const char *line, const char *want)
{
    const char *got = after_arg0(line);
    if (strcmp(got, want) != 0) {
        printf("FAIL: [%s] -> [%s], want [%s]\n", line, got, want);
        fails++;
    }
}
int main(void)
{
    chk("lucgcc.exe REM", "REM");
    chk("\"C:\\tools\\lucgcc.exe\" REM", "REM");
    chk("\"C:\\tools\\luc gcc.exe\" REM", "REM");
    chk("lucgcc.exe \"C:\\a b\\gcc.exe\" --version", "\"C:\\a b\\gcc.exe\" --version");
    /* batch file quoting: ...\lucgcc.exe" leaves an escaped quote in argv */
    chk("\"C:\\Users\\a b\\Documents\\LUC\\lucgcc.exe\" REM", "REM");
    chk("\"C:\\dir\\lucgcc.exe\" cd /d \"C:\\app dir\" && gcc", "cd /d \"C:\\app dir\" && gcc");
    chk("lucgcc.exe ", "");
    chk("", "");
    printf(fails ? "TESTS FAILED\n" : "all argv[0] parse tests passed\n");
    return fails != 0;
}
#else
int main(void)
{
    char full[32768];
    const char *cmd, *p;
    int n = 0;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD rc = 1, wait;

    cmd = after_arg0(GetCommandLineA());

    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                 SEM_NOOPENFILEERRORBOX);

    if (!*cmd)
        return 2;

    /* cmd.exe /C "<cmd>"  - outer quotes defeat cmd's quote-stripping */
    p = "cmd.exe /C ";
    while (*p)
        full[n++] = *p++;
    if (n + lstrlenA(cmd) + 2 >= (int)sizeof(full))
        return 2;
    full[n++] = '"';
    while (*cmd)
        full[n++] = *cmd++;
    full[n++] = '"';
    full[n] = 0;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(NULL, full, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return 3;
    wait = WaitForSingleObject(pi.hProcess, 120000);  /* safety: 2 min */
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        rc = 1;
    } else if (!GetExitCodeProcess(pi.hProcess, &rc)) {
        rc = 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)rc;
}
#endif
