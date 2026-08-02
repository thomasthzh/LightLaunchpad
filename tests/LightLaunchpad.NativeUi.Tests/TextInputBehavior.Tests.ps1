param(
    [string]$EvidenceDirectory = ''
)

$ErrorActionPreference = 'Stop'

$llpRepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$llpTempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("lightlaunchpad-text-input-behavior-" + [System.Guid]::NewGuid().ToString('N'))
$llpOutputDirectory = Join-Path $llpTempRoot 'development'
$llpAppData = Join-Path $llpTempRoot 'appdata'
$llpLocalAppData = Join-Path $llpTempRoot 'localappdata'
$llpUserProfile = Join-Path $llpTempRoot 'profile'
$llpDataDirectory = Join-Path $llpAppData 'LightLaunchpad'
$llpLaunchpad = Join-Path $llpTempRoot 'Launchpad'
$llpProbePath = Join-Path $llpTempRoot 'text-input-probe.json'
$llpProcess = $null
$llpOriginalClipboard = $null
$llpClipboardCaptured = $false

function Assert-Equal {
    param([object]$Expected, [object]$Actual, [string]$Message)
    if ($Expected -ne $Actual) { throw "$Message Expected '$Expected', actual '$Actual'." }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}

function Set-LlpTextSelection {
    param($TextPattern, [int]$Start, [int]$End)
    $llpRange = $TextPattern.DocumentRange.Clone()
    $llpLength = $TextPattern.DocumentRange.GetText(-1).Length
    [void]$llpRange.MoveEndpointByUnit([System.Windows.Automation.Text.TextPatternRangeEndpoint]::Start, [System.Windows.Automation.Text.TextUnit]::Character, $Start)
    [void]$llpRange.MoveEndpointByUnit([System.Windows.Automation.Text.TextPatternRangeEndpoint]::End, [System.Windows.Automation.Text.TextUnit]::Character, $End - $llpLength)
    $llpRange.Select()
}

function Send-LlpControlKey {
    param([IntPtr]$Window, [UInt16]$Key)
    Assert-True ([LightLaunchpadTextProbeNative]::SendControlKey($Window, $Key)) "Failed to deliver Ctrl+$([char]$Key) to the NativeUi text probe."
    Start-Sleep -Milliseconds 30
}

function Save-LlpWindowEvidence {
    param([IntPtr]$Window, [string]$Path)
    $llpRect = [LightLaunchpadTextProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadTextProbeNative]::GetWindowRect($Window, [ref]$llpRect)) 'Could not read the NativeUi evidence window geometry.'
    $llpWidth = $llpRect.Right - $llpRect.Left
    $llpHeight = $llpRect.Bottom - $llpRect.Top
    Assert-True ($llpWidth -gt 0 -and $llpHeight -gt 0) 'NativeUi evidence window geometry was empty.'
    $llpBitmap = [System.Drawing.Bitmap]::new($llpWidth, $llpHeight, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $llpGraphics = [System.Drawing.Graphics]::FromImage($llpBitmap)
    $llpDc = $llpGraphics.GetHdc()
    try {
        Assert-True ([LightLaunchpadTextProbeNative]::PrintWindow($Window, $llpDc, 2)) 'PrintWindow could not capture the rendered NativeUi window.'
    }
    finally {
        $llpGraphics.ReleaseHdc($llpDc)
        $llpGraphics.Dispose()
    }
    try {
        $llpBitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $llpBitmap.Dispose()
    }
}

