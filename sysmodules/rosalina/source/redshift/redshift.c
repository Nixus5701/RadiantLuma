/*
*   Night/Light mode for Luma3DS
*   Based on Starlight3DS by Nutez
*   Enhanced with automatic color temperature switching
*   
*   Features:
*   - Delayed color temperature application at boot (waits for GPU to be ready)
*   - Background thread that monitors time and auto-switches modes
*   - Brightness and color temperature control
*   - Saves and restores previous brightness when switching modes
*/

#include <3ds.h>
#include "menus.h"
#include "redshift/redshift.h"
#include "redshift/colorramp.h"
#include "menus/screen_filters.h"
#include "menu.h"
#include "draw.h"
#include "ifile.h"
#include "memory.h"
#include "fmt.h"
#include "MyThread.h"
#include "utils.h"

bool nightLightSettingsRead = false;
bool nightLightOverride = false;
static bool lightModeActive = false;
static bool nightModeActive = false;
static bool bootColorTempApplied = false;
static bool inConfigMenu = false;
static volatile bool isSleeping = false; // Track sleep state to avoid GPU access while asleep

// Background thread for time monitoring
static MyThread timeCheckThread;
static u8 CTR_ALIGN(8) timeCheckThreadStack[0x1000];
static volatile bool timeCheckThreadRunning = false;

night_light_settings s_nightLight = {
    // Brightness settings
    .light_brightnessLevel = 0,
    .light_ledSuppression = false,
    .light_startHour = 8,
    .light_startMinute = 0,
    .night_brightnessLevel = 1,
    .night_ledSuppression = true,
    .night_startHour = 19,
    .night_startMinute = 30,
    .use_nightMode = false,
    .light_changeBrightness = true,
    .night_changeBrightness = true,
    
    // Color temperature settings
    .light_colorTemp = 6500,
    .night_colorTemp = 2700,
    .use_colorTempSwitch = false,
};

void Redshift_SuppressLeds(void)
{
    mcuHwcInit();
    u8 off = 0;
    MCUHWC_WriteRegister(0x28, &off, 1);
    mcuHwcExit();
}

// Apply brightness level
static void Redshift_ApplyBrightness(u8 level)
{
    if(level < 1) level = 1;
    if(level > 5) level = 5;
    
    backlight_controls s_backlight = { .abl_enabled = 0, .brightness_level = 0 };
    cfguInit();
    CFG_GetConfigInfoBlk8(sizeof(backlight_controls), 0x50001, &s_backlight);
    if(s_backlight.brightness_level != level)
    {
        s_backlight.brightness_level = level;
        CFG_SetConfigInfoBlk8(sizeof(backlight_controls), 0x50001, &s_backlight);
    }
    cfguExit();
}

// Apply color temperature
static void Redshift_ApplyColorTemperature(u16 temperature)
{
    if (temperature < 1000) temperature = 1000;
    if (temperature > 25100) temperature = 25100;
    
    topScreenFilter.cct = temperature;
    bottomScreenFilter.cct = temperature;
    
    float wp[3];
    colorramp_get_white_point(wp, temperature);
    
    if(!inConfigMenu)
    {
        svcKernelSetState(0x10000, 2);
        svcSleepThread(5 * 1000 * 100LL);
    }
    
    GPU_FB_TOP_COL_LUT_INDEX = 0;
    for (int i = 0; i <= 255; i++)
    {
        float r_f = (float)i * wp[0];
        float g_f = (float)i * wp[1];
        float b_f = (float)i * wp[2];
        
        u8 r = (u8)(r_f < 0.0f ? 0 : (r_f > 255.0f ? 255 : r_f));
        u8 g = (u8)(g_f < 0.0f ? 0 : (g_f > 255.0f ? 255 : g_f));
        u8 b = (u8)(b_f < 0.0f ? 0 : (b_f > 255.0f ? 255 : b_f));
        
        GPU_FB_TOP_COL_LUT_ELEM = r | (g << 8) | (b << 16);
    }
    
    GPU_FB_BOTTOM_COL_LUT_INDEX = 0;
    for (int i = 0; i <= 255; i++)
    {
        float r_f = (float)i * wp[0];
        float g_f = (float)i * wp[1];
        float b_f = (float)i * wp[2];
        
        u8 r = (u8)(r_f < 0.0f ? 0 : (r_f > 255.0f ? 255 : r_f));
        u8 g = (u8)(g_f < 0.0f ? 0 : (g_f > 255.0f ? 255 : g_f));
        u8 b = (u8)(b_f < 0.0f ? 0 : (b_f > 255.0f ? 255 : b_f));
        
        GPU_FB_BOTTOM_COL_LUT_ELEM = r | (g << 8) | (b << 16);
    }
    
    if(!inConfigMenu)
    {
        svcKernelSetState(0x10000, 2);
        svcSleepThread(5 * 1000 * 100LL);
    }
}

