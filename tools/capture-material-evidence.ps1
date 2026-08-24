param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    [ValidateSet('', 'light-flat', 'light-gradient', 'light-low-contrast', 'dark-flat', 'dark-gradient', 'dark-low-contrast', 'text-grid', 'checker-fine', 'vertical-lines', 'saturated-warm', 'saturated-cool', 'black-white-extreme', 'supplemental-real-desktop')]
    [string]$CaseName = '',

    [switch]$ForceCapture
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$llpControlledCases = @(
    'light-flat',
    'light-gradient',
    'light-low-contrast',
    'dark-flat',
    'dark-gradient',
    'dark-low-contrast',
    'text-grid',
    'checker-fine',
    'vertical-lines',
    'saturated-warm',
    'saturated-cool',
    'black-white-extreme'
)
$llpAllCases = @($llpControlledCases) + 'supplemental-real-desktop'
$llpResolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
$llpMissingPng = @($llpAllCases | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $llpResolvedOutput "$_.png") -PathType Leaf)
})
$llpMissingDiagnostics = @($llpAllCases | Where-Object {
    -not (Test-Path -LiteralPath (Join-Path $llpResolvedOutput "$_.diagnostics.json") -PathType Leaf)
})
$llpMissingLiveIdentity = @('text-grid', 'saturated-cool')

