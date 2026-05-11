# MCOS Resource Pool Governor (Windows Job Objects)
#
# Pure Win32 P/Invoke. No external dependencies. Implements the policy
# declared in .claude/mcp-state/pool-policy.json.
#
# Job-Object semantics chosen here:
#   - JOBOBJECT_CPU_RATE_CONTROL with HARD_CAP at 9000 (=90.00% of total
#     cycles across all logical processors).
#   - JOB_OBJECT_LIMIT_JOB_MEMORY at 90% of physical memory (combined
#     commit across all processes in the job).
#   - JOB_OBJECT_LIMIT_AFFINITY across the full 16-core mask.
#   - BREAKAWAY_OK NOT set so children inherit the job.
#   - KILL_ON_JOB_CLOSE NOT set so closing handles does not terminate
#     workers.
#
# Cmdlets:
#   Initialize-McosResourcePool   -- idempotent create + apply limits
#   Add-ProcessToMcosResourcePool -- assign a pid to a named pool
#   Get-McosResourcePoolStatus    -- real Job Object accounting
#   Test-PoolGovernor             -- self-test against a sleep child

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# P/Invoke surface
# ---------------------------------------------------------------------------
if (-not ("McosPool.JobApi" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

namespace McosPool {

    [StructLayout(LayoutKind.Sequential)]
    public struct IO_COUNTERS {
        public ulong ReadOperationCount;
        public ulong WriteOperationCount;
        public ulong OtherOperationCount;
        public ulong ReadTransferCount;
        public ulong WriteTransferCount;
        public ulong OtherTransferCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_LIMIT_INFORMATION {
        public long PerProcessUserTimeLimit;
        public long PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize;
        public UIntPtr MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass;
        public uint SchedulingClass;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION {
        public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
        public IO_COUNTERS IoInfo;
        public UIntPtr ProcessMemoryLimit;
        public UIntPtr JobMemoryLimit;
        public UIntPtr PeakProcessMemoryUsed;
        public UIntPtr PeakJobMemoryUsed;
    }

    [StructLayout(LayoutKind.Explicit)]
    public struct JOBOBJECT_CPU_RATE_CONTROL_INFORMATION {
        [FieldOffset(0)] public uint ControlFlags;
        [FieldOffset(4)] public uint CpuRate;       // when ENABLE | HARD_CAP
        [FieldOffset(4)] public uint Weight;        // when ENABLE without HARD_CAP
        [FieldOffset(4)] public ushort MinRate;     // when MIN_MAX_RATE
        [FieldOffset(6)] public ushort MaxRate;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_ACCOUNTING_INFORMATION {
        public long TotalUserTime;
        public long TotalKernelTime;
        public long ThisPeriodTotalUserTime;
        public long ThisPeriodTotalKernelTime;
        public uint TotalPageFaultCount;
        public uint TotalProcesses;
        public uint ActiveProcesses;
        public uint TotalTerminatedProcesses;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION {
        public JOBOBJECT_BASIC_ACCOUNTING_INFORMATION BasicInfo;
        public IO_COUNTERS IoInfo;
    }

    public static class JobApi {
        public const uint JOB_OBJECT_LIMIT_JOB_MEMORY     = 0x00000200;
        public const uint JOB_OBJECT_LIMIT_AFFINITY       = 0x00000010;
        public const uint JOB_OBJECT_LIMIT_BREAKAWAY_OK   = 0x00000800;
        public const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000;

        public const uint JOB_OBJECT_CPU_RATE_CONTROL_ENABLE   = 0x1;
        public const uint JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP = 0x4;

        public const int JobObjectBasicAccountingInformation        = 1;
        public const int JobObjectBasicAndIoAccountingInformation   = 8;
        public const int JobObjectExtendedLimitInformation          = 9;
        public const int JobObjectCpuRateControlInformation         = 15;

        // Access rights for OpenJobObject
        public const uint JOB_OBJECT_ASSIGN_PROCESS = 0x0001;
        public const uint JOB_OBJECT_SET_ATTRIBUTES = 0x0002;
        public const uint JOB_OBJECT_QUERY          = 0x0004;
        public const uint JOB_OBJECT_TERMINATE      = 0x0008;
        public const uint JOB_OBJECT_ALL_ACCESS     = 0x1F001F;

        public const uint PROCESS_TERMINATE   = 0x0001;
        public const uint PROCESS_SET_QUOTA   = 0x0100;
        public const uint PROCESS_QUERY_INFO  = 0x0400;

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr CreateJobObjectW(IntPtr lpJobAttributes, string lpName);

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        public static extern IntPtr OpenJobObjectW(uint dwDesiredAccess, bool bInheritHandle, string lpName);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool SetInformationJobObject(IntPtr hJob, int JobObjectInfoClass, IntPtr lpJobObjectInfo, uint cbJobObjectInfoLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool QueryInformationJobObject(IntPtr hJob, int JobObjectInfoClass, IntPtr lpJobObjectInfo, uint cbJobObjectInfoLength, IntPtr lpReturnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool AssignProcessToJobObject(IntPtr hJob, IntPtr hProcess);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr OpenProcess(uint dwDesiredAccess, bool bInheritHandle, uint dwProcessId);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern IntPtr GetCurrentProcess();

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool CloseHandle(IntPtr hObject);

        [DllImport("kernel32.dll", SetLastError = true)]
        public static extern bool IsProcessInJob(IntPtr ProcessHandle, IntPtr JobHandle, out bool Result);
    }
}
"@
}

# ---------------------------------------------------------------------------
# Policy loader
# ---------------------------------------------------------------------------
function Get-McosPoolPolicy {
    [CmdletBinding()]
    param(
        [string]$PolicyPath = (Join-Path $PSScriptRoot "..\mcp-state\pool-policy.json")
    )
    $resolved = Resolve-Path $PolicyPath -ErrorAction Stop
    return (Get-Content $resolved -Raw | ConvertFrom-Json)
}

# ---------------------------------------------------------------------------
# Core: open-or-create a named Job Object and apply limits
# ---------------------------------------------------------------------------
function Initialize-McosResourcePool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$PoolKey,        # e.g. "mcp-servers"
        [object]$PolicyObject = $null,                  # optional pre-loaded policy
        [switch]$KeepHandle                             # if set, returns the open handle (anchor)
    )

    if (-not $PolicyObject) { $PolicyObject = Get-McosPoolPolicy }
    $pool = $PolicyObject.pools.$PoolKey
    if (-not $pool) { throw "Pool '$PoolKey' not in pool-policy.json" }

    $name = $pool.jobObjectName

    # OpenJobObject with full access; if it doesn't exist, create it.
    $hJob = [McosPool.JobApi]::OpenJobObjectW([McosPool.JobApi]::JOB_OBJECT_ALL_ACCESS, $false, $name)
    $created = $false
    if ($hJob -eq [IntPtr]::Zero) {
        $hJob = [McosPool.JobApi]::CreateJobObjectW([IntPtr]::Zero, $name)
        if ($hJob -eq [IntPtr]::Zero) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "CreateJobObjectW('$name') failed: Win32 error $err"
        }
        $created = $true
    }

    # --- Extended limit info: memory cap + affinity + flags -----------------
    # NOTE: PowerShell exposes value-type FIELDS by COPY. Mutating
    # $ext.BasicLimitInformation.LimitFlags would modify a throwaway copy;
    # we must read the nested struct, mutate, and write the whole thing back.
    $ext   = New-Object McosPool.JOBOBJECT_EXTENDED_LIMIT_INFORMATION
    $basic = $ext.BasicLimitInformation
    $flags = [McosPool.JobApi]::JOB_OBJECT_LIMIT_JOB_MEMORY -bor [McosPool.JobApi]::JOB_OBJECT_LIMIT_AFFINITY
    $basic.LimitFlags = $flags
    # PowerShell can't auto-parse "0xFFFF" string to uint; do it explicitly.
    $affinityValue = [Convert]::ToUInt64($pool.limits.affinityMaskHex, 16)
    $basic.Affinity = [UIntPtr]::new($affinityValue)
    $ext.BasicLimitInformation = $basic
    $ext.JobMemoryLimit = [UIntPtr]::new([uint64]$pool.limits.jobMemoryBytes)

    $size = [System.Runtime.InteropServices.Marshal]::SizeOf([type][McosPool.JOBOBJECT_EXTENDED_LIMIT_INFORMATION])
    $buf  = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($size)
    try {
        [System.Runtime.InteropServices.Marshal]::StructureToPtr($ext, $buf, $false)
        $ok = [McosPool.JobApi]::SetInformationJobObject($hJob, [McosPool.JobApi]::JobObjectExtendedLimitInformation, $buf, [uint32]$size)
        if (-not $ok) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "SetInformationJobObject(Extended) failed: Win32 error $err"
        }
    } finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buf)
    }

    # --- CPU rate control: HARD_CAP at policy.cpuRateHundredthsOfPercent ---
    $cpu = New-Object McosPool.JOBOBJECT_CPU_RATE_CONTROL_INFORMATION
    $cpuFlags = [McosPool.JobApi]::JOB_OBJECT_CPU_RATE_CONTROL_ENABLE
    if ($pool.limits.cpuRateHardCap) { $cpuFlags = $cpuFlags -bor [McosPool.JobApi]::JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP }
    $cpu.ControlFlags = $cpuFlags
    $cpu.CpuRate = [uint32]$pool.limits.cpuRateHundredthsOfPercent

    $cpuSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][McosPool.JOBOBJECT_CPU_RATE_CONTROL_INFORMATION])
    $cpuBuf  = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($cpuSize)
    try {
        [System.Runtime.InteropServices.Marshal]::StructureToPtr($cpu, $cpuBuf, $false)
        $ok = [McosPool.JobApi]::SetInformationJobObject($hJob, [McosPool.JobApi]::JobObjectCpuRateControlInformation, $cpuBuf, [uint32]$cpuSize)
        if (-not $ok) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            throw "SetInformationJobObject(CpuRate) failed: Win32 error $err"
        }
    } finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($cpuBuf)
    }

    $result = [PSCustomObject]@{
        PoolKey            = $PoolKey
        JobObjectName      = $name
        Created            = $created
        Reused             = -not $created
        CpuRateHundredths  = $pool.limits.cpuRateHundredthsOfPercent
        CpuHardCap         = [bool]$pool.limits.cpuRateHardCap
        JobMemoryBytes     = $pool.limits.jobMemoryBytes
        AffinityMaskHex    = $pool.limits.affinityMaskHex
        Handle             = $hJob
    }

    if (-not $KeepHandle) {
        [void][McosPool.JobApi]::CloseHandle($hJob)
        $result.Handle = [IntPtr]::Zero
    }
    return $result
}

