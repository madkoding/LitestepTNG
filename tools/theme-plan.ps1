# Builds a theme's install plan: which modules and fonts it needs and whether each one is
# ok, mapped to an nModule, missing or blocked (spec:
# docs\superpowers\specs\2026-11-26-theme-install-design.md, sections 2 and 3).
# Dot-source it; Windows PowerShell 5.1 compatible. Nothing here downloads or changes the
# theme: the plan file (<run folder>\plans\<Theme>.json) is what a future downloader reads.
#
# Modules are resolved exactly like litestep\ModuleManager.cpp + ModuleResolver.cpp:
#   - name = file name only, lower case, no .dll, no trailing "-<digit>..." (ConvertTo-ModuleKey);
#   - no *ModuleAlias for it, or the theme already names the target (its file name sans .dll
#     equals the target and it has no folder, the run folder, or a $Variable$ folder): the DLL as
#     written -> ok if it exists, else missing; a bare name (no folder, no extension) stands for
#     <run folder>\<name>.dll;
#   - <run folder>\<Target>.dll exists -> mapped;
#   - else the original exists -> ok, with note "original used, nModule not installed";
#   - else missing, named after the target nModule.
#   Desk is blocked when the theme's mode is overlay (the default).
#
# Item order: the manifest's modules (manifest order), then the modules the theme's files load
# that the manifest does not list (LoadModule lines in file order, following include, then
# *NetLoadModule, then *NetInstallModule lines, as litestep reads them), then the manifest's
# fonts. Two modules that resolve to the same file are reported once (the first).
# Without a manifest (or with one that is not valid JSON) the modules come from the scan only
# and there are no fonts.

$script:DeskReason = "takes over Explorer's desktop"
$script:FallbackNote = 'original used, nModule not installed'

# File name only (everything up to the last \, / or $ is a directory), lower case, no .dll and
# no trailing "-" + text that starts with a digit. Pinned by tools\tests\fixtures\module-names.txt.
function ConvertTo-ModuleKey([string]$Name) {
    $key = ($Name -split '[\\/$]')[-1].ToLowerInvariant()
    if ($key.EndsWith('.dll')) { $key = $key.Substring(0, $key.Length - 4) }
    return ($key -replace '-[0-9].*$', '')
}

# Splits an rc line into tokens: whitespace separates them, "..." and '...' group (quotes
# removed), and a ; outside quotes starts a comment.
function Split-RcLine([string]$Line) {
    $tokens = New-Object System.Collections.Generic.List[string]
    $current = New-Object System.Text.StringBuilder
    $quote = [char]0
    $inToken = $false
    foreach ($c in $Line.ToCharArray()) {
        if ($quote -ne [char]0) {
            if ($c -eq $quote) { $quote = [char]0 } else { [void]$current.Append($c) }
            continue
        }
        if ($c -eq '"' -or $c -eq "'") { $quote = $c; $inToken = $true; continue }
        if ($c -eq ';') { break }
        if ($c -eq ' ' -or $c -eq "`t") {
            if ($inToken) { $tokens.Add($current.ToString()); [void]$current.Clear(); $inToken = $false }
            continue
        }
        [void]$current.Append($c); $inToken = $true
    }
    if ($inToken) { $tokens.Add($current.ToString()) }
    return , $tokens.ToArray()
}