try {
    Add-Type -AssemblyName UIAutomationClient
    Add-Type -AssemblyName UIAutomationTypes
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class LightLaunchpadTextProbeNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr SendMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hwnd, uint message, UIntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(IntPtr hwnd, IntPtr processId);
    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")]
    private static extern bool AttachThreadInput(uint attach, uint attachTo, bool value);
    [DllImport("user32.dll")]
    private static extern bool GetKeyboardState(byte[] state);
    [DllImport("user32.dll")]
    private static extern bool SetKeyboardState(byte[] state);

    public static bool SendControlKey(IntPtr hwnd, ushort key) {
        uint currentThread = GetCurrentThreadId();
        uint targetThread = GetWindowThreadProcessId(hwnd, IntPtr.Zero);
        bool attached = currentThread == targetThread || AttachThreadInput(currentThread, targetThread, true);
        if (!attached) return false;
        byte[] original = new byte[256];
        bool captured = GetKeyboardState(original);
        if (!captured) {
            if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, false);
            return false;
        }
        byte[] modified = (byte[])original.Clone();
        modified[0x11] = (byte)(modified[0x11] | 0x80);
        bool applied = SetKeyboardState(modified);
        try {
            if (applied) {
                SendMessage(hwnd, 0x0100, new UIntPtr(key), IntPtr.Zero);
                SendMessage(hwnd, 0x0101, new UIntPtr(key), IntPtr.Zero);
            }
        }
        finally {
            SetKeyboardState(original);
            if (currentThread != targetThread) AttachThreadInput(currentThread, targetThread, false);
        }
        return applied;
    }
}
'@

    New-Item -ItemType Directory -Force -Path $llpOutputDirectory, $llpDataDirectory, $llpLocalAppData, $llpUserProfile, $llpLaunchpad | Out-Null
    [System.IO.File]::WriteAllText((Join-Path $llpLaunchpad 'Alpha.url'), "[InternetShortcut]`r`nURL=https://example.invalid/alpha`r`n", [System.Text.UTF8Encoding]::new($false))
    $llpSettings = [ordered]@{
        SchemaVersion = 2
        LaunchpadFolder = $llpLaunchpad
        Hotkey = 'Alt+D'
        StartWithWindows = $false
        IconSize = 'Medium'
        ViewMode = 'InlineRegions'
        DisplayMode = 'Launchpad'
        Language = 'English'
        SpotlightWidth = 980
        SpotlightHeight = 720
        AppSpacing = 8
        WheelSensitivity = 1.0
        ResetSearchOnOpen = $true
    } | ConvertTo-Json -Depth 8
    [System.IO.File]::WriteAllText((Join-Path $llpDataDirectory 'settings.json'), $llpSettings, [System.Text.UTF8Encoding]::new($false))

    & (Join-Path $llpRepoRoot 'tools\build-native-ui.ps1') `
        -OutputDirectory $llpOutputDirectory `
        -ProductVersion '1.11.0' `
        -BuildCommit 'textinputtest' `
        -BuildChannel Development | Out-Null
    $llpExecutable = Join-Path $llpOutputDirectory 'LightLaunchpad.NativeUi.exe'

    $llpPriorAppData = $env:APPDATA
    $llpPriorLocalAppData = $env:LOCALAPPDATA
    $llpPriorUserProfile = $env:USERPROFILE
    try {
        $env:APPDATA = $llpAppData
        $env:LOCALAPPDATA = $llpLocalAppData
        $env:USERPROFILE = $llpUserProfile
        $llpProcess = Start-Process -FilePath $llpExecutable -ArgumentList @('--text-input-probe-file', $llpProbePath) -PassThru
    }
    finally {
        $env:APPDATA = $llpPriorAppData
        $env:LOCALAPPDATA = $llpPriorLocalAppData
        $env:USERPROFILE = $llpPriorUserProfile
    }

    $llpDeadline = [DateTime]::UtcNow.AddSeconds(8)
    $llpWindow = [IntPtr]::Zero
    while ([DateTime]::UtcNow -lt $llpDeadline -and $llpWindow -eq [IntPtr]::Zero) {
        Start-Sleep -Milliseconds 50
        $llpProcess.Refresh()
        $llpWindow = $llpProcess.MainWindowHandle
    }
    Assert-True ($llpWindow -ne [IntPtr]::Zero) 'The text-input probe did not expose a real NativeUi window.'

    $llpElement = [System.Windows.Automation.AutomationElement]::FromHandle($llpWindow)
    Assert-True ($null -ne $llpElement) 'UI Automation could not discover the NativeUi search element.'
    Assert-Equal 'Search applications' $llpElement.Current.Name 'UI Automation search name mismatch.'
    Assert-Equal 'ControlType.Edit' $llpElement.Current.ControlType.ProgrammaticName 'UI Automation search role mismatch.'
    Assert-Equal 'LightLaunchpad.Search' $llpElement.Current.AutomationId 'UI Automation search ID mismatch.'
    Assert-Equal $true $llpElement.Current.IsKeyboardFocusable 'UI Automation search field is not keyboard focusable.'

    $llpValueObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern, [ref]$llpValueObject)) 'UI Automation Value pattern is unavailable.'
    $llpValue = [System.Windows.Automation.ValuePattern]$llpValueObject
    $llpValue.SetValue('alpha beta')
    Assert-Equal 'alpha beta' $llpValue.Current.Value 'UI Automation Value pattern did not update production text.'

    $llpTextObject = $null
    Assert-True ($llpElement.TryGetCurrentPattern([System.Windows.Automation.TextPattern]::Pattern, [ref]$llpTextObject)) 'UI Automation Text pattern is unavailable.'
    $llpText = [System.Windows.Automation.TextPattern]$llpTextObject
    Assert-Equal 'alpha beta' $llpText.DocumentRange.GetText(-1) 'UI Automation document range text mismatch.'
    $llpRange = $llpText.DocumentRange.Clone()
    [void]$llpRange.MoveEndpointByUnit([System.Windows.Automation.Text.TextPatternRangeEndpoint]::End, [System.Windows.Automation.Text.TextUnit]::Character, -5)
    $llpRange.Select()
    $llpSelection = @($llpText.GetSelection())
    Assert-Equal 1 $llpSelection.Count 'UI Automation did not expose one text selection.'
    Assert-Equal 'alpha' $llpSelection[0].GetText(-1) 'UI Automation selection range mismatch.'

    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'X', [IntPtr]::Zero)
    Assert-Equal 'X beta' $llpValue.Current.Value 'WM_CHAR did not replace the UI Automation selection.'
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0100, [UIntPtr]0x24, [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0100, [UIntPtr]0x2E, [IntPtr]::Zero)
    Assert-Equal ' beta' $llpValue.Current.Value 'Home/Delete did not edit at the caret.'

    $llpValue.SetValue('mouse')
    $llpClient = [LightLaunchpadTextProbeNative+RECT]::new()
    Assert-True ([LightLaunchpadTextProbeNative]::GetClientRect($llpWindow, [ref]$llpClient)) 'Could not read the NativeUi probe client geometry.'
    $llpStartPoint = [IntPtr](71 -bor (50 -shl 16))
    $llpEndPoint = [IntPtr](($llpClient.Right - 71) -bor (50 -shl 16))
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0201, [UIntPtr]::Zero, $llpStartPoint)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0202, [UIntPtr]::Zero, $llpStartPoint)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'S', [IntPtr]::Zero)
    Assert-Equal 'Smouse' $llpValue.Current.Value 'Mouse positioning did not move the caret to the start of text.'
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0201, [UIntPtr]::Zero, $llpStartPoint)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0200, [UIntPtr]1, $llpEndPoint)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0202, [UIntPtr]::Zero, $llpEndPoint)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'D', [IntPtr]::Zero)
    Assert-Equal 'D' $llpValue.Current.Value 'Mouse drag selection did not replace the selected text.'

    $llpOriginalClipboard = [System.Windows.Forms.Clipboard]::GetDataObject()
    $llpClipboardCaptured = $true
    $llpValue.SetValue('copy me')
    Set-LlpTextSelection $llpText 0 4
    Send-LlpControlKey $llpWindow ([UInt16][char]'C')
    Assert-Equal 'copy' ([System.Windows.Forms.Clipboard]::GetText()) 'Ctrl+C did not copy the UI Automation selection.'
    $llpValue.SetValue('cut me')
    Set-LlpTextSelection $llpText 0 3
    Send-LlpControlKey $llpWindow ([UInt16][char]'X')
    Assert-Equal ' me' $llpValue.Current.Value 'Ctrl+X did not cut the UI Automation selection.'
    Assert-Equal 'cut' ([System.Windows.Forms.Clipboard]::GetText()) 'Ctrl+X did not update the clipboard.'
    Send-LlpControlKey $llpWindow ([UInt16][char]'V')
    Assert-Equal 'cut me' $llpValue.Current.Value 'Ctrl+V did not paste at the production caret.'
    Send-LlpControlKey $llpWindow ([UInt16][char]'Z')
    Assert-Equal ' me' $llpValue.Current.Value 'Ctrl+Z did not undo the clipboard edit.'
    Send-LlpControlKey $llpWindow ([UInt16][char]'Y')
    Assert-Equal 'cut me' $llpValue.Current.Value 'Ctrl+Y did not redo the clipboard edit.'
    Send-LlpControlKey $llpWindow ([UInt16][char]'A')
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'Q', [IntPtr]::Zero)
    Assert-Equal 'Q' $llpValue.Current.Value 'Ctrl+A did not select all text for replacement.'
    Send-LlpControlKey $llpWindow ([UInt16][char]'Z')
    Assert-Equal 'cut me' $llpValue.Current.Value 'The select-all replacement was not undoable.'

    $llpValue.SetValue('ab')
    Set-LlpTextSelection $llpText 0 0
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'S', [IntPtr]::Zero)
    Set-LlpTextSelection $llpText 2 2
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'M', [IntPtr]::Zero)
    Set-LlpTextSelection $llpText 4 4
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0102, [UIntPtr][char]'E', [IntPtr]::Zero)
    Assert-Equal 'SaMbE' $llpValue.Current.Value 'English input did not insert at start, middle, and end.'

    $llpValue.SetValue('ab')
    Set-LlpTextSelection $llpText 0 0
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010D, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0286, [UIntPtr][char]'中', [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010E, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-Equal '中ab' $llpValue.Current.Value 'Chinese IME input did not insert once at the start.'
    Set-LlpTextSelection $llpText 2 2
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010D, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0286, [UIntPtr][char]'文', [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010E, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-Equal '中a文b' $llpValue.Current.Value 'Chinese IME input did not insert once in the middle.'
    Set-LlpTextSelection $llpText 4 4
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010D, [UIntPtr]::Zero, [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x0286, [UIntPtr][char]'末', [IntPtr]::Zero)
    [void][LightLaunchpadTextProbeNative]::SendMessage($llpWindow, 0x010E, [UIntPtr]::Zero, [IntPtr]::Zero)
    Assert-Equal '中a文b末' $llpValue.Current.Value 'Chinese IME input did not insert once at the end.'

    if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
        $llpEvidenceRoot = [System.IO.Path]::GetFullPath($EvidenceDirectory)
        New-Item -ItemType Directory -Force -Path $llpEvidenceRoot | Out-Null
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot 'llp008-caret-ime.png')
        Set-LlpTextSelection $llpText 1 4
        Save-LlpWindowEvidence $llpWindow (Join-Path $llpEvidenceRoot 'llp008-selection.png')
        Set-LlpTextSelection $llpText 5 5
    }

    Assert-True ([LightLaunchpadTextProbeNative]::PostMessage($llpWindow, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)) 'Failed to close the text-input probe window.'
    if (-not $llpProcess.WaitForExit(8000)) { throw 'The text-input probe did not exit within eight seconds.' }
    Assert-Equal 0 $llpProcess.ExitCode 'Text-input probe exit code mismatch.'
    $llpProbe = Get-Content -Raw -Encoding UTF8 -LiteralPath $llpProbePath | ConvertFrom-Json
    Assert-Equal '中a文b末' ([string]$llpProbe.text) 'Probe output lost the final committed text.'
    Assert-Equal $false ([bool]$llpProbe.composing) 'Probe exited with a stale IME composition.'
    Assert-Equal $true ([bool]$llpProbe.candidatePositionApplied) 'NativeUi did not apply the IME composition/candidate position.'
    Assert-True ([int]$llpProbe.candidateX -gt 0 -and [int]$llpProbe.candidateY -gt 0) 'IME candidate position was not placed at a visible caret coordinate.'
    Assert-Equal $true ([bool]$llpProbe.accessibilityReady) 'NativeUi did not initialize its UI Automation providers.'
    Assert-Equal $true ([bool]$llpProbe.windowVisible) 'The real NativeUi probe was not visibly rendered.'
    Write-Output "METRIC TextInputUIA value='$([string]$llpProbe.text)' caret=$([int]$llpProbe.caret) candidate=($([int]$llpProbe.candidateX),$([int]$llpProbe.candidateY))"
    Write-Output 'PASS TextInputBehavior_UiaValueTextSelectionKeyboardAndImePosition'
}
finally {
    if ($llpProcess -and -not $llpProcess.HasExited) {
        Stop-Process -Id $llpProcess.Id -Force
        $llpProcess.WaitForExit()
    }
    if ($llpClipboardCaptured) {
        if ($null -eq $llpOriginalClipboard) {
            [System.Windows.Forms.Clipboard]::Clear()
        }
        else {
            [System.Windows.Forms.Clipboard]::SetDataObject($llpOriginalClipboard, $true)
        }
    }
    $llpResolvedTemp = [System.IO.Path]::GetFullPath($llpTempRoot)
    $llpExpectedTempRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    if ($llpResolvedTemp.StartsWith($llpExpectedTempRoot, [System.StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $llpResolvedTemp)) {
        $llpTempItem = Get-Item -LiteralPath $llpResolvedTemp -Force
        if (($llpTempItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Refusing to clean reparse-point test output $llpResolvedTemp." }
        Remove-Item -LiteralPath $llpResolvedTemp -Recurse -Force
    }
}