# ---------------------------------------------------------------------------
# Assign a process by pid to a named pool
# ---------------------------------------------------------------------------
function Add-ProcessToMcosResourcePool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$PoolKey,
        [Parameter(Mandatory)] [int]$ProcessId,
        [object]$PolicyObject = $null
    )

    if (-not $PolicyObject) { $PolicyObject = Get-McosPoolPolicy }
    $pool = $PolicyObject.pools.$PoolKey
    if (-not $pool) { throw "Pool '$PoolKey' not in pool-policy.json" }

    $hJob = [McosPool.JobApi]::OpenJobObjectW([McosPool.JobApi]::JOB_OBJECT_ALL_ACCESS, $false, $pool.jobObjectName)
    if ($hJob -eq [IntPtr]::Zero) {
        # Pool wasn't initialized yet; bring it up now.
        $init = Initialize-McosResourcePool -PoolKey $PoolKey -PolicyObject $PolicyObject -KeepHandle
        $hJob = $init.Handle
    }

    $hProc = [McosPool.JobApi]::OpenProcess(
        ([McosPool.JobApi]::PROCESS_SET_QUOTA -bor [McosPool.JobApi]::PROCESS_TERMINATE -bor [McosPool.JobApi]::PROCESS_QUERY_INFO),
        $false, [uint32]$ProcessId)
    if ($hProc -eq [IntPtr]::Zero) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        [void][McosPool.JobApi]::CloseHandle($hJob)
        throw "OpenProcess(pid=$ProcessId) failed: Win32 error $err"
    }

    try {
        # Idempotent skip if already in this exact pool. Otherwise attempt the
        # assign and let kernel32 decide — Win8+ supports nested jobs, so
        # being in some outer (Claude Code / session) job is not fatal.
        $inThisJob = $false
        [void][McosPool.JobApi]::IsProcessInJob($hProc, $hJob, [ref]$inThisJob)
        if ($inThisJob) {
            return [PSCustomObject]@{ ProcessId = $ProcessId; PoolKey = $PoolKey; Assigned = $false; Reason = "already in this pool" }
        }

        $ok = [McosPool.JobApi]::AssignProcessToJobObject($hJob, $hProc)
        if (-not $ok) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            return [PSCustomObject]@{ ProcessId = $ProcessId; PoolKey = $PoolKey; Assigned = $false; Reason = "AssignProcessToJobObject failed: Win32 error $err (likely outer job blocks nesting with HARD_CAP)" }
        }
        return [PSCustomObject]@{ ProcessId = $ProcessId; PoolKey = $PoolKey; Assigned = $true }
    }
    finally {
        [void][McosPool.JobApi]::CloseHandle($hProc)
        [void][McosPool.JobApi]::CloseHandle($hJob)
    }
}