function Get-RunFolderPrefix([string]$LiteStepDir) {
    $dir = [IO.Path]::GetFullPath($LiteStepDir)
    if (-not $dir.EndsWith('\')) { $dir += '\' }
    return $dir
}

# Adds the *ModuleAlias lines of one file to $Map (lower-case normalised name -> target); a name
# already in the map keeps its target (first line wins, as in the shell).
function Add-ModuleAliases([hashtable]$Map, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        $tokens = Split-RcLine $line
        if ($tokens.Count -ne 3 -or $tokens[0] -ne '*ModuleAlias') { continue }
        $from = ConvertTo-ModuleKey $tokens[1]
        if ($from -and -not $Map.ContainsKey($from)) { $Map[$from] = $tokens[2] }
    }
}

# The module map the shell uses: personal.rc first (step.rc includes it before the map), then
# config\module-map.rc. Lower-case normalised name -> target (DLL name without .dll).
function Get-ModuleMap([string]$LiteStepDir) {
    $map = @{}
    Add-ModuleAliases $map (Join-Path $LiteStepDir 'personal.rc')
    Add-ModuleAliases $map (Join-Path $LiteStepDir 'config\module-map.rc')
    return $map
}

# Reads the theme's rc files from theme.rc, following include lines. $LiteStepDir$ and
# $ThemeName$ are expanded; a path inside <run>\themes\<ThemeName>\ is read from ThemeDir
# (so a theme can be planned before it is copied there). Missing includes are skipped, as the
# shell does. Returns LoadModule, *NetLoadModule, *NetInstallModule and *ModuleAlias lines.
function Read-ThemeModuleLines([string]$ThemeDir, [string]$LiteStepDir) {
    $themeName = Split-Path $ThemeDir -Leaf
    $runPrefix = Get-RunFolderPrefix $LiteStepDir
    $themePrefix = $runPrefix + 'themes\' + $themeName + '\'
    $themeDirFull = Get-RunFolderPrefix $ThemeDir
    $result = @{
        Load = New-Object System.Collections.Generic.List[string]
        NetLoad = New-Object System.Collections.Generic.List[string]
        NetInstall = New-Object System.Collections.Generic.List[string]
        Aliases = New-Object System.Collections.Generic.List[object]
    }
    $visited = @{}
    $expand = {
        param([string]$s)
        $s = $s -ireplace '\$LiteStepDir\$', ($runPrefix -replace '\$', '$$$$')
        return ($s -ireplace '\$ThemeName\$', ($themeName -replace '\$', '$$$$'))
    }
    $read = $null
    $read = {
        param([string]$path)
        $full = [IO.Path]::GetFullPath($path)
        if ($visited.ContainsKey($full.ToLowerInvariant())) { return }
        $visited[$full.ToLowerInvariant()] = $true
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { return }
        foreach ($line in [IO.File]::ReadAllLines($full)) {
            $tokens = Split-RcLine $line
            if ($tokens.Count -lt 2) { continue }
            $key = $tokens[0]
            if ($key -eq 'include') {
                $target = & $expand $tokens[1]
                if ($target.StartsWith($themePrefix, [StringComparison]::OrdinalIgnoreCase)) {
                    $target = $themeDirFull + $target.Substring($themePrefix.Length)
                }
                elseif (-not [IO.Path]::IsPathRooted($target)) {
                    $target = Join-Path $themeDirFull $target
                }
                if ($target.IndexOf('$') -lt 0) { & $read $target }
            }
            elseif ($key -eq 'LoadModule') { $result.Load.Add((& $expand $tokens[1])) }
            elseif ($key -eq '*NetLoadModule') { $result.NetLoad.Add((& $expand $tokens[1])) }
            elseif ($key -eq '*NetInstallModule') { $result.NetInstall.Add((& $expand $tokens[1])) }
            elseif ($key -eq '*ModuleAlias' -and $tokens.Count -eq 3) { $result.Aliases.Add($tokens) }
        }
    }
    & $read (Join-Path $ThemeDir 'theme.rc')
    return $result
}

# The module's name as the theme wrote it: file name only, without .dll ("$LiteStepDir$Popup.dll"
# -> "Popup", "label-1.99" -> "label-1.99").
function Get-ModuleDisplayName([string]$Location) {
    $name = ($Location -split '[\\/$]')[-1]
    if ($name.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)) { $name = $name.Substring(0, $name.Length - 4) }
    return $name
}

