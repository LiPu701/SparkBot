# ESP-SparkBot 示例

此目录包含一系列 ESP-SparkBot 示例项目。这些示例旨在演示桌面机器人的部分功能

# 示例列表

- [factory_demo_v1](./factory_demo_v1): **出厂固件**，包含：桌面钟、大模型语音对话、敲木鱼、摇色子、2048、摄像头
- [volc_rtc_sparkbot](./volc_rtc_sparkbot): 连接豆包 volcano rtc 云端并进行语音交互
- [human_face_recognition_no_wakenet](./human_face_recognition_no_wakenet)：离线人脸识别和移动检测
- [usb_extend_screen_s3](./usb_extend_screen_s3)：将 ESP-SparkBot 变成 Windows 副屏
- [bilibili_fans](./bilibili_fans): 实时显示当前的 B 站粉丝数
- [tank](./tank)：无线遥控侦查小车
  - [c2_tracked_chassis](./tank/c2_tracked_chassis)：ESP32-C2 控制履带小车底盘
  - [sparkbot_motion_control](./tank/sparkbot_motion_control)：ESP-SparkBot 作为小车主控
- [yolo_demo](./yolo_demo)：通过本地 YOLO 模型实现视觉识别。轻触头部开始识别，获得识别结果后再次触摸头部可返回准备界面