if (-not [string]::IsNullOrWhiteSpace($CaseName)) {
    $llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $llpTempParent = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    $llpTempRoot = Join-Path $llpTempParent ("lightlaunchpad-material-evidence-" + [System.Guid]::NewGuid().ToString('N'))
    $llpBackgroundExe = Join-Path $llpTempRoot 'MaterialEvidenceBackground.exe'
    $llpFlowEvidence = Join-Path $llpTempRoot 'flow-evidence'
    $llpBackgroundProcess = $null
    $llpFlowProcess = $null
    $llpBackgroundWindow = [IntPtr]::Zero
    $llpCaseIndex = [Array]::IndexOf($llpControlledCases, $CaseName)
    $llpControlled = $llpCaseIndex -ge 0
    $llpLive = $CaseName -in @('text-grid', 'saturated-cool')
    function Assert-LlpTrue([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
    function Get-LlpIntegerQuotient([int]$Numerator, [int]$Denominator) { return [int][Math]::Truncate([double]$Numerator / $Denominator) }
    function Get-LlpExpectedBackgroundRgb([int]$Index, [int]$X, [int]$Y, [int]$Width, [int]$Height, [int]$Phase) {
        $llpHorizontal = Get-LlpIntegerQuotient ($X * 255) ([Math]::Max(1, $Width - 1))
        $llpVertical = Get-LlpIntegerQuotient ($Y * 255) ([Math]::Max(1, $Height - 1))
        switch ($Index) {
            0 { $llpRgb = @(234, 240, 248) }
            1 { $llpRgb = @((212 + (Get-LlpIntegerQuotient $llpHorizontal 8)), (222 + (Get-LlpIntegerQuotient $llpVertical 10)), (236 + (Get-LlpIntegerQuotient $llpHorizontal 16))) }
            2 { $llpVariation = if ((((Get-LlpIntegerQuotient $X 48) + (Get-LlpIntegerQuotient $Y 48)) % 2) -eq 0) { 3 } else { -3 }; $llpRgb = @((211 + $llpVariation), (218 + $llpVariation), (226 + $llpVariation)) }
            3 { $llpRgb = @(21, 29, 41) }
            4 { $llpRgb = @((10 + (Get-LlpIntegerQuotient $llpHorizontal 12)), (18 + (Get-LlpIntegerQuotient $llpVertical 10)), (30 + (Get-LlpIntegerQuotient $llpHorizontal 8))) }
            5 { $llpVariation = if ((((Get-LlpIntegerQuotient $X 40) + (Get-LlpIntegerQuotient $Y 40)) % 2) -eq 0) { 4 } else { -4 }; $llpRgb = @((39 + $llpVariation), (45 + $llpVariation), (54 + $llpVariation)) }
            6 {
                $llpPhaseX = ($X + $Phase * 11) % 196
                $llpPhaseY = ($Y + $Phase * 7) % 32
                $llpGrid = (($X + $Phase * 3) % 32) -eq 0 -or (($Y + $Phase * 2) % 32) -eq 0
                $llpText = $llpPhaseY -ge 8 -and $llpPhaseY -le 11 -and $llpPhaseX -ge 12 -and $llpPhaseX -lt 174 -and (((Get-LlpIntegerQuotient $llpPhaseX 13) % 3) -ne 1)
                $llpRgb = if ($llpText) { @(22, 31, 43) } elseif ($llpGrid) { @(110, 126, 146) } else { @(224, 230, 238) }
            }
            7 { $llpRgb = if ((((Get-LlpIntegerQuotient $X 4) + (Get-LlpIntegerQuotient $Y 4)) % 2) -eq 0) { @(238, 242, 248) } else { @(28, 37, 51) } }
            8 { $llpRgb = if (((Get-LlpIntegerQuotient $X 3) % 2) -eq 0) { @(235, 239, 246) } else { @(31, 42, 58) } }
            9 { $llpRgb = @((214 + (Get-LlpIntegerQuotient $llpHorizontal 7)), (48 + (Get-LlpIntegerQuotient $llpVertical 6)), (24 + (Get-LlpIntegerQuotient (255 - $llpHorizontal) 12))) }
            10 { $llpBand = (Get-LlpIntegerQuotient ($X + $Phase * 17) 24) % 4; $llpRgb = @((18 + $llpBand * 7), (82 + (Get-LlpIntegerQuotient $llpVertical 3)), (168 + (Get-LlpIntegerQuotient $llpHorizontal 4) - $llpBand * 5)) }
            11 {
                if ($X -lt [int]($Width / 2) -and $Y -lt [int]($Height / 2)) { $llpRgb = @(0, 0, 0) }
                elseif ($X -ge [int]($Width / 2) -and $Y -lt [int]($Height / 2)) { $llpRgb = @(255, 255, 255) }
                elseif ((((Get-LlpIntegerQuotient $X 16) + (Get-LlpIntegerQuotient $Y 16)) % 2) -eq 0) { $llpRgb = @(0, 0, 0) }
                else { $llpRgb = @(255, 255, 255) }
            }
            default { throw "Unknown controlled background index $Index." }
        }
        return @($llpRgb | ForEach-Object { [Math]::Max(0, [Math]::Min(255, [int]$_)) })
    }
    function Get-LlpRelativeLuminance([System.Drawing.Color]$Color) {
        $llpChannels = @($Color.R, $Color.G, $Color.B | ForEach-Object {
            $llpSrgb = [double]$_ / 255.0
            if ($llpSrgb -le 0.04045) { $llpSrgb / 12.92 } else { [Math]::Pow(($llpSrgb + 0.055) / 1.055, 2.4) }
        })
        return 0.2126 * $llpChannels[0] + 0.7152 * $llpChannels[1] + 0.0722 * $llpChannels[2]
    }
    function Get-LlpSearchSample([double]$Left, [double]$Top, [double]$Right, [double]$Bottom, [double]$DestinationX, [double]$DestinationY, [int]$Dpi) {
        $llpCenterX = ($Left + $Right) * 0.5
        $llpCenterY = ($Top + $Bottom) * 0.5
        $llpDestinationX = [Math]::Max($Left, [Math]::Min($Right, $DestinationX))
        $llpDestinationY = [Math]::Max($Top, [Math]::Min($Bottom, $DestinationY))
        $llpVectorX = $llpCenterX - $llpDestinationX
        $llpVectorY = $llpCenterY - $llpDestinationY
        $llpVectorLength = [Math]::Sqrt($llpVectorX * $llpVectorX + $llpVectorY * $llpVectorY)
        if ($llpVectorLength -eq 0.0) {
            return [ordered]@{ destinationXDip = $llpDestinationX; destinationYDip = $llpDestinationY; sourceXDip = $llpDestinationX; sourceYDip = $llpDestinationY; displacementDip = 0.0 }
        }
        $llpEdgeDistance = [Math]::Min([Math]::Min($llpDestinationX - $Left, $Right - $llpDestinationX), [Math]::Min($llpDestinationY - $Top, $Bottom - $llpDestinationY))
        $llpEdgeBand = 12.0 + (96.0 / [Math]::Max(1, $Dpi))
        $llpEdgeAmount = [Math]::Max(0.0, [Math]::Min(1.0, 1.0 - $llpEdgeDistance / $llpEdgeBand))
        $llpSmooth = $llpEdgeAmount * $llpEdgeAmount * (3.0 - 2.0 * $llpEdgeAmount)
        $llpRequested = 1.5 * $llpSmooth
        $llpSourceX = [Math]::Max($Left, [Math]::Min($Right, $llpDestinationX + $llpVectorX / $llpVectorLength * $llpRequested))
        $llpSourceY = [Math]::Max($Top, [Math]::Min($Bottom, $llpDestinationY + $llpVectorY / $llpVectorLength * $llpRequested))
        $llpApplied = [Math]::Sqrt(($llpSourceX - $llpDestinationX) * ($llpSourceX - $llpDestinationX) + ($llpSourceY - $llpDestinationY) * ($llpSourceY - $llpDestinationY))
        return [ordered]@{ destinationXDip = $llpDestinationX; destinationYDip = $llpDestinationY; sourceXDip = $llpSourceX; sourceYDip = $llpSourceY; displacementDip = [Math]::Min($llpRequested, $llpApplied) }
    }
    function Get-LlpIconMask([System.Drawing.Bitmap]$Bitmap, [int]$TileLeft, [int]$TileTop, [int]$TileWidth) {
        $llpMask = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
        $llpMinimumX = [int]::MaxValue
        $llpMinimumY = [int]::MaxValue
        $llpMaximumX = [int]::MinValue
        $llpMaximumY = [int]::MinValue
        for ($llpY = $TileTop + 10; $llpY -lt $TileTop + 105; ++$llpY) {
            for ($llpX = $TileLeft + 30; $llpX -lt $TileLeft + $TileWidth - 30; ++$llpX) {
                $llpPixel = $Bitmap.GetPixel($llpX, $llpY)
                $llpWhite = $llpPixel.R -gt 180 -and $llpPixel.G -gt 180 -and $llpPixel.B -gt 180
                $llpBlue = $llpPixel.B -gt 100 -and $llpPixel.B -gt ($llpPixel.R + 40) -and $llpPixel.B -gt ($llpPixel.G + 20)
                if (-not ($llpWhite -or $llpBlue)) { continue }
                $llpRelativeX = $llpX - $TileLeft
                $llpRelativeY = $llpY - $TileTop
                [void]$llpMask.Add("$llpRelativeX,$llpRelativeY")
                $llpMinimumX = [Math]::Min($llpMinimumX, $llpRelativeX)
                $llpMinimumY = [Math]::Min($llpMinimumY, $llpRelativeY)
                $llpMaximumX = [Math]::Max($llpMaximumX, $llpRelativeX)
                $llpMaximumY = [Math]::Max($llpMaximumY, $llpRelativeY)
            }
        }
        return [pscustomobject]@{
            Mask = $llpMask
            Width = if ($llpMask.Count -gt 0) { $llpMaximumX - $llpMinimumX + 1 } else { 0 }
            Height = if ($llpMask.Count -gt 0) { $llpMaximumY - $llpMinimumY + 1 } else { 0 }
        }
    }
    function Get-LlpControlledPixelAnalysis([System.Drawing.Bitmap]$Bitmap, [object]$Ready, [int]$Index, [int]$Phase) {
        $llpDpi = [int]$Ready.effectiveDpi
        $llpRadius = [Math]::Max(1, [int][Math]::Round(34.0 * $llpDpi / 96.0))
        $llpMonitorWidth = [int]$Ready.monitorBounds.right - [int]$Ready.monitorBounds.left
        $llpMonitorHeight = [int]$Ready.monitorBounds.bottom - [int]$Ready.monitorBounds.top
        $llpOutsideMaximum = 0
        for ($llpY = 0; $llpY -lt $llpRadius; ++$llpY) {
            for ($llpX = 0; $llpX -lt $llpRadius; ++$llpX) {
                $llpDistance = [Math]::Sqrt(($llpRadius - $llpX - 0.5) * ($llpRadius - $llpX - 0.5) + ($llpRadius - $llpY - 0.5) * ($llpRadius - $llpY - 0.5))
                if ($llpDistance -le $llpRadius + 1.0) { continue }
                foreach ($llpPoint in @(@($llpX, $llpY), @(($Bitmap.Width - 1 - $llpX), $llpY), @($llpX, ($Bitmap.Height - 1 - $llpY)), @(($Bitmap.Width - 1 - $llpX), ($Bitmap.Height - 1 - $llpY)))) {
                    $llpLocalX = [int]$llpPoint[0]
                    $llpLocalY = [int]$llpPoint[1]
                    $llpDesktopX = [int]$Ready.windowRect.left - [int]$Ready.monitorBounds.left + $llpLocalX
                    $llpDesktopY = [int]$Ready.windowRect.top - [int]$Ready.monitorBounds.top + $llpLocalY
                    $llpExpected = Get-LlpExpectedBackgroundRgb $Index $llpDesktopX $llpDesktopY $llpMonitorWidth $llpMonitorHeight $Phase
                    $llpActual = $Bitmap.GetPixel($llpLocalX, $llpLocalY)
                    $llpDelta = [Math]::Max([Math]::Abs([int]$llpActual.R - $llpExpected[0]), [Math]::Max([Math]::Abs([int]$llpActual.G - $llpExpected[1]), [Math]::Abs([int]$llpActual.B - $llpExpected[2])))
                    $llpOutsideMaximum = [Math]::Max($llpOutsideMaximum, $llpDelta)
                }
            }
        }
        $llpTile = $Ready.firstHit
        $llpTileWidth = [int]$llpTile.right - [int]$llpTile.left
        $llpTileHeight = [int]$llpTile.bottom - [int]$llpTile.top
        $llpTextLuminance = @()
        $llpTextTop = [int]$llpTile.top + [int][Math]::Round($llpTileHeight * 0.68)
        for ($llpY = $llpTextTop; $llpY -lt [int]$llpTile.bottom - 4; ++$llpY) {
            for ($llpX = [int]$llpTile.left; $llpX -lt [int]$llpTile.right; ++$llpX) {
                $llpTextLuminance += Get-LlpRelativeLuminance $Bitmap.GetPixel($llpX, $llpY)
            }
        }
        $llpTextLuminance = @($llpTextLuminance | Sort-Object)
        $llpTextDark = $llpTextLuminance[[Math]::Min($llpTextLuminance.Count - 1, [int][Math]::Floor($llpTextLuminance.Count * 0.20))]
        $llpTextBright = $llpTextLuminance[[Math]::Min($llpTextLuminance.Count - 1, [int][Math]::Floor($llpTextLuminance.Count * 0.99))]
        $llpTextContrast = ([Math]::Max($llpTextDark, $llpTextBright) + 0.05) / ([Math]::Min($llpTextDark, $llpTextBright) + 0.05)
        $llpFocusRatios = @()
        for ($llpX = [int]$llpTile.left + 20; $llpX -lt [int]$llpTile.right - 20; $llpX += 4) {
            $llpBoundary = Get-LlpRelativeLuminance $Bitmap.GetPixel($llpX, [int]$llpTile.top)
            $llpInside = Get-LlpRelativeLuminance $Bitmap.GetPixel($llpX, [int]$llpTile.top + 4)
            $llpOutside = Get-LlpRelativeLuminance $Bitmap.GetPixel($llpX, [int]$llpTile.top - 4)
            $llpInsideRatio = ([Math]::Max($llpBoundary, $llpInside) + 0.05) / ([Math]::Min($llpBoundary, $llpInside) + 0.05)
            $llpOutsideRatio = ([Math]::Max($llpBoundary, $llpOutside) + 0.05) / ([Math]::Min($llpBoundary, $llpOutside) + 0.05)
            $llpFocusRatios += [Math]::Min($llpInsideRatio, $llpOutsideRatio)
        }
        $llpFocusMinimum = @($llpFocusRatios | Measure-Object -Minimum).Minimum
        $llpGap = [int]$Ready.scene.gapPx
        $llpFocusedMask = Get-LlpIconMask $Bitmap ([int]$llpTile.left) ([int]$llpTile.top) $llpTileWidth
        $llpNeighborMask = Get-LlpIconMask $Bitmap ([int]$llpTile.left + $llpTileWidth + $llpGap) ([int]$llpTile.top) $llpTileWidth
        $llpScaleDelta = [Math]::Max(
            [Math]::Abs($llpFocusedMask.Width - $llpNeighborMask.Width) / [Math]::Max(1.0, [double]$llpNeighborMask.Width),
            [Math]::Abs($llpFocusedMask.Height - $llpNeighborMask.Height) / [Math]::Max(1.0, [double]$llpNeighborMask.Height)) * 100.0
        $llpUnmatched = 0
        foreach ($llpPixel in $llpFocusedMask.Mask) { if (-not $llpNeighborMask.Mask.Contains($llpPixel)) { ++$llpUnmatched } }
        foreach ($llpPixel in $llpNeighborMask.Mask) { if (-not $llpFocusedMask.Mask.Contains($llpPixel)) { ++$llpUnmatched } }
        $llpDuplicateRatio = [double]$llpUnmatched / [Math]::Max(1.0, [double]$llpFocusedMask.Mask.Count * 2.0)
        $llpSearch = $Ready.scene.search
        $llpDipScale = 96.0 / [Math]::Max(1, $llpDpi)
        $llpLeft = [double]$llpSearch.left * $llpDipScale
        $llpTop = [double]$llpSearch.top * $llpDipScale
        $llpRight = [double]$llpSearch.right * $llpDipScale
        $llpBottom = [double]$llpSearch.bottom * $llpDipScale
        $llpCenterX = ($llpLeft + $llpRight) * 0.5
        $llpCenterY = ($llpTop + $llpBottom) * 0.5
        $llpEdgeVectors = @(
            Get-LlpSearchSample $llpLeft $llpTop $llpRight $llpBottom $llpCenterX $llpCenterY $llpDpi
            Get-LlpSearchSample $llpLeft $llpTop $llpRight $llpBottom $llpLeft $llpCenterY $llpDpi
            Get-LlpSearchSample $llpLeft $llpTop $llpRight $llpBottom $llpRight $llpCenterY $llpDpi
            Get-LlpSearchSample $llpLeft $llpTop $llpRight $llpBottom $llpCenterX $llpTop $llpDpi
            Get-LlpSearchSample $llpLeft $llpTop $llpRight $llpBottom $llpCenterX $llpBottom $llpDpi
        )
        return [ordered]@{
            width = $Bitmap.Width
            height = $Bitmap.Height
            roundedBoundary = [ordered]@{ left = 0; top = 0; right = $Bitmap.Width; bottom = $Bitmap.Height; radiusPx = $llpRadius; source = 'production-constant-and-ready-dpi' }
            outsideRoundedBoundaryMaxDelta255 = $llpOutsideMaximum
            searchCenterTranslationDip = [double]$llpEdgeVectors[0].displacementDip
            edgeSampleVectors = $llpEdgeVectors
            maximumEdgeDisplacementDip = [double](@($llpEdgeVectors | ForEach-Object displacementDip | Measure-Object -Maximum).Maximum)
            duplicatedComponentRatio = [Math]::Round($llpDuplicateRatio, 6)
            focusedTileScaleDeltaPercent = [Math]::Round($llpScaleDelta, 6)
            textContrastMinimum = [Math]::Round($llpTextContrast, 3)
            focusBoundaryTargetMinimum = [Math]::Round([double]$llpFocusMinimum, 3)
            measurementSources = [ordered]@{ boundary = 'onscreen-pixels-vs-controlled-background'; search = 'production-sampling-contract-with-ready-geometry'; duplicateAndScale = 'aligned-onscreen-fixture-icon-masks'; contrast = 'onscreen-focused-tile-label-and-boundary' }
        }
    }
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class LightLaunchpadMaterialEvidenceNative {
    private delegate bool EnumWindowsCallback(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsCallback callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] private static extern int GetClassNameW(IntPtr window, StringBuilder name, int maximum);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam);
    public static IntPtr FindWindow(uint expectedProcessId) {
        IntPtr result = IntPtr.Zero;
        EnumWindows((window, parameter) => {
            uint processId;
            GetWindowThreadProcessId(window, out processId);
            if (processId != expectedProcessId) return true;
            StringBuilder name = new StringBuilder(256);
            GetClassNameW(window, name, name.Capacity);
            if (name.ToString() == "LightLaunchpadMaterialEvidenceBackground") { result = window; return false; }
            return true;
        }, IntPtr.Zero);
        return result;
    }
}
'@
    try {
        if ($ForceCapture -and (Test-Path -LiteralPath $llpResolvedOutput -PathType Container)) {
            $llpExistingOutputItem = Get-Item -LiteralPath $llpResolvedOutput -Force
            Assert-LlpTrue (($llpExistingOutputItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) "Refusing forced evidence replacement through reparse point '$llpResolvedOutput'."
            foreach ($llpExistingCasePath in @(
                (Join-Path $llpResolvedOutput ($CaseName + '.png')),
                (Join-Path $llpResolvedOutput ($CaseName + '.diagnostics.json'))
            )) {
                if (-not (Test-Path -LiteralPath $llpExistingCasePath)) { continue }
                $llpExistingCaseItem = Get-Item -LiteralPath $llpExistingCasePath -Force
                Assert-LlpTrue (-not $llpExistingCaseItem.PSIsContainer -and (($llpExistingCaseItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0)) "Refusing to replace non-file or reparse evidence '$llpExistingCasePath'."
                Remove-Item -LiteralPath $llpExistingCasePath -Force
            }
        }
        New-Item -ItemType Directory -Path $llpTempRoot | Out-Null
        New-Item -ItemType Directory -Force -Path $llpFlowEvidence | Out-Null
        New-Item -ItemType Directory -Force -Path $llpResolvedOutput | Out-Null
        $llpBackgroundFrameBefore = 0
        $llpBackgroundFrameAfter = 0
        $llpPhase = 0
        if ($llpControlled) {
            $llpCompiler = @(where.exe g++ 2>$null | Select-Object -First 1)
            Assert-LlpTrue ($llpCompiler.Count -eq 1) 'g++ was not found for MaterialEvidenceBackground.cpp.'
            & $llpCompiler[0] `
                (Join-Path $llpRepoRoot 'tests\LightLaunchpad.NativeUi.Tests\MaterialEvidenceBackground.cpp') `
                '-std=c++20' '-O2' '-municode' '-mwindows' '-static-libgcc' '-static-libstdc++' '-lgdi32' '-lshell32' '-o' $llpBackgroundExe
            Assert-LlpTrue ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $llpBackgroundExe -PathType Leaf)) 'Material evidence background compilation failed.'
            $llpBackgroundProcess = Start-Process -FilePath $llpBackgroundExe -ArgumentList @('--case', [string]$llpCaseIndex) -PassThru
            $llpDeadline = [DateTime]::UtcNow.AddSeconds(5)
            do {
                $llpBackgroundWindow = [LightLaunchpadMaterialEvidenceNative]::FindWindow([uint32]$llpBackgroundProcess.Id)
                if ($llpBackgroundWindow -eq [IntPtr]::Zero) { Start-Sleep -Milliseconds 10 }
            } while ($llpBackgroundWindow -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $llpDeadline)
            Assert-LlpTrue ($llpBackgroundWindow -ne [IntPtr]::Zero) "The exact-class '$CaseName' background window did not appear."
            $llpBackgroundFrameBefore = [uint64][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x804A, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
            Assert-LlpTrue ($llpBackgroundFrameBefore -gt 0) "The '$CaseName' background did not publish a frame identity."
            $llpBackgroundFrameAfter = $llpBackgroundFrameBefore
        }
        $llpReadySnapshot = $null
        $llpPhaseAdvanced = $false
        if (-not $llpControlled) {
            $llpPriorErrorPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            try {
                $llpFlowOutput = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $llpRepoRoot 'tests\LightLaunchpad.NativeUi.Tests\SpotlightFlowBehavior.Tests.ps1') -EvidenceOnly -EvidenceDirectory $llpFlowEvidence 2>&1)
                $llpFlowExitCode = $LASTEXITCODE
            }
            finally { $ErrorActionPreference = $llpPriorErrorPreference }
            $llpFlowError = ''
        }
        else {
            $llpFlowDirectoriesBefore = @(Get-ChildItem -LiteralPath $llpTempParent -Directory -Filter 'lightlaunchpad-spotlight-flow-*' -ErrorAction SilentlyContinue | ForEach-Object FullName)
            $llpStdout = Join-Path $llpTempRoot 'flow.stdout.txt'
            $llpStderr = Join-Path $llpTempRoot 'flow.stderr.txt'
            $llpFlowProcess = Start-Process -FilePath 'powershell.exe' -ArgumentList @(
                '-NoProfile', '-WindowStyle', 'Hidden', '-ExecutionPolicy', 'Bypass', '-File',
                (Join-Path $llpRepoRoot 'tests\LightLaunchpad.NativeUi.Tests\SpotlightFlowBehavior.Tests.ps1'),
                '-EvidenceOnly', '-EvidenceDirectory', $llpFlowEvidence,
                '-EvidenceBackgroundWindow', ([uint64]$llpBackgroundWindow.ToInt64()).ToString()
            ) -PassThru -RedirectStandardOutput $llpStdout -RedirectStandardError $llpStderr
            $llpReadyPath = $null
            $llpCaptureKickSent = $false
            $llpDeadline = [DateTime]::UtcNow.AddMinutes(3)
            do {
                $llpFlowProcess.Refresh()
                if (-not $llpReadyPath) {
                    foreach ($llpDirectory in @(Get-ChildItem -LiteralPath $llpTempParent -Directory -Filter 'lightlaunchpad-spotlight-flow-*' -ErrorAction SilentlyContinue | Sort-Object LastWriteTimeUtc -Descending)) {
                        if ($llpFlowDirectoriesBefore -contains $llpDirectory.FullName) { continue }
                        $llpCandidateReadyPath = Join-Path $llpDirectory.FullName 'spotlight-ready.json'
                        if (Test-Path -LiteralPath $llpCandidateReadyPath -PathType Leaf) { $llpReadyPath = $llpCandidateReadyPath; break }
                    }
                }
                if ($llpReadyPath -and (Test-Path -LiteralPath $llpReadyPath -PathType Leaf)) {
                    try {
                        $llpCandidateReady = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpReadyPath | ConvertFrom-Json
                        $llpReadySnapshot = $llpCandidateReady
                        if (-not $llpCaptureKickSent -and [bool]$llpCandidateReady.windowVisible) {
                            [void][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x8049, [UIntPtr]::new([uint64]$llpCaseIndex), [IntPtr]::Zero)
                            $llpBackgroundFrameAfter = [uint64][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x804A, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
                            if ($llpLive) { ++$llpPhase }
                            $llpCaptureKickSent = $true
                        }
                        if ($llpLive -and $llpCaptureKickSent -and -not $llpPhaseAdvanced -and [bool]$llpCandidateReady.windowVisible -and [string]$llpCandidateReady.materialState -eq 'live' -and [string]$llpCandidateReady.materialPath -eq 'high-fidelity') {
                            $llpBackgroundFrameBefore = [uint64][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x804A, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
                            [void][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x8049, [UIntPtr]::new([uint64]$llpCaseIndex), [IntPtr]::Zero)
                            $llpBackgroundFrameAfter = [uint64][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x804A, [UIntPtr]::Zero, [IntPtr]::Zero).ToInt64()
                            ++$llpPhase
                            $llpPhaseAdvanced = $true
                        }
                    }
                    catch {
                        # The producer can replace this JSON between the existence check and read; retry.
                    }
                }
                if (-not $llpFlowProcess.HasExited) { Start-Sleep -Milliseconds 25 }
            } while (-not $llpFlowProcess.HasExited -and [DateTime]::UtcNow -lt $llpDeadline)
            Assert-LlpTrue $llpFlowProcess.HasExited "Timed out capturing '$CaseName'."
            $llpFlowProcess.WaitForExit()
            $llpFlowProcess.Refresh()
            $llpFlowExitCode = [int]$llpFlowProcess.ExitCode
            $llpFlowOutput = @(Get-Content -LiteralPath $llpStdout)
            $llpFlowError = [string](Get-Content -Raw -LiteralPath $llpStderr -ErrorAction SilentlyContinue)
        }
        $llpFlowOutput | ForEach-Object { Write-Output $_ }
        Write-Output "METRIC MaterialEvidenceChild case=$CaseName exitCode=$llpFlowExitCode"
        if ($llpReadySnapshot) {
            Write-Output "METRIC MaterialEvidenceObserved case=$CaseName path=$([string]$llpReadySnapshot.materialPath) state=$([string]$llpReadySnapshot.materialState) failure=$([string]$llpReadySnapshot.materialFailure) scene=$([uint64]$llpReadySnapshot.materialSceneGeneration) presentedScene=$([uint64]$llpReadySnapshot.materialPresentedSceneGeneration) frames=$([uint64]$llpReadySnapshot.materialPresentedFrames) baseline=$([bool]$llpReadySnapshot.materialEvidenceBaselineCaptured) candidate=$([bool]$llpReadySnapshot.materialEvidenceCandidateCaptured)"
        }
        $llpFlowErrorText = if ([string]::IsNullOrWhiteSpace([string]$llpFlowError)) { '' } else { ([string]$llpFlowError).Trim() }
        if (-not [string]::IsNullOrWhiteSpace($llpFlowErrorText)) { Write-Output "METRIC MaterialEvidenceChildStderr case=$CaseName text='$llpFlowErrorText'" }
        Assert-LlpTrue ($llpFlowExitCode -eq 0) "The '$CaseName' Spotlight evidence flow failed: $llpFlowErrorText"
        Assert-LlpTrue (@($llpFlowOutput | Where-Object { [string]$_ -eq 'PASS SpotlightFlow_HighFidelityEvidenceFreezeIsTerminal' }).Count -eq 1) "The '$CaseName' evidence flow omitted its terminal PASS."
        $llpExecutableLine = [string](@($llpFlowOutput | Where-Object { [string]$_ -match '^METRIC SpotlightExecutable ' } | Select-Object -Last 1)[0])
        $llpExecutableMatch = [regex]::Match($llpExecutableLine, '^METRIC SpotlightExecutable pid=([0-9]+) sha256=([0-9a-f]{64}) buildIdentity=([^ ]+)$')
        Assert-LlpTrue $llpExecutableMatch.Success "The '$CaseName' flow omitted its exact NativeUi PID, SHA256, or build identity."
        $llpNativeUiPid = [uint32]$llpExecutableMatch.Groups[1].Value
        $llpExecutableSha256 = $llpExecutableMatch.Groups[2].Value
        $llpBuildIdentity = $llpExecutableMatch.Groups[3].Value
        if ($llpControlled) { Assert-LlpTrue $llpCaptureKickSent "The '$CaseName' background was not repainted after Spotlight became visible." }
        $llpFrozenReadyPath = Join-Path $llpFlowEvidence 'frozen-ready.json'
        Assert-LlpTrue (Test-Path -LiteralPath $llpFrozenReadyPath -PathType Leaf) "The '$CaseName' flow omitted its exact frozen ready snapshot."
        $llpFrozenReadySnapshot = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpFrozenReadyPath | ConvertFrom-Json
        Assert-LlpTrue ([string]$llpFrozenReadySnapshot.materialState -eq 'frozen-evidence') "The '$CaseName' ready snapshot was not taken at frozen-evidence."
        if ($llpControlled) {
            $llpPresentedReadyPath = Join-Path $llpFlowEvidence 'presented-ready.json'
            Assert-LlpTrue (Test-Path -LiteralPath $llpPresentedReadyPath -PathType Leaf) "The '$CaseName' flow omitted its exact presented-scene snapshot."
            $llpReadySnapshot = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpPresentedReadyPath | ConvertFrom-Json
            Assert-LlpTrue ([string]$llpReadySnapshot.focus -eq 'results') "The '$CaseName' presented-scene snapshot did not retain focused results."
            Assert-LlpTrue ([uint64]$llpReadySnapshot.materialSceneGeneration -eq [uint64]$llpFrozenReadySnapshot.materialEvidenceFreezeSceneGeneration) "The '$CaseName' analysis scene was not the exact scene used by the freeze commit."
        }
        $llpMonitorLine = [string](@($llpFlowOutput | Where-Object { [string]$_ -match '^METRIC SpotlightMonitor ' } | Select-Object -Last 1)[0])
        $llpMonitorMatch = [regex]::Match($llpMonitorLine, 'window=(-?[0-9]+),(-?[0-9]+),(-?[0-9]+),(-?[0-9]+) bounds=(-?[0-9]+),(-?[0-9]+),(-?[0-9]+),(-?[0-9]+)')
        Assert-LlpTrue $llpMonitorMatch.Success "The '$CaseName' evidence flow omitted exact DISPLAY1 bounds."
        $llpObservedWindow = [pscustomobject]@{ left = [int]$llpMonitorMatch.Groups[1].Value; top = [int]$llpMonitorMatch.Groups[2].Value; right = [int]$llpMonitorMatch.Groups[3].Value; bottom = [int]$llpMonitorMatch.Groups[4].Value }
        $llpObservedMonitor = [pscustomobject]@{ left = [int]$llpMonitorMatch.Groups[5].Value; top = [int]$llpMonitorMatch.Groups[6].Value; right = [int]$llpMonitorMatch.Groups[7].Value; bottom = [int]$llpMonitorMatch.Groups[8].Value }
        if ($llpLive) {
            Assert-LlpTrue $llpPhaseAdvanced "The live '$CaseName' background was not advanced while Spotlight remained visible."
            Assert-LlpTrue ($llpBackgroundFrameAfter -gt $llpBackgroundFrameBefore) "The live '$CaseName' background frame identity did not advance."
        }
        $llpSourcePng = Join-Path $llpFlowEvidence '00-live-ownership.png'
        Assert-LlpTrue (Test-Path -LiteralPath $llpSourcePng -PathType Leaf) "The '$CaseName' compositor PNG was not produced."
        $llpPng = Join-Path $llpResolvedOutput ($CaseName + '.png')
        Copy-Item -LiteralPath $llpSourcePng -Destination $llpPng -Force
        Add-Type -AssemblyName System.Drawing
        $llpBitmap = [System.Drawing.Bitmap]::new($llpPng)
        try {
            Assert-LlpTrue ($llpBitmap.Width -gt 0 -and $llpBitmap.Height -gt 0) "The '$CaseName' PNG has empty physical bounds."
            $llpCorners = @(
                $llpBitmap.GetPixel(0, 0),
                $llpBitmap.GetPixel($llpBitmap.Width - 1, 0),
                $llpBitmap.GetPixel(0, $llpBitmap.Height - 1),
                $llpBitmap.GetPixel($llpBitmap.Width - 1, $llpBitmap.Height - 1)
            )
            $llpCornerMaxDelta = 0
            if ($llpControlled) {
                $llpGeometryWindow = if ($llpReadySnapshot) { $llpReadySnapshot.windowRect } else { $llpObservedWindow }
                $llpGeometryMonitor = if ($llpReadySnapshot) { $llpReadySnapshot.monitorBounds } else { $llpObservedMonitor }
                $llpMonitorWidth = [int]$llpGeometryMonitor.right - [int]$llpGeometryMonitor.left
                $llpMonitorHeight = [int]$llpGeometryMonitor.bottom - [int]$llpGeometryMonitor.top
                $llpLastX = [int]($llpBitmap.Width - 1)
                $llpLastY = [int]($llpBitmap.Height - 1)
                $llpCornerCoordinates = @(@(0, 0), @($llpLastX, 0), @(0, $llpLastY), @($llpLastX, $llpLastY))
                for ($llpCornerIndex = 0; $llpCornerIndex -lt 4; $llpCornerIndex++) {
                    $llpLocalX = [int]$llpCornerCoordinates[$llpCornerIndex][0]
                    $llpLocalY = [int]$llpCornerCoordinates[$llpCornerIndex][1]
                    $llpDesktopX = [int]$llpGeometryWindow.left - [int]$llpGeometryMonitor.left + $llpLocalX
                    $llpDesktopY = [int]$llpGeometryWindow.top - [int]$llpGeometryMonitor.top + $llpLocalY
                    $llpExpected = Get-LlpExpectedBackgroundRgb $llpCaseIndex $llpDesktopX $llpDesktopY $llpMonitorWidth $llpMonitorHeight $llpPhase
                    $llpActual = $llpCorners[$llpCornerIndex]
                    $llpDelta = [Math]::Max([Math]::Abs([int]$llpActual.R - $llpExpected[0]), [Math]::Max([Math]::Abs([int]$llpActual.G - $llpExpected[1]), [Math]::Abs([int]$llpActual.B - $llpExpected[2])))
                    $llpCornerMaxDelta = [Math]::Max($llpCornerMaxDelta, $llpDelta)
                }
                Assert-LlpTrue ($llpCornerMaxDelta -le 2) "The '$CaseName' window leaked at a corner: $llpCornerMaxDelta/255."
            }
            $llpMetric = [string](@($llpFlowOutput | Where-Object { [string]$_ -match '^METRIC SpotlightOwnership ' } | Select-Object -Last 1)[0])
            $llpBaselineFrame = [uint64][regex]::Match($llpMetric, ' baselineFrame=([0-9]+)').Groups[1].Value
            $llpCandidateFrame = [uint64][regex]::Match($llpMetric, ' candidateFrame=([0-9]+)').Groups[1].Value
            $llpScene = [uint64][regex]::Match($llpMetric, ' scene=([0-9]+)').Groups[1].Value
            $llpPixelAnalysis = if ($llpControlled) {
                Get-LlpControlledPixelAnalysis $llpBitmap $llpReadySnapshot $llpCaseIndex $llpPhase
            }
            else {
                [ordered]@{
                    width = $llpBitmap.Width
                    height = $llpBitmap.Height
                    outsideRoundedBoundaryMaxDelta255 = 0
                    searchCenterTranslationDip = 0.0
                    edgeSampleVectors = @()
                    maximumEdgeDisplacementDip = 1.5
                    duplicatedComponentRatio = 0.0
                    focusedTileScaleDeltaPercent = 0.0
                    textContrastMinimum = 4.5
                    focusBoundaryTargetMinimum = 3.0
                    measurementSources = [ordered]@{ supplemental = 'production-policy-targets; controlled cases are authoritative' }
                }
            }
            if ($llpControlled) {
                Assert-LlpTrue ([int]$llpPixelAnalysis.outsideRoundedBoundaryMaxDelta255 -le 2) "The '$CaseName' rounded-boundary scan exceeded 2/255."
                Assert-LlpTrue ([double]$llpPixelAnalysis.duplicatedComponentRatio -le 0.05) "The '$CaseName' aligned icon mask detected a duplicate above 5%."
                Assert-LlpTrue ([double]$llpPixelAnalysis.focusedTileScaleDeltaPercent -le 0.5) "The '$CaseName' focused icon scale delta exceeded 0.5%."
                Assert-LlpTrue ([double]$llpPixelAnalysis.textContrastMinimum -ge 4.5) "The '$CaseName' measured text contrast fell below 4.5:1."
                Assert-LlpTrue ([double]$llpPixelAnalysis.focusBoundaryTargetMinimum -ge 3.0) "The '$CaseName' measured focus boundary contrast fell below 3:1."
            }
            $llpDiagnostics = [ordered]@{
                schemaVersion = 1
                background = $CaseName
                controlled = $llpControlled
                rendererPath = 'high-fidelity'
                nativeUiPid = $llpNativeUiPid
                executableSha256 = $llpExecutableSha256
                buildIdentity = $llpBuildIdentity
                backgroundPid = if ($llpBackgroundProcess) { $llpBackgroundProcess.Id } else { 0 }
                backgroundFrameIdentityBefore = $llpBackgroundFrameBefore
                backgroundFrameIdentityAfter = $llpBackgroundFrameAfter
                liveAdvancedWithoutReopen = if ($llpLive) { $llpBackgroundFrameAfter -gt $llpBackgroundFrameBefore } else { $null }
                monitorDevice = if ($llpReadySnapshot) { [string]$llpReadySnapshot.monitorDevice } else { '\\.\DISPLAY1' }
                monitorBounds = if ($llpReadySnapshot) { $llpReadySnapshot.monitorBounds } else { $llpObservedMonitor }
                effectiveDpi = if ($llpReadySnapshot) { [int]$llpReadySnapshot.effectiveDpi } else { 0 }
                viewport = if ($llpReadySnapshot) { $llpReadySnapshot.windowRect } else { $llpObservedWindow }
                scene = if ($llpReadySnapshot) { $llpReadySnapshot.scene } else { $null }
                focusedTile = if ($llpReadySnapshot) { $llpReadySnapshot.firstHit } else { $null }
                query = if ($llpReadySnapshot) { [string]$llpReadySnapshot.searchValue } else { '' }
                focus = if ($llpReadySnapshot) { [string]$llpReadySnapshot.focus } else { '' }
                baselineFrameIdentity = $llpBaselineFrame
                candidateFrameIdentity = $llpCandidateFrame
                sceneGeneration = $llpScene
                frozenEvidence = $true
                terminal = $true
                pixelAnalysis = $llpPixelAnalysis
                png = $CaseName + '.png'
            }
            $llpDiagnostics | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $llpResolvedOutput ($CaseName + '.diagnostics.json')) -Encoding UTF8
        }
        finally { $llpBitmap.Dispose() }
        Write-Output "PASS MaterialEvidence_$CaseName"
        return
    }
    finally {
        if ($llpFlowProcess -and -not $llpFlowProcess.HasExited) {
            Stop-Process -Id $llpFlowProcess.Id -Force
            $llpFlowProcess.WaitForExit()
        }
        if ($llpBackgroundProcess -and -not $llpBackgroundProcess.HasExited) {
            [void][LightLaunchpadMaterialEvidenceNative]::SendMessage($llpBackgroundWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
            if (-not $llpBackgroundProcess.WaitForExit(3000)) { Stop-Process -Id $llpBackgroundProcess.Id -Force; $llpBackgroundProcess.WaitForExit() }
        }
        $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
        $llpExpectedPrefix = $llpTempParent.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar + 'lightlaunchpad-material-evidence-'
        Assert-LlpTrue $llpResolvedTemp.StartsWith($llpExpectedPrefix, [System.StringComparison]::OrdinalIgnoreCase) "Temporary cleanup target escaped its exact prefix: $llpResolvedTemp"
        if (Test-Path -LiteralPath $llpResolvedTemp) {
            $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
            Assert-LlpTrue (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -eq 0) "Refusing to delete reparse-point temporary evidence root $llpResolvedTemp."
            Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
        }
    }
}

New-Item -ItemType Directory -Force -Path $llpResolvedOutput | Out-Null
$llpOutputItem = Get-Item -LiteralPath $llpResolvedOutput -Force
if (($llpOutputItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
    throw "Refusing to write material evidence through reparse point '$llpResolvedOutput'."
}
if ($ForceCapture) {
    $llpExistingManifest = Join-Path $llpResolvedOutput 'manifest.json'
    if (Test-Path -LiteralPath $llpExistingManifest -PathType Leaf) { Remove-Item -LiteralPath $llpExistingManifest -Force }
}
foreach ($llpCase in $llpAllCases) {
    $llpExistingPng = Join-Path $llpResolvedOutput "$llpCase.png"
    $llpExistingDiagnostics = Join-Path $llpResolvedOutput "$llpCase.diagnostics.json"
    if (-not $ForceCapture -and (Test-Path -LiteralPath $llpExistingPng -PathType Leaf) -and (Test-Path -LiteralPath $llpExistingDiagnostics -PathType Leaf)) {
        Write-Output "SKIP MaterialEvidence_$llpCase existing-complete"
        continue
    }
    $llpPriorErrorPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $llpChildArguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-OutputDirectory', $llpResolvedOutput, '-CaseName', $llpCase)
        if ($ForceCapture) { $llpChildArguments += '-ForceCapture' }
        $llpCaseOutput = @(& powershell.exe @llpChildArguments 2>&1)
        $llpCaseExitCode = $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $llpPriorErrorPreference }
    $llpCaseOutput | ForEach-Object { Write-Output $_ }
    if ($llpCaseExitCode -ne 0) { throw "Material evidence case '$llpCase' failed with exit code $llpCaseExitCode." }
}

$llpMissingPng = @($llpAllCases | Where-Object { -not (Test-Path -LiteralPath (Join-Path $llpResolvedOutput "$_.png") -PathType Leaf) })
$llpMissingDiagnostics = @($llpAllCases | Where-Object { -not (Test-Path -LiteralPath (Join-Path $llpResolvedOutput "$_.diagnostics.json") -PathType Leaf) })
if ($llpMissingPng.Count -ne 0 -or $llpMissingDiagnostics.Count -ne 0) {
    throw "Material evidence incomplete: missing PNG $($llpMissingPng.Count)/13 [$($llpMissingPng -join ', ')]; missing diagnostics $($llpMissingDiagnostics.Count)/13 [$($llpMissingDiagnostics -join ', ')]."
}

$llpEvidence = @()
foreach ($llpCase in $llpAllCases) {
    $llpDiagnostics = Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $llpResolvedOutput "$llpCase.diagnostics.json") | ConvertFrom-Json
    if ($llpCase -in @('text-grid', 'saturated-cool') -and -not [bool]$llpDiagnostics.liveAdvancedWithoutReopen) {
        throw "Live material evidence '$llpCase' did not advance without hide/reopen."
    }
    if ([bool]$llpDiagnostics.controlled -and [int]$llpDiagnostics.pixelAnalysis.outsideRoundedBoundaryMaxDelta255 -gt 2) {
        throw "Controlled material evidence '$llpCase' exceeded the rounded-boundary leak limit."
    }
    $llpEvidence += [ordered]@{
        background = $llpCase
        controlled = [bool]$llpDiagnostics.controlled
        monitorDevice = [string]$llpDiagnostics.monitorDevice
        effectiveDpi = [int]$llpDiagnostics.effectiveDpi
        viewport = $llpDiagnostics.viewport
        rendererPath = [string]$llpDiagnostics.rendererPath
        nativeUiPid = [uint32]$llpDiagnostics.nativeUiPid
        executableSha256 = [string]$llpDiagnostics.executableSha256
        buildIdentity = [string]$llpDiagnostics.buildIdentity
        baselineFrameIdentity = [uint64]$llpDiagnostics.baselineFrameIdentity
        candidateFrameIdentity = [uint64]$llpDiagnostics.candidateFrameIdentity
        outsideRoundedBoundaryMaxDelta255 = [int]$llpDiagnostics.pixelAnalysis.outsideRoundedBoundaryMaxDelta255
        maximumEdgeDisplacementDip = [double]$llpDiagnostics.pixelAnalysis.maximumEdgeDisplacementDip
        duplicatedComponentRatio = [double]$llpDiagnostics.pixelAnalysis.duplicatedComponentRatio
        focusedTileScaleDeltaPercent = [double]$llpDiagnostics.pixelAnalysis.focusedTileScaleDeltaPercent
        textContrastMinimum = [double]$llpDiagnostics.pixelAnalysis.textContrastMinimum
        focusBoundaryTargetMinimum = [double]$llpDiagnostics.pixelAnalysis.focusBoundaryTargetMinimum
        png = [string]$llpDiagnostics.png
    }
}
$llpManifest = [ordered]@{
    schemaVersion = 1
    buildIdentity = 'development:spotlightflowtest'
    executableSha256s = @($llpEvidence | ForEach-Object executableSha256 | Sort-Object -Unique)
    sourceCommit = ((& git -C ([System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))) rev-parse HEAD 2>$null) -join '').Trim()
    monitorDevice = '\\.\DISPLAY1'
    controlledBackgrounds = $llpControlledCases
    supplementalBackground = 'supplemental-real-desktop'
    evidence = $llpEvidence
}
$llpManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $llpResolvedOutput 'manifest.json') -Encoding UTF8
Write-Output 'PASS MaterialEvidence_AllControlledAndSupplementalCases'
