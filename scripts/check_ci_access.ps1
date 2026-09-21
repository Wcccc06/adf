$ErrorActionPreference='Continue'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$H = @{ 'User-Agent'='dsh'; 'Accept'='application/vnd.github+json' }
Write-Output '=== 1. 未登录能不能查公开仓库的 Actions 产物（决定我能不能直接下编译结果）==='
try {
  $r = Invoke-RestMethod -Uri 'https://api.github.com/repos/bmax121/KernelPatch/actions/artifacts' -Headers $H -TimeoutSec 25
  Write-Output ('  查询成功, 产物数 = ' + $r.total_count)
  if ($r.artifacts.Count -gt 0) { $a = $r.artifacts[0]; Write-Output ('  样例: ' + $a.name + '  expired=' + $a.expired + '  url=' + $a.archive_download_url) }
} catch { Write-Output ('  查询失败: ' + $_.Exception.Message) }

Write-Output '=== 2. 未登录能不能下载那个产物（401/403 就说明必须带 token）==='
try {
  $r2 = Invoke-WebRequest -Uri 'https://api.github.com/repos/bmax121/KernelPatch/actions/artifacts' -Headers $H -TimeoutSec 25 -UseBasicParsing
  $j = $r2.Content | ConvertFrom-Json
  if ($j.artifacts.Count -gt 0) {
    $u = $j.artifacts[0].archive_download_url
    try { $d = Invoke-WebRequest -Uri $u -Headers $H -Method Head -TimeoutSec 25 -UseBasicParsing; Write-Output ('  可下载 status=' + $d.StatusCode) }
    catch { Write-Output ('  下载被拒: ' + $_.Exception.Message) }
  } else { Write-Output '  该仓库没有产物可测' }
} catch { Write-Output ('  失败: ' + $_.Exception.Message) }

Write-Output '=== 3. 未登录能不能看 run 日志/状态（判断你上传后的进度）==='
try {
  $r3 = Invoke-RestMethod -Uri 'https://api.github.com/repos/bmax121/KernelPatch/actions/runs?per_page=1' -Headers $H -TimeoutSec 25
  Write-Output ('  run 查询成功, 最近 run: ' + $r3.workflow_runs[0].name + ' status=' + $r3.workflow_runs[0].status)
} catch { Write-Output ('  失败: ' + $_.Exception.Message) }

Write-Output '=== 4. 检查本机是否有可用的 git 身份（推送需要）==='
git config --global user.name; git config --global user.email
Write-Output ('  credential helper: ' + (git config --global credential.helper))
Write-Output '=== 5. 代理/网络（跑国内镜像加速，编译不受影响）==='
Write-Output ('  HTTP_PROXY=' + $env:HTTP_PROXY + ' HTTPS_PROXY=' + $env:HTTPS_PROXY)