#ifndef SERVER_CONFIG_H
#define SERVER_CONFIG_H

// ⚠️ 请修改为您的服务器地址
// 服务器配置说明：
//
// 1. 本地测试（服务器在同一台电脑）：
//    - 先查看电脑的局域网IP：
//      Windows: 打开cmd，输入 ipconfig，查看"IPv4 地址"
//      示例：192.168.1.100
//    - 将下面的地址改为：http://192.168.1.100:2233
//
// 2. 远程服务器（有公网IP或域名）：
//    - 使用公网IP：http://123.45.67.89:2233
//    - 使用域名：http://your-domain.com:2233
//
// 3. 使用原作者的在线服务器：
//    - http://http-embedded-music.miao-lab.top:2233

// 👇 在这里修改您的服务器地址
#define MUSIC_SERVER_URL "http://110.42.59.54:2233"

// 自动生成的完整API地址（不要修改）
#define MUSIC_API_URL MUSIC_SERVER_URL "/stream_pcm"
#define DEVICE_BIND_API_URL MUSIC_SERVER_URL "/api/esp32/bind"
#define DEVICE_VERIFY_API_URL MUSIC_SERVER_URL "/api/esp32/verify"

// 歌单与收藏 API
#define FAVORITE_LIST_API_URL MUSIC_SERVER_URL "/api/favorite/list"
#define PLAYLIST_LIST_API_URL MUSIC_SERVER_URL "/api/user/playlists"
#define PLAYLIST_DETAIL_API_URL MUSIC_SERVER_URL "/api/user/playlists" // 详情可能需要带ID参数

#endif // SERVER_CONFIG_H
