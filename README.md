# pet-monitor

家用网络摄像头通过 RTSP 接入。RK3588 上拉流、硬解、用 NPU 做猫检测。判断为真时，把画了框的 JPEG 存到本地，并发送邮件。

每路相机一条线程，各用各的模型和置信度。邮件在另一条线程里发出。`SIGINT` 或 `SIGTERM` 结束进程。

## 默认判断

检测在整幅画面上做，检出的框都保留。检测区域只参与一只猫那条规则。区域写成 `roi = x,y,w,h`，单位是原图像素。框的中心落在里面才算。没写区域就看整幅画面。

两条规则是或的关系，计时互不相消：

- 全画面同时有两只及以上的猫，并且连续满 `pair_sec`（缺省 3 秒）。留下当前这一帧，文件名带 `2cats`。中间掉成一只或没有，只清这条计时。
- 检测区域内有一只猫。出现满 `shot_sec`（缺省 10 秒）留下那一帧，连续停留满 `present_sec`（缺省 20 秒）再发出，文件名带 `event`。区域内这一拍没有猫，这条计时清掉，已留的图也丢掉。

任意一种发出去之后，这一路进入冷却，`cooldown_sec`（缺省 600 秒）内不再发。冷却结束从那一拍重新计。`absent_sec` 仍从配置读入，再发间隔以 `cooldown_sec` 为准。

文件名是 `相机名_event_年月日_时分秒.jpg` 或 `相机名_2cats_年月日_时分秒.jpg`，时间用东八区。目录是 `[event] shot_dir`，相对路径接到 `[base] dir` 后面。画框和置信度用 RGA，再编成 JPEG。落盘之后把同一张图交给邮件队列。

判断和执行都可以换。默认判断是 `app_watch_judge`，默认执行是 `app_act_run`。启动前调用 `app_run_set_judge` 或 `app_run_set_act` 换上自己的函数。空调用恢复默认。

## 运行

在有 MPP、librga 和 RKNN 的机器上配置平台头文件路径后编译，再把样例配置里的地址和发信项改成自己的：

```sh
cmake -S . -B build -DPM_PLATFORM=rk3588 \
  -DPM_MPP_INCLUDE=<mpp 头文件目录> \
  -DPM_RGA_INCLUDE=<librga 头文件目录> \
  -DPM_RKNN_INCLUDE=<rknn 头文件目录>
cmake --build build --target pet-monitor
./build/pet-monitor conf/pet-monitor.ini
```

不带参数时读 `conf/pet-monitor.ini`。没有芯片时用 `-DPM_PLATFORM=stub` 编译接口测试。`stub` 的 `plt_init` 会失败，不能跑实机拉流。

邮箱账号、授权码和真实的相机地址放在自己的 ini 里，不要提交。样例里的发信项是空的，`[email] enable` 缺省关闭。

## 分层

上层只包含 `base` 和 `media`。`media` 的实现才能包含 `src/plt/include/plt`。芯片头文件只出现在 `src/plt/rk3588`，不放进仓库。

换芯片时新增 `src/plt/<芯片>`，`PM_PLATFORM` 选那个目录，公开头不动。这一版只有 `stub` 和 `rk3588`。

图像过接口时是 `BaseImage`。有没有图像看 `handle`。`fd` 和 `ptr` 可以没有。像素格式只有 NV12 和 RGB888，芯片内部的其它格式在平台层转完再交出来。解码只出整帧。整图缩放到模型输入用 `media_pic_scale`。检测先等比例缩进模型尺寸，再在短边两侧补灰，用 `media_pic_preprocess`。芯片做不到的能力返回 `BASE_UNSUPPORTED`。

## 平台层

`PM_PLATFORM` 选 `stub` 或 `rk3588`。`stub` 的 `plt_init` 会失败，用来在没有芯片的机器上编译和跑接口测试。`rk3588` 需要本机的 MPP、librga、RKNN 头文件，用 `PM_MPP_INCLUDE`、`PM_RGA_INCLUDE`、`PM_RKNN_INCLUDE` 指过去。

