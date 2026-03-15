/* redshift.h -- Main program header
   Night/Light mode for Luma3DS
   Based on Starlight3DS by Nutez
*/

#ifndef REDSHIFT_REDSHIFT_H
#define REDSHIFT_REDSHIFT_H

#include <stdio.h>
#include <stdlib.h>
#include <3ds/types.h>

#define NEUTRAL_TEMP  6500

extern bool nightLightSettingsRead;
extern bool nightLightOverride;

typedef struct {
   u8 abl_enabled;
   u8 brightness_level;
} backlight_controls;

typedef struct {
   // Brightness settings
   u8 light_brightnessLevel;
   bool light_ledSuppression;
   u8 light_startHour;
   u8 light_startMinute;
   u8 night_brightnessLevel;
   bool night_ledSuppression; 
   u8 night_startHour;
   u8 night_startMinute;
   bool use_nightMode;
   bool light_changeBrightness;
   bool night_changeBrightness;
   
   // Color temperature settings
   u16 light_colorTemp;
   u16 night_colorTemp;
   bool use_colorTempSwitch;
} night_light_settings;

void Redshift_SuppressLeds(void);
bool Redshift_ReadNightLightSettings(void);
void Redshift_ApplyNightLightSettings(void);
void Redshift_StartTimeCheckThread(void);  // NEW: Start background time monitor
void Redshift_NotifySleep(void);
void Redshift_NotifyWake(void);
void ApplyLightMode(void);
void ApplyNightMode(void);
void Redshift_ConfigureNightLightSettings(void);

#endif /* ! REDSHIFT_REDSHIFT_H */
