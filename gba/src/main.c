/* Copyright 2026 Maddie Lim
 *
 * Kitty Advance Plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Kitty Advance Plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with  Kitty Advance Plugin.  If not, see <http://www.gnu.org/licenses/>.
 */
 
#include <stdio.h>
#include <tonc.h>
#include <tonc_bios.h>
#include "demobank.h"
#include "kitty.h"

#define MAX_MIDI_BUFSIZE 512
vu16* midiCopyBufferSize = (vu16*)0x3007000;
u8* midiCopyBuffer = (u8*)0x3007002;

KittyState kitty;

int main() {
  irq_init(NULL);
  irq_add(II_VBLANK, NULL);

  DemoBankInit();

  KittyConfig kcfg = {
    .voices = 32,
    .masterVolume = 11,
    .freqMode = KT_FREQ_MODE_13379HZ,
    .reverb = 10,
    .bank0 = &sndbank,
    .bank127 = &drumbank
};  
  
  KittyInit(&kitty, kcfg);
  KittyLiveMidiInit(&kitty, KT_LIVEMIDI_MODE_FIXED_BUFFER, midiCopyBuffer, midiCopyBufferSize);
  KittySetVSync(&kitty, 1);

  while(1) {
    VBlankIntrWait();
    KittyVSync(&kitty);
    KittyMain(&kitty);
  }
  return 0;
}
