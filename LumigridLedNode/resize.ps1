Add-Type -AssemblyName System.Drawing
$paths = @(
  'C:\Users\Slaff\Documents\LumigridLedNode\cropped-Reclame-Fabriek-Cube-e1725950229680 - Copy.png',
  'C:\Users\Slaff\Documents\LumigridLedNode\a-minimalist--pure-black-and-white-logo-design-for (1).png'
)
foreach ($p in $paths) {
  $img = [System.Drawing.Image]::FromFile($p)
  $targetWidth = 150
  $ratio = $targetWidth / $img.Width
  $targetHeight = [math]::Floor($img.Height * $ratio)
  $bmp = New-Object System.Drawing.Bitmap($targetWidth, $targetHeight)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.DrawImage($img, 0, 0, $targetWidth, $targetHeight)
  $ms = New-Object System.IO.MemoryStream
  $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
  $base64 = [Convert]::ToBase64String($ms.ToArray())
  Write-Host "File: $p"
  Write-Host "Base64: data:image/png;base64,$base64"
  Write-Host "---"
}
