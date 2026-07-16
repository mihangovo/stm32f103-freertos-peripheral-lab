$main = Get-Content "$PSScriptRoot/../Core/Src/main.c" -Raw
$dmp = $main.IndexOf('while (mpu_dmp_init())')
$iwdg = $main.IndexOf('  MX_IWDG_Init();')
$scheduler = $main.IndexOf('  osKernelStart();')

if ($dmp -lt 0 -or $iwdg -lt 0 -or $scheduler -lt 0 -or !($dmp -lt $iwdg -and $iwdg -lt $scheduler)) {
    throw 'MX_IWDG_Init must follow DMP initialization and precede osKernelStart.'
}
