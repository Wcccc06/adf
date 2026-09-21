$ErrorActionPreference='Continue'
Write-Output '=== WSL 有没有（决定能不能在本机构建内核模块）==='
try { $w = & wsl.exe --status 2>&1 | Out-String; Write-Output $w } catch { Write-Output ('wsl err: ' + $_.Exception.Message) }
try { $l = & wsl.exe -l -v 2>&1 | Out-String; Write-Output ('已装发行版: ' + $l) } catch { Write-Output 'no distro' }
Write-Output '=== 大小探测（Range 请求，只看头 1KB 拿不到长度就报告服务器响应）==='
$urls = @(
 'https://android.googlesource.com/kernel/common/+archive/refs/heads/android14-6.1.tar.gz',
 'https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/main/clang-r487747c.tar.gz'
)
foreach ($u in $urls) {
  try {
    $req = [System.Net.HttpWebRequest]::Create($u)
    $req.Method = 'HEAD'
    $req.UserAgent = 'dsh'
    $req.AllowAutoRedirect = $true
    $resp = $req.GetResponse()
    Write-Output ('  OK  status=' + [int]$resp.StatusCode + ' length=' + $resp.ContentLength + ' (' + [math]::Round($resp.ContentLength/1MB,1) + ' MB)')
    Write-Output ('      ' + $u)
    $resp.Close()
  } catch { Write-Output ('  ERR ' + $u + ' :: ' + $_.Exception.Message) }
}