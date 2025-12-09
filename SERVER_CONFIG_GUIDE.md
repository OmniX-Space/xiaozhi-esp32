# ESP32服务器地址配置指南

## 📍 **配置文件位置**

```
xiaozhi-esp32_music/main/server_config.h
```

## 🔧 **如何修改服务器地址**

### **步骤1：查看您的服务器IP地址**

#### **Windows系统**
打开命令提示符（CMD），输入：
```cmd
ipconfig
```

查找"IPv4 地址"，例如：`192.168.1.100`

#### **Linux/Mac系统**
打开终端，输入：
```bash
ifconfig
# 或
ip addr
```

查找局域网IP地址。

---

### **步骤2：编辑配置文件**

打开文件：
```
d:\esp32-music-server\Meow\MeowEmbeddedMusicServer\xiaozhi-esp32_music\xiaozhi-esp32_music\main\server_config.h
```

找到这一行：
```cpp
#define MUSIC_SERVER_URL "http://192.168.1.100:2233"
```

将 `192.168.1.100` 替换为您的服务器IP地址。

---

## 🌐 **配置示例**

### **示例1：本地局域网（推荐）**

服务器和ESP32在同一个WiFi网络中：

```cpp
#define MUSIC_SERVER_URL "http://192.168.1.100:2233"
```

**优点**：
- ✅ 速度快
- ✅ 延迟低
- ✅ 不需要公网IP

---

### **示例2：公网IP**

如果您有公网IP或使用花生壳等内网穿透：

```cpp
#define MUSIC_SERVER_URL "http://123.45.67.89:2233"
```

**注意**：
- ⚠️ 确保路由器端口转发2233端口
- ⚠️ 注意服务器安全

---

### **示例3：域名**

如果您有域名：

```cpp
#define MUSIC_SERVER_URL "http://your-music-server.com:2233"
```

**注意**：
- ⚠️ 确保域名解析正确
- ⚠️ 如果使用HTTPS，改为`https://`

---

### **示例4：使用原作者在线服务器**

```cpp
#define MUSIC_SERVER_URL "http://http-embedded-music.miao-lab.top:2233"
```

**说明**：
- ✅ 无需自己搭建服务器
- ⚠️ 依赖外部服务可用性
- ⚠️ 无法使用设备绑定等个性化功能

---

## 🔍 **如何测试服务器地址是否正确**

### **方法1：浏览器测试**

在浏览器中访问：
```
http://您的服务器IP:2233
```

应该看到Meow Music的Web界面。

---

### **方法2：curl测试**

```bash
curl http://您的服务器IP:2233/api/search?song=江南
```

应该返回JSON格式的搜索结果。

---

## 📝 **完整配置检查清单**

- [ ] 确认服务器正在运行（`go run .`）
- [ ] 确认服务器端口是2233
- [ ] 确认ESP32和服务器在同一网络（或有公网连接）
- [ ] 修改`server_config.h`中的IP地址
- [ ] 保存文件
- [ ] 重新编译ESP32固件（`idf.py build`）
- [ ] 烧录到ESP32（`idf.py flash`）
- [ ] 测试连接

---

## 🐛 **常见问题**

### **问题1：ESP32无法连接服务器**

**现象**：
```
[Esp32Music] Failed to connect to music API
```

**排查**：
1. 检查服务器是否运行
2. 检查IP地址是否正确
3. 检查ESP32是否连接WiFi
4. Ping服务器IP测试网络连通性

---

### **问题2：地址写错了**

**现象**：
```
[Esp32Music] HTTP GET failed with status code: 404
```

**解决**：
- 检查URL格式是否正确
- 确保有`http://`前缀
- 确保端口号是`:2233`

---

### **问题3：防火墙阻止**

**现象**：
- ESP32无法连接
- 但浏览器可以访问

**解决（Windows）**：
```
控制面板 → Windows防火墙 → 允许应用通过防火墙
→ 找到Go程序 → 允许专用和公用网络
```

---

## 🎯 **推荐配置**

### **开发测试阶段**

使用局域网IP：
```cpp
#define MUSIC_SERVER_URL "http://192.168.1.100:2233"
```

### **生产环境**

使用域名：
```cpp
#define MUSIC_SERVER_URL "http://music.your-domain.com:2233"
```

---

## 💡 **高级技巧**

### **使用环境变量（未来功能）**

可以考虑在ESP32端添加NVS配置，通过Web界面修改服务器地址，无需重新编译。

### **mDNS服务发现（未来功能）**

可以使用mDNS实现服务器自动发现：
```
http://meow-music.local:2233
```

---

## 📞 **技术支持**

如有问题，请加入：
**喵波音律QQ交流群：865754861**

---

**配置完成后，记得重新编译并烧录固件！** 🚀
