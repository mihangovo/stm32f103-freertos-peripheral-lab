$source = Get-Content "$PSScriptRoot/../User/TASK/system_init.c" -Raw

if ($source -notmatch 'osFlagsWaitAny\s*\|\s*osFlagsNoClear') {
    throw 'System-ready event waits must retain the flag for every task.'
}
