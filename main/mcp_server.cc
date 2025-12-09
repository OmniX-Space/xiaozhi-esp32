/*
 * MCP Server Implementation
 * Reference: https://modelcontextprotocol.io/specification/2024-11-05
 */

#include "mcp_server.h"
#include <esp_log.h>
#include <esp_app_desc.h>
#include <algorithm>
#include <cstring>
#include <esp_pthread.h>

#include "application.h"
#include "display.h"
#include "oled_display.h"
#include "board.h"
#include "settings.h"
#include "lvgl_theme.h"
#include "lvgl_display.h"
#include "boards/common/esp32_music.h"
#include "device_manager.h"
#define TAG "MCP"

McpServer::McpServer() {
}

McpServer::~McpServer() {
    for (auto tool : tools_) {
        delete tool;
    }
    tools_.clear();
}

void McpServer::AddCommonTools() {
    // *Important* To speed up the response time, we add the common tools to the beginning of
    // the tools list to utilize the prompt cache.
    // **重要** 为了提升响应速度，我们把常用的工具放在前面，利用 prompt cache 的特性。

    // Backup the original tools list and restore it after adding the common tools.
    auto original_tools = std::move(tools_);
    auto& board = Board::GetInstance();

    // Do not add custom tools here.
    // Custom tools must be added in the board's InitializeTools function.

    AddTool("self.get_device_status",
        "Provides the real-time information of the device, including the current status of the audio speaker, screen, battery, network, etc.\n"
        "Use this tool for: \n"
        "1. Answering questions about current condition (e.g. what is the current volume of the audio speaker?)\n"
        "2. As the first step to control the device (e.g. turn up / down the volume of the audio speaker, etc.)",
        PropertyList(),
        [&board](const PropertyList& properties) -> ReturnValue {
            return board.GetDeviceStatusJson();
        });

    AddTool("self.audio_speaker.set_volume", 
        "Set the volume of the audio speaker. If the current volume is unknown, you must call `self.get_device_status` tool first and then call this tool.",
        PropertyList({
            Property("volume", kPropertyTypeInteger, 0, 100)
        }), 
        [&board](const PropertyList& properties) -> ReturnValue {
            auto codec = board.GetAudioCodec();
            codec->SetOutputVolume(properties["volume"].value<int>());
            return true;
        });
    
    auto backlight = board.GetBacklight();
    if (backlight) {
        AddTool("self.screen.set_brightness",
            "Set the brightness of the screen.",
            PropertyList({
                Property("brightness", kPropertyTypeInteger, 0, 100)
            }),
            [backlight](const PropertyList& properties) -> ReturnValue {
                uint8_t brightness = static_cast<uint8_t>(properties["brightness"].value<int>());
                backlight->SetBrightness(brightness, true);
                return true;
            });
    }

#ifdef HAVE_LVGL
    auto display = board.GetDisplay();
    if (display && display->GetTheme() != nullptr) {
        AddTool("self.screen.set_theme",
            "Set the theme of the screen. The theme can be `light` or `dark`.",
            PropertyList({
                Property("theme", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto theme_name = properties["theme"].value<std::string>();
                auto& theme_manager = LvglThemeManager::GetInstance();
                auto theme = theme_manager.GetTheme(theme_name);
                if (theme != nullptr) {
                    display->SetTheme(theme);
                    return true;
                }
                return false;
            });
    }

    auto camera = board.GetCamera();
    if (camera) {
        AddTool("self.camera.take_photo",
            "Take a photo and explain it. Use this tool after the user asks you to see something.\n"
            "Args:\n"
            "  `question`: The question that you want to ask about the photo.\n"
            "Return:\n"
            "  A JSON object that provides the photo information.",
            PropertyList({
                Property("question", kPropertyTypeString)
            }),
            [camera](const PropertyList& properties) -> ReturnValue {
                // Lower the priority to do the camera capture
                TaskPriorityReset priority_reset(1);

                if (!camera->Capture()) {
                    throw std::runtime_error("Failed to capture photo");
                }
                auto question = properties["question"].value<std::string>();
                return camera->Explain(question);
            });
    }
    auto music = board.GetMusic();
    if (music)
    {
        AddTool("self.music.play_song",
                "Play the specified song. When users request to play music, this tool will automatically retrieve song details and start streaming.\n"
                "parameters:\n"
                "  `song_name`: The name of the song to be played.\n"
                "  `artist`: (Optional) The artist name. Highly recommended when playing from playlists to ensure correct song match.\n"
                "return:\n"
                "  Play status information without confirmation, immediately play the song.",
                PropertyList({
                    Property("song_name", kPropertyTypeString),
                    Property("artist", kPropertyTypeString, "")
                }),
                [music](const PropertyList &properties) -> ReturnValue
                {
                    auto song_name = properties["song_name"].value<std::string>();
                    auto artist = properties["artist"].value<std::string>();
                    
                    // 分别传递歌名和艺术家，不拼接
                    if (!music->Download(song_name, artist))
                    {
                        return "{\"success\": false, \"message\": \"Failed to obtain music resources\"}";
                    }
                    auto download_result = music->GetDownloadResult();
                    ESP_LOGD(TAG, "Music details result: %s", download_result.c_str());
                    return true;
                });

    }
    
    // Device binding tools
    AddTool("self.device.bind",
        "Bind this ESP32 device to a user account using a 6-digit binding code.\n"
        "Users need to:\n"
        "1. Login to the web console (http://47.118.17.234:2233)\n"
        "2. Generate a binding code (valid for 5 minutes)\n"
        "3. Tell the device: '绑定设备，绑定码123456'\n"
        "Parameters:\n"
        "  `binding_code`: 6-digit binding code from web console\n"
        "  `device_name`: Optional custom device name (default: ESP32音乐播放器)\n"
        "Returns:\n"
        "  Success message with bound username, or error message.",
        PropertyList({
            Property("binding_code", kPropertyTypeString),
            Property("device_name", kPropertyTypeString, "")
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& device_manager = DeviceManager::GetInstance();
            
            std::string binding_code = properties["binding_code"].value<std::string>();
            std::string device_name = properties["device_name"].value<std::string>();
            
            if (binding_code.empty()) {
                return "错误：绑定码不能为空";
            }
            
            if (binding_code.length() != 6) {
                return "错误：绑定码必须是6位数字";
            }
            
            // Check if device is already bound
            if (device_manager.IsDeviceBound()) {
                std::string username = device_manager.GetBoundUsername();
                return "设备已绑定到用户: " + username + "\n如需重新绑定，请先解绑。";
            }
            
            // Attempt to bind
            bool success = device_manager.BindDevice(binding_code, device_name);
            
            if (success) {
                std::string username = device_manager.GetBoundUsername();
                return "✅ 设备绑定成功！\n已绑定到用户: " + username;
            } else {
                return "❌ 绑定失败！请检查：\n"
                       "1. 绑定码是否正确\n"
                       "2. 绑定码是否已过期（有效期5分钟）\n"
                       "3. 网络连接是否正常";
            }
        });

    AddTool("self.device.unbind",
        "Unbind this device from the current user account.\n"
        "This will remove the device binding and require re-binding to use personalized features.\n"
        "Returns:\n"
        "  Success or error message.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& device_manager = DeviceManager::GetInstance();
            
            if (!device_manager.IsDeviceBound()) {
                return "设备未绑定，无需解绑";
            }
            
            std::string username = device_manager.GetBoundUsername();
            bool success = device_manager.ClearDeviceToken();
            
            if (success) {
                return "✅ 设备已解绑\n之前绑定的用户: " + username;
            } else {
                return "❌ 解绑失败，请稍后重试";
            }
        });

    AddTool("self.device.status",
        "Get the current device binding status and information.\n"
        "Returns:\n"
        "  Device binding status, MAC address, bound username, etc.",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& device_manager = DeviceManager::GetInstance();
            
            std::string result = "📱 设备信息:\n\n";
            result += "MAC地址: " + device_manager.GetMACAddress() + "\n";
            
            if (device_manager.IsDeviceBound()) {
                result += "绑定状态: ✅ 已绑定\n";
                result += "绑定用户: " + device_manager.GetBoundUsername() + "\n";
                
                // Try to verify with server
                bool verified = device_manager.VerifyDevice();
                result += "服务器验证: " + std::string(verified ? "✅ 通过" : "❌ 失败") + "\n";
            } else {
                result += "绑定状态: ❌ 未绑定\n";
                result += "\n💡 提示: 使用 '绑定设备' 功能来绑定账号";
            }
            
            return result;
        });

    // 歌单相关工具
    AddTool("self.music.favorite_list",
        "获取我的'我喜欢'歌单中的歌曲列表。\n"
        "Returns:\n"
        "  歌曲列表JSON数组，每首歌包含：\n"
        "  - title: 歌曲名\n"
        "  - artist: 艺术家名\n"
        "  - duration: 时长\n"
        "  **播放选项**:\n"
        "  1. 播放单首歌：使用 play_song 工具，同时传递 song_name 和 artist 参数\n"
        "  2. 播放整个歌单：使用 play_playlist 工具，传递完整的歌曲JSON数组",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& device_manager = DeviceManager::GetInstance();
            if (!device_manager.IsDeviceBound()) {
                return "错误：设备未绑定，请先绑定账号";
            }
            
            std::string result = device_manager.GetFavorites();
            if (result.empty()) {
                return "获取歌单失败或歌单为空";
            }
            return result;
        });

    AddTool("self.music.my_playlists",
        "获取我创建的歌单列表。\n"
        "Returns:\n"
        "  歌单列表JSON数组，每个歌单包含 songs 数组，每首歌包含：\n"
        "  - title: 歌曲名\n"
        "  - artist: 艺术家名\n"
        "  - duration: 时长\n"
        "  **重要**: 播放歌单中的歌曲时，请同时传递 song_name 和 artist 参数给 play_song 工具。",
        PropertyList(),
        [](const PropertyList& properties) -> ReturnValue {
            auto& device_manager = DeviceManager::GetInstance();
            if (!device_manager.IsDeviceBound()) {
                return "错误：设备未绑定，请先绑定账号";
            }
            
            std::string result = device_manager.GetUserPlaylists();
            if (result.empty()) {
                return "获取歌单失败或没有歌单";
            }
            return result;
        });

    // 播放整个歌单工具
    AddTool("self.music.play_playlist",
        "播放整个歌单，连续播放歌单中的所有歌曲。\n"
        "parameters:\n"
        "  `songs`: JSON格式的歌曲数组，每首歌必须包含 title 和 artist 字段\n"
        "return:\n"
        "  开始播放歌单的状态信息",
        PropertyList({
            Property("songs", kPropertyTypeString)
        }),
        [music](const PropertyList &properties) -> ReturnValue
        {
            auto songs_json = properties["songs"].value<std::string>();
            
            // 解析歌曲JSON数组
            cJSON* json = cJSON_Parse(songs_json.c_str());
            if (!json || !cJSON_IsArray(json)) {
                if (json) cJSON_Delete(json);
                return "{\"success\": false, \"message\": \"Invalid songs JSON format\"}";
            }
            
            std::vector<SongInfo> playlist;
            int array_size = cJSON_GetArraySize(json);
            
            for (int i = 0; i < array_size; i++) {
                cJSON* song_item = cJSON_GetArrayItem(json, i);
                if (!song_item) continue;
                
                cJSON* title = cJSON_GetObjectItem(song_item, "title");
                cJSON* artist = cJSON_GetObjectItem(song_item, "artist");
                
                if (cJSON_IsString(title) && cJSON_IsString(artist)) {
                    playlist.emplace_back(title->valuestring, artist->valuestring);
                }
            }
            
            cJSON_Delete(json);
            
            if (playlist.empty()) {
                return "{\"success\": false, \"message\": \"No valid songs found in playlist\"}";
            }
            
            // 开始播放歌单
            if (music->PlayPlaylist(playlist)) {
                return "{\"success\": true, \"message\": \"Started playing playlist with " + 
                       std::to_string(playlist.size()) + " songs\"}";
            } else {
                return "{\"success\": false, \"message\": \"Failed to start playlist\"}";
            }
        });

    // 播放队列控制工具
    AddTool("self.music.next_song",
        "播放下一首歌曲（仅在播放歌单时有效）。\n"
        "return:\n"
        "  切换到下一首歌的状态信息",
        PropertyList(),
        [music](const PropertyList &properties) -> ReturnValue
        {
            if (!music->IsPlaylistMode()) {
                return "{\"success\": false, \"message\": \"Not in playlist mode\"}";
            }
            
            if (music->NextSong()) {
                return "{\"success\": true, \"message\": \"Switched to next song\"}";
            } else {
                return "{\"success\": false, \"message\": \"Already at last song or playlist ended\"}";
            }
        });

    AddTool("self.music.previous_song",
        "播放上一首歌曲（仅在播放歌单时有效）。\n"
        "return:\n"
        "  切换到上一首歌的状态信息",
        PropertyList(),
        [music](const PropertyList &properties) -> ReturnValue
        {
            if (!music->IsPlaylistMode()) {
                return "{\"success\": false, \"message\": \"Not in playlist mode\"}";
            }
            
            if (music->PreviousSong()) {
                return "{\"success\": true, \"message\": \"Switched to previous song\"}";
            } else {
                return "{\"success\": false, \"message\": \"Already at first song\"}";
            }
        });

    AddTool("self.music.stop_playlist",
        "停止播放歌单。\n"
        "return:\n"
        "  停止播放歌单的状态信息",
        PropertyList(),
        [music](const PropertyList &properties) -> ReturnValue
        {
            music->StopPlaylist();
            return "{\"success\": true, \"message\": \"Playlist stopped\"}";
        });
    
    // 闹钟功能工具
    AddTool("self.alarm.add",
        "Set a new alarm with music playback. When users request to set an alarm, this tool will create the alarm with specified parameters.\n"
        "🎵 Music Feature: If no specific music is provided, the system will randomly select from 40+ popular songs including Chinese pop, classics, and international hits.\n"
        "Parameters:\n"
        "  `hour`: Hour of the alarm (0-23)\n"
        "  `minute`: Minute of the alarm (0-59)\n"
        "  `repeat_mode`: Repeat mode (0=once, 1=daily, 2=weekdays, 3=weekends)\n"
        "  `label`: Optional label/description for the alarm\n"
        "  `music_name`: Optional specific music to play (leave empty for random selection)\n"
        "Returns:\n"
        "  Alarm ID if successful, error message if failed.",
        PropertyList({
            Property("hour", kPropertyTypeInteger, 0, 23),
            Property("minute", kPropertyTypeInteger, 0, 59),
            Property("repeat_mode", kPropertyTypeInteger, 0, 0, 3),
            Property("label", kPropertyTypeString, ""),
            Property("music_name", kPropertyTypeString, "")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            
            int hour = properties["hour"].value<int>();
            int minute = properties["minute"].value<int>();
            AlarmRepeatMode repeat_mode = (AlarmRepeatMode)properties["repeat_mode"].value<int>();
            std::string label = properties["label"].value<std::string>();
            std::string music_name = properties["music_name"].value<std::string>();
            
            int alarm_id = alarm_manager.AddAlarm(hour, minute, repeat_mode, label, music_name);
            
            if (alarm_id > 0) {
                std::string result = "已设置闹钟: " + AlarmManager::FormatTime(hour, minute);
                if (!label.empty()) {
                    result += " - " + label;
                }
                if (!music_name.empty()) {
                    result += " (音乐: " + music_name + ")";
                }
                
                // 显示重复模式
                switch (repeat_mode) {
                    case kAlarmOnce: result += " (一次性)"; break;
                    case kAlarmDaily: result += " (每日)"; break;
                    case kAlarmWeekdays: result += " (工作日)"; break;
                    case kAlarmWeekends: result += " (周末)"; break;
                    case kAlarmCustom: result += " (自定义)"; break;
                }
                
                return result;
            } else {
                return "设置闹钟失败，请检查时间格式";
            }
        });

    AddTool("self.alarm.list",
        "List all alarms and show their status.\n"
        "Returns:\n"
        "  List of all alarms with their details.",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            auto alarms = alarm_manager.GetAllAlarms();
            
            if (alarms.empty()) {
                return "没有设置任何闹钟";
            }
            
            std::string result = "闹钟列表:\n";
            for (const auto& alarm : alarms) {
                result += "ID " + std::to_string(alarm.id) + ": ";
                result += AlarmManager::FormatAlarmTime(alarm);
                
                if (!alarm.label.empty()) {
                    result += " - " + alarm.label;
                }
                
                switch (alarm.status) {
                    case kAlarmEnabled: result += " [启用]"; break;
                    case kAlarmDisabled: result += " [禁用]"; break;
                    case kAlarmTriggered: result += " [正在响铃]"; break;
                    case kAlarmSnoozed: result += " [贪睡中]"; break;
                }
                
                if (!alarm.music_name.empty()) {
                    result += " (音乐: " + alarm.music_name + ")";
                }
                result += "\n";
            }
            
            result += "\n" + alarm_manager.GetNextAlarmInfo();
            return result;
        });

    AddTool("self.alarm.remove",
        "Remove/delete an alarm by ID.\n"
        "Parameters:\n"
        "  `alarm_id`: ID of the alarm to remove\n"
        "Returns:\n"
        "  Success or error message.",
        PropertyList({
            Property("alarm_id", kPropertyTypeInteger)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            int alarm_id = properties["alarm_id"].value<int>();
            
            if (alarm_manager.RemoveAlarm(alarm_id)) {
                return "已删除闹钟 ID " + std::to_string(alarm_id);
            } else {
                return "未找到闹钟 ID " + std::to_string(alarm_id);
            }
        });

    AddTool("self.alarm.toggle",
        "Enable or disable an alarm by ID.\n"
        "Parameters:\n"
        "  `alarm_id`: ID of the alarm to toggle\n"
        "  `enabled`: True to enable, false to disable\n"
        "Returns:\n"
        "  Success or error message.",
        PropertyList({
            Property("alarm_id", kPropertyTypeInteger),
            Property("enabled", kPropertyTypeBoolean, true)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            int alarm_id = properties["alarm_id"].value<int>();
            bool enabled = properties["enabled"].value<bool>();
            
            if (alarm_manager.EnableAlarm(alarm_id, enabled)) {
                return "闹钟 ID " + std::to_string(alarm_id) + (enabled ? " 已启用" : " 已禁用");
            } else {
                return "未找到闹钟 ID " + std::to_string(alarm_id);
            }
        });

    AddTool("self.alarm.snooze",
        "Snooze the currently active alarm.\n"
        "Parameters:\n"
        "  `alarm_id`: ID of the alarm to snooze (optional, will snooze first active alarm if not specified)\n"
        "Returns:\n"
        "  Success or error message.",
        PropertyList({
            Property("alarm_id", kPropertyTypeInteger, -1)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            int alarm_id = properties["alarm_id"].value<int>();
            
            if (alarm_id == -1) {
                // 贪睡第一个活动的闹钟
                auto active_alarms = alarm_manager.GetActiveAlarms();
                if (!active_alarms.empty()) {
                    alarm_id = active_alarms[0].id;
                } else {
                    return "没有正在响铃的闹钟";
                }
            }
            
            if (alarm_manager.SnoozeAlarm(alarm_id)) {
                return "闹钟已贪睡5分钟";
            } else {
                return "无法贪睡闹钟，可能已达到最大贪睡次数";
            }
        });

    AddTool("self.alarm.stop",
        "Stop the currently active alarm.\n"
        "Parameters:\n"
        "  `alarm_id`: ID of the alarm to stop (optional, will stop first active alarm if not specified)\n"
        "Returns:\n"
        "  Success or error message.",
        PropertyList({
            Property("alarm_id", kPropertyTypeInteger, -1)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& alarm_manager = AlarmManager::GetInstance();
            int alarm_id = properties["alarm_id"].value<int>();
            
            if (alarm_id == -1) {
                // 停止第一个活动的闹钟
                auto active_alarms = alarm_manager.GetActiveAlarms();
                if (!active_alarms.empty()) {
                    alarm_id = active_alarms[0].id;
                } else {
                    return "没有正在响铃的闹钟";
                }
            }
            
            if (alarm_manager.StopAlarm(alarm_id)) {
                return "闹钟已关闭";
            } else {
                return "未找到活动的闹钟";
            }
        });

    AddTool("self.alarm.music_list",
        "Show the list of default alarm music. Users can reference this list when setting custom alarm music.\n"
        "Returns:\n"
        "  List of available alarm music songs.",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            auto music_list = app.GetDefaultAlarmMusicList();
            
            if (music_list.empty()) {
                return "暂无可用的闹钟音乐";
            }
            
            std::string result = "🎵 可用的闹钟音乐列表:\n\n";
            result += "📝 使用说明: 设置闹钟时可以指定以下任意一首歌曲作为闹钟铃声\n";
            result += "🎲 如果不指定音乐，系统会随机播放其中一首\n\n";
            
            // 分类显示音乐
            result += "🇨🇳 中文流行:\n";
            std::vector<std::string> chinese_songs = {
                "晴天", "七里香", "青花瓷", "稻香", "彩虹", "告白气球", "说好不哭",
                "夜曲", "花海", "简单爱", "听妈妈的话", "东风破", "菊花台",
                "起风了", "红豆", "好久不见", "匆匆那年", "老男孩", "那些年",
                "小幸运", "成都", "南山南", "演员", "体面", "盗将行", "大鱼"
            };
            
            for (size_t i = 0; i < chinese_songs.size() && i < 15; i++) {
                result += "  • " + chinese_songs[i] + "\n";
            }
            
            result += "\n🎼 经典怀旧:\n";
            std::vector<std::string> classic_songs = {
                "新不了情", "月亮代表我的心", "甜蜜蜜", "我只在乎你",
                "友谊之光", "童年", "海阔天空", "光辉岁月", "真的爱你", "喜欢你"
            };
            
            for (const auto& song : classic_songs) {
                result += "  • " + song + "\n";
            }
            
            result += "\n🌍 国际流行:\n";
            std::vector<std::string> international_songs = {
                "closer", "sugar", "shape of you", "despacito", 
                "perfect", "happier", "someone like you"
            };
            
            for (const auto& song : international_songs) {
                result += "  • " + song + "\n";
            }
            
            result += "\n💡 示例: \"明天早上7点播放青花瓷叫我起床\"";
            return result;
        });

    AddTool("self.alarm.test_music_ui",
        "Test the new vinyl record music UI interface. This tool will simulate a music playback to showcase the new rotating vinyl record interface.\n"
        "Parameters:\n"
        "  `song_name`: Name of the song to display (optional)\n"
        "  `duration`: Test duration in seconds (default 10 seconds)\n"
        "Returns:\n"
        "  Status message about the UI test.",
        PropertyList({
            Property("song_name", kPropertyTypeString, "晴天"),
            Property("duration", kPropertyTypeInteger, 10, 5, 60)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            auto display = board.GetDisplay();
            
            if (!display) {
                return "显示器不可用，无法测试音乐界面";
            }
            
            std::string song_name = properties["song_name"].value<std::string>();
            int duration = properties["duration"].value<int>();
            
            if (song_name.empty()) {
                song_name = "UI测试 - 旋转唱片界面";
            }
            
            // 显示音乐界面
            display->SetMusicProgress(song_name.c_str(), 0, duration, 0.0f);
            
            return "🎵 已启动音乐界面测试！\n"
                   "✨ 特色功能展示:\n"
                   "  🎵 旋转唱片 - 黑胶唱片持续旋转\n"
                   "  📡 唱片臂 - 自动放下/收起动画\n" 
                   "  📊 进度条 - 实时显示播放进度\n"
                   "  ⏰ 时间显示 - 当前时间/总时长\n"
                   "  🌊 音波装饰 - 动态音乐波形\n"
                   "测试时长: " + std::to_string(duration) + " 秒\n"
                   "歌曲: " + song_name;
        });
#endif

    // Restore the original tools list to the end of the tools list
    tools_.insert(tools_.end(), original_tools.begin(), original_tools.end());
}

void McpServer::AddUserOnlyTools() {
    // System tools
    AddUserOnlyTool("self.get_system_info",
        "Get the system information",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& board = Board::GetInstance();
            return board.GetSystemInfoJson();
        });

    AddUserOnlyTool("self.reboot", "Reboot the system",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            auto& app = Application::GetInstance();
            app.Schedule([&app]() {
                ESP_LOGW(TAG, "User requested reboot");
                vTaskDelay(pdMS_TO_TICKS(1000));

                app.Reboot();
            });
            return true;
        });

    // Firmware upgrade
    AddUserOnlyTool("self.upgrade_firmware", "Upgrade firmware from a specific URL. This will download and install the firmware, then reboot the device.",
        PropertyList({
            Property("url", kPropertyTypeString, "The URL of the firmware binary file to download and install")
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            ESP_LOGI(TAG, "User requested firmware upgrade from URL: %s", url.c_str());
            
            auto& app = Application::GetInstance();
            app.Schedule([url, &app]() {
                auto ota = std::make_unique<Ota>();
                
                bool success = app.UpgradeFirmware(*ota, url);
                if (!success) {
                    ESP_LOGE(TAG, "Firmware upgrade failed");
                }
            });
            
            return true;
        });

    // Display control
#ifdef HAVE_LVGL
    auto display = dynamic_cast<LvglDisplay*>(Board::GetInstance().GetDisplay());
    if (display) {
        AddUserOnlyTool("self.screen.get_info", "Information about the screen, including width, height, etc.",
            PropertyList(),
            [display](const PropertyList& properties) -> ReturnValue {
                cJSON *json = cJSON_CreateObject();
                cJSON_AddNumberToObject(json, "width", display->width());
                cJSON_AddNumberToObject(json, "height", display->height());
                if (dynamic_cast<OledDisplay*>(display)) {
                    cJSON_AddBoolToObject(json, "monochrome", true);
                } else {
                    cJSON_AddBoolToObject(json, "monochrome", false);
                }
                return json;
            });

#if CONFIG_LV_USE_SNAPSHOT
        AddUserOnlyTool("self.screen.snapshot", "Snapshot the screen and upload it to a specific URL",
            PropertyList({
                Property("url", kPropertyTypeString),
                Property("quality", kPropertyTypeInteger, 80, 1, 100)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto quality = properties["quality"].value<int>();

                std::string jpeg_data;
                if (!display->SnapshotToJpeg(jpeg_data, quality)) {
                    throw std::runtime_error("Failed to snapshot screen");
                }

                ESP_LOGI(TAG, "Upload snapshot %u bytes to %s", jpeg_data.size(), url.c_str());
                
                // 构造multipart/form-data请求体
                std::string boundary = "----ESP32_SCREEN_SNAPSHOT_BOUNDARY";
                
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
                http->SetHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
                if (!http->Open("POST", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                {
                    // 文件字段头部
                    std::string file_header;
                    file_header += "--" + boundary + "\r\n";
                    file_header += "Content-Disposition: form-data; name=\"file\"; filename=\"screenshot.jpg\"\r\n";
                    file_header += "Content-Type: image/jpeg\r\n";
                    file_header += "\r\n";
                    http->Write(file_header.c_str(), file_header.size());
                }

                // JPEG数据
                http->Write((const char*)jpeg_data.data(), jpeg_data.size());

                {
                    // multipart尾部
                    std::string multipart_footer;
                    multipart_footer += "\r\n--" + boundary + "--\r\n";
                    http->Write(multipart_footer.c_str(), multipart_footer.size());
                }
                http->Write("", 0);

                if (http->GetStatusCode() != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(http->GetStatusCode()));
                }
                std::string result = http->ReadAll();
                http->Close();
                ESP_LOGI(TAG, "Snapshot screen result: %s", result.c_str());
                return true;
            });
        
        AddUserOnlyTool("self.screen.preview_image", "Preview an image on the screen",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [display](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);

                if (!http->Open("GET", url)) {
                    throw std::runtime_error("Failed to open URL: " + url);
                }
                int status_code = http->GetStatusCode();
                if (status_code != 200) {
                    throw std::runtime_error("Unexpected status code: " + std::to_string(status_code));
                }

                size_t content_length = http->GetBodyLength();
                char* data = (char*)heap_caps_malloc(content_length, MALLOC_CAP_8BIT);
                if (data == nullptr) {
                    throw std::runtime_error("Failed to allocate memory for image: " + url);
                }
                size_t total_read = 0;
                while (total_read < content_length) {
                    int ret = http->Read(data + total_read, content_length - total_read);
                    if (ret < 0) {
                        heap_caps_free(data);
                        throw std::runtime_error("Failed to download image: " + url);
                    }
                    if (ret == 0) {
                        break;
                    }
                    total_read += ret;
                }
                http->Close();

                auto image = std::make_unique<LvglAllocatedImage>(data, content_length);
                display->SetPreviewImage(std::move(image));
                return true;
            });
#endif // CONFIG_LV_USE_SNAPSHOT
    }
#endif // HAVE_LVGL

    // Assets download url
    auto& assets = Assets::GetInstance();
    if (assets.partition_valid()) {
        AddUserOnlyTool("self.assets.set_download_url", "Set the download url for the assets",
            PropertyList({
                Property("url", kPropertyTypeString)
            }),
            [](const PropertyList& properties) -> ReturnValue {
                auto url = properties["url"].value<std::string>();
                Settings settings("assets", true);
                settings.SetString("download_url", url);
                return true;
            });
    }
}

void McpServer::AddTool(McpTool* tool) {
    // Prevent adding duplicate tools
    if (std::find_if(tools_.begin(), tools_.end(), [tool](const McpTool* t) { return t->name() == tool->name(); }) != tools_.end()) {
        ESP_LOGW(TAG, "Tool %s already added", tool->name().c_str());
        return;
    }

    ESP_LOGI(TAG, "Add tool: %s%s", tool->name().c_str(), tool->user_only() ? " [user]" : "");
    tools_.push_back(tool);
}

void McpServer::AddTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    AddTool(new McpTool(name, description, properties, callback));
}

void McpServer::AddUserOnlyTool(const std::string& name, const std::string& description, const PropertyList& properties, std::function<ReturnValue(const PropertyList&)> callback) {
    auto tool = new McpTool(name, description, properties, callback);
    tool->set_user_only(true);
    AddTool(tool);
}

void McpServer::ParseMessage(const std::string& message) {
    cJSON* json = cJSON_Parse(message.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Failed to parse MCP message: %s", message.c_str());
        return;
    }
    ParseMessage(json);
    cJSON_Delete(json);
}

void McpServer::ParseCapabilities(const cJSON* capabilities) {
    auto vision = cJSON_GetObjectItem(capabilities, "vision");
    if (cJSON_IsObject(vision)) {
        auto url = cJSON_GetObjectItem(vision, "url");
        auto token = cJSON_GetObjectItem(vision, "token");
        if (cJSON_IsString(url)) {
            auto camera = Board::GetInstance().GetCamera();
            if (camera) {
                std::string url_str = std::string(url->valuestring);
                std::string token_str;
                if (cJSON_IsString(token)) {
                    token_str = std::string(token->valuestring);
                }
                camera->SetExplainUrl(url_str, token_str);
            }
        }
    }
}

void McpServer::ParseMessage(const cJSON* json) {
    // Check JSONRPC version
    auto version = cJSON_GetObjectItem(json, "jsonrpc");
    if (version == nullptr || !cJSON_IsString(version) || strcmp(version->valuestring, "2.0") != 0) {
        ESP_LOGE(TAG, "Invalid JSONRPC version: %s", version ? version->valuestring : "null");
        return;
    }
    
    // Check method
    auto method = cJSON_GetObjectItem(json, "method");
    if (method == nullptr || !cJSON_IsString(method)) {
        ESP_LOGE(TAG, "Missing method");
        return;
    }
    
    auto method_str = std::string(method->valuestring);
    if (method_str.find("notifications") == 0) {
        return;
    }
    
    // Check params
    auto params = cJSON_GetObjectItem(json, "params");
    if (params != nullptr && !cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params for method: %s", method_str.c_str());
        return;
    }

    auto id = cJSON_GetObjectItem(json, "id");
    if (id == nullptr || !cJSON_IsNumber(id)) {
        ESP_LOGE(TAG, "Invalid id for method: %s", method_str.c_str());
        return;
    }
    auto id_int = id->valueint;
    
    if (method_str == "initialize") {
        if (cJSON_IsObject(params)) {
            auto capabilities = cJSON_GetObjectItem(params, "capabilities");
            if (cJSON_IsObject(capabilities)) {
                ParseCapabilities(capabilities);
            }
        }
        auto app_desc = esp_app_get_description();
        std::string message = "{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"" BOARD_NAME "\",\"version\":\"";
        message += app_desc->version;
        message += "\"}}";
        ReplyResult(id_int, message);
    } else if (method_str == "tools/list") {
        std::string cursor_str = "";
        bool list_user_only_tools = false;
        if (params != nullptr) {
            auto cursor = cJSON_GetObjectItem(params, "cursor");
            if (cJSON_IsString(cursor)) {
                cursor_str = std::string(cursor->valuestring);
            }
            auto with_user_tools = cJSON_GetObjectItem(params, "withUserTools");
            if (cJSON_IsBool(with_user_tools)) {
                list_user_only_tools = with_user_tools->valueint == 1;
            }
        }
        GetToolsList(id_int, cursor_str, list_user_only_tools);
    } else if (method_str == "tools/call") {
        if (!cJSON_IsObject(params)) {
            ESP_LOGE(TAG, "tools/call: Missing params");
            ReplyError(id_int, "Missing params");
            return;
        }
        auto tool_name = cJSON_GetObjectItem(params, "name");
        if (!cJSON_IsString(tool_name)) {
            ESP_LOGE(TAG, "tools/call: Missing name");
            ReplyError(id_int, "Missing name");
            return;
        }
        auto tool_arguments = cJSON_GetObjectItem(params, "arguments");
        if (tool_arguments != nullptr && !cJSON_IsObject(tool_arguments)) {
            ESP_LOGE(TAG, "tools/call: Invalid arguments");
            ReplyError(id_int, "Invalid arguments");
            return;
        }
        DoToolCall(id_int, std::string(tool_name->valuestring), tool_arguments);
    } else {
        ESP_LOGE(TAG, "Method not implemented: %s", method_str.c_str());
        ReplyError(id_int, "Method not implemented: " + method_str);
    }
}

void McpServer::ReplyResult(int id, const std::string& result) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id) + ",\"result\":";
    payload += result;
    payload += "}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::ReplyError(int id, const std::string& message) {
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":";
    payload += std::to_string(id);
    payload += ",\"error\":{\"message\":\"";
    payload += message;
    payload += "\"}}";
    Application::GetInstance().SendMcpMessage(payload);
}

void McpServer::GetToolsList(int id, const std::string& cursor, bool list_user_only_tools) {
    const int max_payload_size = 8000;
    std::string json = "{\"tools\":[";
    
    bool found_cursor = cursor.empty();
    auto it = tools_.begin();
    std::string next_cursor = "";
    
    while (it != tools_.end()) {
        // 如果我们还没有找到起始位置，继续搜索
        if (!found_cursor) {
            if ((*it)->name() == cursor) {
                found_cursor = true;
            } else {
                ++it;
                continue;
            }
        }

        if (!list_user_only_tools && (*it)->user_only()) {
            ++it;
            continue;
        }
        
        // 添加tool前检查大小
        std::string tool_json = (*it)->to_json() + ",";
        if (json.length() + tool_json.length() + 30 > max_payload_size) {
            // 如果添加这个tool会超出大小限制，设置next_cursor并退出循环
            next_cursor = (*it)->name();
            break;
        }
        
        json += tool_json;
        ++it;
    }
    
    if (json.back() == ',') {
        json.pop_back();
    }
    
    if (json.back() == '[' && !tools_.empty()) {
        // 如果没有添加任何tool，返回错误
        ESP_LOGE(TAG, "tools/list: Failed to add tool %s because of payload size limit", next_cursor.c_str());
        ReplyError(id, "Failed to add tool " + next_cursor + " because of payload size limit");
        return;
    }

    if (next_cursor.empty()) {
        json += "]}";
    } else {
        json += "],\"nextCursor\":\"" + next_cursor + "\"}";
    }
    
    ReplyResult(id, json);
}

void McpServer::DoToolCall(int id, const std::string& tool_name, const cJSON* tool_arguments) {
    auto tool_iter = std::find_if(tools_.begin(), tools_.end(), 
                                 [&tool_name](const McpTool* tool) { 
                                     return tool->name() == tool_name; 
                                 });
    
    if (tool_iter == tools_.end()) {
        ESP_LOGE(TAG, "tools/call: Unknown tool: %s", tool_name.c_str());
        ReplyError(id, "Unknown tool: " + tool_name);
        return;
    }

    PropertyList arguments = (*tool_iter)->properties();
    try {
        for (auto& argument : arguments) {
            bool found = false;
            if (cJSON_IsObject(tool_arguments)) {
                auto value = cJSON_GetObjectItem(tool_arguments, argument.name().c_str());
                if (argument.type() == kPropertyTypeBoolean && cJSON_IsBool(value)) {
                    argument.set_value<bool>(value->valueint == 1);
                    found = true;
                } else if (argument.type() == kPropertyTypeInteger && cJSON_IsNumber(value)) {
                    argument.set_value<int>(value->valueint);
                    found = true;
                } else if (argument.type() == kPropertyTypeString && cJSON_IsString(value)) {
                    argument.set_value<std::string>(value->valuestring);
                    found = true;
                }
            }

            if (!argument.has_default_value() && !found) {
                ESP_LOGE(TAG, "tools/call: Missing valid argument: %s", argument.name().c_str());
                ReplyError(id, "Missing valid argument: " + argument.name());
                return;
            }
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "tools/call: %s", e.what());
        ReplyError(id, e.what());
        return;
    }

    // Use main thread to call the tool
    auto& app = Application::GetInstance();
    app.Schedule([this, id, tool_iter, arguments = std::move(arguments)]() {
        try {
            ReplyResult(id, (*tool_iter)->Call(arguments));
        } catch (const std::exception& e) {
            ESP_LOGE(TAG, "tools/call: %s", e.what());
            ReplyError(id, e.what());
        }
    });
}
