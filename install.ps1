<#
  Dessa installer for Windows.

  Run it from the Dessa folder:
      powershell -ExecutionPolicy Bypass -File install.ps1

  It checks for a C++ compiler and CMake (and offers to install them with
  winget), builds dessa and riv, copies everything to an install folder,
  puts dessa and riv on your PATH, lets riv use the bundled packages
  (sql, request, json, ashwee, ...) without downloading them, and gets the
  WebAssembly compiler that web pages need (dessa setup wasm, about 50 MB).

  Options:
      -Yes          accept every default, ask nothing
      -Prefix DIR   install folder (default: %LOCALAPPDATA%\Dessa)
      -NoPath       don't change PATH
      -NoFileTypes  don't register the .ds / .dhtml file icons
      -NoVSCode     don't install the VS Code extension
      -NoWasm       don't download the WebAssembly compiler now (later: dessa setup wasm)
      -Uninstall    remove Dessa (the folder, the PATH entry, the file types)
#>
param(
    [switch]$Yes,
    [string]$Prefix = (Join-Path $env:LOCALAPPDATA "Dessa"),
    [switch]$NoPath,
    [switch]$NoFileTypes,
    [switch]$NoVSCode,
    [switch]$NoWasm,
    [switch]$Uninstall
)

$ErrorActionPreference = "Stop"
$Source = $PSScriptRoot
$Bin = Join-Path $Prefix "bin"
$Extensions = @(".ds", ".dm", ".dx", ".dz", ".dsx", ".dxx", ".dzx", ".dds", ".dss")

# ------------------------------------------------------------
# Little helpers
# ------------------------------------------------------------