公开接口是 C。C++11 用于两处：RK3588 平台层，因为 librga 的头文件带 C++ 默认参数；应用层发信。

`plt_rga_*` 是二维引擎，名字是历史留下的。上层用 `media_pic`，不直接调用它。

## 网络

拉流在 `net`。头文件是 `net_cfg.h` 和 `net_pull.h`。这一层只包含 `base`，FFmpeg 的类型不出公开头。

`net_cfg_from_ini` 读 `[camera]` 的超时和传输方式，以及每一路 `[camera.名字]` 的 `url`。不读 `place`。`url` 要以 `rtsp://` 开头。传输方式缺省是 tcp。

`net_open` 连接一路。`net_info` 给出编码、宽高和参数集，参数集保持到 `net_close`。`net_read` 在调用方线程上阻塞，直到交出一帧 Annex B。这一帧保持到下一次读或关闭。断开返回失败，不在这里重连。`net_close` 可以从另一线程打断正在读的那一次。

## 媒体层

媒体层放基础图像接口，目录是 `src/media`。库名是 `media`。用其它媒体接口之前先 `media_init`，用完 `media_deinit`。

- 缓冲：`media_mem_alloc`、`media_mem_free`、`media_mem_flush`。解码帧用 `media_dec_release` 归还，不要用 `media_mem_free`。
- 推理：`media_npu_open` 加载单输入模型，`media_npu_run` 跑一次。张量带类型、量化方式、零点和 scale。这里不做检测后处理。
- 解码：`media_dec_open`、`media_dec_send`、`media_dec_receive`、`media_dec_release`。类型是 `MediaDec`。
- 缩放、拷贝、转色：`media_pic_scale`。尺寸相同是拷贝，格式不同时顺带转色。
- 推理前预处理：`media_pic_preprocess`。等比例缩放后把短边两侧补成 RGB 114，并写出灰边位置，后面把框映回原图时用这一份。
- 画线、画框、填色：`media_pic_line`、`media_pic_box`、`media_pic_fill`。里面调用平台层对应的接口。
- 旋转、翻转、平移：`media_pic_rotate`、`media_pic_flip`、`media_pic_translate`。旋转为顺时针 90/180/270。
- JPEG：`media_jpeg_encode` 编码，`media_jpeg_decode` 解码。编码返回时码流已经出来，用 `media_jpeg_free` 释放。解码得到全范围 NV12，用 `media_mem_free` 释放。
- 视频编码：类型是 `MediaEnc`，打开用 `media_enc_open`。参数不合法返回失败。平台层没有视频编码器时，参数合法也返回不支持。送帧、取流、参数集、关键帧、改码率和感兴趣区域都交给已打开的编码器。参数集用 `media_enc_extradata` 取，关闭前一直有效。感兴趣区域由调用方给出，这里不按画面区域改写。OSD 用 `media_enc_set_osd` 送最多 8 块下标图，颜色用 `media_enc_set_osd_palette`。编码旋转、镜像用 `media_enc_set_rotation`、`media_enc_set_mirror`。旋转为逆时针 0/90/180/270，镜像 0 无、1 水平、2 垂直。

公共类型在 `base`，例如 `BaseImage`、`BaseStatus`、`BASE_OK`。平台层库名是 `plt`。

## 智能

智能在 `algo`，头文件是 `algo.h`，库是共享库 `libalgo`。它调用 `media` 的缓冲、预处理和 `media_npu_run`，不包含芯片头文件。一个句柄一条队列，最多挂 4 种算法。打开时记下分辨率、连接方式、设备号、模块号和队列深度。设备号大于 0 时，内置算法用它选择 NPU 核。队列深度为 0 时用 2，最大 8。队列满返回 `BASE_AGAIN`，这一包没有收下，调用方稍后重送。

检测、追踪、识别、猫检测四项一起编进 `libalgo`，启动时自己登记。别的算法做成外部插件，导出 `algo_module`，用 `algo_load` 装入。每种算法自己提供打开、预处理、推理、后处理和旁路处理。挂上一种算法后再用 `algo_set_callback` 登记它的回调，配置原文用 `algo_set_config` 保存，回调时一并带上。没有回调的算法不跑。

