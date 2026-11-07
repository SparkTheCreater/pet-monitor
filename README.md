# pet-monitor

摄像头对着食盆和猫砂盆，记录猫的进食和上厕所。

家用网络摄像头通过 RTSP 接入，检测跑在 RK3588 上。一次进食或上厕所结束后留下记录。

用法和配置以后再补。

## 平台层

`PM_PLATFORM` 选 `stub` 或 `rk3588`。`stub` 的 `plt_init` 会失败，用来在没有芯片的机器上编译和跑接口测试。`rk3588` 需要本机的 MPP、librga、RKNN 头文件，用 `PM_MPP_INCLUDE`、`PM_RGA_INCLUDE`、`PM_RKNN_INCLUDE` 指过去。芯片头文件不放进仓库。

公开接口是 C。RK3588 的实现用 C++11 编译，因为 librga 的头文件带 C++ 默认参数。