# ---------------------------------------------------------------------------
# Honest accounting: query basic accounting + extended limits
# ---------------------------------------------------------------------------
function Get-McosResourcePoolStatus {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$PoolKey,
        [object]$PolicyObject = $null
    )

    if (-not $PolicyObject) { $PolicyObject = Get-McosPoolPolicy }
    $pool = $PolicyObject.pools.$PoolKey
    if (-not $pool) { throw "Pool '$PoolKey' not in pool-policy.json" }

    $hJob = [McosPool.JobApi]::OpenJobObjectW([McosPool.JobApi]::JOB_OBJECT_QUERY, $false, $pool.jobObjectName)
    if ($hJob -eq [IntPtr]::Zero) {
        return [PSCustomObject]@{
            PoolKey       = $PoolKey
            JobObjectName = $pool.jobObjectName
            Exists        = $false
            Note          = "pool not initialized this session"
        }
    }
    try {
        # Accounting
        $accSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][McosPool.JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION])
        $accBuf  = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($accSize)
        try {
            $ok = [McosPool.JobApi]::QueryInformationJobObject($hJob, [McosPool.JobApi]::JobObjectBasicAndIoAccountingInformation, $accBuf, [uint32]$accSize, [IntPtr]::Zero)
            if (-not $ok) {
                $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "QueryInformationJobObject(Accounting) failed: Win32 error $err"
            }
            $acc = [System.Runtime.InteropServices.Marshal]::PtrToStructure($accBuf, [type][McosPool.JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION])
        } finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($accBuf)
        }

        # Extended limits (for PeakJobMemoryUsed)
        $extSize = [System.Runtime.InteropServices.Marshal]::SizeOf([type][McosPool.JOBOBJECT_EXTENDED_LIMIT_INFORMATION])
        $extBuf  = [System.Runtime.InteropServices.Marshal]::AllocHGlobal($extSize)
        try {
            $ok = [McosPool.JobApi]::QueryInformationJobObject($hJob, [McosPool.JobApi]::JobObjectExtendedLimitInformation, $extBuf, [uint32]$extSize, [IntPtr]::Zero)
            if (-not $ok) {
                $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
                throw "QueryInformationJobObject(Extended) failed: Win32 error $err"
            }
            $ext = [System.Runtime.InteropServices.Marshal]::PtrToStructure($extBuf, [type][McosPool.JOBOBJECT_EXTENDED_LIMIT_INFORMATION])
        } finally {
            [System.Runtime.InteropServices.Marshal]::FreeHGlobal($extBuf)
        }

        return [PSCustomObject]@{
            PoolKey                 = $PoolKey
            JobObjectName           = $pool.jobObjectName
            Exists                  = $true
            ActiveProcesses         = [int]$acc.BasicInfo.ActiveProcesses
            TotalProcessesEverInJob = [int]$acc.BasicInfo.TotalProcesses
            TotalUserTime100ns      = [long]$acc.BasicInfo.TotalUserTime
            TotalKernelTime100ns    = [long]$acc.BasicInfo.TotalKernelTime
            PeakJobMemoryBytes      = [int64][uint64]$ext.PeakJobMemoryUsed
            JobMemoryLimitBytes     = [int64][uint64]$ext.JobMemoryLimit
            AuthorizedCpuPercent    = $pool.authorizedShare.cpuPercent
            AuthorizedMemoryPercent = $pool.authorizedShare.memoryPercent
        }
    } finally {
        [void][McosPool.JobApi]::CloseHandle($hJob)
    }
}