# Resolves one module location (as written, variables expanded) to a plan item. Returns
# @{ Key = <lower-case resolved path, for duplicates>; Item = [ordered] item }.
function Resolve-PlanModule([string]$Location, [hashtable]$Map, [string]$RunPrefix, [string]$Mode,
    [object]$Url = $null, [object]$Sha256 = $null) {
    $display = Get-ModuleDisplayName $Location
    $key = ConvertTo-ModuleKey $Location
    $endsWithDll = $Location.EndsWith('.dll', [StringComparison]::OrdinalIgnoreCase)
    $isBare = ($Location.IndexOfAny([char[]]'\/:$') -lt 0) -and -not $endsWithDll
    $original = if ($isBare) { $RunPrefix + $Location + '.dll' } else { $Location }
    $shortName = {
        param([string]$path)
        if ($path.StartsWith($RunPrefix, [StringComparison]::OrdinalIgnoreCase) -and
            $path.Substring($RunPrefix.Length).IndexOfAny([char[]]'\/') -lt 0) {
            return $path.Substring($RunPrefix.Length)
        }
        return $path
    }
    $missing = {
        param([string]$name, [string]$note)
        $item = [ordered]@{ kind = 'module'; name = $name; status = 'missing' }
        if ($note) { $item.note = $note }
        $item.url = $Url
        $item.sha256 = $Sha256
        return $item
    }

    if ($key -eq 'desk' -and $Mode -ne 'shell') {
        return @{ Key = $original.ToLowerInvariant()
            Item = [ordered]@{ kind = 'module'; name = $display; status = 'blocked'; reason = $script:DeskReason } }
    }

    $target = if ($Map.ContainsKey($key)) { $Map[$key] } else { $null }
    $isTargetItself = $false
    if ($target) {
        $slash = $Location.LastIndexOfAny([char[]]'\/$')
        $folder = if ($slash -lt 0) { '' } else { $Location.Substring(0, $slash + 1) }
        $isTargetItself = ($display -ieq $target) -and
            ($folder -eq '' -or $folder.IndexOf('$') -ge 0 -or $folder -ieq $RunPrefix)
    }

    if (-not $target -or $isTargetItself) {
        if (Test-Path -LiteralPath $original -PathType Leaf) {
            return @{ Key = $original.ToLowerInvariant()
                Item = [ordered]@{ kind = 'module'; name = $display; status = 'ok'; resolved = (& $shortName $original) } }
        }
        return @{ Key = $original.ToLowerInvariant(); Item = (& $missing $display $null) }
    }

    $targetPath = $RunPrefix + $target + '.dll'
    if (Test-Path -LiteralPath $targetPath -PathType Leaf) {
        return @{ Key = $targetPath.ToLowerInvariant()
            Item = [ordered]@{ kind = 'module'; name = $display; status = 'mapped'; resolved = ($target + '.dll') } }
    }
    if (Test-Path -LiteralPath $original -PathType Leaf) {
        return @{ Key = $original.ToLowerInvariant()
            Item = [ordered]@{ kind = 'module'; name = $display; status = 'ok'; resolved = (& $shortName $original)
                note = $script:FallbackNote } }
    }
    return @{ Key = $targetPath.ToLowerInvariant(); Item = (& $missing $target ('replaces ' + $display)) }
}

# Font family names installed for the machine and the user: the Fonts registry keys (value
# names without the "(TrueType)" kind) and the files in %LOCALAPPDATA%\Microsoft\Windows\Fonts.
# Lower case. Read once per session.
function Get-InstalledFontNames {
    if ($script:InstalledFonts) { return $script:InstalledFonts }
    $names = @{}
    foreach ($key in 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts',
        'HKCU:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts') {
        $props = Get-ItemProperty -Path $key -ErrorAction SilentlyContinue
        if (-not $props) { continue }
        foreach ($p in $props.PSObject.Properties) {
            if ($p.Name -like 'PS*') { continue }
            $value = ($p.Name -replace '\s*\([^)]*\)\s*$', '')
            foreach ($part in ($value -split '\s+&\s+')) { $names[$part.Trim().ToLowerInvariant()] = $true }
        }
    }
    $userFonts = Join-Path $env:LOCALAPPDATA 'Microsoft\Windows\Fonts'
    if (Test-Path -LiteralPath $userFonts) {
        try {
            Add-Type -AssemblyName System.Drawing
            $collection = New-Object System.Drawing.Text.PrivateFontCollection
            Get-ChildItem -LiteralPath $userFonts -File -Include '*.ttf', '*.otf', '*.ttc' -Recurse |
                ForEach-Object { try { $collection.AddFontFile($_.FullName) } catch { } }
            foreach ($family in $collection.Families) { $names[$family.Name.ToLowerInvariant()] = $true }
            $collection.Dispose()
        }
        catch { }
    }
    $script:InstalledFonts = $names
    return $names
}

