[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string]$SourcePath
)

# Static-only inventory. This does not import, compile, or execute the payload.
$lineNumber = 0
$rows = foreach ($line in Get-Content -LiteralPath $SourcePath -Encoding UTF8) {
    $lineNumber++
    $pattern = '(?<operation>getattr|hasattr|setattr|delattr)\([^,]+,\s*["''](?<field>[A-Za-z_][A-Za-z0-9_]*)["'']'
    foreach ($match in [regex]::Matches($line, $pattern)) {
        [PSCustomObject]@{
            Line = $lineNumber
            Operation = $match.Groups['operation'].Value
            Field = $match.Groups['field'].Value
            Source = $line.Trim()
        }
    }
}

$rows |
    Sort-Object Field, Line |
    Format-Table -AutoSize

"`nSummary: {0} literal attribute-access occurrences; {1} unique names." -f $rows.Count, (($rows | Select-Object -ExpandProperty Field -Unique).Count)
