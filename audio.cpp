// ============================================================
// 音频模块（全 MCI 方案，winmm.dll，零第三方依赖）
//
// - 背景音乐：mciSendString 播 MP3，循环播放
// - 音效：mciSendString 播 WAV（waveaudio），别名池轮换避免快速连播冲突
// - 音量：setaudio <alias> volume to 0-1000，BGM 与音效独立控制，不碰系统音量
// - 文件定位：exe 所在目录（GetModuleFileNameW），不受工作目录影响
// - 任何失败静默降级，缺音频文件不崩溃
// ============================================================

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include "audio.h"
#include "settings.h"

#define SFX_MAX_ALIASES 8    // 最多同时打开的音效 MCI 设备
#define SFX_PLACE_POOL   2   // 落子音别名池大小（快速连落不冲突）

// ---- 基础工具 ----

// 执行 MCI 命令，失败打印错误信息，返回 -1
static int mci_cmd(const wchar_t* cmd)
{
    MCIERROR err = mciSendStringW(cmd, NULL, 0, NULL);
    if (err != 0)
    {
        wchar_t buf[256];
        if (mciGetErrorStringW(err, buf, 256))
            printf("[audio] MCI(%ls) 失败: %ls\n", cmd, buf);
        else
            printf("[audio] MCI(%ls) 失败: 错误码 %u\n", cmd, err);
        return -1;
    }
    return 0;
}

// 静默执行 MCI 命令（用于尽力而为的音量设置、播放等，失败不打扰）
static int mci_cmd_quiet(const wchar_t* cmd)
{
    return mciSendStringW(cmd, NULL, 0, NULL) == 0 ? 0 : -1;
}

// 拼接 exe 同目录下的文件完整路径
static void exe_dir_path(const wchar_t* filename, wchar_t* out)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = L'\0';
    swprintf(out, MAX_PATH, L"%ls\\%ls", exe, filename);
}

// ---- 音效别名池 ----

typedef struct
{
    const wchar_t* file;   // 音频文件名，如 L"place.wav"
    const wchar_t* base;   // 别名前缀，如 L"place"
    int   pool;            // 该音效使用的别名个数
    int   next;            // 轮换游标
} SfxDef;

static SfxDef g_sfx[] =
{
    { L"place.wav", L"place", SFX_PLACE_POOL },
    { L"win.wav",   L"win",   1 },
    { L"lose.wav",  L"lose",  1 },
};
#define SFX_COUNT (sizeof(g_sfx) / sizeof(g_sfx[0]))

static wchar_t g_open_aliases[SFX_MAX_ALIASES][16];   // 已打开的别名列表
static int g_open_count = 0;

static int alias_opened(const wchar_t* alias)
{
    for (int i = 0; i < g_open_count; i++)
        if (wcscmp(g_open_aliases[i], alias) == 0) return 1;
    return 0;
}

static void add_open_alias(const wchar_t* alias)
{
    if (g_open_count >= SFX_MAX_ALIASES) return;
    wcscpy(g_open_aliases[g_open_count], alias);
    g_open_count++;
}

// 停止所有已打开的音效（避免与 BGM 叠加）
static void stop_all_sfx()
{
    for (int i = 0; i < g_open_count; i++)
    {
        wchar_t cmd[64];
        swprintf(cmd, 64, L"stop %ls", g_open_aliases[i]);
        mci_cmd_quiet(cmd);
    }
}

// ---- 状态 ----

static int g_bgm_open    = 0;  // 背景音乐设备是否已打开
static int g_bgm_playing = 0;  // 背景音乐是否正在播放
static int g_bgm_vol  = 50;    // 缓存当前 BGM 音量 0-100
static int g_sfx_vol  = 80;    // 缓存当前音效音量 0-100

// ---- 背景音乐 ----

