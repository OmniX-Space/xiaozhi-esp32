#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <string>

class DeviceManager {
public:
    static DeviceManager& GetInstance();
    
    // 获取设备MAC地址
    std::string GetMACAddress();
    
    // 保存设备Token到NVS
    bool SaveDeviceToken(const std::string& token);
    
    // 从NVS读取设备Token
    std::string GetDeviceToken();
    
    // 清除设备Token（解绑）
    bool ClearDeviceToken();
    
    // 设备绑定流程
    bool BindDevice(const std::string& binding_code, const std::string& device_name = "");
    
    // 验证设备状态
    bool VerifyDevice();
    
    // 歌单相关
    std::string GetFavorites();
    std::string GetUserPlaylists();
    
    // 检查设备是否已绑定
    bool IsDeviceBound();
    
    // 获取绑定的用户名
    std::string GetBoundUsername();
    
private:
    DeviceManager();
    ~DeviceManager();
    
    // 禁止拷贝
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;
    
    std::string mac_address_;
    std::string device_token_;
    std::string bound_username_;
    bool is_bound_;
    
    // NVS命名空间
    static constexpr const char* NVS_NAMESPACE = "device";
    static constexpr const char* NVS_KEY_TOKEN = "token";
    static constexpr const char* NVS_KEY_USERNAME = "username";
    
    // 从NVS加载配置
    void LoadFromNVS();
    
    // 尝试从服务器获取 token（用于网页端绑定后自动同步）
    bool TryFetchTokenFromServer();
};

#endif // DEVICE_MANAGER_H
