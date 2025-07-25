# XiaoZhi Card

 
-----------------------------------
## 编译配置命令

 
1. 配置编译目标  

```shell
idf.py set-target esp32s3
```

2. 修改配置

```bash
cp ./boards/xiaozhi-card/sdkconfig sdkconfig 
```
 
3. 编译，烧录固件 

> [!NOTE]
> 长按底座按键到灯亮绿色，进入 USB 烧录固件模式。

```shell
idf.py build flash monitor
```