void audio_play_bgm()
{
    if (!game_settings.bgm_enabled) return;
    if (g_bgm_playing) return;  // 正在播放

    // 首次播放：打开设备；若曾被 stop（仅暂停）则直接恢复
    if (!g_bgm_open)
    {
        // 新开 BGM 前先停掉可能残留的胜负音
        stop_all_sfx();

        wchar_t path[MAX_PATH], cmd[1024];
        exe_dir_path(L"bgm.mp3", path);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        {
            printf("[audio] bgm.mp3 未找到\n");
            return;
        }

        swprintf(cmd, 1024, L"open \"%ls\" type mpegvideo alias bgm", path);
        if (mci_cmd(cmd)) return;
        g_bgm_open = 1;
        audio_set_bgm_volume(game_settings.bgm_volume);
    }

    if (mci_cmd(L"play bgm repeat")) return;
    g_bgm_playing = 1;
}

void audio_stop_bgm()
{
    if (g_bgm_open)
    {
        mci_cmd_quiet(L"stop bgm");   // 保留设备，恢复播放很快
        g_bgm_playing = 0;
    }
}

void audio_set_bgm_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_bgm_vol = vol;
    if (g_bgm_open)
    {
        // MP3 的 setaudio 属尽力而为，个别系统不支持时静默降级
        wchar_t cmd[64];
        swprintf(cmd, 64, L"setaudio bgm volume to %d", vol * 10);
        mci_cmd_quiet(cmd);
    }
}

// ---- 音效 ----

static void sfx_play(int idx)
{
    SfxDef* s = &g_sfx[idx];
    int slot = s->next;
    s->next = (s->next + 1) % s->pool;

    wchar_t alias[16];
    swprintf(alias, 16, L"%ls%d", s->base, slot);

    // 首次使用时打开 MCI 设备，之后复用
    if (!alias_opened(alias))
    {
        wchar_t path[MAX_PATH], cmd[1024];
        exe_dir_path(s->file, path);
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
        {
            printf("[audio] %ls 未找到\n", s->file);
            return;
        }
        swprintf(cmd, 1024, L"open \"%ls\" type waveaudio alias %ls", path, alias);
        if (mci_cmd(cmd)) return;
        add_open_alias(alias);
    }

    // 应用当前音效音量（waveaudio 原生支持，可靠）
    wchar_t vcmd[64];
    swprintf(vcmd, 64, L"setaudio %ls volume to %d", alias, g_sfx_vol * 10);
    mci_cmd_quiet(vcmd);

    wchar_t pcmd[64];
    swprintf(pcmd, 64, L"play %ls from 0", alias);
    mci_cmd_quiet(pcmd);
}

void audio_play_place()
{
    if (!game_settings.sfx_place_enabled) return;
    sfx_play(0);
}

void audio_play_win()
{
    if (!game_settings.sfx_win_enabled) return;
    sfx_play(1);
}

void audio_play_lose()
{
    if (!game_settings.sfx_win_enabled) return;
    sfx_play(2);
}

void audio_set_sfx_volume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;
    g_sfx_vol = vol;
    for (int i = 0; i < g_open_count; i++)
    {
        wchar_t cmd[64];
        swprintf(cmd, 64, L"setaudio %ls volume to %d", g_open_aliases[i], vol * 10);
        mci_cmd_quiet(cmd);
    }
}

// ---- 生命周期 ----

void audio_init()
{
    g_bgm_vol = game_settings.bgm_volume;
    g_sfx_vol = game_settings.sfx_volume;
}

void audio_cleanup()
{
    stop_all_sfx();

    if (g_bgm_open)
    {
        mci_cmd_quiet(L"close bgm");
        g_bgm_open = 0;
        g_bgm_playing = 0;
    }

    for (int i = 0; i < g_open_count; i++)
    {
        wchar_t cmd[64];
        swprintf(cmd, 64, L"close %ls", g_open_aliases[i]);
        mci_cmd_quiet(cmd);
    }
    g_open_count = 0;
}