static bool IsNightTime(void)
{
    u64 timeInSeconds = osGetTime() / 1000;
    u64 dayTime = timeInSeconds % 86400;
    u8 hour = dayTime / 3600;
    u8 minute = (dayTime % 3600) / 60;

    if((hour > s_nightLight.night_startHour || (hour == s_nightLight.night_startHour && minute >= s_nightLight.night_startMinute)) 
        || (hour < s_nightLight.light_startHour || (hour == s_nightLight.light_startHour && minute < s_nightLight.light_startMinute)))
    {
        return true;
    }
    return false;
}

static void ApplyLightModeNoColor(void)
{
    if(s_nightLight.light_ledSuppression) Redshift_SuppressLeds();

    // Apply the configured day brightness level (0 = off, don't touch brightness)
    if(s_nightLight.light_brightnessLevel > 0)
        Redshift_ApplyBrightness(s_nightLight.light_brightnessLevel);

    lightModeActive = true;
    nightModeActive = false;
}

static void ApplyNightModeNoColor(void)
{
    if(s_nightLight.night_ledSuppression) Redshift_SuppressLeds();

    // Apply night brightness
    Redshift_ApplyBrightness(s_nightLight.night_brightnessLevel);

    lightModeActive = false;
    nightModeActive = true;
}

void ApplyLightMode(void)
{
    ApplyLightModeNoColor();
    
    if(bootColorTempApplied)
    {
        if(s_nightLight.use_colorTempSwitch)
            Redshift_ApplyColorTemperature(s_nightLight.light_colorTemp);
        else
            Redshift_ApplyColorTemperature(6500);
    }
}

void ApplyNightMode(void)
{
    ApplyNightModeNoColor();
    
    if(bootColorTempApplied)
    {
        if(s_nightLight.use_colorTempSwitch)
            Redshift_ApplyColorTemperature(s_nightLight.night_colorTemp);
        else
            Redshift_ApplyColorTemperature(6500);
    }
}

static void timeCheckThreadMain(void)
{
    svcSleepThread(5000000000LL);
    
    bootColorTempApplied = true;
    
    if(nightLightSettingsRead)
    {
        if(s_nightLight.use_colorTempSwitch)
        {
            if(s_nightLight.use_nightMode && IsNightTime())
                Redshift_ApplyColorTemperature(s_nightLight.night_colorTemp);
            else
                Redshift_ApplyColorTemperature(s_nightLight.light_colorTemp);
        }
    }
    
    bool wasNightTime = IsNightTime();
    
    while(timeCheckThreadRunning)
    {
        svcSleepThread(30000000000LL);
        
        if(!nightLightSettingsRead || !s_nightLight.use_nightMode || nightLightOverride || inConfigMenu)
            continue;
        
        bool isNight = IsNightTime();
        
        if(isNight != wasNightTime)
        {
            wasNightTime = isNight;
            
            // If the system is sleeping, do NOT touch the GPU.
            // The shell-open handler (notification 0x213) will call
            // Redshift_ApplyNightLightSettings() on wake, which will
            // apply the correct mode at that point.
            if(isSleeping)
                continue;
            
            if(isNight)
            {
                ApplyNightModeNoColor();
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.night_colorTemp);
            }
            else
            {
                ApplyLightModeNoColor();
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.light_colorTemp);
            }
        }
    }
}

