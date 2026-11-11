# pet-monitor

摄像头对着食盆和猫砂盆，记录猫的进食和上厕所。

家用网络摄像头通过 RTSP 接入，检测跑在 RK3588 上。一次进食或上厕所结束后留下记录。

用法和配置以后再补。

## 平台层

`PM_PLATFORM` 选 `stub` 或 `rk3588`。`stub` 的 `plt_init` 会失败，用来在没有芯片的机器上编译和跑接口测试。`rk3588` 需要本机的 MPP、librga、RKNN 头文件，用 `PM_MPP_INCLUDE`、`PM_RGA_INCLUDE`、`PM_RKNN_INCLUDE` 指过去。芯片头文件不放进仓库。

公开接口是 C。C++11 用于两处：RK3588 平台层，因为 librga 的头文件带 C++ 默认参数；应用层发信。

## 媒体层

媒体层放基础图像接口，目录是 `src/media`。库名是 `media`。

- 解码：`media_dec_open`、`media_dec_send`、`media_dec_receive`、`media_dec_release`。类型是 `MediaDec`。
- 缩放、拷贝、转色：`media_pic_scale`。尺寸相同是拷贝，格式不同时顺带转色。
- 画线、画框：`media_pic_line`、`media_pic_box`。里面调用平台层对应的接口。
- 旋转、翻转、平移：`media_pic_rotate`、`media_pic_flip`、`media_pic_translate`。旋转为顺时针 90/180/270。
- JPEG：`media_jpeg_encode` 编码，`media_jpeg_decode` 解码。编码返回时码流已经出来，用 `media_jpeg_free` 释放。解码得到全范围 NV12，用 `plt_mem_free` 释放。
- 视频编码：类型是 `MediaEnc`，打开用 `media_enc_open`。参数不合法返回失败。平台层没有视频编码器时，参数合法也返回不支持。送帧、取流、关键帧、改码率和感兴趣区域都交给已打开的编码器。感兴趣区域由调用方给出，这里不按画面区域改写。

公共类型在 `base`，例如 `BaseImage`、`BaseStatus`、`BASE_OK`。平台层库名是 `plt`。

## 应用层

应用层目前只放发信。目录是 `src/app`，库名是 `app`。头文件在 `src/app/include`，接口是 `app_email_send`。实现用 C++11。

发信依赖 OpenSSL（`libssl` 和 `libcrypto`）。嵌入式设备上的库路径和链接关系需要自行配置。本工程使用 `find_package(OpenSSL)` 去链接系统里的 OpenSSL。
