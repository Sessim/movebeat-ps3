# Dot-sourced by the RPCS3 smoke scripts.
#
# The game replaces run_status.ini by unlinking it and renaming a fresh copy
# over it. RPCS3's sys_fs_unlink crashes the emulated process (access
# violation) when Windows refuses the delete because another process has the
# file open without FILE_SHARE_DELETE, which Get-Content does. Read it with
# every share mode, so the game can replace the file while it is being read.
function Read-SharedLines {
    param([Parameter(Mandatory = $true)][string]$Path)
    $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
    } catch [System.Management.Automation.MethodInvocationException] {
        # Callers catch the IOException (file missing between replace steps).
        throw $_.Exception.InnerException
    }
    try {
        $reader = New-Object IO.StreamReader($stream)
        $lines = New-Object 'System.Collections.Generic.List[string]'
        while ($null -ne ($line = $reader.ReadLine())) { $lines.Add($line) }
        return , $lines.ToArray()
    } finally {
        $stream.Dispose()
    }
}