void Redshift_StartTimeCheckThread(void)
{
    if(timeCheckThreadRunning)
        return;
    
    timeCheckThreadRunning = true;
    MyThread_Create(&timeCheckThread, timeCheckThreadMain, timeCheckThreadStack, 0x1000, 0x3F, -2);
}

void Redshift_NotifySleep(void)
{
    isSleeping = true;
}

void Redshift_NotifyWake(void)
{
    isSleeping = false;
}

void Redshift_ApplyNightLightSettings(void)
{
    if(!nightLightOverride)
    {
        if(s_nightLight.use_nightMode && IsNightTime())
        {
            ApplyNightModeNoColor();
            if(bootColorTempApplied)
            {
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.night_colorTemp);
                else
                    Redshift_ApplyColorTemperature(6500);
            }
        }
        else
        {
            ApplyLightModeNoColor();
            if(bootColorTempApplied)
            {
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.light_colorTemp);
                else
                    Redshift_ApplyColorTemperature(6500);
            }
        }
    }
    else
    {
        if (lightModeActive)
        {
            ApplyLightModeNoColor();
            if(bootColorTempApplied)
            {
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.light_colorTemp);
                else
                    Redshift_ApplyColorTemperature(6500);
            }
        }
        else if(nightModeActive)
        {
            ApplyNightModeNoColor();
            if(bootColorTempApplied)
            {
                if(s_nightLight.use_colorTempSwitch)
                    Redshift_ApplyColorTemperature(s_nightLight.night_colorTemp);
                else
                    Redshift_ApplyColorTemperature(6500);
            }
        }
    }
}

bool Redshift_ReadNightLightSettings(void)
{
    IFile file;
    Result res = IFile_Open(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
            fsMakePath(PATH_ASCII, "/luma/configBootshift.bin"), FS_OPEN_READ);
        
    if(R_SUCCEEDED(res))
    {
        u64 total;
        if(R_SUCCEEDED(IFile_Read(&file, &total, &s_nightLight, sizeof(s_nightLight))))
        {
            IFile_Close(&file);
            
            if (s_nightLight.light_colorTemp < 1000 || s_nightLight.light_colorTemp > 25100)
                s_nightLight.light_colorTemp = 6500;
            if (s_nightLight.night_colorTemp < 1000 || s_nightLight.night_colorTemp > 25100)
                s_nightLight.night_colorTemp = 2700;
            if (s_nightLight.light_brightnessLevel > 5)
                s_nightLight.light_brightnessLevel = 5;
            if (s_nightLight.night_brightnessLevel < 1 || s_nightLight.night_brightnessLevel > 5)
                s_nightLight.night_brightnessLevel = 1;
                
            return true;
        }
        IFile_Close(&file);
    }
    return false;
}

static void WriteNightLightSettings(void)
{
    IFile file;
    Result res = IFile_Open(&file, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
            fsMakePath(PATH_ASCII, "/luma/configBootshift.bin"), FS_OPEN_CREATE | FS_OPEN_WRITE);
        
    if(R_SUCCEEDED(res))
    {
        u64 total;
        IFile_Write(&file, &total, &s_nightLight, sizeof(s_nightLight), 0);
        IFile_Close(&file);
    }
}

// Apply all current settings (color temp + brightness)
static void ApplyAllCurrentSettings(void)
{
    // Apply color temperature
    u16 temp;
    if(!s_nightLight.use_colorTempSwitch)
    {
        temp = 6500;
    }
    else if(s_nightLight.use_nightMode && IsNightTime())
    {
        temp = s_nightLight.night_colorTemp;
    }
    else
    {
        temp = s_nightLight.light_colorTemp;
    }
    Redshift_ApplyColorTemperature(temp);
    
    // Apply brightness based on current mode
    if(s_nightLight.use_nightMode && IsNightTime())
    {
        Redshift_ApplyBrightness(s_nightLight.night_brightnessLevel);
        nightModeActive = true;
        lightModeActive = false;
    }
    else
    {
        if(s_nightLight.light_brightnessLevel > 0)
            Redshift_ApplyBrightness(s_nightLight.light_brightnessLevel);
        lightModeActive = true;
        nightModeActive = false;
    }
}

