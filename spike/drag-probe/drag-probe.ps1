# freeloops - free, open-source sample browser
# Copyright (C) 2026 Eren and the freeloops contributors
# SPDX-License-Identifier: AGPL-3.0-or-later

# Phase 0 drag probe.
#   1. Drag source: starts a plain Win32/OLE file drag with a chosen set of drop effects,
#      to find which effects FL Studio accepts.
#   2. Drop target: logs what a drag offers (effects + clipboard formats), to compare
#      Electron's startDrag with Explorer.
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$Fx = [System.Windows.Forms.DragDropEffects]
$presets = [ordered]@{
    'Copy + Link   (what Electron startDrag offers)' = [System.Windows.Forms.DragDropEffects]($Fx::Copy -bor $Fx::Link)
    'Copy + Move + Link   (what Explorer offers)'    = [System.Windows.Forms.DragDropEffects]($Fx::Copy -bor $Fx::Move -bor $Fx::Link)
    'Copy only'                                      = $Fx::Copy
    'Move only'                                      = $Fx::Move
    'Copy + Move'                                    = [System.Windows.Forms.DragDropEffects]($Fx::Copy -bor $Fx::Move)
    'Link only'                                      = $Fx::Link
}

$script:files = New-Object System.Collections.ArrayList
$script:press = $null

$form = New-Object System.Windows.Forms.Form
$form.Text = 'freeloops drag probe'
$form.ClientSize = New-Object System.Drawing.Size(560, 600)
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9)
$form.StartPosition = 'CenterScreen'

function Add-Control($ctrl, $x, $y, $w, $h) {
    $ctrl.Location = New-Object System.Drawing.Point($x, $y)
    $ctrl.Size = New-Object System.Drawing.Size($w, $h)
    $form.Controls.Add($ctrl)
    $ctrl
}

$srcLabel = Add-Control (New-Object System.Windows.Forms.Label) 12 10 536 20
$srcLabel.Text = '1. Drag source: pick the effects to offer, then drag a loop into FL Studio'
$srcLabel.Font = New-Object System.Drawing.Font('Segoe UI', 9, [System.Drawing.FontStyle]::Bold)

$combo = Add-Control (New-Object System.Windows.Forms.ComboBox) 12 34 536 24
$combo.DropDownStyle = 'DropDownList'
foreach ($k in $presets.Keys) { [void]$combo.Items.Add($k) }
$combo.SelectedIndex = 0

$list = Add-Control (New-Object System.Windows.Forms.ListBox) 12 64 536 84

$addBtn = Add-Control (New-Object System.Windows.Forms.Button) 12 154 90 26
$addBtn.Text = 'Add file...'

$onTop = Add-Control (New-Object System.Windows.Forms.CheckBox) 116 156 140 24
$onTop.Text = 'Always on top'

$dstLabel = Add-Control (New-Object System.Windows.Forms.Label) 12 194 536 20
$dstLabel.Text = '2. Drop target: drag from the Electron spike (or Explorer) onto the box below'
$dstLabel.Font = $srcLabel.Font

$zone = Add-Control (New-Object System.Windows.Forms.Label) 12 218 536 70
$zone.Text = 'drop here'
$zone.TextAlign = 'MiddleCenter'
$zone.BorderStyle = 'FixedSingle'
$zone.BackColor = [System.Drawing.Color]::FromArgb(245, 245, 245)
$zone.AllowDrop = $true

$log = Add-Control (New-Object System.Windows.Forms.TextBox) 12 300 536 288
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = 'Vertical'
$log.Font = New-Object System.Drawing.Font('Consolas', 9)

function Log($msg) {
    $log.AppendText(('{0}  {1}' -f (Get-Date -Format 'HH:mm:ss'), $msg) + "`r`n")
}

function Add-File($path) {
    [void]$script:files.Add($path)
    [void]$list.Items.Add([IO.Path]::GetFileName($path))
}

# First effect the source allows, in the order a normal file target prefers.
function Pick-Effect($allowed) {
    foreach ($name in 'Copy', 'Move', 'Link') {
        $fx = [System.Windows.Forms.DragDropEffects]$name
        if (($allowed -band $fx) -eq $fx) { return $fx }
    }
    [System.Windows.Forms.DragDropEffects]::None
}

$samples = Join-Path $env:APPDATA 'freeloops-drag-spike\samples'
if (Test-Path $samples) { Get-ChildItem $samples -Filter *.wav | ForEach-Object { Add-File $_.FullName } }
if ($script:files.Count -eq 0) { Log "no loops in $samples - run the Electron spike once, or use Add file..." }

$addBtn.Add_Click({
    $dlg = New-Object System.Windows.Forms.OpenFileDialog
    $dlg.Filter = 'Audio|*.wav;*.mp3;*.ogg;*.flac;*.aif;*.aiff|All files|*.*'
    $dlg.Multiselect = $true
    if ($dlg.ShowDialog($form) -eq 'OK') { $dlg.FileNames | ForEach-Object { Add-File $_ } }
})

$onTop.Add_CheckedChanged({ $form.TopMost = $onTop.Checked })

$list.Add_MouseDown({
    param($s, $e)
    $i = $list.IndexFromPoint($e.Location)
    if ($e.Button -eq 'Left' -and $i -ge 0) { $script:press = @{ X = $e.X; Y = $e.Y; Index = $i } }
    else { $script:press = $null }
})

$list.Add_MouseMove({
    param($s, $e)
    if (-not $script:press -or $e.Button -ne 'Left') { return }
    $ds = [System.Windows.Forms.SystemInformation]::DragSize
    if ([Math]::Abs($e.X - $script:press.X) -lt $ds.Width -and [Math]::Abs($e.Y - $script:press.Y) -lt $ds.Height) { return }

    $path = $script:files[$script:press.Index]
    $script:press = $null
    $allowed = $presets[$combo.SelectedItem]

    $drop = New-Object System.Collections.Specialized.StringCollection
    [void]$drop.Add($path)
    $data = New-Object System.Windows.Forms.DataObject
    $data.SetFileDropList($drop)

    Log "drag $([IO.Path]::GetFileName($path)) offering [$allowed]"
    $result = $list.DoDragDrop($data, $allowed)
    if ($result -eq 'None') { Log '  -> None (target refused, or drag cancelled)' }
    else { Log "  -> accepted as $result" }
})

$zone.Add_DragEnter({
    param($s, $e)
    Log "target: drag entered, source offers [$($e.AllowedEffect)]"
    Log "  formats: $($e.Data.GetFormats() -join ', ')"
    $e.Effect = Pick-Effect $e.AllowedEffect
})

$zone.Add_DragOver({
    param($s, $e)
    $e.Effect = Pick-Effect $e.AllowedEffect
})

$zone.Add_DragDrop({
    param($s, $e)
    $dropped = $e.Data.GetData([System.Windows.Forms.DataFormats]::FileDrop)
    Log "  dropped as $($e.Effect): $($dropped -join '; ')"
})

[void]$form.ShowDialog()
