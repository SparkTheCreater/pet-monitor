# pet-monitor

摄像头对着食盆和猫砂盆，记录猫的进食和上厕所。

家用网络摄像头通过 RTSP 接入，检测跑在 RK3588 上。一次进食或上厕所结束后留下记录。

用法和配置以后再补。

## 分层

上层只包含 `base` 和 `media`。`media` 的实现才能包含 `src/plt/include/plt`。芯片头文件只出现在 `src/plt/rk3588`，不放进仓库。

换芯片时新增 `src/plt/<芯片>`，`PM_PLATFORM` 选那个目录，公开头不动。这一版只有 `stub` 和 `rk3588`。

图像过接口时是 `BaseImage`。有没有图像看 `handle`。`fd` 和 `ptr` 可以没有。像素格式只有 NV12 和 RGB888，芯片内部的其它格式在平台层转完再交出来。解码只出整帧，缩放到模型输入用 `media_pic_scale`。芯片做不到的能力返回 `BASE_UNSUPPORTED`。

## 平台层

`PM_PLATFORM` 选 `stub` 或 `rk3588`。`stub` 的 `plt_init` 会失败，用来在没有芯片的机器上编译和跑接口测试。`rk3588` 需要本机的 MPP、librga、RKNN 头文件，用 `PM_MPP_INCLUDE`、`PM_RGA_INCLUDE`、`PM_RKNN_INCLUDE` 指过去。

公开接口是 C。C++11 用于两处：RK3588 平台层，因为 librga 的头文件带 C++ 默认参数；应用层发信。

`plt_rga_*` 是二维引擎，名字是历史留下的。上层用 `media_pic`，不直接调用它。

## 媒体层

媒体层放基础图像接口，目录是 `src/media`。库名是 `media`。用其它媒体接口之前先 `media_init`，用完 `media_deinit`。

- 缓冲：`media_mem_alloc`、`media_mem_free`、`media_mem_flush`。解码帧用 `media_dec_release` 归还，不要用 `media_mem_free`。
- 推理：`media_npu_open` 加载单输入模型，`media_npu_run` 跑一次。张量带类型、量化方式、零点和 scale。这里不做检测后处理。
- 解码：`media_dec_open`、`media_dec_send`、`media_dec_receive`、`media_dec_release`。类型是 `MediaDec`。
- 缩放、拷贝、转色：`media_pic_scale`。尺寸相同是拷贝，格式不同时顺带转色。
- 画线、画框、填色：`media_pic_line`、`media_pic_box`、`media_pic_fill`。里面调用平台层对应的接口。
- 旋转、翻转、平移：`media_pic_rotate`、`media_pic_flip`、`media_pic_translate`。旋转为顺时针 90/180/270。
- JPEG：`media_jpeg_encode` 编码，`media_jpeg_decode` 解码。编码返回时码流已经出来，用 `media_jpeg_free` 释放。解码得到全范围 NV12，用 `media_mem_free` 释放。
- 视频编码：类型是 `MediaEnc`，打开用 `media_enc_open`。参数不合法返回失败。平台层没有视频编码器时，参数合法也返回不支持。送帧、取流、参数集、关键帧、改码率和感兴趣区域都交给已打开的编码器。参数集用 `media_enc_extradata` 取，关闭前一直有效。感兴趣区域由调用方给出，这里不按画面区域改写。OSD 用 `media_enc_set_osd` 送最多 8 块下标图，颜色用 `media_enc_set_osd_palette`。编码旋转、镜像用 `media_enc_set_rotation`、`media_enc_set_mirror`。旋转为逆时针 0/90/180/270，镜像 0 无、1 水平、2 垂直。

公共类型在 `base`，例如 `BaseImage`、`BaseStatus`、`BASE_OK`。平台层库名是 `plt`。

## 应用层

应用层目前只放发信。目录是 `src/app`，库名是 `app`。头文件在 `src/app/include`，接口是 `app_email_send`。实现用 C++11。

发信依赖 OpenSSL（`libssl` 和 `libcrypto`）。嵌入式设备上的库路径和链接关系需要自行配置。本工程使用 `find_package(OpenSSL)` 去链接系统里的 OpenSSL。