void Redshift_ConfigureNightLightSettings(void)
{
    Draw_Lock();
    Draw_ClearFramebuffer();
    Draw_FlushFramebuffer();
    Draw_Unlock();
    
    bootColorTempApplied = true;
    inConfigMenu = true;

    int sel = 0;
    const int maxSel = 6;
    u8 minBri = 1, maxBri = 5;
    char fmtbuf[64];
    u32 held = 0;
    bool settingsSaved = false;
    u32 savedMsgTimer = 0;

    do
    {
        u32 pressed = waitInputWithTimeoutEx(&held, 1000);
        
        if(savedMsgTimer > 0) savedMsgTimer--;

        if (pressed & DIRECTIONAL_KEYS)
        {
            settingsSaved = false;
            
            if(pressed & KEY_DOWN) { if(++sel > maxSel) sel = 0; }
            else if(pressed & KEY_UP) { if(--sel < 0) sel = maxSel; }
            else if (pressed & KEY_RIGHT)
            {
                switch(sel){
                    case 0: // Day starts at
                        if(held & (KEY_L | KEY_R)) {
                            s_nightLight.light_startHour++;
                            if(s_nightLight.light_startHour > 23) s_nightLight.light_startHour = 0;
                        } else {
                            s_nightLight.light_startMinute += 5;
                            if(s_nightLight.light_startMinute >= 60) s_nightLight.light_startMinute = 0;
                        }
                        break;
                    case 1: // Day brightness (0=off, 1-5)
                        s_nightLight.light_brightnessLevel++;
                        if(s_nightLight.light_brightnessLevel > maxBri) s_nightLight.light_brightnessLevel = 0;
                        break;
                    case 2: // Enable Night Mode
                        s_nightLight.use_nightMode = !s_nightLight.use_nightMode; 
                        break;
                    case 3: // Night brightness
                        s_nightLight.night_brightnessLevel++; 
                        if(s_nightLight.night_brightnessLevel > maxBri) s_nightLight.night_brightnessLevel = minBri; 
                        break;
                    case 4: // Night starts at
                        if(held & (KEY_L | KEY_R)) {
                            s_nightLight.night_startHour++;
                            if(s_nightLight.night_startHour > 23) s_nightLight.night_startHour = 0;
                        } else {
                            s_nightLight.night_startMinute += 5;
                            if(s_nightLight.night_startMinute >= 60) s_nightLight.night_startMinute = 0;
                        }
                        break;
                    case 5: // Blue light filter
                        s_nightLight.use_colorTempSwitch = !s_nightLight.use_colorTempSwitch;
                        break;
                    case 6: // Night color temp
                        s_nightLight.night_colorTemp += 100; 
                        if(s_nightLight.night_colorTemp > 10000) s_nightLight.night_colorTemp = 1000;
                        break;
                }
            }
            else if (pressed & KEY_LEFT)
            {
                switch(sel){
                    case 0: // Day starts at
                        if(held & (KEY_L | KEY_R)) {
                            if(s_nightLight.light_startHour == 0) s_nightLight.light_startHour = 23;
                            else s_nightLight.light_startHour--;
                        } else {
                            if(s_nightLight.light_startMinute < 5) s_nightLight.light_startMinute = 55;
                            else s_nightLight.light_startMinute -= 5;
                        }
                        break;
                    case 1: // Day brightness (0=off, 1-5)
                        if(s_nightLight.light_brightnessLevel == 0) s_nightLight.light_brightnessLevel = maxBri;
                        else s_nightLight.light_brightnessLevel--;
                        break;
                    case 2: // Enable Night Mode
                        s_nightLight.use_nightMode = !s_nightLight.use_nightMode; 
                        break;
                    case 3: // Night brightness
                        s_nightLight.night_brightnessLevel--; 
                        if(s_nightLight.night_brightnessLevel < minBri) s_nightLight.night_brightnessLevel = maxBri; 
                        break;
                    case 4: // Night starts at
                        if(held & (KEY_L | KEY_R)) {
                            if(s_nightLight.night_startHour == 0) s_nightLight.night_startHour = 23;
                            else s_nightLight.night_startHour--;
                        } else {
                            if(s_nightLight.night_startMinute < 5) s_nightLight.night_startMinute = 55;
                            else s_nightLight.night_startMinute -= 5;
                        }
                        break;
                    case 5: // Blue light filter
                        s_nightLight.use_colorTempSwitch = !s_nightLight.use_colorTempSwitch;
                        break;
                    case 6: // Night color temp
                        s_nightLight.night_colorTemp -= 100; 
                        if(s_nightLight.night_colorTemp < 1000) s_nightLight.night_colorTemp = 10000;
                        break;
                }
            }
        }
        else if (pressed & KEY_START) 
        { 
            WriteNightLightSettings(); 
            nightLightSettingsRead = true;
            ApplyAllCurrentSettings();
            settingsSaved = true;
            savedMsgTimer = 3;
        }
        else if (pressed & KEY_B) { break; }

        u64 timeInSeconds = osGetTime() / 1000;
        u64 dayTime = timeInSeconds % 86400;
        u8 curHour = dayTime / 3600;
        u8 curMinute = (dayTime % 3600) / 60;

        Draw_Lock();
        Draw_ClearFramebuffer();
        Draw_DrawString(10, 10, COLOR_TITLE, "Night/Light Config");
        u32 posY = 30;

        sprintf(fmtbuf, "%c Day starts at: %02d:%02d", (sel == 0 ? '>' : ' '), s_nightLight.light_startHour, s_nightLight.light_startMinute);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        if(s_nightLight.light_brightnessLevel == 0)
            sprintf(fmtbuf, "%c Day brightness: off", (sel == 1 ? '>' : ' '));
        else
            sprintf(fmtbuf, "%c Day brightness: %d", (sel == 1 ? '>' : ' '), s_nightLight.light_brightnessLevel);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        posY += SPACING_Y;

        sprintf(fmtbuf, "%c Enable Night Mode: %s", (sel == 2 ? '>' : ' '), s_nightLight.use_nightMode ? "[on]" : "[off]");
        posY = Draw_DrawString(10, posY, COLOR_RED, fmtbuf) + SPACING_Y;

        sprintf(fmtbuf, "%c Night brightness: %d", (sel == 3 ? '>' : ' '), s_nightLight.night_brightnessLevel);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;
        
        sprintf(fmtbuf, "%c Night starts at: %02d:%02d", (sel == 4 ? '>' : ' '), s_nightLight.night_startHour, s_nightLight.night_startMinute);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        posY += SPACING_Y;

        sprintf(fmtbuf, "%c Blue light filter: %s", (sel == 5 ? '>' : ' '), s_nightLight.use_colorTempSwitch ? "[on]" : "[off]");
        posY = Draw_DrawString(10, posY, COLOR_GREEN, fmtbuf) + SPACING_Y;
        
        sprintf(fmtbuf, "%c Night color temp: %dK", (sel == 6 ? '>' : ' '), s_nightLight.night_colorTemp);
        posY = Draw_DrawString(10, posY, COLOR_WHITE, fmtbuf) + SPACING_Y;

        posY += SPACING_Y * 2;

        posY = Draw_DrawString(10, posY, COLOR_WHITE, "D-Pad: Navigate/Edit values") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "Hold L/R + D-Pad: Change hours") + SPACING_Y;
        posY = Draw_DrawString(10, posY, COLOR_WHITE, "START: Save | B: Exit") + SPACING_Y;
        
        if(settingsSaved && savedMsgTimer > 0)
        {
            posY += SPACING_Y;
            Draw_DrawString(10, posY, COLOR_GREEN, "Settings saved successfully!");
        }

        sprintf(fmtbuf, "%02d:%02d  %s", curHour, curMinute, IsNightTime() ? "NIGHT" : "DAY");
        Draw_DrawString(SCREEN_BOT_WIDTH - 90, SCREEN_BOT_HEIGHT - 20, COLOR_GREEN, fmtbuf);

        Draw_FlushFramebuffer();
        Draw_Unlock();
    }
    while (!menuShouldExit);
    
    inConfigMenu = false;
}