送入用 `algo_input`。帧走预处理、推理、后处理。头、私有数据、码流和音频不跑模型。帧和这些旁路数据要保持到这次送入的回调都返回。回调在工作线程里，里面不能关闭句柄。同一帧上多种算法可以串行，也可以并行。串行按挂上的顺序。并行时每种算法各跑各的模型。优先级越大越先取出。

```mermaid
flowchart TD
  open["打开句柄"] --> mount["挂上算法，最多 4 种"]
  mount --> reg["按种类登记回调和配置"]
  reg --> feed["送入一包"]
  feed --> gate{"有回调且队列未满"}
  gate -->|没有回调| fail["返回失败"]
  gate -->|队列已满| again["返回 BASE_AGAIN"]
  gate -->|收下| queue["按优先级入队"]
  queue --> worker["工作线程取出"]
  worker --> kind{"数据类型"}
  kind -->|帧| mode["串行或并行"]
  mode --> pre["预处理"]
  pre --> infer["推理"]
  infer --> post["后处理"]
  post --> done{"三步都成功"}
  done -->|是| data["数据回调"]
  done -->|否| bad["状态回调"]
  kind -->|头、私有数据、码流、音频| side["不跑模型"]
  side --> echo["状态回调，原文在 buf"]
```

四项内置算法的结果各用各的结构。没有目标时检测、追踪和猫检测仍回调，`count` 为 0。检测和追踪加载 `models/yolov5s-640-640.rknn`。猫检测直接 `algo_add(ai, ALGO_CAT_DET, "models/Catto.rknn")`，不用先 `algo_load`。两份模型输入都是 640×640。`Catto.rknn` 是 FP16，单个输出已经是框和分数。

| 算法 | 预处理 | 推理之后 | 回调里的 buf |
|---|---|---|---|
| 检测 | 等比例缩放，短边两侧补灰 | 只留猫，框映回原图，最多 32 个 | `AlgoDetectResult` |
| 追踪 | 与检测相同 | 用上一帧的框延续编号 | `AlgoTrackResult` |
| 识别 | 整图缩放到模型输入 | 取得分最高的一类 | `AlgoRecog` |
| 猫检测 | 等比例缩放，短边两侧补灰 | 框映回原图，类别是猫 | `AlgoDetectResult` |

## 应用层

应用层在 `src/app`，库名是 `app`。头文件在 `src/app/include`。它把拉流、解码、推理、判断和执行串起来。可执行文件是 `pet-monitor`，入口读一份 ini，然后调用 `app_run`。

配置按段拆开。`[base]` 只提供路径根。`[camera]` 是各路共用的超时和传输方式，每一路是 `[camera.名字]`，写 `url`、`place`、`algo`、`score_min`，可选 `roi`。`place` 取 `eat`、`litter`、`living`、`客厅`、`balcony`、`阳台`。`algo` 取 `detect`、`track`、`recognize`、`cat`，缺省是 `cat`。`[algo]` 给出模型路径和缺省阈值。`[event]` 给出停留、留图、两只猫、冷却和存图目录。`[email]` 给出 SMTP。样例是 `conf/pet-monitor.ini`。

`app_cfg_from_ini` 只读上面这些段。运行时用 `app_cfg_camera_algo`、`app_cfg_camera_score` 和 `app_cfg_box_inside` 取值。

一路相机的顺序是：`net_read` 取出 Annex B，`media_dec` 解出 NV12，按这路的算法和置信度送进 `algo`。全画面的猫数和区域内的猫数分开交给判断函数。判断只返回 `keep`、`send`、`drop`。执行函数按这三个值画框、编 JPEG、写盘或把图交给 `app_mail_post`。邮件线程里调用 `app_email_send`。

发信实现是 C++11，依赖 OpenSSL（`libssl` 和 `libcrypto`）。本工程用 `find_package(OpenSSL)` 链接系统里的 OpenSSL。
