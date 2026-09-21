# adf 项目总纲（两对四条线）

对标对象：LinYu 的四个文件 = **两对**。

```
对① 驱动对（进内核）            对② 客户端对（用户态跑）
  LinYuDriverLoader (3.5MB)       lib20260914_082802.so (10MB)
  LinYuKernel       (4.8MB)       嗯嗯启动器.sh           (717B)
  两者都是 ELFPROT 加壳的 ELF     一个加壳 ELF + 一个真脚本
```

## 一、他们四条线各干什么（实测结论）

| 线 | 文件 | 我查到的 |
|---|---|---|
| 驱动·加载器 | LinYuDriverLoader.Ver.7.3.sh | AArch64 ELF + ELFPROT 壳；负责把内核部分送进内核 |
| 驱动·内核 | LinYuKernel.Ver.3.3.0.sh | 同样是加壳 ELF；内核态主体。**目前没加载**（/proc/modules、kallsyms 都查不到） |
| 客户端·宿主 | lib20260914_082802.so | 加壳 ELF，**导出函数 0 个**，53 项符号全是通用 libc，9.6MB 加密段。菜单本体 |
| 客户端·启动器 | 嗯嗯启动器.sh | 真脚本：读卡密 → chmod 700 → exec 那个 .so -k 卡密 --record-mirror |

**四个里只有 717 字节那个启动器能读，其余三个是加壳 ELF。所以只能重做，不能改。**

## 二、我们的四条线（一一对应）

| 他们的 | 我们的 | 源文件 | GitHub 依据 | 状态 |
|---|---|---|---|---|
| LinYuKernel | adf_driver.ko | driver/adf_driver.c | KernelPatch(GPL-2.0) 的 hook 用法；idandev/hidefile(Apache-2.0) 的 getdents64 过滤；HoK 研究(MIT) 的内核态安全规则 | 源码完成，待编译 |
| LinYuDriverLoader | adf_driver_load.sh | scripts/adf_driver_load.sh | KernelSU 的 insmod 通道 | 完成 |
| lib….so | adf_host | host/adf_host.c | 自己的实现（检查逻辑对标 LSPosed 那套） | 源码完成，待编译 |
| 嗯嗯启动器.sh | adf_launcher.sh | scripts/adf_launcher.sh | 照抄流程，去掉卡密 | **完成，可直接用** |

另外加了两个它没有的：install.sh（一次铺完）、adf_verify.sh（验收 PASS/FAIL）。

## 三、手机上的最终布局

```
/data/adb/adf/
├── adf_driver.ko        0644   ← 对①内核
├── adf_driver_load.sh   0755   ← 对①加载器
├── adf_host             0755   ← 对②宿主
├── adf_launcher.sh      0755   ← 对②启动器
├── adf_verify.sh        0755   ← 验收
└── install.sh           0755   ← 一次装完
```

## 四、唯一的阻塞：编译（实测）

| 位置 | 能编吗 | 实测 |
|---|---|---|
| 你这台电脑 | 不能 | 无 clang/gcc，无 WSL |
| 你的手机 | 不能 | 无 clang/gcc/cc，Termux 未安装 |
| GitHub Actions | 能 | 免费 Linux 机器；KernelPatch 和 HoK 两个项目就是在上面编的 |

## 五、对②里现在就能用的部分

宿主 adf_host 的 check（扫 /proc 进程名、试开各个 ADB 路径、读属性）不依赖内核，编出来就能跑，用来做前后对比。
Java 层（df-hide/dfhide-hook）你机器上 LSPosed 在跑，装上就能隐藏**游戏进程内**的探测。
但 **adbd 本体、系统属性、系统级 /proc** 这三样只有对①能盖——所以驱动必须编出来。

## 六、编译路线（推荐 A，文件已备好）

| 路线 | 做法 | 需要 |
|---|---|---|
| A | GitHub Actions 编，产物走代理下载，adb 装进手机 | 一个你自己的 GitHub 仓库 |
| B | 手机装 Termux，pkg install clang make，就地编 | 手机上装 Termux |
| C | 电脑装 WSL，拉内核源码+clang，本机编 | 装 WSL 可能重启，约 5GB |

A 的文件我已经写好：.github/workflows/build-adf.yml（一次同时编驱动和宿主）。