# ---------------------------------------------------------------------------
# Connect (init + assign self) atomically. Caller MUST hold the returned
# handle for the lifetime of the calling process (typically: just keep the
# handle in scope and never call CloseHandle on it).
# ---------------------------------------------------------------------------
function Connect-McosResourcePool {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)] [string]$PoolKey,
        [object]$PolicyObject = $null
    )
    if (-not $PolicyObject) { $PolicyObject = Get-McosPoolPolicy }
    # Initialize with -KeepHandle so the kernel object never has zero
    # handles + zero processes during the assign.
    $init = Initialize-McosResourcePool -PoolKey $PoolKey -PolicyObject $PolicyObject -KeepHandle
    $hJob = $init.Handle

    $hProc = [McosPool.JobApi]::OpenProcess(
        ([McosPool.JobApi]::PROCESS_SET_QUOTA -bor [McosPool.JobApi]::PROCESS_TERMINATE -bor [McosPool.JobApi]::PROCESS_QUERY_INFO),
        $false, [uint32]$PID)
    if ($hProc -eq [IntPtr]::Zero) {
        $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        [void][McosPool.JobApi]::CloseHandle($hJob)
        throw "OpenProcess(self) failed: Win32 error $err"
    }
    try {
        $inThisJob = $false
        [void][McosPool.JobApi]::IsProcessInJob($hProc, $hJob, [ref]$inThisJob)
        if ($inThisJob) {
            return [PSCustomObject]@{ PoolKey = $PoolKey; JobObjectName = $init.JobObjectName; Handle = $hJob; Assigned = $false; Reason = "already in this pool" }
        }
        $ok = [McosPool.JobApi]::AssignProcessToJobObject($hJob, $hProc)
        if (-not $ok) {
            $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
            return [PSCustomObject]@{ PoolKey = $PoolKey; JobObjectName = $init.JobObjectName; Handle = $hJob; Assigned = $false; Reason = "AssignProcessToJobObject failed: Win32 error $err (likely outer job blocks nesting with HARD_CAP)" }
        }
        return [PSCustomObject]@{ PoolKey = $PoolKey; JobObjectName = $init.JobObjectName; Handle = $hJob; Assigned = $true }
    } finally {
        [void][McosPool.JobApi]::CloseHandle($hProc)
        # NOTE: $hJob is intentionally NOT closed here. The caller must hold
        # it for the lifetime of the process so the named kernel object
        # remains addressable to other processes.
    }
}