function Say([string]$text, [string]$color = "Gray") { Write-Host $text -ForegroundColor $color }
function Step([string]$text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Ok([string]$text) { Write-Host "   ok  $text" -ForegroundColor Green }
function Warn([string]$text) { Write-Host "   !   $text" -ForegroundColor Yellow }
function Fail([string]$text) {
    Write-Host ""
    Write-Host "Installation stopped: $text" -ForegroundColor Red
    exit 1
}

function Ask([string]$question, [bool]$default = $true) {
    if ($Yes) { return $default }
    $hint = if ($default) { "[Y/n]" } else { "[y/N]" }
    while ($true) {
        $answer = (Read-Host "   $question $hint").Trim().ToLower()
        if ($answer -eq "") { return $default }
        if ($answer -in @("y", "yes")) { return $true }
        if ($answer -in @("n", "no")) { return $false }
    }
}

function AskText([string]$question, [string]$default) {
    if ($Yes) { return $default }
    $answer = (Read-Host "   $question [$default]").Trim()
    if ($answer -eq "") { return $default }
    return $answer
}

# PATH as Windows has it now (not as this window started with)
function RefreshPath {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user"
}

function Find([string]$program, [string[]]$extraFolders = @()) {
    $found = Get-Command $program -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($found) { return $found.Source }
    foreach ($folder in $extraFolders) {
        $candidate = Join-Path $folder "$program.exe"
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

# Runs a program with its output appended to the log. (Not with `& exe 2>&1`:
# Windows PowerShell 5.1 treats anything a program prints to stderr as an error.)
function RunLogged([string]$exe, [string[]]$arguments) {
    $quoted = ($arguments | ForEach-Object { "`"$_`"" }) -join " "
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -NoNewWindow -PassThru `
        -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
    $null = $process.Handle   # keeps the exit code readable
    $process.WaitForExit()
    Get-Content "$log.out", "$log.err" | Add-Content $log
    Remove-Item "$log.out", "$log.err" -ErrorAction SilentlyContinue
    return $process.ExitCode
}

function UserPath { return [Environment]::GetEnvironmentVariable("Path", "User") }

function AddToUserPath([string]$folder) {
    $parts = @((UserPath) -split ";" | Where-Object { $_ -ne "" })
    if ($parts | Where-Object { $_.TrimEnd("\") -ieq $folder.TrimEnd("\") }) { return $false }
    [Environment]::SetEnvironmentVariable("Path", (($parts + $folder) -join ";"), "User")
    return $true
}

function RemoveFromUserPath([string]$folder) {
    $parts = @((UserPath) -split ";" | Where-Object { $_ -ne "" -and $_.TrimEnd("\") -ine $folder.TrimEnd("\") })
    [Environment]::SetEnvironmentVariable("Path", ($parts -join ";"), "User")
}

# An .ico that holds a PNG (supported since Windows Vista)
function PngToIco([string]$png, [string]$ico) {
    $bytes = [IO.File]::ReadAllBytes($png)
    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    $writer.Write([UInt16]0); $writer.Write([UInt16]1); $writer.Write([UInt16]1)   # header: icon, 1 image
    $writer.Write([Byte]0); $writer.Write([Byte]0)                                   # 256 x 256 (0 = 256 or "from the PNG")
    $writer.Write([Byte]0); $writer.Write([Byte]0)
    $writer.Write([UInt16]1); $writer.Write([UInt16]32)
    $writer.Write([UInt32]$bytes.Length); $writer.Write([UInt32]22)                  # size, offset
    $writer.Write($bytes)
    [IO.File]::WriteAllBytes($ico, $stream.ToArray())
}

function RegisterFileType([string]$extension, [string]$progId, [string]$name, [string]$icon, [string]$runCommand) {
    $classes = "HKCU:\Software\Classes"
    New-Item -Path "$classes\$extension" -Force | Out-Null
    Set-ItemProperty -Path "$classes\$extension" -Name "(default)" -Value $progId
    New-Item -Path "$classes\$progId\DefaultIcon" -Force | Out-Null
    Set-ItemProperty -Path "$classes\$progId" -Name "(default)" -Value $name
    Set-ItemProperty -Path "$classes\$progId\DefaultIcon" -Name "(default)" -Value $icon
    if ($runCommand) {
        New-Item -Path "$classes\$progId\shell\run\command" -Force | Out-Null
        Set-ItemProperty -Path "$classes\$progId\shell\run" -Name "(default)" -Value "Run with Dessa"
        Set-ItemProperty -Path "$classes\$progId\shell\run\command" -Name "(default)" -Value $runCommand
    }
}

function RefreshIcons {
    Add-Type -Namespace Dessa -Name Shell -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("shell32.dll")]
public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
"@ -ErrorAction SilentlyContinue
    [Dessa.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
}

# ------------------------------------------------------------
# Uninstall
# ------------------------------------------------------------

if ($Uninstall) {
    Step "Removing Dessa from $Prefix"
    if (-not (Ask "Remove Dessa, its PATH entry and its file types?")) { exit 0 }
    RemoveFromUserPath $Bin
    Ok "removed $Bin from PATH"
    foreach ($ext in $Extensions + ".phl" + ".dhtml") {
        Remove-Item -Path "HKCU:\Software\Classes\$ext" -Recurse -Force -ErrorAction SilentlyContinue
    }
    Remove-Item -Path "HKCU:\Software\Classes\Dessa.Source", "HKCU:\Software\Classes\Dessa.Phl", "HKCU:\Software\Classes\Dessa.Page" -Recurse -Force -ErrorAction SilentlyContinue
    RefreshIcons
    Ok "removed the file types"
    if (Test-Path $Prefix) { Remove-Item $Prefix -Recurse -Force; Ok "deleted $Prefix" }
    Say ""
    Say "Dessa is uninstalled. (Your riv store in ~\.riv and riv_modules folders were left alone.)" "Green"
    exit 0
}

# ------------------------------------------------------------
# 1. Welcome
# ------------------------------------------------------------

Write-Host ""
Write-Host "  Dessa installer" -ForegroundColor Magenta
Write-Host "  ---------------" -ForegroundColor Magenta
# A downloaded release has bin\dessa.exe already: nothing to build
$Prebuilt = Test-Path (Join-Path $Source "bin\dessa.exe")
if ($Prebuilt) {
    Say "  This installs the prebuilt Dessa in $Source for your Windows user."
} elseif (Test-Path (Join-Path $Source "CMakeLists.txt")) {
    Say "  This builds Dessa from $Source and installs it for your Windows user."
} else {
    Fail "run this script from the Dessa folder (a release with bin\dessa.exe, or the source with CMakeLists.txt)"
}

$Prefix = AskText "Install folder" $Prefix
$Bin = Join-Path $Prefix "bin"

# ------------------------------------------------------------
# 2. Tools: a C++ compiler and CMake
# ------------------------------------------------------------

Step "Checking the tools Dessa needs"
$mingwFolders = @("C:\mingw64\bin", "C:\msys64\ucrt64\bin", "C:\msys64\mingw64\bin", "$env:LOCALAPPDATA\Microsoft\WinGet\Links")

$gxx = Find "g++" $mingwFolders
if (-not $gxx) {
    Warn "No C++ compiler (g++) found. Dessa turns your programs into C and C++ and needs it to build them."
    if ((Get-Command winget -ErrorAction SilentlyContinue) -and (Ask "Install MinGW g++ (WinLibs) with winget now?")) {
        winget install --exact --id BrechtSanders.WinLibs.POSIX.UCRT --accept-source-agreements --accept-package-agreements
        RefreshPath
        $gxx = Find "g++" $mingwFolders
    }
    if (-not $gxx) { Fail "install a C++20 compiler (e.g. https://winlibs.com) and run the installer again" }
}
$gxxBin = Split-Path $gxx
Ok "C++ compiler: $gxx"

# clang (LLVM MinGW) builds the C that Dessa makes; without it gcc does
$clang = Find "clang"
if (-not $clang) {
    Say "  Dessa builds programs through C, with clang when it's installed (gcc otherwise)."
    if ((Get-Command winget -ErrorAction SilentlyContinue) -and (Ask "Install clang (LLVM MinGW) with winget now?")) {
        winget install --exact --id MartinStorsjo.LLVM-MinGW.UCRT --accept-source-agreements --accept-package-agreements
        RefreshPath
        $clang = Find "clang"
    }
}
if ($clang) { Ok "C compiler: $clang" } else { Ok "C compiler: gcc (next to g++)" }

$cmake = if ($Prebuilt) { "(not needed)" } else { Find "cmake" @("C:\Program Files\CMake\bin") }
if (-not $cmake) {
    Warn "CMake was not found (it's only needed to build Dessa)."
    if ((Get-Command winget -ErrorAction SilentlyContinue) -and (Ask "Install CMake with winget now?")) {
        winget install --exact --id Kitware.CMake --accept-source-agreements --accept-package-agreements
        RefreshPath
        $cmake = Find "cmake" @("C:\Program Files\CMake\bin")
    }
    if (-not $cmake) { Fail "install CMake (https://cmake.org/download) and run the installer again" }
}
Ok "CMake: $cmake"

# The build runs g++ and mingw32-make from the compiler's folder
if (-not (($env:Path -split ";") -contains $gxxBin)) { $env:Path = "$gxxBin;$env:Path" }

# ------------------------------------------------------------
# 3. Build
# ------------------------------------------------------------

if ($Prebuilt) {
    $build = Join-Path $Source "bin"
} else {
    Step "Building dessa and riv (a minute or two the first time)"
    $build = Join-Path $Source "build-release"
    $log = Join-Path $env:TEMP "dessa-install.log"
    "" | Out-File $log
    $generator = if (Find "mingw32-make" @($gxxBin)) { "MinGW Makefiles" } elseif (Find "ninja") { "Ninja" } else { $null }
    if (-not $generator) { Fail "no build tool found (mingw32-make or ninja next to g++)" }

    if ((RunLogged $cmake @("-S", $Source, "-B", $build, "-G", $generator, "-DCMAKE_BUILD_TYPE=Release")) -ne 0) {
        Fail "CMake could not configure the build; see $log"
    }
    if ((RunLogged $cmake @("--build", $build, "-j", [Environment]::ProcessorCount)) -ne 0) { Fail "building dessa failed; see $log" }
    Ok "dessa built"
    if ((RunLogged $cmake @("--build", $build, "--target", "riv")) -ne 0) { Fail "building riv failed; see $log" }
    Ok "riv built"
}

# ------------------------------------------------------------
# 4. Install the files
# ------------------------------------------------------------

Step "Installing to $Prefix"
New-Item -ItemType Directory -Force -Path $Bin | Out-Null
Copy-Item (Join-Path $build "dessa.exe") $Bin -Force
Copy-Item (Join-Path $build "riv.exe") $Bin -Force
foreach ($folder in @("runtime", "include")) {
    $target = Join-Path $Prefix $folder
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
    Copy-Item (Join-Path $Source $folder) $target -Recurse
}
$packages = Join-Path $Prefix "packages"
if (Test-Path $packages) { Remove-Item $packages -Recurse -Force }
Copy-Item (Join-Path $Source "tools\riv\packages") $packages -Recurse
$icons = Join-Path $Prefix "icons"
New-Item -ItemType Directory -Force -Path $icons | Out-Null
PngToIco (Join-Path $Source "tools\vscode-dessa\icons\dessa-128x128.png") (Join-Path $icons "dessa.ico")
Copy-Item (Join-Path $Source "LICENSE") $Prefix -Force
Ok "dessa.exe, riv.exe, runtime, include, packages"

# ------------------------------------------------------------
# 5. PATH
# ------------------------------------------------------------

if (-not $NoPath) {
    Step "PATH"
    if (Ask "Add $Bin to your PATH (so you can type dessa and riv anywhere)?") {
        if (AddToUserPath $Bin) { Ok "added $Bin" } else { Ok "already on PATH" }
        # dessa runs g++ when it builds a program
        if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) {
            if (Ask "Also add the C++ compiler folder $gxxBin to your PATH (dessa needs it)?") {
                if (AddToUserPath $gxxBin) { Ok "added $gxxBin" }
            } else {
                Warn "then set DESSA_CXX to $gxx yourself"
            }
        }
        RefreshPath
    }
}

# ------------------------------------------------------------
# 6. riv packages
# ------------------------------------------------------------

Step "Telling riv where the bundled packages are"
& (Join-Path $Bin "riv.exe") bundled $packages
if ($LASTEXITCODE -ne 0) { Warn "riv could not note the packages; run: riv bundled `"$packages`"" }

# ------------------------------------------------------------
# 7. The WebAssembly compiler (web pages run as WebAssembly)
# ------------------------------------------------------------

if (-not $NoWasm) {
    Step "The WebAssembly compiler (for web pages, about 50 MB, once)"
    & (Join-Path $Bin "dessa.exe") setup wasm
    if ($LASTEXITCODE -ne 0) { Warn "it couldn't be downloaded now; pages need it, so run later: dessa setup wasm" }
}

# ------------------------------------------------------------
# 8. Extras: file icons, VS Code
# ------------------------------------------------------------

if (-not $NoFileTypes) {
    Step "File types"
    if (Ask "Show Dessa icons for .ds / .dm / .dhtml files and add 'Run with Dessa' to their menu?") {
        $run = "cmd.exe /k `"`"$Bin\dessa.exe`" run `"%1`"`""
        foreach ($ext in $Extensions) {
            RegisterFileType $ext "Dessa.Source" "Dessa source file" (Join-Path $icons "dessa.ico") $run
        }
        RegisterFileType ".dhtml" "Dessa.Page" "Dessa web page" (Join-Path $icons "dessa.ico") "cmd.exe /k `"`"$Bin\dessa.exe`" run `"%1`"`""
        RefreshIcons
        Ok "registered $($Extensions -join ' ') and .dhtml"
    }
}

if (-not $NoVSCode) {
    $vscodeExtensions = Join-Path $env:USERPROFILE ".vscode\extensions"
    if (Test-Path $vscodeExtensions) {
        Step "VS Code"
        # (the leading comma keeps a list of one list from being flattened)
        $extensionsToInstall = @(
            ,@("dessa-0.1.0", "tools\vscode-dessa", "the Dessa extension (highlighting, errors, IntelliSense)")
        )
        foreach ($extension in $extensionsToInstall) {
            $target = Join-Path $vscodeExtensions $extension[0]
            $existing = Get-Item $target -ErrorAction SilentlyContinue
            if ($existing -and $existing.LinkType) {
                Ok "$($extension[0]) is already linked to $($existing.Target); left as is"
            } elseif (Ask "Install $($extension[2])?") {
                if ($existing) { Remove-Item $target -Recurse -Force }
                Copy-Item (Join-Path $Source $extension[1]) $target -Recurse
                Ok "installed $($extension[0]); restart VS Code to use it"
            }
        }
    }
}

# ------------------------------------------------------------
# Done
# ------------------------------------------------------------

$version = & (Join-Path $Bin "dessa.exe") --version
Write-Host ""
Write-Host "  $version is installed." -ForegroundColor Green
Say "  Open a NEW terminal (so it sees the new PATH) and try:"
Say "      dessa run hello.ds"
Say "      riv init myapp"
Say "      riv add sql"
Say "  Uninstall: powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall"
Write-Host ""
