# RadiantLuma

A fork of [Luma3DS](https://github.com/LumaTeam/Luma3DS) (based on v13.3.3) that adds enhanced brightness control features for the Nintendo 3DS.

## Features

### Temporary Brightness Boost
Boosts screen brightness beyond the standard 3DS limits. Includes a simple toggle to enable or disable, and the setting persists even when entering sleep mode.

This feature has been tested for safety; I ran it for a week straight with a white background while the 3DS was plugged in with no issues.
Useful for those that have 3DS's with yellowed screens. I myself tried it with my japanese New 3DS and it reduced the yellow tint substantially.

Can also be used outdoors for a better viewing experience since it considerably increases both screens' brightnesses, but with the tradeoff of higher battery usage.

#### How It Works
The 3DS OS constantly enforces a brightness cap of 172 or below. It does so by checking the ambient brightness every ~10ms using the inner camera and resets the value to the current set "Screen Brightness" level, even if "Auto-Brightness" is disabled. To bypass this, RadiantLuma runs a background thread that reapplies a brightness value of 400 every 5ms, effectively overriding the OS limit. The thread keeps running in the background to ensure the boost persists through sleep mode.
The 400 value is arbitrary, any value above 172 will have the same effect with the same exact luminance. I just chose 400 because the brightness seems more than double the maximum 172.
‎ 

### Permanent Brightness Recalibration
Implementation of [Nutez's Permanent Brightness Recalibration](https://github.com/DullPointer/Luma3DS/commit/0e67a667077f601680f74ddc10ef88a799a5a7ad#diff-33c1f680c1c6c4ed3d06898a6535d80f17bc93753dfbb3ab4d61e9d7f940ecaeR139-R142), allowing you to modify the vanilla brightness values for levels 1–5.

**Note:** The brightness boost value cannot be applied to these levels, as the 3DS OS refuses to accept values beyond 172.

## Limitations

- **DS Mode / GBA Mode:** The brightness boost does not work in these modes. Since the process runs in the background while Luma is active, it cannot persist into legacy compatibility modes. This is unlikely to change.

## Installation

Follow the standard [Luma3DS installation process](https://github.com/LumaTeam/Luma3DS/wiki). RadiantLuma is a drop-in replacement.

## License

This project is licensed under the GPLv3. See the [LICENSE](LICENSE) file for details.

## Credits

- [LumaTeam](https://github.com/LumaTeam) for Luma3DS
- Nutez for Permanent Brightness Recalibration
