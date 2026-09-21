$ErrorActionPreference='Continue'
[Net.ServicePointManager]::SecurityProtocol=[Net.SecurityProtocolType]::Tls12
$H = @{ 'User-Agent'='dsh' }
function Head($url) {
  try {
    $r = Invoke-WebRequest -Uri $url -Method Head -Headers $H -TimeoutSec 30 -UseBasicParsing
    $len = $r.Headers['Content-Length']
    if ($len -is [array]) { $len = $len[0] }
    $mb = if ($len) { [math]::Round([double]$len/1MB,1) } else { -1 }
    Write-Output ('  OK   {0} MB  {1}' -f $mb, $url)
  } catch {
    try {
      $r2 = Invoke-WebRequest -Uri $url -Method Get -Headers $H -TimeoutSec 20 -UseBasicParsing -MaximumRedirection 5
      Write-Output ('  OK?  status={0} bytes={1}  {2}' -f $r2.StatusCode, $r2.RawContentLength, $url)
    } catch { Write-Output ('  FAIL {0}  ::  {1}' -f $url, $_.Exception.Message) }
  }
}
Write-Output '=== 1. 内核源码 (android14-6.1) ==='
Head 'https://android.googlesource.com/kernel/common/+archive/refs/heads/android14-6.1.tar.gz'
Head 'https://codeload.github.com/aosp-mirror/kernel_common/tar.gz/refs/heads/android14-6.1'
Write-Output '=== 2. 编译器 (AOSP prebuilt clang) ==='
Head 'https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/main/clang-r510928.tar.gz'
Head 'https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/refs/heads/main/clang-r487747c.tar.gz'
Write-Output '=== 3. 构建脚本仓库 (build/) ==='
Head 'https://android.googlesource.com/kernel/build/+archive/refs/heads/main.tar.gz'
Head 'https://codeload.github.com/aosp-mirror/kernel_build/tar.gz/refs/heads/main'
Write-Output '=== 4. 备选：KernelPatch SDK（里面有 GKI 模块编译需要的头） ==='
Head 'https://codeload.github.com/bmax121/KernelPatch/tar.gz/refs/heads/main'
Head 'https://codeload.github.com/wwweeeqqu/honor-of-kings-RE-research/tar.gz/refs/heads/main'