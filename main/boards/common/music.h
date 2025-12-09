#ifndef MUSIC_H
#define MUSIC_H

#include <string>
#include <vector>

// 前向声明
struct SongInfo;

class Music {
public:
    virtual ~Music() = default;  // 添加虚析构函数
    
    virtual bool Download(const std::string& song_name, const std::string& artist_name = "") = 0;
    virtual std::string GetDownloadResult() = 0;
    
    // 新增流式播放相关方法
    virtual bool StartStreaming(const std::string& music_url) = 0;
    virtual bool StopStreaming() = 0;  // 停止流式播放
    virtual size_t GetBufferSize() const = 0;
    virtual bool IsDownloading() const = 0;
    virtual int16_t* GetAudioData() = 0;
    
    // 音乐播放信息获取方法
    virtual int GetCurrentSongDurationSeconds() const = 0;
    virtual int GetCurrentPlayTimeSeconds() const = 0;
    virtual float GetPlayProgress() const = 0;
    
    // 播放队列相关方法
    virtual bool PlayPlaylist(const std::vector<SongInfo>& songs) = 0;
    virtual bool NextSong() = 0;
    virtual bool PreviousSong() = 0;
    virtual void StopPlaylist() = 0;
    virtual bool IsPlaylistMode() const = 0;
    virtual int GetCurrentPlaylistIndex() const = 0;
    virtual size_t GetPlaylistSize() const = 0;
    virtual SongInfo GetCurrentSong() const = 0;
};

#endif // MUSIC_H 