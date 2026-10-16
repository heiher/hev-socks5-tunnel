# ============================================================================
#  build-windows.ps1 - Native MSVC/Windows build for hev-socks5-tunnel
#
#  PowerShell counterpart to build-apple.sh: builds with NMake + MSVC
#  (cl / lib / link / ml64), no CMake required.
#
#  Prerequisites:
#    - Visual Studio 2019/2022/2026 (or VS Build Tools) with the x64 C++
#      toolset. MSVC 19.35+ (VS2022 17.5+) is required because the C code
#      uses _Generic. This is a toolchain limitation and is NOT worked
#      around here.
#    - No Developer Command Prompt needed: if cl.exe / link.exe /
#      ml64.exe / nmake.exe are not on PATH, the script locates Visual
#      Studio via vswhere.exe and imports the developer environment with
#      Microsoft.VisualStudio.DevShell.dll + Enter-VsDevShell.
#
#  Outputs:
#    build-msvc\dist\bin\     - hev-socks5-tunnel.exe, .dll, wintun.dll
#    build-msvc\dist\lib\     - hev-socks5-tunnel-static.lib, import .lib
#    build-msvc\dist\include\ - public header (hev-main.h)
#
#  Usage:
#    .\build-windows.ps1 [-d|-DebugBuild] [-StaticCrt] [-Clean] [-Help]
#                        [-- ...nmake args...]
#    Extra args after -- are forwarded to nmake, e.g.:
#      .\build-windows.ps1 -- DEBUG=1
#
#  Runs on Windows PowerShell 5.1 and PowerShell 7+.
# ============================================================================
[CmdletBinding()]
param(
    # DEBUG=1. -d is the short alias. Note: -Debug is a PowerShell common
    # parameter (from [CmdletBinding()]); it is also accepted and treated
    # exactly the same as -d / -DebugBuild.
    [Alias('d')]
    [switch]$DebugBuild,

    # STATIC_CRT=1 (link the static CRT instead of the DLL runtime).
    [switch]$StaticCrt,

    # Run "nmake /F Makefile.msvc clean" and exit.
    [switch]$Clean,

    # Print usage. (-? also works: PowerShell shows this file's help.)
    [Alias('h')]
    [switch]$Help,

    # Everything else: VAR=val assignments, known targets, and any tokens
    # after a literal "--" separator (raw nmake passthrough).
    [Parameter(Position = 0, ValueFromRemainingArguments = $true)]
    [string[]]$NmakeArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# ============================================================================
#  Helpers
# ============================================================================

function Write-Usage {
    Write-Host 'Usage: build-windows.ps1 [-d|-DebugBuild] [-StaticCrt] [-Clean] [-Help] [-- ...nmake args...]'
    Write-Host '       VAR=val and targets (all/static/shared/exe/dist/clean) may be passed directly.'
}

function Get-RequiredToolMissing {
    # Returns the subset of $ToolNames that is not resolvable on PATH.
    # The leading comma keeps the result an array even for a single item.
    param([string[]]$ToolNames)
    , @($ToolNames | Where-Object { -not (Get-Command -Name $_ -ErrorAction SilentlyContinue) })
}

function Invoke-VsDevShellSetup {
    # Locates Visual Studio via vswhere.exe and imports the developer
    # environment (cl/lib/link/ml64/nmake + INCLUDE/LIB/WindowsSdkDir)
    # into the current PowerShell session.
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw @(
            'vswhere.exe not found (expected at:'
            "  $vswhere"
            'This ships with any Visual Studio 2017+ or VS Build Tools install.'
            'Fix: install Visual Studio with the "Desktop development with C++" workload,'
            '     or open a Developer Command Prompt and re-run this script.'
        ) -join [Environment]::NewLine
    }

    # Prefer an installation that actually has the C++ toolset; fall back to
    # the workload-level requirement for newer VS versions (VS2026+).
    $vswhereArgs = @('-latest', '-products', '*', '-prerelease',
        '-property', 'installationPath', '-format', 'value',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64')
    $installPath = [string](& $vswhere @vswhereArgs | Select-Object -First 1)
    if (-not $installPath) {
        # Newer Visual Studio versions (VS2026+) may no longer ship that
        # exact component id; fall back to the workload-level requirement.
        $vswhereArgs = @('-latest', '-products', '*', '-prerelease',
            '-property', 'installationPath', '-format', 'value',
            '-requires', 'Microsoft.VisualStudio.Workload.VCTools')
        $installPath = [string](& $vswhere @vswhereArgs | Select-Object -First 1)
    }
    if (-not $installPath) {
        throw @(
            'vswhere found no Visual Studio with the C++ toolset.'
            'Fix: install the "Desktop development with C++" workload'
            '(VS2019/VS2022/VS2026 or VS Build Tools) and re-run.'
        ) -join [Environment]::NewLine
    }
    Write-Host "INFO: using Visual Studio at: $installPath"

    $devShellDll = Join-Path $installPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path -LiteralPath $devShellDll)) {
        throw "Microsoft.VisualStudio.DevShell.dll not found at: $devShellDll"
    }
    Import-Module -Name $devShellDll
    Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64'
}

