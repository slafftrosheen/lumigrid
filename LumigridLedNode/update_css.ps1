$b64 = Get-Content 'C:\Users\Slaff\Documents\LumigridLedNode\logos_b64.txt' -Raw
$b64 = $b64 -split "

"
$brandLogo = $b64[0]
$controllerLogo = $b64[1]

$cssPath = 'C:\Users\Slaff\Documents\LumigridLedNode\wled00\data\style.css'
$css = Get-Content $cssPath -Raw
$css = $css -replace 'background: #111;', 'background: linear-gradient(135deg, #1a0000 0%, #330000 100%); background-attachment: fixed;'
$css = $css -replace 'background: #222;', 'background: rgba(50, 0, 0, 0.4); backdrop-filter: blur(12px); -webkit-backdrop-filter: blur(12px); border: 1px solid rgba(255, 255, 255, 0.05);'
$css = $css -replace 'border-radius: 20px;', 'border-radius: 24px; box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.5);'
$css = $css -replace 'background: #333;', 'background: rgba(211, 16, 16, 0.8);'
$css = $css -replace 'border: 0.3ch solid #333;', 'border: 1px solid rgba(255,255,255,0.2);'
$css = $css -replace 'border-color:#555;', 'border-color: #ff3333; background: #ff3333; box-shadow: 0 0 15px rgba(255, 51, 51, 0.8);'
$css = $css -replace 'background:#555;', 'background: #ff3333;'

$css += "
.logo-brand {
    background-image: url('$brandLogo');
    background-size: contain;
    background-repeat: no-repeat;
    background-position: center;
    width: 150px;
    height: 150px;
    margin: 15px auto;
}
.logo-controller {
    background-image: url('$controllerLogo');
    background-size: contain;
    background-repeat: no-repeat;
    background-position: center;
    width: 250px;
    height: 80px;
    margin: 10px auto;
    filter: invert(1) brightness(2);
}
"

[IO.File]::WriteAllText($cssPath, $css)
