# /data/adb 里长什么样 —— 对标 LinYu 的两个流程

你这台机器上 LinYu 的实际布局（adb 实测）：

    /data/adb/
    ├── LinYuDriverLoader.Ver.7.0.sh      -rwxrwxrwx   1972088   流程一：驱动加载器（其实是 ELF 可执行）
    ├── LinYuDriverLoader.Ver.7.3.sh      -rwxrwxrwx   3542241   同上，新版本
    ├── LinYuKernel.Ver.3.2.8.sh          -rwxrwxrwx   4823602   流程一：内核侧（同样是 ELF，ELFPROT 加壳）
    ├── LinYuKernel.Ver.3.2.9.sh          -rwxrwxrwx   4823831
    ├── LinYuKernel.Ver.3.3.0.sh          -rwxrwxrwx   4822991
    ├── lib20260914_082802.so             -rwx------  10053968   流程二：宿主（菜单本体）
    ├── 嗯嗯启动器.sh                      -rw-rw----       843   流程二：启动器（读卡密 → exec 宿主）
    ├── KM / IMEI / 1 / 1.bak                              授权与配置数据
    └── YH/{YHGames,YH_YC}                                 月虹游戏助手（Zygisk 模块，与内核无关）

**他们的两个流程**（从文件名 + 启动器源码看出来的）：

    流程一（刷驱动）: LinYuDriverLoader -> LinYuKernel
        DriverLoader 负责把 Kernel 那部分送进内核；两个都是 0777、root 跑。

    流程二（跑宿主）: 下载 lib*.so -> 点 嗯嗯启动器.sh
        启动器做的事: 读卡密 -> chmod 700 那个 .so -> exec "$so" -k "$卡密" --record-mirror

## 我们照抄的形状（adf）

    /data/adb/adf/
    ├── adf_driver.ko          0644   流程一：我们的内核模块（LKM，源码在我们仓库里）
    ├── adf_driver_load.sh     0755   流程一的脚本（对标 DriverLoader + Kernel）
    ├── adf_host               0755   流程二：我们的宿主（可执行）
    └── adf_launcher.sh        0755   流程二的脚本（对标 嗯嗯启动器.sh）

命令（都在 root 下）：

    su -c 'sh /data/adb/adf/adf_driver_load.sh'    # 流程一：刷驱动
    su -c 'sh /data/adb/adf/adf_launcher.sh'       # 流程二：起宿主
    su -c '/data/adb/adf/adf_host check'           # 自检：输出 PASS / FAIL

## 权限：他们给 0777，我给 0755 / 0700 —— 说清理由

| 文件 | LinYu | 我们 | 理由 |
|---|---|---|---|
| 驱动 / 内核可执行 | 0777 | **0755** | 可执行就够了；others 可写 = 任何应用能替换你的驱动 |
| 目录 | 0755 | **0700** | 只有 root 能进；普通应用连 ls 都失败 |
| 宿主 .so | 0700（启动器里 chmod 700） | **0755** | 可执行且不可被 others 改写 |
| 配置文件 | 0644 | **0600** | 里面有隐藏名单关键词，被读到等于自曝 |

他们那样给 777 的实际后果（这台机器上就是这样）：

- **任何本地应用都能改写那几个文件**，把驱动换成它的；
- 反作弊扫目录时，一堆 0777 的 root 级可执行文件本身就是最显眼的特征。

## 装我们的东西之前的基线（同一台机器实测）

| 探测 | 现在的值 |
|---|---|
| ls /data/adb（adb shell） | Permission denied |
| ls /data/adb（root） | 全部可见，含 LinYu 全部文件 |
| ps 里的 adbd | 可见（pid 6260） |
| /proc/6260/comm | 读到 adbd |
| getprop init.svc.adbd | running |

装好之后这三条应该变成：看不见 adbd、读不到 comm、属性为空。