# ---------------------------------------------------------------------------
# Self-test: spawn a sleep, assign it, query, terminate
# ---------------------------------------------------------------------------
function Test-PoolGovernor {
    [CmdletBinding()]
    param(
        [string]$PoolKey = "mcp-servers"
    )
    Write-Host "Initializing pool '$PoolKey'..." -ForegroundColor Cyan
    $init = Initialize-McosResourcePool -PoolKey $PoolKey
    Write-Host ("  job=$($init.JobObjectName) created=$($init.Created)") -ForegroundColor DarkGray

    Write-Host "Spawning a 5-second sleep child..." -ForegroundColor Cyan
    $proc = Start-Process -FilePath "powershell.exe" -ArgumentList "-NoProfile","-Command","Start-Sleep -Seconds 5" -PassThru -WindowStyle Hidden

    $assign = Add-ProcessToMcosResourcePool -PoolKey $PoolKey -ProcessId $proc.Id
    Write-Host ("  pid=$($proc.Id) assigned=$($assign.Assigned)" + ($(if ($assign.Reason) { " reason='$($assign.Reason)'" } else { "" }))) -ForegroundColor DarkGray

    $status = Get-McosResourcePoolStatus -PoolKey $PoolKey
    Write-Host "Pool status after assignment:" -ForegroundColor Cyan
    $status | Format-List

    Write-Host "Cleaning up child..." -ForegroundColor Cyan
    try { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue } catch {}
    return $status
}

Export-ModuleMember -Function Get-McosPoolPolicy, Initialize-McosResourcePool, Add-ProcessToMcosResourcePool, Connect-McosResourcePool, Get-McosResourcePoolStatus, Test-PoolGovernor
