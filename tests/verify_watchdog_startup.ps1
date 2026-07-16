$main = Get-Content "$PSScriptRoot/../Core/Src/main.c" -Raw
$main = $main -replace '(?s)#if 0.*?#endif', ''
$init = Get-Content "$PSScriptRoot/../User/TASK/system_init.c" -Raw
$dmp = $init.IndexOf('while (mpu_dmp_init() != 0)')
$iwdg = $init.IndexOf('MX_IWDG_Init();')
$release = $init.IndexOf('System_Init_ReleaseTasks();')

if ($main -match '(?m)^\s*MX_IWDG_Init\(\);\s*$' -or $dmp -lt 0 -or $iwdg -lt 0 -or $release -lt 0 -or !($dmp -lt $iwdg -and $iwdg -lt $release)) {
    throw 'IWDG must start in InitTask only after DMP initialization succeeds and before tasks are released.'
}