function Fail-MissingTools {
    # Reports exactly which tools are missing and how to fix it.
    param([string[]]$ToolNames)
    Write-Host "ERROR: required tool(s) still not found on PATH: $($ToolNames -join ', ')" `
        -ForegroundColor Red
    Write-Host 'Fix: import the VS developer environment first, either:'
    Write-Host '  - open a "Developer Command Prompt for VS" and re-run this script, or'
    Write-Host '  - run vcvarsall.bat: "& \"<VS path>\VC\Auxiliary\Build\vcvarsall.bat\" x64", or'
    Write-Host '  - Enter-VsDevShell -VsInstallPath <VS path> -SkipAutomaticLocation'
    Write-Host "      -DevCmdArguments '-arch=x64 -host_arch=x64'"
    exit 1
}

# ============================================================================
#  Main
# ============================================================================

try {
    Set-Location -LiteralPath $PSScriptRoot

    Write-Verbose ("Running under PowerShell {0} ({1} edition)" -f `
        $PSVersionTable.PSVersion, $PSVersionTable.PSEdition)

    # ------------------------------------------------------------------
    #  Argument handling
    # ------------------------------------------------------------------
    if ($Help) {
        Write-Usage
        exit 1
    }

    $nmakeArgsList = New-Object 'System.Collections.Generic.List[string]'

    if ($Clean) {
        # Matches the original .cmd: --clean runs only "nmake clean" and exits;
        # other options on the same command line are ignored.
        $nmakeArgsList.Add('clean')
    }
    else {
        if ($DebugBuild -or $PSBoundParameters.ContainsKey('Debug')) {
            $nmakeArgsList.Add('DEBUG=1')
        }
        if ($StaticCrt) {
            $nmakeArgsList.Add('STATIC_CRT=1')
        }
    }

    $knownTargets = @('all', 'static', 'shared', 'exe', 'dist', 'clean')
    $passthrough = @()

    if ($NmakeArgs) {
        # A literal "--" separates validated tokens from raw nmake
        # passthrough. Note: when the script is invoked from inside a
        # PowerShell session, PowerShell itself consumes "--" before it
        # reaches $NmakeArgs; when invoked via "pwsh -File", "--" arrives
        # literally. Both shapes are handled here.
        $sepIndex = [Array]::IndexOf($NmakeArgs, '--')
        if ($sepIndex -ge 0) {
            if ($sepIndex -gt 0) {
                $validatedPart = @($NmakeArgs | Select-Object -First $sepIndex)
            }
            else {
                $validatedPart = @()
            }
            $passthrough = @($NmakeArgs | Select-Object -Skip ($sepIndex + 1))
        }
        else {
            $validatedPart = @($NmakeArgs)
        }

        foreach ($token in $validatedPart) {
            $isVarAssignment = $token -match '^[^=\s]+='
            $isKnownTarget = $knownTargets -icontains $token
            if (-not ($isVarAssignment -or $isKnownTarget)) {
                Write-Host "ERROR: unknown argument $token" -ForegroundColor Red
                Write-Usage
                exit 1
            }
            $nmakeArgsList.Add($token)
        }
    }

    # ------------------------------------------------------------------
    #  Toolchain discovery (Developer Prompt OR auto-setup via vswhere)
    # ------------------------------------------------------------------
    $requiredTools = @('nmake', 'cl', 'ml64', 'link')
    $missing = Get-RequiredToolMissing $requiredTools

    if ($missing.Count -gt 0) {
        Write-Host ('INFO: {0} not on PATH; attempting Visual Studio auto-setup ' -f ($missing -join ', ')) `
            -NoNewline
        Write-Host '(vswhere + Enter-VsDevShell)...'
        Invoke-VsDevShellSetup
        $missing = Get-RequiredToolMissing $requiredTools
        if ($missing.Count -gt 0) {
            Fail-MissingTools $missing
        }
    }

    # ------------------------------------------------------------------
    #  Sanity checks and warnings (after auto-setup, as in the .cmd)
    # ------------------------------------------------------------------
    if (-not (Test-Path -LiteralPath (Join-Path (Get-Location) 'Makefile.msvc'))) {
        throw "Makefile.msvc not found in $(Get-Location)."
    }
    if (-not [Environment]::GetEnvironmentVariable('WindowsSdkDir')) {
        Write-Warning 'WindowsSdkDir not set. Windows SDK may be missing; link may fail with LNK1181 Ws2_32.lib.'
    }
    if (-not [Environment]::GetEnvironmentVariable('LIB')) {
        Write-Warning 'LIB not set. Run from a VS Developer Command Prompt (or let this script auto-setup); link may fail with LNK1181.'
    }

    # ------------------------------------------------------------------
    #  Build
    # ------------------------------------------------------------------
    $finalArgs = @('/F', 'Makefile.msvc') + @($nmakeArgsList) + $passthrough
    Write-Host ("=== nmake " + ($finalArgs -join ' ') + " ===")
    & nmake @finalArgs
    $lastExit = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    if (-not $lastExit -or $lastExit.Value -ne 0) {
        throw "nmake exited with code $LASTEXITCODE."
    }
    if ($Clean) {
        # Matches the original .cmd: --clean exits immediately, no summary.
        exit $lastExit.Value
    }

    Write-Host ''
    Write-Host 'Build finished.'
    Write-Host '  binaries:   build-msvc\dist\bin'
    Write-Host '  libraries:  build-msvc\dist\lib'
    Write-Host '  headers:    build-msvc\dist\include'
    exit 0
}
catch {
    Write-Host ("ERROR: " + $_.Exception.Message) -ForegroundColor Red
    # Propagate the exit code of the failing native tool when available;
    # $LASTEXITCODE may be unset if no native command ran, which throws
    # under StrictMode, hence the Get-Variable probe.
    $lastExit = Get-Variable -Name LASTEXITCODE -ErrorAction SilentlyContinue
    if ($lastExit -and $lastExit.Value -ne 0) {
        exit $lastExit.Value
    }
    exit 1
}
finally {
    Write-Verbose 'build-windows.ps1 exiting.'
}