# True when a font family is installed: a registered name equals it, or is it followed by a style
# ("Exo 2 Bold" for "Exo 2").
function Test-FontInstalled([string]$Name) {
    $wanted = $Name.Trim().ToLowerInvariant()
    foreach ($installed in (Get-InstalledFontNames).Keys) {
        if ($installed -eq $wanted -or $installed.StartsWith($wanted + ' ')) { return $true }
    }
    return $false
}

function Get-JsonProperty($Object, [string]$Name) {
    if ($null -eq $Object -or $Object -isnot [pscustomobject]) { return $null }
    $p = $Object.PSObject.Properties[$Name]
    if ($p) { return $p.Value }
    return $null
}

# The install plan of the theme in ThemeDir, checked against the run folder LiteStepDir.
function Get-ThemePlan {
    param(
        [Parameter(Mandatory = $true)][string]$ThemeDir,
        [Parameter(Mandatory = $true)][string]$LiteStepDir
    )
    $ThemeDir = [IO.Path]::GetFullPath($ThemeDir.TrimEnd('\', '/'))
    $runPrefix = Get-RunFolderPrefix $LiteStepDir
    $warnings = New-Object System.Collections.Generic.List[string]

    # Manifest (optional).
    $manifest = $null
    $manifestPath = Join-Path $ThemeDir 'theme.json'
    if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        try {
            $manifest = [IO.File]::ReadAllText($manifestPath) | ConvertFrom-Json -ErrorAction Stop
            if ($manifest -isnot [pscustomobject]) { throw 'not an object' }
        }
        catch {
            $manifest = $null
            $warnings.Add("theme.json is not valid JSON; the modules were read from the theme's files.")
        }
    }
    $mode = 'overlay'
    $manifestModules = @()
    $manifestFonts = @()
    if ($manifest) {
        $m = Get-JsonProperty $manifest 'mode'
        if ($m) {
            if ($m -eq 'overlay' -or $m -eq 'shell') { $mode = $m }
            else { $warnings.Add("theme.json: unknown mode '$m'; using overlay.") }
        }
        $requires = Get-JsonProperty $manifest 'requires'
        $manifestModules = @(Get-JsonProperty $requires 'modules' | Where-Object { $null -ne $_ })
        $manifestFonts = @(Get-JsonProperty $requires 'fonts' | Where-Object { $null -ne $_ })
    }

    # Module map: the shell's, plus *ModuleAlias lines in the theme (read after the map).
    $scan = Read-ThemeModuleLines $ThemeDir $LiteStepDir
    $map = Get-ModuleMap $LiteStepDir
    foreach ($tokens in $scan.Aliases) {
        $from = ConvertTo-ModuleKey $tokens[1]
        if ($from -and -not $map.ContainsKey($from)) { $map[$from] = $tokens[2] }
    }

    $items = New-Object System.Collections.Generic.List[object]
    $seen = @{}
    foreach ($entry in $manifestModules) {
        $name = if ($entry -is [string]) { $entry } else { [string](Get-JsonProperty $entry 'name') }
        if (-not $name) { $warnings.Add('theme.json: a module without a name was ignored.'); continue }
        $r = Resolve-PlanModule $name $map $runPrefix $mode (Get-JsonProperty $entry 'url') (Get-JsonProperty $entry 'sha256')
        if ($seen.ContainsKey($r.Key)) { continue }
        $seen[$r.Key] = $true
        $items.Add($r.Item)
    }
    foreach ($location in @($scan.Load) + @($scan.NetLoad) + @($scan.NetInstall)) {
        $r = Resolve-PlanModule $location $map $runPrefix $mode
        if ($seen.ContainsKey($r.Key)) { continue }
        $seen[$r.Key] = $true
        $items.Add($r.Item)
        if ($manifest) { $warnings.Add("theme.json does not list $($r.Item.name), which the theme loads.") }
    }
    foreach ($entry in $manifestFonts) {
        $name = if ($entry -is [string]) { $entry } else { [string](Get-JsonProperty $entry 'name') }
        if (-not $name) { $warnings.Add('theme.json: a font without a name was ignored.'); continue }
        if (Test-FontInstalled $name) {
            $items.Add([ordered]@{ kind = 'font'; name = $name; status = 'ok' })
        }
        else {
            $item = [ordered]@{ kind = 'font'; name = $name; status = 'missing' }
            $note = Get-JsonProperty $entry 'note'
            if ($note) { $item.note = $note }
            $item.url = Get-JsonProperty $entry 'url'
            $items.Add($item)
        }
    }

    return [pscustomobject][ordered]@{
        theme = (Split-Path $ThemeDir -Leaf)
        generated = (Get-Date).ToUniversalTime().ToString("yyyy-MM-dd'T'HH:mm:ss'Z'", [Globalization.CultureInfo]::InvariantCulture)
        items = @($items | ForEach-Object { [pscustomobject]$_ })
        warnings = [string[]]$warnings.ToArray()
    }
}

# The plan as JSON text (items and warnings always arrays).
function ConvertTo-ThemePlanJson($Plan) {
    $copy = [pscustomobject][ordered]@{
        theme = $Plan.theme
        generated = $Plan.generated
        items = [object[]]@($Plan.items)
        warnings = [object[]]@($Plan.warnings)
    }
    return (ConvertTo-Json -InputObject $copy -Depth 6)
}

# Writes <LiteStepDir>\plans\<Theme>.json (UTF-8 without BOM) and returns its path.
function Write-ThemePlan($Plan, [string]$LiteStepDir) {
    $dir = Join-Path $LiteStepDir 'plans'
    if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $path = Join-Path $dir ($Plan.theme + '.json')
    [IO.File]::WriteAllText($path, (ConvertTo-ThemePlanJson $Plan), (New-Object Text.UTF8Encoding $false))
    return $path
}

# Prints the plan grouped by status: blocked, missing, mapped, ok; then the warnings.
function Show-ThemePlan($Plan) {
    Write-Host "Theme '$($Plan.theme)':"
    $groups = @(
        @{ Status = 'blocked'; Color = 'Red'; Title = 'Blocked (not loaded in this mode)' },
        @{ Status = 'missing'; Color = 'Yellow'; Title = 'Missing' },
        @{ Status = 'mapped'; Color = 'Cyan'; Title = 'Replaced by an nModule' },
        @{ Status = 'ok'; Color = 'Green'; Title = 'Ok' })
    foreach ($g in $groups) {
        $items = @($Plan.items | Where-Object { $_.status -eq $g.Status })
        if (-not $items.Count) { continue }
        Write-Host "  $($g.Title):" -ForegroundColor $g.Color
        foreach ($i in $items) {
            $text = "    $($i.kind) $($i.name)"
            $p = $i.PSObject.Properties
            if ($p['resolved'] -and $i.resolved -and $i.resolved -ne "$($i.name).dll") { $text += " -> $($i.resolved)" }
            if ($p['note'] -and $i.note) { $text += " ($($i.note))" }
            if ($p['reason'] -and $i.reason) { $text += ": $($i.reason)" }
            Write-Host $text -ForegroundColor $g.Color
        }
    }
    foreach ($w in @($Plan.warnings)) { if ($w) { Write-Host "  warning: $w" -ForegroundColor Yellow } }
}
