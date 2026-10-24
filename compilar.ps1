# Compila BrysOS desde Windows con MSYS2 y el compilador cruzado de E:\BrysOS-dev
#   .\compilar.ps1                 compila
#   .\compilar.ps1 limpiar         borra los archivos generados
#   .\compilar.ps1 qemu            compila y arranca en QEMU
#   .\compilar.ps1 make ...        cualquier otro objetivo (p. ej. .\compilar.ps1 make bsi VERSION=1.3)
#   .\compilar.ps1 prueba x.sh     ejecuta una prueba de tools/ (p. ej. .\compilar.ps1 prueba test_install.sh)
#   .\compilar.ps1 bash "orden"    ejecuta una orden en el entorno de compilación
param([string]$accion = "", [Parameter(ValueFromRemainingArguments)] [string[]]$resto)

$msys = "E:\msys64\usr\bin\bash.exe"
$dev  = "/e/BrysOS-dev"
# Las variables del Makefile van como variables de entorno (?=), así también las ven los
# scripts de tools/ que llaman a make por su cuenta
$entorno = "export PATH=$dev/cross/bin:`$PATH CROSS=x86_64-elf- GRUB_DIR=$dev/grub/lib/grub " +
           "GRUB_MKIMAGE=$dev/grub/bin/grub-mkimage LIMINE=limine/limine.exe " +
           "OVMF=/ucrt64/share/qemu/edk2-x86_64-code.fd MAKEFLAGS=-j12"

switch ($accion) {
    "limpiar" { $orden = "make clean" }
    "qemu"    { $orden = "make run" }
    "make"    { $orden = "make $($resto -join ' ')" }
    "prueba"  { $orden = "sh tools/$($resto -join ' ')" }
    "bash"    { $orden = $resto -join ' ' }
    default   { $orden = "make" }
}

$env:MSYSTEM = "UCRT64"
$env:CHERE_INVOKING = "1"
$env:MSYS2_PATH_TYPE = "minimal"
Push-Location $PSScriptRoot
& $msys -lc "$entorno; $orden"
$codigo = $LASTEXITCODE
Pop-Location
if ($codigo -eq 0 -and $accion -eq "") { Write-Host "Listo: $PSScriptRoot\BrysOS.iso" }
exit $codigo
