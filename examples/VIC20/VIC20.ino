/*
  Created by Fabrizio Di Vittorio (fdivitto2013@gmail.com) - www.fabgl.com
  Copyright (c) 2019 Fabrizio Di Vittorio.
  All rights reserved.

  This file is part of FabGL Library.

  FabGL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FabGL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FabGL.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "esp_spiffs.h"
#include "esp_task_wdt.h"
#include <stdio.h>

#include "fabgl.h"
#include "fabutils.h"

#include "machine.h"



// embedded (free) games
#include "progs/embedded/blue_star.h"
#include "progs/embedded/omega_fury.h"
#include "progs/embedded/quikman.h"
#include "progs/embedded/splatform.h"
#include "progs/embedded/demo_birthday.h"
#include "progs/embedded/arukanoido.h"
#include "progs/embedded/demo_bah_bah_final.h"
#include "progs/embedded/demo_digit.h"
#include "progs/embedded/dragonwing.h"
#include "progs/embedded/kikstart.h"
#include "progs/embedded/kweepoutmc.h"
#include "progs/embedded/nibbler.h"
#include "progs/embedded/pooyan.h"
#include "progs/embedded/popeye.h"
#include "progs/embedded/pulse.h"
#include "progs/embedded/spikes.h"
#include "progs/embedded/tank_battalion.h"
#include "progs/embedded/tetris_plus.h"
#include "progs/embedded/astro-panic.h"
#include "progs/embedded/frogger07.h"
#include "progs/embedded/froggie.h"
#include "progs/embedded/tammerfors.h"


// non-free list
char const *  LIST_URL    = "http://cloud.cbm8bit.com/adamcost/vic20list.txt";
constexpr int MAXLISTSIZE = 16384;


// root SPIFFS directory
char const * ROOTDIR = "/spiffs";


// name of embedded programs directory
char const * EMBDIR = "Free";


#define DEBUG 0


struct EmbeddedProgDef {
  char const *    filename;
  uint8_t const * data;
  int             size;
};


// Embedded programs
//   To convert a PRG to header file use...
//         xxd -i myprogram.prg >myprogram.h
//   ...modify like files in progs/embedded, and put filename and binary data in following list.
const EmbeddedProgDef embeddedProgs[] = {
  { arukanoido_filename, arukanoido_prg, sizeof(arukanoido_prg) },
  //{ demo_bah_bah_final_filename, demo_bah_bah_final_prg, sizeof(demo_bah_bah_final_prg) },
  //{ demo_digit_filename, demo_digit_prg, sizeof(demo_digit_prg) },
  { dragonwing_filename, dragonwing_prg, sizeof(dragonwing_prg) },
  { kikstart_filename, kikstart_prg, sizeof(kikstart_prg) },
  { kweepoutmc_filename, kweepoutmc_prg, sizeof(kweepoutmc_prg) },
  { nibbler_filename, nibbler_prg, sizeof(nibbler_prg) },
  { pooyan_filename, pooyan_prg, sizeof(pooyan_prg) },
  { popeye_filename, popeye_prg, sizeof(popeye_prg) },
  { pulse_filename, pulse_prg, sizeof(pulse_prg) },
  { spikes_filename, spikes_prg, sizeof(spikes_prg) },
  { tank_battalion_filename, tank_battalion_prg, sizeof(tank_battalion_prg) },
  { tetris_plus_filename, tetris_plus_prg, sizeof(tetris_plus_prg) },
  //{ blue_star_filename, blue_star_prg, sizeof(blue_star_prg) },
  { demo_birthday_filename, demo_birthday_prg, sizeof(demo_birthday_prg) },
  { omega_fury_filename, omega_fury_prg, sizeof(omega_fury_prg) },
  { splatform_filename, splatform_prg, sizeof(splatform_prg) },
  { quikman_filename, quikman_prg, sizeof(quikman_prg) },
  { astro_panic_filename, astro_panic_prg, sizeof(astro_panic_prg) },
  { frogger07_filename, frogger07_prg, sizeof(frogger07_prg) },
  { froggie_filename, froggie_prg, sizeof(froggie_prg) },
  { tammerfors_filename, tammerfors_prg, sizeof(tammerfors_prg) },
};


#if DEBUG
static volatile int  scycles = 0;
static volatile bool restart = false;
void vTimerCallback1SecExpired(xTimerHandle pxTimer)
{
  Serial.printf("%d - ", scycles / 5);
  Serial.printf("Free memory (total, min, largest): %d, %d, %d\n", heap_caps_get_free_size(0), heap_caps_get_minimum_free_size(0), heap_caps_get_largest_free_block(0));
  restart = true;
}
#endif


// where to store WiFi info, etc...
Preferences preferences;


// Main machine
Machine machine;


void initSPIFFS()
{
  // setup SPIFFS
  esp_vfs_spiffs_conf_t conf = {
      .base_path              = ROOTDIR,
      .partition_label        = NULL,
      .max_files              = 2,
      .format_if_mount_failed = true
  };
  fabgl::suspendInterrupts();
  esp_vfs_spiffs_register(&conf);
  fabgl::resumeInterrupts();
}


// copies embedded programs into SPIFFS
void copyEmbeddedPrograms()
{
  auto dir = FileBrowser();
  dir.setDirectory(ROOTDIR);
  if (!dir.exists(EMBDIR)) {
    // there isn't a EMBDIR folder, let's create and populate it
    dir.makeDirectory(EMBDIR);
    dir.changeDirectory(EMBDIR);
    for (int i = 0; i < sizeof(embeddedProgs) / sizeof(EmbeddedProgDef); ++i) {
      int fullpathlen = dir.getFullPath(embeddedProgs[i].filename);
      char fullpath[fullpathlen];
      dir.getFullPath(embeddedProgs[i].filename, fullpath, fullpathlen);
      FILE * f = fopen(fullpath, "wb");
      if (f) {
        fwrite(embeddedProgs[i].data, 1, embeddedProgs[i].size, f);
        fclose(f);
      }
    }
  }
}


struct DownloadProgressFrame : public uiFrame {

  uiLabel * label1;
  uiLabel * label2;
  uiButton * button;

  DownloadProgressFrame(uiFrame * parent)
    : uiFrame(parent, "Download", Point(50, 100), Size(150, 110), false) {
    frameProps().resizeable        = false;
    frameProps().moveable          = false;
    frameProps().hasCloseButton    = false;
    frameProps().hasMaximizeButton = false;
    frameProps().hasMinimizeButton = false;

    label1 = new uiLabel(this, "", Point(10, 25));
    label2 = new uiLabel(this, "", Point(10, 45));
    button = new uiButton(this, "Abort", Point(50, 75), Size(50, 20));
    button->onClick = [&]() { exitModal(1); };
  }

};



struct HelpFame : public uiFrame {

  HelpFame(uiFrame * parent)
    : uiFrame(parent, "Help", Point(50, 95), Size(160, 210), false) {

    auto button = new uiButton(this, "OK", Point(57, 180), Size(50, 20));
    button->onClick = [&]() { exitModal(0); };

    onPaint = [&]() {
      Canvas.selectFont(Canvas.getPresetFontInfoFromHeight(12, false));
      int x = 10;
      int y = 10;
      Canvas.setPenColor(RGB(0, 2, 0));
      Canvas.drawText(x, y += 14, "Keyboard Shortcuts:");
      Canvas.setPenColor(RGB(0, 0, 0));
      Canvas.drawText(x, y += 14, "   F12: Switch Emulator and Menu");
      Canvas.drawText(x, y += 14, "   DEL: Delete File or Folder");
      Canvas.drawText(x, y += 14, "   ALT + A-S-W-Z: Move Screen");
      Canvas.setPenColor(RGB(0, 0, 2));
      Canvas.drawText(x, y += 18, "\"None\" Joystick Mode:");
      Canvas.setPenColor(RGB(0, 0, 0));
      Canvas.drawText(x, y += 14, "   ALT + MENU: Joystick Fire");
      Canvas.drawText(x, y += 14, "   ALT + CURSOR: Joystick Move");
      Canvas.setPenColor(RGB(2, 0, 0));
      Canvas.drawText(x, y += 18, "\"Cursor Keys\" Joystick Mode:");
      Canvas.setPenColor(RGB(0, 0, 0));
      Canvas.drawText(x, y += 14, "   MENU: Joystick Fire");
      Canvas.drawText(x, y += 14, "   CURSOR: Joystick Move");
    };
  }

};



class Menu : public uiApp {

  uiFileBrowser * fileBrowser;
  uiComboBox *    RAMExpComboBox;
  uiLabel *       WiFiStatusLbl;
  uiLabel *       freeSpaceLbl;

  void init() {
    rootWindow()->frameStyle().backgroundColor = RGB(3, 3, 3);

    // some static text
    rootWindow()->onPaint = [&]() {
      Canvas.selectFont(Canvas.getPresetFontInfoFromHeight(12, false));
      Canvas.setPenColor(RGB(3, 1, 1));
      Canvas.drawText(155, 345, "V I C 2 0  Emulator");
      Canvas.setPenColor(RGB(2, 1, 1));
      Canvas.drawText(167, 357, "www.fabgl.com");
      Canvas.drawText(141, 369, "2019 by Fabrizio Di Vittorio");
    };

    // programs list
    fileBrowser = new uiFileBrowser(rootWindow(), Point(5, 10), Size(140, 290));
    fileBrowser->listBoxStyle().backgroundColor = RGB(0, 3, 0);

    fileBrowser->listBoxStyle().selectedBackgroundColor = RGB(3, 0, 0);
    fileBrowser->listBoxStyle().selectedBackgroundColor = RGB(2, 0, 0);
    fileBrowser->listBoxStyle().focusedSelectedBackgroundColor = RGB(3, 0, 0);
    fileBrowser->windowStyle().focusedBorderColor = RGB(3, 0, 0);

    fileBrowser->listBoxStyle().focusedBackgroundColor = RGB(0, 3, 0);
    fileBrowser->setDirectory(ROOTDIR);
    fileBrowser->onChange = [&]() {
      setSelectedProgramConf();
    };

    // handles following keys:
    //   RETURN -> load selected program and return to vic
    fileBrowser->onKeyUp = [&](uiKeyEventInfo key) {
      if (key.VK == VirtualKey::VK_RETURN) {
        if (!fileBrowser->isDirectory() && loadSelectedProgram())
          runVIC20();
      } else if (key.VK == VirtualKey::VK_DELETE) {
        // delete file or directory
        if (messageBox("Delete Item", "Are you sure?", "Yes", "Cancel") == uiMessageBoxResult::Button1) {
          fileBrowser->content().remove( fileBrowser->filename() );
          fileBrowser->update();
          updateFreeSpaceLabel();
        }
      }
    };

    // handles double click on program list
    fileBrowser->onDblClick = [&]() {
      if (!fileBrowser->isDirectory() && loadSelectedProgram())
        runVIC20();
    };

    int x = 168;

    // "Back to VIC" button - run the VIC20
    auto VIC20Button = new uiButton(rootWindow(), "Back to VIC", Point(x, 10), Size(65, 19));
    VIC20Button->onClick = [&]() {
      runVIC20();
    };

    // "Load" button
    auto loadButton = new uiButton(rootWindow(), "Load", Point(x, 35), Size(65, 19));
    loadButton->onClick = [&]() {
      if (loadSelectedProgram())
        runVIC20();
    };

    // "reset" button
    auto resetButton = new uiButton(rootWindow(), "Soft Reset", Point(x, 60), Size(65, 19));
    resetButton->onClick = [&]() {
      machine.reset();
      runVIC20();
    };

    // "Hard Reset" button
    auto hresetButton = new uiButton(rootWindow(), "Hard Reset", Point(x, 85), Size(65, 19));
    hresetButton->onClick = [&]() {
      machine.removeCRT();
      machine.reset();
      runVIC20();
    };

    // "help" button
    auto helpButton = new uiButton(rootWindow(), "Help", Point(x, 282), Size(65, 19));
    helpButton->onClick = [&]() {
      auto hframe = new HelpFame(rootWindow());
      showModalWindow(hframe);
      destroyWindow(hframe);
    };

    // RAM expansion options
    int y = 120;
    auto lbl = new uiLabel(rootWindow(), "RAM Expansion:", Point(150, y));
    lbl->labelStyle().textColor = RGB(1, 1, 3);
    RAMExpComboBox = new uiComboBox(rootWindow(), Point(158, y + 20), Size(85, 19), 130);
    char const * RAMOPTS[] = { "Unexpanded", "3K", "8K", "16K", "24K", "27K (24K+3K)", "32K", "35K (32K+3K)" };
    for (int i = 0; i < 8; ++i)
      RAMExpComboBox->items().append(RAMOPTS[i]);
    RAMExpComboBox->selectItem((int)machine.RAMExpansion());
    RAMExpComboBox->onChange = [&]() {
      machine.setRAMExpansion((RAMExpansionOption)(RAMExpComboBox->selectedItem()));
    };

    // joystick emulation options
    y += 50;
    lbl = new uiLabel(rootWindow(), "Joystick:", Point(150, y));
    lbl->labelStyle().textColor = RGB(1, 1, 3);
    new uiLabel(rootWindow(), "None", Point(180, y + 20));
    auto radioJNone = new uiCheckBox(rootWindow(), Point(160, y + 20), Size(16, 16), uiCheckBoxKind::RadioButton);
    new uiLabel(rootWindow(), "Cursor Keys", Point(180, y + 40));
    auto radioJCurs = new uiCheckBox(rootWindow(), Point(160, y + 40), Size(16, 16), uiCheckBoxKind::RadioButton);
    new uiLabel(rootWindow(), "Mouse", Point(180, y + 60));
    auto radioJMous = new uiCheckBox(rootWindow(), Point(160, y + 60), Size(16, 16), uiCheckBoxKind::RadioButton);
    radioJNone->setGroupIndex(1);
    radioJCurs->setGroupIndex(1);
    radioJMous->setGroupIndex(1);
    radioJNone->setChecked(machine.joyEmu() == JE_None);
    radioJCurs->setChecked(machine.joyEmu() == JE_CursorKeys);
    radioJMous->setChecked(machine.joyEmu() == JE_Mouse);
    radioJNone->onChange = [&]() { machine.setJoyEmu(JE_None); };
    radioJCurs->onChange = [&]() { machine.setJoyEmu(JE_CursorKeys); };
    radioJMous->onChange = [&]() { machine.setJoyEmu(JE_Mouse); };

    // setup wifi button
    auto setupWifiBtn = new uiButton(rootWindow(), "Setup", Point(28, 330), Size(40, 19));
    setupWifiBtn->onClick = [&]() {
      char SSID[32] = "";
      char psw[32]  = "";
      if (inputBox("WiFi Connect", "WiFi Name", SSID, sizeof(SSID), "OK", "Cancel") == uiMessageBoxResult::Button1 &&
          inputBox("WiFi Connect", "Password", psw, sizeof(psw), "OK", "Cancel") == uiMessageBoxResult::Button1) {
        fabgl::suspendInterrupts();
        preferences.putString("SSID", SSID);
        preferences.putString("WiFiPsw", psw);
        fabgl::resumeInterrupts();
      }
    };

    // ON wifi button
    auto onWiFiBtn = new uiButton(rootWindow(), "On", Point(72, 330), Size(40, 19));
    onWiFiBtn->onClick = [&]() {
      connectWiFi();
    };

    // free space label
    freeSpaceLbl = new uiLabel(rootWindow(), "", Point(5, 305));
    updateFreeSpaceLabel();

    // WiFi Status label
    WiFiStatusLbl = new uiLabel(rootWindow(), "WiFi", Point(5, 332));
    WiFiStatusLbl->labelStyle().textColor = RGB(2, 2, 2);

    // "Download From" label
    auto downloadFromLbl = new uiLabel(rootWindow(), "Download From:", Point(5, 354));
    downloadFromLbl->labelStyle().textColor = RGB(1, 1, 3);
    downloadFromLbl->labelStyle().textFont = Canvas.getPresetFontInfoFromHeight(12, false);

    // Download List button (download programs listed and linked in LIST_URL)
    auto downloadProgsBtn = new uiButton(rootWindow(), "List", Point(75, 352), Size(27, 19));
    downloadProgsBtn->onClick = [&]() {
      if (!WiFiConnected()) {
        messageBox("Network Error", "Please activate WiFi", "OK", nullptr, nullptr, uiMessageBoxIcon::Error);
      } else if (messageBox("Download Programs listed in \"LIST_URL\"", "Check your local laws for restrictions", "OK", "Cancel", nullptr, uiMessageBoxIcon::Warning) == uiMessageBoxResult::Button1) {
        auto pframe = new DownloadProgressFrame(rootWindow());
        auto modalStatus = initModalWindow(pframe);
        processModalWindowEvents(modalStatus, 100);
        prepareForDownload();
        char * list = downloadList();
        char * plist = list;
        int count = countListItems(list);
        int dcount = 0;
        for (int i = 0; i < count; ++i) {
          char const * filename;
          char const * URL;
          plist = getNextListItem(plist, &filename, &URL);
          pframe->label1->setText(filename);
          if (!processModalWindowEvents(modalStatus, 100))
            break;
          if (downloadURL(URL, filename))
            ++dcount;
          pframe->label2->setTextFmt("Downloaded %d/%d", dcount, count);
        }
        free(list);
        // modify "abort" button to "OK"
        pframe->button->setText("OK");
        pframe->button->repaint();
        // wait for OK
        processModalWindowEvents(modalStatus, -1);
        endModalWindow(modalStatus);
        destroyWindow(pframe);
        fileBrowser->update();
        updateFreeSpaceLabel();
      }
    };

    // Download from URL
    auto downloadURLBtn = new uiButton(rootWindow(), "URL", Point(107, 352), Size(27, 19));
    downloadURLBtn->onClick = [&]() {
      char * URL = new char[128];
      char * filename = new char[25];
      strcpy(URL, "http://");
      if (inputBox("Download From URL", "URL", URL, 127, "OK", "Cancel") == uiMessageBoxResult::Button1) {
        char * lastslash = strrchr(URL, '/');
        if (lastslash) {
          strcpy(filename, lastslash + 1);
          if (inputBox("Download From URL", "Filename", filename, 24, "OK", "Cancel") == uiMessageBoxResult::Button1) {
            if (downloadURL(URL, filename)) {
              messageBox("Success", "Download OK!", "OK", nullptr, nullptr);
              fileBrowser->update();
              updateFreeSpaceLabel();
            } else
              messageBox("Error", "Download Failed!", "OK", nullptr, nullptr, uiMessageBoxIcon::Error);
          }
        }
      }
      delete [] filename;
      delete [] URL;
    };

    // process ESC and F12: boths run the emulator
    rootWindow()->onKeyUp = [&](uiKeyEventInfo key) {
      if (key.VK == VirtualKey::VK_ESCAPE || key.VK == VirtualKey::VK_F12)
        runVIC20();
    };

    // focus on programs list
    setFocusedWindow(fileBrowser);
  }

  // this is the main loop where VIC20 instructions are executed until F12 has been pressed
  void runVIC20()
  {
    enableKeyboardAndMouseEvents(false);
    Keyboard.emptyVirtualKeyQueue();
    machine.VIC().enableAudio(true);
    Canvas.setBrushColor(0, 0, 0);
    Canvas.clear();

    bool run = true;
    while (run) {

      #if DEBUG
        int cycles = machine.run();
        if (restart) {
          scycles = cycles;
          restart = false;
        } else
          scycles += cycles;
      #else
        machine.run();
      #endif

      // read keyboard
      if (Keyboard.virtualKeyAvailable()) {
        bool keyDown;
        VirtualKey vk = Keyboard.getNextVirtualKey(&keyDown);
        switch (vk) {

          // F12 - stop running
          case VirtualKey::VK_F12:
            if (!keyDown)
              run = false;  // exit loop
            break;

          // other keys
          default:
            // send to emulated machine
            machine.setKeyboard(vk, keyDown);
            break;
        }
      }

    }
    machine.VIC().enableAudio(false);
    Keyboard.emptyVirtualKeyQueue();
    enableKeyboardAndMouseEvents(true);
    rootWindow()->repaint();
  }

  // extract required RAM size from selected filename (a space and 3K, 8K...)
  void setSelectedProgramConf()
  {
    char const * fname = fileBrowser->filename();
    if (fname) {
      // select required memory expansion
      static char const * EXP[8] = { "", " 3K", " 8K", " 16K", " 24K", " 27K", " 32K", " 35K" };
      int r = 0;
      for (int i = 1; i < 8; ++i)
        if (strstr(fname, EXP[i])) {
          r = i;
          break;
        }
      RAMExpComboBox->selectItem(r);
    }
  }

  // extract address from selected filename. "20"=0x2000, "40"=0x4000, "60"=0x6000 or "A0"=0xA000, "A0" or automatic is the default
  // addrPos if specified will contain where address has been found
  int getAddress(char * filename, char * * addrPos = nullptr)
  {
    static char const * ASTR[] = { " 20", " 40", " 60", " a0", " A0" };
    static const int    ADDR[] = { 0x2000, 0x4000, 0x6000, 0xa000, 0xa000 };
    for (int i = 0; i < sizeof(ADDR) / sizeof(int); ++i) {
      auto p = strstr(filename, ASTR[i]);
      if (p) {
        if (addrPos)
          *addrPos = p + 1;
        return ADDR[i];
      }
    }
    return -1;
  }

  // configuration given by filename:
  //    PROGNAME[EXP][ADDRESS].EXT
  //      PROGNAME : program name (not included extension)
  //      EXP      : expansion ("3K", "8K", "16K", "24K", "27K", "32K", "35K")
  //      ADDRESS  : hex address ("20"=0x2000, "40"=0x4000, "60"=0x6000 or "A0"=0xA000), "A0" or automatic is the default
  //      EXT      : "CRT" or "PRG"
  bool loadSelectedProgram()
  {
    bool backToVIC = false;
    char const * fname = fileBrowser->filename();
    if (fname) {
      FileBrowser & dir  = fileBrowser->content();

      bool isPRG = strstr(fname, ".PRG") || strstr(fname, ".prg");
      bool isCRT = strstr(fname, ".CRT") || strstr(fname, ".crt");

      if (isPRG || isCRT) {

        machine.removeCRT();

        int fullpathlen = dir.getFullPath(fname);
        char fullpath[fullpathlen];
        dir.getFullPath(fname, fullpath, fullpathlen);

        if (isPRG) {
          // this is a PRG, just reset, load and run
          machine.loadPRG(fullpath, true, true);
        } else if (isCRT) {
          // this a CRT, find other parts, load and reset
          char * addrPos = nullptr;
          int addr = getAddress(fullpath, &addrPos);
          if (addrPos) {
            // address specified, try to load all possible positions (2000, 4000, 6000 and a000)
            static const char * POS = { "246Aa" }; // 2=2000, 4=4000, 6=6000, A=a000
            for (int i = 0; i < sizeof(POS); ++i) {
              *addrPos = POS[i];  // actually replaces char inside "fullpath"
              machine.loadCRT(fullpath, true, getAddress(fullpath));
            }
          } else {
            // no address specified, just load it
            machine.loadCRT(fullpath, true, addr);
          }
        }
        backToVIC = true;
      }
    }
    return backToVIC;
  }

  // return true if WiFi is connected
  bool WiFiConnected() {
    fabgl::suspendInterrupts();
    bool r = WiFi.status() == WL_CONNECTED;
    fabgl::resumeInterrupts();
    return r;
  }

  // connect to wifi using SSID and PSW from Preferences
  void connectWiFi()
  {
    fabgl::suspendInterrupts();
    char SSID[32], psw[32];
    if (preferences.getString("SSID", SSID, sizeof(SSID)) && preferences.getString("WiFiPsw", psw, sizeof(psw))) {
      WiFi.begin(SSID, psw);
      for (int i = 0; i < 6 && WiFi.status() != WL_CONNECTED; ++i) {
        WiFi.reconnect();
        delay(1000);
      }
    }
    WiFiStatusLbl->labelStyle().textColor = (WiFi.status() == WL_CONNECTED ? RGB(0, 2, 0) : RGB(2, 2, 2));
    WiFiStatusLbl->update();
    fabgl::resumeInterrupts();
    if (WiFi.status() != WL_CONNECTED)
      messageBox("Network Error", "Failed to connect WiFi. Try again!", "OK", nullptr, nullptr, uiMessageBoxIcon::Error);
  }

  // creates List folder and makes it current
  void prepareForDownload()
  {
    static char const * DOWNDIR = "List";
    FileBrowser & dir = fileBrowser->content();

    fabgl::suspendInterrupts();
    dir.setDirectory(ROOTDIR);
    dir.makeDirectory(DOWNDIR);
    dir.changeDirectory(DOWNDIR);
    fabgl::resumeInterrupts();
  }

  // download list from LIST_URL
  // ret nullptr on fail
  char * downloadList()
  {
    fabgl::suspendInterrupts();
    auto list = (char*) malloc(MAXLISTSIZE);
    auto dest = list;
    HTTPClient http;
    http.begin(LIST_URL);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
      WiFiClient * stream = http.getStreamPtr();
      int bufspace = MAXLISTSIZE;
      while (http.connected() && bufspace > 1) {
        auto size = stream->available();
        if (size) {
          int c = stream->readBytes(dest, fabgl::imin(bufspace, size));
          dest += c;
          bufspace -= c;
        }
      }
    }
    *dest = 0;
    fabgl::resumeInterrupts();
    return list;
  }

  // count number of items in download list
  int countListItems(char const * list)
  {
    int count = 0;
    auto p = list;
    while (*p++)
      if (*p == 0x0a)
        ++count;
    return (count + 1) / 3;
  }

  // extract filename and URL from downloaded list
  char * getNextListItem(char * list, char const * * filename, char const * * URL)
  {
    // bypass spaces
    while (*list == 0x0a || *list == 0x0d || *list == 0x20)
      ++list;
    // get filename
    *filename = list;
    while (*list && *list != 0x0a && *list != 0x0d)
      ++list;
    *list++ = 0;
    // bypass spaces
    while (*list && (*list == 0x0a || *list == 0x0d || *list == 0x20))
      ++list;
    // get URL
    *URL = list;
    while (*list && *list != 0x0a && *list != 0x0d)
      ++list;
    *list++ = 0;
    return list;
  }

  // download specified filename from URL
  bool downloadURL(char const * URL, char const * filename)
  {
    fabgl::suspendInterrupts();
    FileBrowser & dir = fileBrowser->content();
    if (dir.exists(filename)) {
      fabgl::resumeInterrupts();
      return true;
    }
    bool success = false;
    HTTPClient http;
    http.begin(URL);
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {

      int fullpathLen = dir.getFullPath(filename);
      char fullpath[fullpathLen];
      dir.getFullPath(filename, fullpath, fullpathLen);
      FILE * f = fopen(fullpath, "wb");

      if (f) {
        int len = http.getSize();
        uint8_t * buf = (uint8_t*) malloc(128);
        WiFiClient * stream = http.getStreamPtr();
        int dsize = 0;
        while (http.connected() && (len > 0 || len == -1)) {
          size_t size = stream->available();
          if (size) {
            int c = stream->readBytes(buf, fabgl::imin(sizeof(buf), size));
            fwrite(buf, c, 1, f);
            dsize += c;
            if (len > 0)
              len -= c;
          }
        }
        free(buf);
        fclose(f);
        success = (len == 0 || (len == -1 && dsize > 0));
      }
    }
    fabgl::resumeInterrupts();
    return success;
  }

  // show free SPIFFS space
  void updateFreeSpaceLabel() {
    size_t total = 0, used = 0;
    esp_spiffs_info(NULL, &total, &used);
    freeSpaceLbl->setTextFmt("%d KB Free", (total - used) / 1024);
    freeSpaceLbl->update();
  }



};



///////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////


void setup()
{
  Serial.begin(115200); // for debug purposes

  preferences.begin("VIC20", false);

  PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, KbdMode::CreateVirtualKeysQueue);
  Keyboard.setLayout(&fabgl::UKLayout);

  VGAController.begin();
  VGAController.setResolution(VGA_256x384_60Hz);
  VGAController.enableBackgroundPrimitiveTimeout(false);

  // adjust this to center screen in your monitor
  //VGAController.moveScreen(20, -2);

  Canvas.selectFont(Canvas.getPresetFontInfo(32, 48));

  Canvas.clear();
  Canvas.drawText(25, 10, "Initializing SPIFFS...");
  Canvas.waitCompletion();
  initSPIFFS();

  Canvas.drawText(25, 30, "Copying embedded programs...");
  Canvas.waitCompletion();
  copyEmbeddedPrograms();
}


void loop()
{
  #if DEBUG
  TimerHandle_t xtimer = xTimerCreate("", pdMS_TO_TICKS(5000), pdTRUE, (void*)0, vTimerCallback1SecExpired);
  xTimerStart(xtimer, 0);
  #endif

  auto menu = new Menu;
  menu->run();
}

